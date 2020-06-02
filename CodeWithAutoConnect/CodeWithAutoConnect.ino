#if defined(ARDUINO_ARCH_ESP8266)
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#define GET_CHIPID()  (ESP.getChipId())
#elif defined(ARDUINO_ARCH_ESP32)
#include <WiFi.h>
#include <WebServer.h>
#include <SPIFFS.h>
#include <HTTPClient.h>
#define GET_CHIPID()  ((uint16_t)(ESP.getEfuseMac()>>32))
#endif
#include <FS.h>
#include <PubSubClient.h>
#include <AutoConnect.h>

#define PARAM_FILE      "/param.json"
#define AUX_MQTTSETTING "/mqtt_setting"
#define AUX_MQTTSAVE    "/mqtt_save"
#define AUX_MQTTCLEAR   "/mqtt_clear"

// Adjusting WebServer class with between ESP8266 and ESP32.
#if defined(ARDUINO_ARCH_ESP8266)
typedef ESP8266WebServer  WiFiWebServer;
#elif defined(ARDUINO_ARCH_ESP32)
typedef WebServer WiFiWebServer;
#endif

#define MQTT_USER_ID "no_one"

AutoConnect  portal;
AutoConnectConfig config;
WiFiClient   wifiClient;
PubSubClient mqttClient(wifiClient);
int lastDataPublishTime = 0;

// JSON Document variables for Energy Data
StaticJsonDocument<100> energyDataJsonObject;

// MQTT Configuration parameters
String clientId = "ARandom_client_id_with_123";

// Parameters red from the saved JSON file
String mqttBrokerIP;
String mqttBrokerPort;
String mqttUsername;
String mqttPassword;
String mqttDataPublishTopic;
int mqttDataPublishInterval;
 
// Connect to the MQTT broker
bool mqttConnect() {
  uint8_t retry = 3;
  while (!mqttClient.connected()) {
    
    if (mqttBrokerIP.length() <= 0)
      break;

    mqttClient.setServer(mqttBrokerIP.c_str(),mqttBrokerPort.toInt());
    Serial.println(String("Attempting MQTT broker connection:") + mqttBrokerIP);
    
    if (mqttClient.connect(clientId.c_str(), mqttUsername.c_str(), mqttPassword.c_str())) {
      Serial.println("Connection to MQTT broker established:" + String(clientId));
      return true;
    } else {
      Serial.println("Connection to MQTT broker failed:" + String(mqttClient.state()));
      if (!--retry)
        break;
      delay(3000);
    }
  }
  return false;
}

// Publish the Energy Data
void mqttPublish() {
  String energyDataString;
  serializeJson(energyDataJsonObject, energyDataString);
  mqttClient.publish(mqttDataPublishTopic.c_str() ,energyDataString.c_str());
}

// Save user entered MQTT params
String saveParams(AutoConnectAux& aux, PageArgument& args) {
  mqttBrokerIP = args.arg("mqtt_broker_url");
  mqttBrokerIP.trim();
  mqttBrokerPort = args.arg("mqtt_broker_port");
  mqttBrokerPort.trim();
  mqttUsername = args.arg("mqtt_username");
  mqttUsername.trim();
  mqttPassword = args.arg("mqtt_password");
  mqttPassword.trim();
  mqttDataPublishTopic = args.arg("mqtt_topic");
  mqttDataPublishTopic.trim();
  mqttDataPublishInterval = args.arg("update_interval").toInt() * 1000;
  
  // The entered value is owned by AutoConnectAux of /mqtt_setting.
  // To retrieve the elements of /mqtt_setting, it is necessary to get
  // the AutoConnectAux object of /mqtt_setting.
  File param = SPIFFS.open(PARAM_FILE, "w");
  portal.aux("/mqtt_setting")->saveElement(param, { "mqtt_broker_url", "mqtt_broker_port", "mqtt_username", "mqtt_password", "mqtt_topic", "update_interval"});
  param.close();

//   Echo back saved parameters to AutoConnectAux page.
  AutoConnectText&  echo = aux["parameters"].as<AutoConnectText>();
  echo.value ="Broker IP: " + mqttBrokerIP + "<br>";
  echo.value += "Port: " + mqttBrokerPort + "<br>";
  echo.value += "Username: " + mqttUsername + "<br>";
  echo.value += "Password: " + mqttPassword + "<br>";
  echo.value += "MQTT Topic: " +mqttDataPublishTopic + "<br>";
  echo.value += "Update Interval (In Seconds): "+ String(mqttDataPublishInterval / 1000) + " sec.<br>";

 return String("");
}

String loadParams(AutoConnectAux& aux, PageArgument& args) {
  (void)(args);
  File param = SPIFFS.open(PARAM_FILE, "r");
  if (param) {
    aux.loadElement(param);
    param.close();
  }
  else
    Serial.println(PARAM_FILE " open failed");
  return String("");
}



void handleRoot() {
  String  content =
    "<html>"
    "<head>"
    "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
    "</head>"
    "<body>"
    "<p style=\"padding-top:10px;text-align:center\">" AUTOCONNECT_LINK(COG_24) "</p>"
    "</body>"
    "</html>";

  WiFiWebServer&  webServer = portal.host();
  webServer.send(200, "text/html", content);
}

// Load AutoConnectAux JSON from SPIFFS.
bool loadAux(const String auxName) {
  bool  rc = false;
  String  fn = auxName + ".json";
  File fs = SPIFFS.open(fn.c_str(), "r");
  if (fs) {
    rc = portal.load(fs);
    fs.close();
  }
  else
    Serial.println("SPIFFS open failed: " + fn);
  return rc;
}

// Load MQTT Settings

void loadMQTTSettings(){
   AutoConnectAux* setting = portal.aux(AUX_MQTTSETTING);
   if (setting) {
    PageArgument  args;
    AutoConnectAux& mqtt_setting = *setting;
    loadParams(mqtt_setting, args);
    
    AutoConnectInput& brokerIpElement = mqtt_setting["mqtt_broker_url"].as<AutoConnectInput>();
    mqttBrokerIP = brokerIpElement.value;

    AutoConnectInput& brokerPortElement = mqtt_setting["mqtt_broker_port"].as<AutoConnectInput>();
    mqttBrokerPort = brokerPortElement.value;

    AutoConnectInput& brokerUsernameElement = mqtt_setting["mqtt_username"].as<AutoConnectInput>();
    mqttUsername = brokerUsernameElement.value;

    AutoConnectInput& brokerPasswordElement = mqtt_setting["mqtt_password"].as<AutoConnectInput>();
    mqttPassword = brokerPasswordElement.value;

    AutoConnectInput& mqttTopicElement = mqtt_setting["mqtt_topic"].as<AutoConnectInput>();
    mqttDataPublishTopic = mqttTopicElement.value;

    AutoConnectInput& publishIntervalElement = mqtt_setting["update_interval"].as<AutoConnectInput>();
    mqttDataPublishInterval = publishIntervalElement.value.toInt() * 1000 ;

    config.homeUri = "/";
    portal.config(config);

    portal.on(AUX_MQTTSETTING, loadParams);
    portal.on(AUX_MQTTSAVE, saveParams);
  }
  else
    Serial.println("aux. load error");

}
void setup() {
  delay(1000);
  Serial.begin(115200);
  Serial.println();
  SPIFFS.begin();

  loadAux(AUX_MQTTSETTING);
  loadAux(AUX_MQTTSAVE);

  loadMQTTSettings();
  
  Serial.print("WiFi ");
  if (portal.begin()) {
    config.bootUri = AC_ONBOOTURI_HOME;
    Serial.println("connected:" + WiFi.SSID());
    Serial.println("IP:" + WiFi.localIP().toString());
  } else {
    Serial.println("connection failed:" + String(WiFi.status()));
    while (1) {
      delay(100);
      yield();
    }
  }

  WiFiWebServer&  webServer = portal.host();
  webServer.on("/", handleRoot);
//  webServer.on(AUX_MQTTCLEAR, handleClearChannel);
}

void loop() {
  portal.handleClient();
  mqttClient.loop();
  if (mqttDataPublishInterval > 0) {
    if ( (millis() - lastDataPublishTime) > mqttDataPublishInterval) {
      updateMeterData();
      if (!mqttClient.connected()) {
        mqttConnect();
      }
      mqttPublish();
      lastDataPublishTime = millis();
    }
  }
}

// Fetch meter data through UART interface ()
void updateMeterData(){
  Serial.print(" Updating meter data !!");
  serializeJsonPretty(energyDataJsonObject, Serial);
  energyDataJsonObject.clear();
  energyDataJsonObject["POWER"] = 4;
  energyDataJsonObject["VOLT"] = 230;
  energyDataJsonObject["AMP"] = 7.1;
  energyDataJsonObject["ENERGY"] = 0.1;
}
