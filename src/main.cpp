#include <Arduino.h>
#include <Streaming.h>
#include <myMacros.h>
#include <myUtils.h>

#define DEBUG_EIN  //"Schalter" zum aktivieren von DEBUG-Ausgaben
#include <myDebug.h>

//------------------------------------------------------------------------------
// https://forum.arduino.cc/t/teensy-3-2-sd-card-module/951789?page=2

// https://github.com/greiman/SdFat/blob/master/examples/AvrAdcLogger/AvrAdcLogger.ino
// https://codebender.cc/example/SdFat/ReadWriteSdFat#ReadWriteSdFat.ino
// https://www.heise.de/blog/Spuren-hinterlassen-Datenlogging-mit-Arduino-3348205.html
// https://www.sdcard.org/downloads/formatter/


/////////////////////////////////////////////////////////////////
//
// Datalogger Demo (c) Michael Stal, 2016
//
// Messung der Temperatur an einem TMP36
// Zeitverarbeitung mit RTClib (Chip DS1307, über IIC)
// SD Card Zugriff mit SdFat   (über SPI MISO/MOSI)
// RTC und SD Card entweder separat angeschlossen
// oder über Adafruit Datalogger Shield
//
/////////////////////////////////////////////////////////////////

// ******************** DEKLARATIONEN ***************************

/////////////////////////////////////////////////////////////////
// DEKLARATIONEN RTC (Echtzeituhr)
/////////////////////////////////////////////////////////////////
#include <Time.h>
#include <TimeLib.h>
#include <Wire.h>

#include "RTClib.h"

#include <ADC.h>
#include <ADC_util.h>

#define LED  2

//RTC_DS1307 rtc;  // Zugriff auf RTC

//char WochenTage[7][12] = {"Sonntag", "Montag", "Dienstag", "Mittwoch", "Donnerstag", "Freitag", "Samstag"};

/////////////////////////////////////////////////////////////////
// DEKLARATIONEN SD Card
/////////////////////////////////////////////////////////////////
#define LOGDATEI "LOG_"

#include <SPI.h>

#include "SdFat.h"

// Chip Selector auf Arduino Board
const uint8_t chipSelect = SS;

// Separatorzeichen für Dateiausgabe
const String SEP = ";";

// Zugriff auf Dateisystem der SD Card
SdFat sd;

// Log-Datei
SdFile datei;

// Fehlermeldungen im Flash ablegen.
#define error(msg) sd.errorHalt(F(msg))

char dateiName[13]         = LOGDATEI "00.csv";  // Z.B. TMP3604.csv

/////////////////////////////////////////////////////////////////
// DEKLARATIONEN TemperaturSensor
/////////////////////////////////////////////////////////////////
const int U_In_PIN                = A2;

const int I_Sens_Pin              = A0;
const float faktorU2I             = 0.100;  // 5A 185mV/A     20A  100mV/A     30A  66mV/A

const int adcResolution = 12;
const int zeroCurrentValue = pow(2,adcResolution)/2 - 1;  // Kalibrierfaktor für ACS712


const int U_Sens_Pin              = A1;
const float VersorgungsSpannung   = 3.3;  // ändern für 3.3V
const int ZeitZwischenMessungen   = 5;    // in Sekunden

// ********************** METHODEN ******************************
void initRTC1();
void initRTC2();
void initSDCardReader();
float stromMessen();
//void ausgebenZeit(DateTime jetzt);
void schreibeHeader();
void schreibeMessung(float messwert_I, float messwert_U, DateTime jetzt);
void dateiSchliessen();

/////////////////////////////////////////////////////////////////
//
// initRTC()
//    Echtzeituhr initialisieren
//
/////////////////////////////////////////////////////////////////
#if 0
void initRTC1() {
  if (!rtc.begin()) {  // ist eine Uhr angeschlossen?
    Serial.println("Echtzeituhr fehlt");
    while (1)
      ;  // Fehlerschleife
  }
  if (!rtc.isrunning()) {  // Uhr schon gesetzt?
    Serial.println("RTC bisher noch nicht gesetzt!");
    // => Initialisiere Zeit und Datum auf Datum/Zeit des Host-PCs
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }
}
#endif
// https://github.com/PaulStoffregen/Time
// https://bigdanzblog.wordpress.com/2015/01/05/using-the-teensy-3-1-real-time-clock-rtc/
void digitalClockDisplay() {
  char s[40];
  snprintf(s, sizeof(s), "%02d.%02d.%02d %02d:%02d:%02d",  day(), month(), year(), hour()+2, minute(), second());
  Serial.println(s);
}

time_t getTeensy3Time() { return Teensy3Clock.get(); }

void initRTC2() {
  setSyncProvider(getTeensy3Time);
  if (timeStatus() != timeSet) {
    Serial.println("Unable to sync with the RTC");
  } else {
    //Serial.println("RTC has set the system time");
     Serial.print("RTC auf Systemzeit gesetzt:");
     digitalClockDisplay();
  }
  setSyncInterval(60*60);
}

/////////////////////////////////////////////////////////////////
//
// initSDCardReader()
//    Kartenleser initialisieren
//
/////////////////////////////////////////////////////////////////
void initSDCardReader() {
  const uint8_t NAMENSLAENGE = sizeof(LOGDATEI) - 1;


  delay(1000);
  // SD Card mit SPI_HALF_SPEED initialisieren, um Fehler
  // bei Breadboardnutzung zu vermeiden.  Sonst => SPI_FULL_SPEED
  if (!sd.begin(chipSelect, SPI_HALF_SPEED)) {  // Zugriff auf SD?
    sd.initErrorHalt();
  }

  // Dateiformat 8.3
  if (NAMENSLAENGE > 6) {
    error("Dateipräfix zu lang");
  }

  // Standarddateiname LOGDATEI  + laufende Nummer, z.B.
  // TMP3603.csv
  // Sobald alle Suffixe 00..09 verbraucht sind,
  // geht es von vorne los: round robin

  while (sd.exists(dateiName)) {
    if (dateiName[NAMENSLAENGE + 1] != '9') {
      dateiName[NAMENSLAENGE + 1]++;
    } else if (dateiName[NAMENSLAENGE] != '9') {
      dateiName[NAMENSLAENGE + 1] = '0';
      dateiName[NAMENSLAENGE]++;
    } else {
      error("Kann Datei nicht erzeugen");
    }
  }
  // Jetzt öffnen:
  if (!datei.open(dateiName, O_CREAT | O_WRITE | O_EXCL)) {
    error("Datei öffnen misslungen!");
  }

  Serial.print(F("Logging auf: "));
  Serial.println(dateiName);

  // Header schreiben
  schreibeHeader();
  //datei.close();
}


ADC *adc_I = new ADC(); // adc object;
ADC *adc_U = new ADC(); // adc object;

void initADC(){
  ///// ADC0 ////
    // reference can be ADC_REFERENCE::REF_3V3, ADC_REFERENCE::REF_1V2 (not for Teensy LC) or ADC_REFERENCE::REF_EXT.
    adc_I->adc0->setReference(ADC_REFERENCE::REF_3V3); // change all 3.3 to 1.2 if you change the reference to 1V2
    adc_U->adc0->setReference(ADC_REFERENCE::REF_3V3);

    adc_I->adc0->setAveraging(16); // set number of averages
    adc_I->adc0->setResolution(adcResolution); // set bits of resolution

    adc_U->adc0->setAveraging(16); // set number of averages
    adc_U->adc0->setResolution(adcResolution); // set bits of resolution

    // it can be any of the ADC_CONVERSION_SPEED enum: VERY_LOW_SPEED, LOW_SPEED, MED_SPEED, HIGH_SPEED_16BITS, HIGH_SPEED or VERY_HIGH_SPEED
    // see the documentation for more information
    // additionally the conversion speed can also be ADACK_2_4, ADACK_4_0, ADACK_5_2 and ADACK_6_2,
    // where the numbers are the frequency of the ADC clock in MHz and are independent on the bus speed.
    adc_I->adc0->setConversionSpeed(ADC_CONVERSION_SPEED::VERY_LOW_SPEED); // change the conversion speed
    adc_U->adc0->setConversionSpeed(ADC_CONVERSION_SPEED::VERY_LOW_SPEED);
    // it can be any of the ADC_MED_SPEED enum: VERY_LOW_SPEED, LOW_SPEED, MED_SPEED, HIGH_SPEED or VERY_HIGH_SPEED
    adc_I->adc0->setSamplingSpeed(ADC_SAMPLING_SPEED::MED_SPEED); // change the sampling speed
    adc_U->adc0->setSamplingSpeed(ADC_SAMPLING_SPEED::MED_SPEED);

    Serial << "ADC:  Auflösung=" << adcResolution << "  MaxValue=" << adc_I->adc0->getMaxValue() << "  zeroCurrentValue=" <<  zeroCurrentValue << endl;

}

/////////////////////////////////////////////////////////////////
//
// stromMessen()
//   Temperatur vom TMP36 über Analogeingang lesen
//
/////////////////////////////////////////////////////////////////
float stromMessen() {
  long rawValue = 0;
  for(int i=0; i<50; i++){
    rawValue += adc_I->adc0->analogRead(I_Sens_Pin);
    //rawValue += analogRead(I_Sens_Pin);
  }
  float digitalWertInc = rawValue/50.0;

  float messwertStrom = (digitalWertInc - zeroCurrentValue)*VersorgungsSpannung /adc_I->adc0->getMaxValue()/faktorU2I;
  Serial << "Messung I (" << I_Sens_Pin << ")  inc="  << digitalWertInc << "  I=" << messwertStrom << "A" << endl;
  return messwertStrom ;
}

float spannungMessen() {
  //int digitalWertInc   = analogRead(U_Sens_Pin);
  int  digitalWertInc = adc_U->adc0->analogRead(U_Sens_Pin);
  float messwertSpannung = digitalWertInc * VersorgungsSpannung / adc_U->adc0->getMaxValue();
  Serial << "Messung U (" << U_Sens_Pin << ")  inc="  << digitalWertInc << "  U="<< messwertSpannung <<"V" << endl;
  return messwertSpannung ;
}

/////////////////////////////////////////////////////////////////
//
// ausgebenZeit()
//   Zeitausgabe zur Diagnose
//
/////////////////////////////////////////////////////////////////
#if 0
void ausgebenZeit(DateTime jetzt)  // easy Code
{
  Serial.print(jetzt.year(), DEC);
  Serial.print('/');
  Serial.print(jetzt.month(), DEC);
  Serial.print('/');
  Serial.print(jetzt.day(), DEC);
  Serial.print(" (");
  Serial.print(WochenTage[jetzt.dayOfTheWeek()]);
  Serial.print(") ");
  Serial.print(jetzt.hour(), DEC);
  Serial.print(':');
  Serial.print(jetzt.minute(), DEC);
  Serial.print(':');
  Serial.print(jetzt.second(), DEC);
  Serial.println();
}
#endif

/////////////////////////////////////////////////////////////////
// Statt in einer .csv-Datei könnte man z.B. auch im JSON Format
// speichern !!!
/////////////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////////////
//
// schreibeHeader
//    Schreiben von Header Information in Datei
//
/////////////////////////////////////////////////////////////////
void schreibeHeader() {
  datei.println(F("Strommessung mit ACS712 mit Nutzung eines Dataloggers"));
  datei.print(F("Datum"));
  datei.print(SEP);
  datei.print(F("Zeit"));
  datei.print(SEP);
  datei.print(F("Strom (A)"));
  datei.print(SEP);
  datei.print(F("Spannung(V)"));
  datei.println();
}

/////////////////////////////////////////////////////////////////
//
// schreibeMessung()
//    messwert nehmen und in gewählter Einheit ausgeben
//    und zusammen mit Datum und Uhrzeit in Datei schreiben
//
/////////////////////////////////////////////////////////////////
void schreibeMessung(float messwert_I, float messwert_U , DateTime jetzt) {
  //Serial << "Messung schreiben Start" << endl;
  if (!datei.isOpen()){
    //Serial << "Datei " << dateiName << " wieder öffnen" << endl;
    if (!datei.open(dateiName, O_APPEND | O_WRITE )) {
     Serial << "Datei " << dateiName ;
     error(" öffnen misslungen!");
    }
  }
  datei.print(jetzt.day());
  datei.print(".");
  datei.print(jetzt.month());
  datei.print(".");
  datei.print(jetzt.year());
  datei.print(SEP);
  datei.print(jetzt.hour()+2);
  datei.print(":");
  datei.print(jetzt.minute());
  datei.print(":");
  datei.print(jetzt.second());
  datei.print(SEP);
  datei.print(messwert_I);
  datei.print("mA");
  datei.print(SEP);
  datei.print(messwert_U);
  datei.print("V");
  datei.println();

  // Dateisync, um Datenverlust zu vermeiden:
  if (!datei.sync() || datei.getWriteError()) {
    error("Schreibfehler!");
  }
  //datei.close();
   //Serial << "Messung schreiben Ende" << endl;
}

/////////////////////////////////////////////////////////////////
//
// dateiSchliessen()
//    Datei wieder schließen
//
/////////////////////////////////////////////////////////////////
void dateiSchliessen() {
  datei.close();
  Serial.println(F("Datei steht bereit!"));
  // SysCall::halt();
}

// ********************** DATEIENDE *****************************

/////////////////////////////////////////////////////////////////
//
// setup()
//    Echtzeituhr und SD Card Leser initialisieren
//
/////////////////////////////////////////////////////////////////
void setup() {
  Serial.begin(BAUDRATE);
  while (!Serial && (millis() < 3000))
    ;
  Serial << "\n\n" << ProjektName << " - " << VERSION << "  (" << BUILDDATE << "  " __TIME__ << ")" << endl;
  pinMode(LED,OUTPUT);
  initRTC2();          // Echtzeituhr initialisieren
  initSDCardReader();  // SD Card initialisieren
  initADC();
}

/////////////////////////////////////////////////////////////////
//
// loop()
//   In jeder Iteration
//      Strom messen
//      Zeit erfassen
//      Daten auf SC Card schreiben
//      Terminierungsbedingung prüfen und ggf. stoppen
//      inkl. Dateischließen!
//
/////////////////////////////////////////////////////////////////
void loop() {


  // Vorgegebene Zeit warten
  delay(ZeitZwischenMessungen * 1000);
  digitalWrite(LED,HIGH);
  delay(100);
  digitalWrite(LED,LOW);


  // Datum & Zeit holen:

  DateTime jetzt = now();
  //time_t teensyTime = getTeensy3Time();

  //DateTime jetzt = rtc.now();

  //ausgebenZeit(jetzt);  // und schreiben am seriellen Monitor

  digitalClockDisplay();
  // Messwert
  float stromMesswert =stromMessen();
  float spannungsMesswert =spannungMessen();
  // Jetzt Daten schreiben:
  schreibeMessung(stromMesswert, spannungsMesswert, jetzt);

  // Zufallsmechanismus für Stopp der Messungen
  if (random(7) == 1) {
    //dateiSchliessen();
  }
}
