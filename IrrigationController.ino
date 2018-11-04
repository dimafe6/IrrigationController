#include "secrets.h"
#include <ArduinoJson.h>
#include "SSD1306Wire.h"
#include "ThingSpeak.h"
#include <WiFi.h>

#define BAUD_RATE 9600

SSD1306Wire  display(0x3c, 21, 22);
WiFiClient  client;

char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;

unsigned long myChannelNumber = SECRET_CH_ID;
const char * myWriteAPIKey = SECRET_WRITE_APIKEY;

long ts;
float temp;
float pressure;
float humidity;
int light;
float watherTemp;
int rain;
long interval;
long thingSpeakInterval;
long refreshInterval;
float values[6];
String pagesNames[] = {"Temp.","Pressure","Humidity","Light","Wather"};
String pagesPostfix[] = {"*C","hPa","%","lux","*C"};
int pagesLength = 4;
int pageIndex = 0;

HardwareSerial radioSerial(1);
HardwareSerial gsmSerial(2);

void setup() {
  Serial.begin(BAUD_RATE);

  WiFi.mode(WIFI_STA);   
  ThingSpeak.begin(client);

  radioSerial.begin(BAUD_RATE, SERIAL_8N1, 5, 18);
  gsmSerial.begin(BAUD_RATE); // 16, 17

  display.init();
  display.flipScreenVertically();
  display.clear();  
}

void loop() {
  // Connect or reconnect to WiFi
  if(WiFi.status() != WL_CONNECTED){
    Serial.print("Attempting to connect to SSID: ");
    Serial.println(ssid);
    while(WiFi.status() != WL_CONNECTED){
      WiFi.begin(ssid, pass);
      Serial.print(".");
      delay(5000);     
    } 
    Serial.println("\nConnected.");
  }

  byte ch;
  String data;
  
  if (radioSerial.available() > 0) {
    DynamicJsonBuffer jsonBuffer(1024);
    JsonObject& root = jsonBuffer.parseObject(radioSerial);
    if(root.success()) {
      ts = root["time"]; // 1541025665
      temp = root["temp"]; // 23.23
      pressure = root["pressure"]; // 1008.638
      humidity = root["humidity"]; // 51.35352
      light = root["light"]; // 6
      watherTemp = root["watherTemp"]; // 23.00
      rain = root["rain"]; // 25669   

      values[0] = temp;
      values[1] = pressure;
      values[2] = humidity;
      values[3] = light;
      values[4] = watherTemp;

      Serial.print("Time: ");Serial.println(ts);
      Serial.print("Temp: ");Serial.println(temp);
      Serial.print("Pressure: ");Serial.println(pressure);
      Serial.print("Humidity: ");Serial.println(humidity);
      Serial.print("Light: ");Serial.println(light);
      Serial.print("Wather temp: ");Serial.println(watherTemp);
      Serial.print("Rain: ");Serial.println(rain);

      if (millis() - thingSpeakInterval > 10000) {
        thingSpeakInterval = millis();

        ThingSpeak.setField(1, ts);
        ThingSpeak.setField(2, temp);
        ThingSpeak.setField(3, pressure);
        ThingSpeak.setField(4, humidity);
        ThingSpeak.setField(5, light);
        ThingSpeak.setField(6, watherTemp);
        ThingSpeak.setField(7, rain);

          // write to the ThingSpeak channel
        int x = ThingSpeak.writeFields(myChannelNumber, myWriteAPIKey);
        if(x == 200){
          Serial.println("Channel update successful.");
        }
        else{
          Serial.println("Problem updating channel. HTTP error code " + String(x));
        }
      }
    }
    yield();
  }

  if (millis() - interval > 10000) {
    interval = millis();
    pageIndex = (pageIndex + 1)  % pagesLength;
  }

  if (millis() - refreshInterval > 1000) {
    refreshInterval= millis();

    display.clear();
    display.setFont(ArialMT_Plain_24);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 0, pagesNames[pageIndex]);
    display.setTextAlignment(TEXT_ALIGN_CENTER);
    display.drawString(64, 32, String(values[pageIndex],1) + pagesPostfix[pageIndex]);
    display.display();
  }
}