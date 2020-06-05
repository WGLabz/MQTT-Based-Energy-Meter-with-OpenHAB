#include <U8x8lib.h> // Library for the OLED module
#include <SoftwareSerial.h> // Library for SoftwareSerial
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

#define DEBUG true

// SoftwareSerial Pins, used to connect the PZEM module
int softRxPin = 13;
int softTxPin = 21;

// Function prototype declarations
void clearOLEDLine(int line);
void setMessageOnOLED(char* message);

// Object for the OLED
U8X8_SSD1306_128X64_NONAME_SW_I2C display_(/* clock=*/ 15, /* data=*/ 4, /* reset=*/ 16);

// Object for SoftwareSerial
SoftwareSerial pzemSerialObj;

uint8_t pzem_response_buffer[8]; // Reponse buffer for PZEM serial communication

float voltage = 0.00;
float current = 0.00;
float power = 0.00;
float energy = 0.00;

// Commands for PZEM004T V2 Module
uint8_t current_[7] = {0xB1, 0xC0, 0xA8, 0x01, 0x01, 0x00, 0x1B};
uint8_t address_[7] = {0xB4, 0xC0, 0xA8, 0x01, 0x01, 0x00, 0x1E};
uint8_t energy_[7]  = {0xB3, 0xC0, 0xA8, 0x01, 0x01, 0x00, 0x1D};
uint8_t voltage_[7] = {0xB0, 0xC0, 0xA8, 0x01, 0x01, 0x00, 0x1A};
uint8_t power_[7] =   {0xB2, 0xC0, 0xA8, 0x01, 0x01, 0x00, 0x1C};

//Timing Intervals
int displayUpdateInterval = 60; // In seconds
int lastDisplayUpdateTime = 0;

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

    mqttClient.setServer(mqttBrokerIP.c_str(), mqttBrokerPort.toInt());
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
  mqttClient.publish(mqttDataPublishTopic.c_str() , energyDataString.c_str());
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
  echo.value = "Broker IP: " + mqttBrokerIP + "<br>";
  echo.value += "Port: " + mqttBrokerPort + "<br>";
  echo.value += "Username: " + mqttUsername + "<br>";
  echo.value += "Password: " + mqttPassword + "<br>";
  echo.value += "MQTT Topic: " + mqttDataPublishTopic + "<br>";
  echo.value += "Update Interval (In Seconds): " + String(mqttDataPublishInterval / 1000) + " sec.<br>";

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

void loadMQTTSettings() {
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
  pzemSerialObj.begin(9600, SWSERIAL_8N1, softRxPin, softTxPin, false, 200, 10);
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
  //Intialize the OLED module
  initializeOLED();
  setMessageOnOLED("Starting....");

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
  if ((millis() - lastDisplayUpdateTime) > (displayUpdateInterval * 1000)) {
    lastDisplayUpdateTime = millis();
    updateMeterData();
    display_.drawString(8, 2, (String(voltage) + "V").c_str ());
    display_.drawString(8, 3, (String(current) + "A").c_str ());
    display_.drawString(8, 4, (String(power) + "W").c_str ());
    display_.drawString(8, 5, (String(energy) + "Kwh").c_str ());
  }
}

void initializeOLED() {
  display_.begin();
  display_.setFont(u8x8_font_pxplusibmcgathin_r);
  display_.drawString(0, 0, "WGLabz");

  //  Display Energy params label on OLED
  display_.drawString(0, 2, "Voltage: ");
  display_.drawString(0, 3, "Current: ");
  display_.drawString(0, 4, "Power: ");
  display_.drawString(0, 5, "Energy: ");

  display_.drawString(8, 2, "230.00V");
  display_.drawString(8, 3, "0.04A");
  display_.drawString(8, 4, "2.00W");
  display_.drawString(8, 5, "56.00Kwh");

  display_.drawString(0, 7, "LoRaWAN Status");
}
void clearOLEDLine(int line) {
  //  Print a blank text line to the OLED display in the passed line val.
  display_.drawString(0, line, "                    ");
}

void setMessageOnOLED(char* message) {
  //    delay(2000);
  clearOLEDLine(7);
  display_.drawString(0, 7, message);
}

// PZEM Module COmmunication related functions

bool updateMeterData() {
  voltage = fetchData(voltage_) ? (pzem_response_buffer[1] << 8) + pzem_response_buffer[2] + (pzem_response_buffer[3] / 10.0) : -1;
  if (DEBUG)
    printPzemResponseBuffer();
  current = fetchData(current_) ? (pzem_response_buffer[1] << 8) + pzem_response_buffer[2] + (pzem_response_buffer[3] / 100.0) : -1;
  if (DEBUG)
    printPzemResponseBuffer();
  power = fetchData(power_) ? (pzem_response_buffer[1] << 8) + pzem_response_buffer[2] : -1;
  if (DEBUG)
    printPzemResponseBuffer();
  energy = fetchData(energy_) ? ((uint32_t)pzem_response_buffer[1] << 16) + ((uint16_t)pzem_response_buffer[2] << 8) + pzem_response_buffer[3] : -1;
  if (DEBUG)
    printPzemResponseBuffer();
}
bool fetchData(uint8_t *command) {
  while (pzemSerialObj.available() > 0) { //Empty in buffer if it holds any data
    pzemSerialObj.read();
  }
  for (int count = 0; count < 7; count++)
    pzemSerialObj.write(command[count]);

  int startTime = millis();
  int receivedBytes = 0;

  while ((receivedBytes < 7 ) && ((millis() - startTime) < 10000)) { // Waits till it recives 7 bytes or a timout of 10 second happens
    if (pzemSerialObj.available() > 0) {
      pzem_response_buffer[receivedBytes++] = (uint8_t)pzemSerialObj.read();
    }
    yield();
  }
  return receivedBytes == 7;
}

void printPzemResponseBuffer() { //For Debugging
  Serial.print("Buffer Data: ");
  for (int x = 0 ; x < 7; x++) {
    Serial.print(int(pzem_response_buffer[x]));
    Serial.print(" ");
  }
  Serial.println("");
}
