#include <U8x8lib.h> // Library for the OLED module
#include <SoftwareSerial.h> // Library for SoftwareSerial

// Include the configuration file
#include "config.h"

// Function prototype declarations
void clearOLEDLine(int line);
void setMessageOnOLED(char* message);

// Object for the OLED
U8X8_SSD1306_128X64_NONAME_SW_I2C display_(/* clock=*/ 15, /* data=*/ 4, /* reset=*/ 16);

// Object for SoftwareSerial
SoftwareSerial pzemSerialObj;

uint8_t pzem_response_buffer[8]; // Reponse buffer for PZEM serial communication

int lastDisplayUpdateTime = 0;
int lastMQTTDataPublishTime = 0;

float voltage = 0.00;
float current = 0.00;
float power = 0.00;
float energy = 0.00;

// Commands for PZEM004T V2 Module
uint8_t current_[7] = {0xB1,0xC0,0xA8,0x01,0x01,0x00,0x1B};
uint8_t address_[7] = {0xB4,0xC0,0xA8,0x01,0x01,0x00,0x1E};
uint8_t energy_[7]  = {0xB3,0xC0,0xA8,0x01,0x01,0x00,0x1D};
uint8_t voltage_[7] = {0xB0,0xC0,0xA8,0x01,0x01,0x00,0x1A};
uint8_t power_[7] =   {0xB2,0xC0,0xA8,0x01,0x01,0x00,0x1C};


void printHex2(unsigned v) {
  v &= 0xff;
  if (v < 16)
    Serial.print('0');
  Serial.print(v, HEX);
}

void setup() {
  delay(5000);

//  Intitialize the serial interface for Debug
  while (!Serial)
  ;
  Serial.begin(115200);
  Serial.println(F("Starting"));

  // Initalize SoftwareSerial interface for PZEM module
  pzemSerialObj.begin(9600, SWSERIAL_8N1, softRxPin, softTxPin, false, 200, 10);

  //Intialize the OLED module
  initializeOLED();
  setMessageOnOLED("Starting....");
  
  updateMeterData();
}

void loop() {
  if((millis() - lastDisplayUpdateTime) > (displayUpdateInterval * 1000)){
    lastDisplayUpdateTime = millis();
    updateMeterData();
    display_.drawString(8, 2,(String(voltage)+"V").c_str ());
    display_.drawString(8, 3, (String(current)+"A").c_str ());
    display_.drawString(8, 4, (String(power)+"W").c_str ());
    display_.drawString(8, 5, (String(energy)+"Kwh").c_str ());
  }
  if((millis() - mqttDataPublishInterval) > (mqttDataPublishInterval * 1000)){
    lastMQTTDataPublishTime = millis();
    updateMeterData();
    
  }
}
void initializeOLED(){
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
void clearOLEDLine(int line){
//  Print a blank text line to the OLED display in the passed line val.
  display_.drawString(0, line, "                    ");
}

void setMessageOnOLED(char* message){
//    delay(2000);
    clearOLEDLine(7);
    display_.drawString(0, 7, message);
}

// PZEM Module COmmunication related functions

bool updateMeterData(){
    voltage = fetchData(voltage_) ? (pzem_response_buffer[1] << 8) + pzem_response_buffer[2] +(pzem_response_buffer[3] / 10.0) : -1;
    if(DEBUG)
      printPzemResponseBuffer();
    current = fetchData(current_)? (pzem_response_buffer[1] << 8) + pzem_response_buffer[2]+ (pzem_response_buffer[3] / 100.0) : -1;   
    if(DEBUG)
      printPzemResponseBuffer();
    power = fetchData(power_) ? (pzem_response_buffer[1] << 8) + pzem_response_buffer[2]: -1;
    if(DEBUG)
      printPzemResponseBuffer();
    energy = fetchData(energy_) ? ((uint32_t)pzem_response_buffer[1] << 16) + ((uint16_t)pzem_response_buffer[2] << 8) + pzem_response_buffer[3] : -1;
    if(DEBUG)
      printPzemResponseBuffer();
}
bool fetchData(uint8_t *command){
  while(pzemSerialObj.available() > 0){ //Empty in buffer if it holds any data
    pzemSerialObj.read();
  }
  for(int count=0;count < 7; count++)
    pzemSerialObj.write(command[count]);

  int startTime = millis();
  int receivedBytes = 0; 

  while((receivedBytes < 7 ) && ((millis()- startTime) < 10000)){ // Waits till it recives 7 bytes or a timout of 10 second happens
      if(pzemSerialObj.available() > 0){
        pzem_response_buffer[receivedBytes++] = (uint8_t)pzemSerialObj.read();
       }
       yield();
    }
  return receivedBytes == 7;
}

void printPzemResponseBuffer(){ //For Debugging
  Serial.print("Buffer Data: ");
  for(int x =0 ;x < 7; x++){
    Serial.print(int(pzem_response_buffer[x]));
    Serial.print(" ");
  }
   Serial.println("");
  }
