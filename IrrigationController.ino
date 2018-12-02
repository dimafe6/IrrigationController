#include "secrets.h"
#include <ArduinoJson.h>
#include "ThingSpeak.h"
#include <WiFi.h>
#include "SPI.h"
#include "TFT_eSPI.h"
#include <SimpleTimer.h>
#include "Free_Fonts.h"

#define BAUD_RATE 9600

uint16_t touchCalibration[5] = { 214, 3478, 258, 3520, 5 };

char ssid[] = SECRET_SSID;
char pass[] = SECRET_PASS;

unsigned long myChannelNumber = SECRET_CH_ID;
const char * myWriteAPIKey = SECRET_WRITE_APIKEY;
long thingSpeakInterval;

SimpleTimer wifiTimer;
HardwareSerial radioSerial(1);
HardwareSerial gsmSerial(2); // 16, 17
TFT_eSPI tft = TFT_eSPI();
WiFiClient  client;

void setup() {
  Serial.begin(BAUD_RATE);

  initTft();
  
  initWiFi();

  radioSerial.begin(BAUD_RATE, SERIAL_8N1, 26, 27);
  gsmSerial.begin(BAUD_RATE);
}

void loop() {
  wifiTimer.run();
  
  listenRadio();
}

void listenRadio() {
    byte ch;
    String data;

    if (radioSerial.available() > 0) {
      DynamicJsonBuffer jsonBuffer(1024);
      JsonObject& root = jsonBuffer.parseObject(radioSerial);
      
      if(root.success()) {
        long  ts = root["time"];               // 1541025665
        float temp = root["temp"];             // 23.23
        float pressure = root["pressure"];     // 1008.638
        float humidity = root["humidity"];     // 51.35352
        int   light = root["light"];           // 6
        float watherTemp = root["watherTemp"]; // 23.00
        int   rain = root["rain"];             // 25669   
        int   groundHum = root["groundHum"];   // 25669

        Serial.print("Time: ");Serial.println(ts);
        Serial.print("Temp: ");Serial.println(temp);
        Serial.print("Pressure: ");Serial.println(pressure);
        Serial.print("Humidity: ");Serial.println(humidity);
        Serial.print("Light: ");Serial.println(light);
        Serial.print("Wather temp: ");Serial.println(watherTemp);
        Serial.print("Rain: ");Serial.println(rain);
        Serial.print("Ground humidity: ");Serial.println(groundHum);

        tft.setFreeFont(FSB9);
        tft.fillRect(0, 0, 159, 65, TFT_BLACK);
        tft.drawString("Temperature", 25, 0, GFXFF);
        tft.setFreeFont(FSB18);
        tft.drawString(String(temp) + "C", 35, 25, GFXFF);

        tft.setFreeFont(FSB9);
        tft.fillRect(161, 0, 159, 65, TFT_BLACK);
        tft.drawString("Humidity", 195, 0, GFXFF);
        tft.setFreeFont(FSB18);
        tft.drawString(String(humidity) + "%", 185, 25, GFXFF);
        
        tft.setFreeFont(FSB9);
        tft.fillRect(0, 67, 159, 65, TFT_BLACK);
        tft.drawString("Pressure", 35, 67, GFXFF);
        tft.setFreeFont(FSB12);
        tft.drawString(String(pressure) + " hPa", 15, 92, GFXFF);

        tft.setFreeFont(FSB9);
        tft.fillRect(161, 67, 159, 65, TFT_BLACK);
        tft.drawString("Light", 215, 67, GFXFF);
        tft.setFreeFont(FSB12);
        tft.drawString(String(light) + " lux", 195, 92, GFXFF);


        if (millis() - thingSpeakInterval > 900000) {
          thingSpeakInterval = millis();

          ThingSpeak.setField(1, ts);
          ThingSpeak.setField(2, temp);
          ThingSpeak.setField(3, pressure);
          ThingSpeak.setField(4, humidity);
          ThingSpeak.setField(5, light);
          ThingSpeak.setField(6, watherTemp);
          ThingSpeak.setField(7, rain);
          ThingSpeak.setField(8, groundHum);

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
}

void initWiFi() {
  WiFi.mode(WIFI_STA);

  Serial.println("");
  Serial.println("Connecting to WIFI");
  WiFi.begin(ssid, pass);

  wifiTimer.setInterval(500, checkWiFiConnect);

  ThingSpeak.begin(client);
}

void checkWiFiConnect() {
  if (WiFi.status() != WL_CONNECTED) {
    //tft.fillScreen(TFT_BLACK);
    //tft.setCursor(0, 0, 2);
    //tft.println("WiFi: Disconnected");
    
    WiFi.reconnect();
  } else {
    //tft.fillScreen(TFT_BLACK);
    //tft.setCursor(0, 0, 2);
    //tft.println("WiFi: Connected   ");
  }
}

void initTft() {
  tft.begin();
  tft.setTouch(touchCalibration);
  tft.setRotation(1);
  tft.fillScreen(TFT_WHITE);
}