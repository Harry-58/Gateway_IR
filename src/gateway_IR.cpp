

#define mitFritzBox

#include <Streaming.h>
#include <myMacros.h>
#include <myMqtt.h>
#include <myUtils.h>

#define DEBUG__EIN  //"Schalter" zum aktivieren von DEBUG-Ausgaben
#include <myDebug.h>


#include <IotWebConf.h>       // https://github.com/prampec/IotWebConf

// UpdateServer includes
#ifdef ESP8266
# include <ESP8266HTTPUpdateServer.h>
#elif defined(ESP32)
// For ESP32 IotWebConf provides a drop-in replacement for UpdateServer.
# include <IotWebConfESP32HTTPUpdateServer.h>
#endif

//#define IR_SEND_PIN 33  //muss vor #include <IRremote.h> definiert werden damit default in private/IRremoteBoardDefs.h überschrieben wird
      // BUG: geht nicht -->  in platformio.ini definieren   build_flags =  -D IR_SEND_PIN=33
#define IR_RECV_PIN 34
#include <IRremote.h>

// -- Initial name of the Thing. Used e.g. as SSID of the own Access Point.
const char thingName[] = "gatewayIR";

// -- Initial password to connect to the Thing, when it creates an own Access Point.
const char wifiInitialApPassword[] = WLAN_AP_PASS;

#define STRING_LEN 64
#define NUMBER_LEN 16

// -- Configuration specific key. The value should be modified if config structure was changed.
#define CONFIG_VERSION "gw5"

// -- When CONFIG_PIN is pulled to ground on startup, the Thing will use the initial
//      password to buld an AP. (E.g. in case of lost password)
#define CONFIG_PIN 27

// -- Status indicator pin.
//      First it will light up (kept LOW), on Wifi connection it will blink,
//      when connected to the Wifi it will turn off (kept HIGH).
#define STATUS_PIN  LED_BUILTIN

IRsend irsend;
IRrecv irrecv(IR_RECV_PIN);
// http://www.righto.com/2009/08/multi-protocol-infrared-remote-library.html
decode_results results;

#ifdef mitFritzBox
// wenn Fritzbox in anderem Subnetz dann VPN definieren
const int FRITZBOX_PORT = 1012;

WiFiClient Fritzbox;
const uint32_t CHECKCONNECTION = 10000;  // Fritzbox Wiederverbindungs Zeit
uint32_t  connectioncheck;
String lastcalltime     = "";
String lastcallnumber   = "";
String lastcallinnumber = "";
#endif
bool needFritzboxConnect = false;
bool fritzboxConnected = false;

// -- Method declarations.
void handleRoot();
void doRestart();
void mqttMessageReceived(String &topic, String &payload);
void mqttCallback(char *topic, byte *payload, unsigned int length);
bool connectMqtt();
bool connectMqttOptions();
// -- Callback methods.
void wifiConnected();
void configSaved(uint16_t mode);
bool formValidator(iotwebconf::WebRequestWrapper *webRequestWrapper);

DNSServer dnsServer;
WebServer server(80);
WiFiClient net;
myMqtt mqttClient(net);

#ifdef ESP8266
ESP8266HTTPUpdateServer httpUpdater;
#elif defined(ESP32)
HTTPUpdateServer httpUpdater;
#endif

char mqttServerValue[STRING_LEN];
char mqttUserNameValue[STRING_LEN];
char mqttUserPasswordValue[STRING_LEN];
char mqttMainTopicValue[STRING_LEN];

char mqttWillMessage[] = "Offline";

String topicIR = "IR-Recv";

IotWebConf iotWebConf(thingName, &dnsServer, &server, wifiInitialApPassword, CONFIG_VERSION);
// -- Es kann auch das Alias Format verwendet werden. Bsp.: IotWebConfParameterGroup(...)  (dann ist  #include <IotWebConfUsing.h> notwendig)
iotwebconf::ParameterGroup mqttGroup                = iotwebconf::ParameterGroup("mqttConf", "MQTT");
iotwebconf::TextParameter mqttServerParam           = iotwebconf::TextParameter("Server", "mqttServer", mqttServerValue, STRING_LEN, MQTT_HOST);
iotwebconf::TextParameter mqttUserNameParam         = iotwebconf::TextParameter("User", "mqttUser", mqttUserNameValue, STRING_LEN, MQTT_USER);
iotwebconf::PasswordParameter mqttUserPasswordParam = iotwebconf::PasswordParameter("Password", "mqttPass", mqttUserPasswordValue, STRING_LEN, MQTT_PASS);
iotwebconf::TextParameter mqttMainTopicParam        = iotwebconf::TextParameter("Main Topic", "mqttTopic", mqttMainTopicValue, STRING_LEN,"Gateway1");

#ifdef mitFritzBox
char fritzboxAktivValue[STRING_LEN];
char fritzboxIpValue[STRING_LEN];

iotwebconf::ParameterGroup fritzboxGroup          = iotwebconf::ParameterGroup("fritzboxConf", "Fritzbox (VPN)");
iotwebconf::TextParameter fritzboxIPParam         = iotwebconf::TextParameter("IP", "fritzboxIP", fritzboxIpValue, STRING_LEN,"192.168.188.1");
iotwebconf::CheckboxParameter fritzboxUsedParam   = iotwebconf::CheckboxParameter("Aktiv", "fritzboxAktiv", fritzboxAktivValue, STRING_LEN, false);
#endif


char irTimervalue[NUMBER_LEN];
iotwebconf::ParameterGroup irGroup        = iotwebconf::ParameterGroup("irConf", "Infrarot");
iotwebconf::NumberParameter irTimerParam  = iotwebconf::NumberParameter("Timer","irTimer",irTimervalue, NUMBER_LEN, "1000", "0..2000", "min='0' max='2000' step='100'");


//**************************************************
// -- Javascript block will be added to the header.
const char CUSTOMHTML_SCRIPT_INNER[] PROGMEM =
    "\n\
document.addEventListener('DOMContentLoaded', function(event) {\n\
  let elements = document.querySelectorAll('input[type=\"password\"]');\n\
  for (let p of elements) {\n\
  	let btn = document.createElement('INPUT'); btn.type = 'button'; btn.value = '🔓'; btn.style.width = '35px'; p.style.width = '90%'; p.parentNode.insertBefore(btn,p.nextSibling);\n\
    btn.onclick = function() { if (p.type === 'password') { p.type = 'text'; btn.value = '🔐'; } else { p.type = 'password'; btn.value = '🔓'; } }\n\
  };\n\
});\n";
// -- HTML element will be added inside the body element.
// const char CUSTOMHTML_BODY_INNER[] PROGMEM = "<div><img src='data:image/png;base64,
// iVBORw0KGgoAAAANSUhEUgAAAS8AAABaBAMAAAAbaM0tAAAAMFBMVEUAAQATFRIcHRsmKCUuLy3TCAJnaWbmaWiHiYYkquKsqKWH0vDzurrNzsvg5+j9//wjKRqOAAAAAWJLR0QAiAUdSAAAAAlwSFlzAAALEwAACxMBAJqcGAAAAAd0SU1FB+MHBhYoILXXEeAAAAewSURBVGje7ZrBaxtXEIfHjlmEwJFOvbXxKbc2/gfa6FRyKTkJX1KyIRCCIfXJ+BLI0gYTAknOwhA7FxMKts7GEJ9CLmmtlmKMCZZzKbGdZBXiOIqc3e2bmfd232pXalabalXwg4jNWm/1aWbeb+bNE3iDORpwDHYMdgz2fwBbsQcVbO0YLBnY7vyAgn2YzzzgOoDdD67dSrZgRxqLdzSnQc5l68oPlQ5gK13iDSDRRxV6AXMr87r7unnSmQW4umT3Cczb1U1W6eJJxwQchX6BhUwWmCnqyRr0FyxkMl9hjyKedAHyde/Vw/N9AwuZrLMnW2D0Nfjbo0xZLAJ7CKP9BgtM5m7dmr31q/2vzxdgLdOo0vXeLHyxrN6wWCrQ6jWW+XmzcLXeO5i3y37buszhDbl67IyTGpgj3kau3aQZ9xjsL1wbrqnueAv4sBRgrFk3wR/Dd7W/ripX6jG2oz6cJxhMTouWV2+OwhKH1TuYty/+XQJ9/KRFILu2KT5s2wezll2LTAiTtrtJjA3IGeINbhGu2XsAVRQYo47q1zuYGA+I5wRcuQJFcTFU9XXjvi8XQviXJVgeTShevLsscSfxkWTCJhmL7ph4o5kOjFxjLDHC4hjAiPLyXFhgrzHYFDoqF0hJnsD4SVOEl/ccvpEKzBEoUNU0C+A7VtxAaIXvgsCqogmNQHxz+Mg8Bzw+xxF3mupGCrA/hLl0lXBKMFynANOL7U1cb3kCa0vlyNhgoTPpb54wuVS+WgowR4QVcnycmVlXaxC+1AJMjS2TmJSPpI7h8PWkpNaPurGTAuxA6s77clmCCQcM214lJjHhOzWwpsJQCgwaWCGd8qP5KRp8sGfraMSz3m40CSxgbGtg1mcHc229cMCAvb7+rlxGx5bLFzHsjLiJ5JgATFjwjvStBChCW65I6soPlbk1Txd1wVMuT7DdbuC6gnqnlBmAsZi5GpjpT+sx+I8qlcqcLf9zhnnEmHnqvWOHWsKXcRY7pYMxjaOBLfiy06tcuKuI9oimYui/K8sxTQ5VutQ9xthdhxpYzU/2KQRWorEsPS4H4zr550SoHqvLD6tGXbmggTVZEZ2pdClpX5B5Y/yfpzNhNDMUZA24ve29sigcAzDxscveZk4D4xJ8yyykTeL7867vMxH80xJsQmW9YAaPkyEwSu3wlQambVqaKcseR0XFR1yOzxjtR7Um2sBydgiMMAxbB+OcatTTF4otNZP1VSyCmWkUjKZWs2LZPWuCcdv2wmDeInxf1XUMq5PxEX6fk660bsJ5H4wWgXDj03W05Gi23Z6m+kos/NPlCyoh5AcGbIJWwEVVzGQMxjHmoBMvyBUgLTZ6VFldtTMDcyj3PMaQv6CXPg6C4VjLCMwli5FKTFz/jQONXXzKe726UqnMZwTmjYvV5waiP1EP771fZ2UxzzRUbaHQbnAhcS/j5nBtGKvWJ9MBGa1LC+yMwd5yCfWeJJ8GWswtGpm20/dRL874wv/eB2uEMlL/2+m4ox3L+fqKLy+fcAX7mUPsk7pqCuyIyusa+ZLAHquE1OryHICEVigkBXMr1ALg6vzl7yRnMiFZ8E12YO6KVHXTd9vMzA25xRmqZwemuHA3EcZojcPX3mcG+/TgX6k8CjY/ubZOAIrY/YzAdoMPdk34IfjrYpFaFEed0vd/DebqDR/fZO5Dv9e5MtcFLGhdmyzQtOXEXtWkrZrYMBnsFuTXaYlyu9re+t70q/boeeWOrAoXikF3ONKGCoOp1jUTMd/fqjMswHCjMtUG5oyLy5F6uPW94R8GxYDVcmoZwFDO9jvDa13AVOt6h3KEi2HpgGo7chObW40a2AJd50PzXegG9ielbKc0PDKpLdB4Z0ow1bpmGaSe7HMwtt0Fbsnm8vYLWkOaXDjYxd0gDdDni12zuzUVD9YiKWtttx3dxFaJEky1rh1qJR/gporymIM4DWomkHs1sENys8UNEDX/jbYdi4AdlrTmsH5qstYZzG9dkwWw3SQbxpZ4VkOG370QGPekqF0bzNdXbRQMTteTpWOtdU12wpcWb6xq4pKbRTsc/T6YRV+fzgKC+QfadizqytJ5LymYHfQJCmy2Qy6VGmILzVUTMWlg3KhxECeYL+T8tt0J7MW4kRjMv8Sv3MJAO1AN2IKkaQcrEg11uLT5ppCNOx3AAE73DoaHrIfydKQ7mMwZwaWcLyUkBqyWZNfdDoYStiPPk3oB85xFdZIYAUv0g7J2MFQFkv83wfpK4kpEs9hk7RzuL1BPASaoSLG08+l4sFDwh+oBeeYePX07UU0BJrRBBku+O1hILkJgrnpS+KPeDtXTuLIpM7cDYHcFCwlsuIKKB3OgmgYMc/coaz7VSxshMOngSEryb9Y7u7JVPJsGzFPFgSgYlnCNhcCa3JGNJHH/sMBYsl/FB79TTLS7jYJZcrU7bWde9Op2KHuCUwwasdXFWKJ2axSspnYzL9QBtQbGNLJQLAaFol8LWaq6jII9h3RgwY8f9i7DueVw8HvuYikorW/CuWpb69t9WOJ6PCbGyBMbXuYjIvRWXh12DRjYG9j+GZKof7/AdtQxy8C5EgCy/6V1bD0GA/GD9QjY3qVw92JgwIRWfDugYIMxjsESg/0DuDZiWzBJr80AAAAASUVORK5CYII='/></div>\n";

const char CUSTOMHTML_STYLE_INNER[] PROGMEM =
    R"(
#content {width: 50%;}
label {display:block; padding-left:5px;}
#iwcSys {display:grid;grid-template-columns:auto auto auto;}
.iwcThingName {grid-column:1/span 3;}
#mqttConf {display:grid;grid-template-columns:auto auto auto;}
#mqttPass, #iwcApPassword, #iwcWifiPassword {width:70% !important;}
.mqttTopic {grid-column:1/span 3;}
input[type="time"], input[type="date"]  {font-size:1.25em;}
legend {color:white;background-color: grey;}
#fritzboxConf {display:grid;grid-template-columns:auto auto;}
#irConf {display:grid;grid-template-columns:auto auto;}
)";

// .param_time, .giessDauer11, .giessDauer12, .giessDauer13, .giessDauer14{float:left;width: 47%;}";

// -- This is an OOP technique to override behaviour of the existing
// IotWebConfHtmlFormatProvider. Here two method are overridden from
// the original class. See IotWebConf.h for all potentially overridable
// methods of IotWebConfHtmlFormatProvider .
class CustomHtmlFormatProvider : public iotwebconf::HtmlFormatProvider {
 protected:
  String getScriptInner() override { return HtmlFormatProvider::getScriptInner() + String(FPSTR(CUSTOMHTML_SCRIPT_INNER)); }
  /*   String getBodyInner() override
    {
      return
        String(FPSTR(CUSTOMHTML_BODY_INNER)) +
        HtmlFormatProvider::getBodyInner();
    } */
  String getStyleInner() override { return HtmlFormatProvider::getStyleInner() + "\n" + String(FPSTR(CUSTOMHTML_STYLE_INNER)); }
};

// -- An instance must be created from the class defined above.
CustomHtmlFormatProvider customHtmlFormatProvider;



bool needMqttConnect = false;
bool needReset       = false;
int pinState         = HIGH;

uint32_t lastReport                = 0;
uint32_t lastMqttConnectionAttempt = 0;
uint32_t nextStatus                = 0;

void irSetup() {
  irrecv.enableIRIn();  // Start the receiver
}

uint32_t irTestTimer = 0;
uint32_t irLastValue;
uint32_t irLastTimer = 0;

void irLoop() {
  static char irBuffer[64];
#if 1 == 2  // IR-Test Hardware
  pinMode(IR_SEND_PIN, OUTPUT);
  if (millis() > irTestTimer) {
    irTestTimer = millis() + 1000;
    Serial << "IR-Test Send Hardware" << endl;
    digitalWrite(IR_SEND_PIN, true);
    delay(1);
    digitalWrite(IR_SEND_PIN, false);
  }

#endif
#if 1 == 2  // IR-Test Send über IRRemote
  if (millis() > irTestTimer) {
    irTestTimer = millis() + 1000;
    // irsend.sendNEC(0x20DF1AE5, 32);
    // irsend.sendSAMSUNG(0xC2CA807F, 32);
    String sPayLoad = "0xC2CA807F";
    uint32_t irCode = strtoul(sPayLoad.c_str(), NULL, 16);
    Serial.printf("IR-Test-Send -- Samsung Code: 0x%X\n", irCode);
    irrecv.disableIRIn();
    irsend.sendSAMSUNG(irCode, 32);
    irrecv.enableIRIn();
  }
#endif

  if (irrecv.decode(&results)) {
    Serial << "IR-RECV --- Typ: " << results.decode_type << " = " << irrecv.getProtocolString() << "\t  Bits: " << results.bits << "\t  Code: 0x" << _HEX(results.value) << endl;
    irrecv.resume();  // Receive the next value

    if ((results.decode_type > 0) && (!results.isRepeat)) {  // nur wenn Type bekannt und keine Wiederholung
      if ((results.value != irLastValue) || (millis() > irLastTimer)) {  // nur wenn neuer Code oder Timer schon abgelaufen
        irLastTimer = millis() + atoi(irTimervalue);
        irLastValue = results.value;
        snprintf(irBuffer, sizeof(irBuffer), "{\"typ\":%d,\"bits\":%d,\"code\":\"0x%X\"}", results.decode_type, results.bits, results.value);
        mqttClient.publish(topicIR, irBuffer);
      }
    }
  }
}

String topicFritzBox = "FritzBox";
#ifdef mitFritzBox
String topicFritzBoxCaller  =  topicFritzBox + "/caller";
String topicFritzBoxState   =  topicFritzBox + "/state";

void connectFB() {
   if (!fritzboxUsedParam.isChecked()){
      DEBUG__PRINTLN("Fritzbox nicht aktiv");
      return;
   }
  uint16_t reconnectCount = 0;
  Serial.println("Connecting to FritzBox...");
  if (!Fritzbox.connected()){
     mqttClient.publish(topicFritzBox,"-");
  }
  while (!Fritzbox.connected()) {
    if (!Fritzbox.connect(fritzboxIpValue, FRITZBOX_PORT)) {

      reconnectCount++;
      if (reconnectCount > 10) {  // Wenn 10 mal Fehler dann Reboot
                                  // EEPROM.writeShort(0, bootCount);
        // EEPROM.commit();
        // ESP.restart();
        Serial.print("FritzBox connection abgebrochen");
        return;
      }
      Serial.print("FritzBox connection failed:");
      Serial.println("Retrying...");
      iotWebConf.delay(1000);
    }
  }
  Serial << "FritzBox connected " << endl;
  mqttClient.publish(topicFritzBox,"Verbunden");
}

void checkFB() {
  if (!fritzboxUsedParam.isChecked()) {
    DEBUG__PRINTLN("Fritzbox nicht aktiv");
    return;
  }
  // Check connection (refresh)
  if ((millis() - connectioncheck) > CHECKCONNECTION) {
    connectioncheck = millis();
    // Send dummy data to "refresh" connection state
    // Serial << "Fritzbox  refresh" << endl;
    Fritzbox.write("x");

    /// Fritzbox Status überprüfen und wenn nicht neu verbinden
    if (!Fritzbox.connected()) {
      Serial << "Fritzbox neu Verbinden";
      if (!Fritzbox.connect(fritzboxIpValue, FRITZBOX_PORT)) {
        Serial << " ...  nicht möglich" << endl;
        mqttClient.publish(topicFritzBox, "-");
      } else {
        Serial << endl;
        mqttClient.publish(topicFritzBox, "Verbunden");
      }
    }
  }

  int size;

  if ((size = Fritzbox.available()) > 0) {
    uint8_t *msg  = (uint8_t *)malloc(size);
    size          = Fritzbox.read(msg, size);
    msg[size - 1] = '\0';

    // DEBUG_PRINTF((topicFBcall + "-->%s\n").c_str(), msg);
    // mqttClient.publish(topicFBcall.c_str(), (char *)msg);

    if (1 == 1) {
      Serial.print(F("->Msg: "));
      Serial.println((char *)msg);
    }
    uint8_t *copymsgforsplit = (uint8_t *)malloc(size);
    memcpy(copymsgforsplit, msg, size);

    // Analyze incoming msg
    int i = 0;
    String DatumZeit, Type, ConnectionID;
    char *pch, *ptr;
    char type[11];
    pch = strtok_r((char *)copymsgforsplit, ";", &ptr);

    while (pch != NULL) {
      if (1 == 1) {
        Serial.print(F("    ->Splitted part "));
        Serial.print(i);
        Serial.print(F(": "));
        Serial.println(pch);
      }
      switch (i) {
        case 0:  // Date and Time
          DatumZeit = pch;
          break;
        case 1:  // TYPE
          strcpy(type, pch);
          break;
        case 2:  // ConnectionID
          break;
        case 3:  // Anrufende Nummer
          if (strcmp(type, "RING") == 0) {
            if (strstr((char *)msg, ";;"))  // Unknown caller?
            {
              Serial << "Eingehender Anruf -- Unbekanter Anrufer" << endl;
              lastcallnumber = "Unbekanter Anrufer";
            } else {
              Serial << "Eingehender Anruf: " << pch << endl;
              lastcallnumber = pch;
            }
            mqttClient.publish(topicFritzBoxCaller, lastcallnumber);
            mqttClient.publish(topicFritzBoxState, "ring");
          }
          if (strcmp(type, "CONNECT") == 0) {
            Serial << "Anruf angenommen: " << lastcallnumber << endl;
            mqttClient.publish(topicFritzBoxCaller, lastcallnumber);
            mqttClient.publish(topicFritzBoxState, "connect");
          }
          if (strcmp(type, "DISCONNECT") == 0) {
            Serial << " Anruf beendet " << endl;
            mqttClient.publish(topicFritzBoxState, "end");
          }
          break;
        case 4:  // Eigene Nummer
          if (strcmp(type, "RING") == 0) {
            lastcalltime     = DatumZeit;
            lastcallinnumber = pch;
          }
          break;
        case 5:
          break;
        default:
          break;
      }
      i++;
      pch = strtok_r(NULL, ";", &ptr);  // Split next part
    }
    free(msg);
    free(copymsgforsplit);
  }
}
#endif

void setup() {
  Serial.begin(BAUD);
  while (!Serial && (millis() < 3000));
  Serial << "\n\n" << ProjektName << " - " << VERSION << "  (" << BUILDDATE << "  " __TIME__ << ")" << endl;

  mqttGroup.addItem(&mqttServerParam);
  mqttGroup.addItem(&mqttUserNameParam);
  mqttGroup.addItem(&mqttUserPasswordParam);
  mqttGroup.addItem(&mqttMainTopicParam);

  iotWebConf.addParameterGroup(&mqttGroup);

#ifdef mitFritzBox
  fritzboxGroup.addItem(&fritzboxIPParam);
  fritzboxGroup.addItem(&fritzboxUsedParam);

  iotWebConf.addParameterGroup(&fritzboxGroup);
#endif

  irGroup.addItem(&irTimerParam);
  iotWebConf.addParameterGroup(&irGroup);

    // -- Applying the new HTML format to IotWebConf.
  iotWebConf.setHtmlFormatProvider(&customHtmlFormatProvider);

  iotWebConf.setStatusPin(STATUS_PIN,HIGH);
  iotWebConf.setConfigPin(CONFIG_PIN);

  iotWebConf.setConfigSavedCallback(&configSaved);
  iotWebConf.setFormValidator(&formValidator);
  iotWebConf.setWifiConnectionCallback(&wifiConnected);

  // iotWebConf.forceApMode(true);   // Wenn nur Access-Point gestartet werden soll
  iotWebConf.skipApStartup();  // Ohne Access-Point, sofort Station-Mode starten


  // -- Define how to handle updateServer calls.
  iotWebConf.setupUpdateServer(
    [](const char* updatePath) { httpUpdater.setup(&server, updatePath); },
    [](const char* userName, char* password) { httpUpdater.updateCredentials(userName, password); });

  // -- Initializing the configuration.
  bool validConfig = iotWebConf.init();

  if (!validConfig) {
    iotWebConf.getWifiSsidParameter();
    iotWebConf.getWifiPasswordParameter();

    // nicht notwendig, wird durch lib automatisch auf Default gesetzt
    /*
    mqttServerValue[0]       = '\0';
    mqttUserNameValue[0]     = '\0';
    mqttUserPasswordValue[0] = '\0';
    mqttMainTopicValue[0]    = '\0';

    fritzboxAktivValue[0] = '\0';
    fritzboxIpValue[0]   = '\0';
    */
  }

  // -- Set up required URL handlers on the web server.
  server.on("/", handleRoot);
  server.on("/config", [] { iotWebConf.handleConfig(); });
  server.onNotFound([]() { iotWebConf.handleNotFound(); });

  server.on("/restart",  doRestart);

  mqttClient.setBaseTopic(mqttMainTopicValue);
  mqttClient.setServer(mqttServerValue, 1883);
  mqttClient.setCallback(mqttCallback);

  irSetup();

  Serial.println("Ready.");
}

void loop() {
  // -- doLoop should be called as frequently as possible.
  iotWebConf.doLoop();
  mqttClient.loop();
  irLoop();

#ifdef mitFritzBox
  if (fritzboxUsedParam.isChecked()) {
    if (needFritzboxConnect) {
      needFritzboxConnect = false;
      connectFB();
      fritzboxConnected = true;
    }
    if (fritzboxConnected) {
      checkFB();
    }
  }
#endif

  if (needMqttConnect) {
    if (connectMqtt()) {
      needMqttConnect = false;
    }
  } else if ((iotWebConf.getState() == iotwebconf::OnLine) && (!mqttClient.connected())) {
    // Serial.println("MQTT reconnect");
    connectMqtt();
  }

  if (needReset) {
    Serial.println("Rebooting after 1 second.");
    iotWebConf.delay(1000);
    ESP.restart();
  }

  if (!needMqttConnect && (iotWebConf.getState() == iotwebconf::OnLine) && (millis() > nextStatus)) {
    nextStatus = millis() + (1000 * 60 * 10);  // 10min
    mqttClient.publish("IP", WiFi.localIP().toString());
  }
}

/**
 * Handle web requests to "/" path.
 */
void handleRoot() {
  // -- Let IotWebConf test and handle captive portal requests.
  if (iotWebConf.handleCaptivePortal()) {
    // -- Captive portal request were already served.
    return;
  }
  String s = "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\", name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/>";
  s += (String)"<title>" + iotWebConf.getThingName() + "</title></head>";
  s += (String)"<body> <h3  style=\"display: inline;\">" + iotWebConf.getThingName() + "</h3> ";
  s += (String)"( " + ProjektName + "-" + VERSION + " &nbsp;&nbsp;- " + BUILDDATE +  " - "+ __TIME__ + ")<br>" ;
  s += "<ul>";
  s += "<li>MQTT server: ";
  s += mqttServerValue;
  if ( fritzboxUsedParam.isChecked()){
    s += "<li>Fritzbox IP: ";
    s += fritzboxIpValue;
  }
  s += "</ul>";
  s += "Gehe zur <a href='config'>Konfigurations Seite</a> um Werte zu ändern.";
  s += "</body></html>\n";

  server.send(200, "text/html", s);
}

void doRestart() {
  String s = "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\", name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/>";
  s += (String)"<title>" + iotWebConf.getThingName() + "</title></head>";
  s += "<body><br><br>Restart in 5 Sekunden";
  s += "</body></html>\n";
  server.send(200, "text/html", s);
  DEBUG__PRINTLN("Restart in 5 Sekunden");

  iotWebConf.delay(5000);
  WiFi.disconnect();
  delay(100);
  DEBUG__PRINTLN("Restart now");
  ESP.restart();
  delay(1000);
}

void wifiConnected() {
  needMqttConnect     = true;
  // WiFi.setHostname(thingName);
  Serial.print("Hostname:");
  Serial.println(WiFi.getHostname());
}

void configSaved(uint16_t mode) {
  Serial.println("Configuration was updated.");
  if (mode == 1) {  // Restart notwendig?
    needReset = true;
  }
}

bool formValidator(iotwebconf::WebRequestWrapper *webRequestWrapper) {
  Serial.println("Validating form.");
  bool valid = true;

  int l = webRequestWrapper->arg(mqttServerParam.getId()).length();
  if (l < 3) {
    mqttServerParam.errorMessage = "Please provide at least 3 characters!";
    valid                        = false;
  }
  return valid;
}

bool connectMqtt() {
  unsigned long now = millis();
  if (5000 > now - lastMqttConnectionAttempt) {
    // Do not repeat within 5 sec.
    return false;
  }
  Serial.println("Connecting to MQTT server...");
  if (!connectMqttOptions()) {
    lastMqttConnectionAttempt = now;
    return false;
  }
  Serial.println("MQTT Connected!");
  mqttClient.subscribe("IR-Send/#");
  mqttClient.publish("", "Online",true);  // Will-Topic überschreiben
  #ifdef mitFritzBox
    if ( fritzboxUsedParam.isChecked()){
      needFritzboxConnect = true;
      fritzboxConnected = false;
      mqttClient.publish(topicFritzBox,"aktiv");
    }else{
       needFritzboxConnect = false;
       fritzboxConnected = false;
       mqttClient.publish(topicFritzBox,"inaktiv");
    }
  #else
    needFritzboxConnect = false;

    mqttClient.publish(topicFritzBox,"inaktiv");
  #endif
  return true;
}

bool connectMqttOptions() {
  bool result;
  String mqttID = mqttClient.makeClientIDfromMac(iotWebConf.getThingName());
  Serial.print("mqttID:"); Serial.println(mqttID);

  if (mqttUserPasswordValue[0] != '\0') {
    result = mqttClient.connect(mqttID.c_str(), mqttUserNameValue, mqttUserPasswordValue, mqttMainTopicValue, 0, true, mqttWillMessage);
  } else {
    result = mqttClient.connect(mqttID.c_str(), mqttMainTopicValue, 0, true, mqttWillMessage);
  }
  return result;
}

void mqttCallback(char *topic, byte *payload, unsigned int length) {
  String sPayLoad="";
  if (payload != nullptr) {
    sPayLoad = (String((const char *)payload)).substring(0, length);
  }
  Serial.println("Incoming: " + String(topic) + " >" + sPayLoad + "<");

  if (strstr(topic, "/IR-Send/Samsung")) {
    DEBUG__PRINT("Samsung  ");
    if (sPayLoad.length() > 2) {  // nur wenn payload vorhanden
      uint32_t irCode = strtoul(sPayLoad.c_str(), NULL, 16);
      Serial.printf("IR_Send -- Code: 0x%X\n", irCode);
      irrecv.disableIRIn();
      irsend.sendSAMSUNG(irCode, 32);
      irrecv.enableIRIn();
    }
  } else if (strstr(topic, "/IR-Send/NEC")) {
    DEBUG__PRINT("NEC  ");
    if (sPayLoad.length() > 2) {  // nur wenn payload vorhanden
      uint32_t irCode = strtoul(sPayLoad.c_str(), NULL, 16);
      Serial.printf("IR_Send -- Code: 0x%X\n", irCode);
      irrecv.disableIRIn();
      irsend.sendNEC(irCode, 32);
      irrecv.enableIRIn();
    }
  }
}