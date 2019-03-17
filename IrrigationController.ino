#include "secrets.h"
#include "config.h"
#include <ArduinoJson.h>
#include "ThingSpeak.h"
#include <WiFi.h>
#include "SPI.h"
#include "TFT_eSPI.h"
#include <SimpleTimer.h>
#include "Free_Fonts.h"
#include <Wire.h>
#include <RtcDS3231.h>

uint16_t touchCalibration[5] = {214, 3478, 258, 3520, 5};

HardwareSerial radioSerial(1);
HardwareSerial SIM800(2);
TFT_eSPI tft = TFT_eSPI();
WiFiClient client;
RtcDS3231<TwoWire> Rtc(Wire);

struct WeatherData
{
  long ts;
  float temp;
  float pressure;
  float humidity;
  int light;
  float watherTemp;
  int rain;
  int groundHum;
} weatherData;

long thingSpeakLastUpdate;
int currentBalance = NULL;

void setup()
{
  initSerial();

  initRtc();

  initTft();

  initWiFi();
}

void loop()
{
  listenSIM800();
  //listenRadio();
}

void initSerial()
{
  Serial.begin(BAUD_RATE);
  radioSerial.begin(BAUD_RATE, SERIAL_8N1, HC_12_RX, HC_12_TX);
  SIM800.begin(BAUD_RATE);
  sendATCommand("AT+CUSD=1,\"*111#\"");
}

void initRtc()
{
  Rtc.Begin();
  Rtc.Enable32kHzPin(false);
  Rtc.SetSquareWavePin(DS3231SquareWavePin_ModeAlarmBoth);
  RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__); //TODO: remove   
  
  if (!Rtc.IsDateTimeValid())
  {
    Serial.println("RTC lost confidence in the DateTime!");
    Rtc.SetDateTime(compiled);
  }

  if (!Rtc.GetIsRunning())
  {
    Serial.println("RTC was not actively running, starting now");
    Rtc.SetIsRunning(true);
  }

  RtcDateTime now = Rtc.GetDateTime();
  if (now < compiled)
  {
    Serial.println("RTC is older than compile time!  (Updating DateTime)");
    Rtc.SetDateTime(compiled);
  }
}

void listenRadio()
{
  byte ch;
  String data;

  if (radioSerial.available())
  {
    DynamicJsonBuffer jsonBuffer(1024);
    JsonObject &root = jsonBuffer.parseObject(radioSerial);

    if (root.success())
    {
      weatherData.ts = root["time"];               // 1541025665
      weatherData.temp = root["temp"];             // 23.23
      weatherData.pressure = root["pressure"];     // 1008.638
      weatherData.humidity = root["humidity"];     // 51.35352
      weatherData.light = root["light"];           // 6
      weatherData.watherTemp = root["watherTemp"]; // 23.00
      weatherData.rain = root["rain"];             // 25669
      weatherData.groundHum = root["groundHum"];   // 25669

      /*Serial.print("Time: ");
      Serial.println(weatherData.ts);
      Serial.print("Temp: ");
      Serial.println(weatherData.temp);
      Serial.print("Pressure: ");
      Serial.println(weatherData.pressure);
      Serial.print("Humidity: ");
      Serial.println(weatherData.humidity);
      Serial.print("Light: ");
      Serial.println(weatherData.light);
      Serial.print("Wather temp: ");
      Serial.println(weatherData.watherTemp);
      Serial.print("Rain: ");
      Serial.println(weatherData.rain);
      Serial.print("Ground humidity: ");
      Serial.println(weatherData.groundHum);*/

      tft.setFreeFont(FSB9);
      tft.fillRect(0, 0, 159, 65, TFT_BLACK);
      tft.drawString("Temperature", 25, 0, GFXFF);
      tft.setFreeFont(FSB18);
      tft.drawString(String(weatherData.temp) + "C", 35, 25, GFXFF);

      tft.setFreeFont(FSB9);
      tft.fillRect(161, 0, 159, 65, TFT_BLACK);
      tft.drawString("Humidity", 195, 0, GFXFF);
      tft.setFreeFont(FSB18);
      tft.drawString(String(weatherData.humidity) + "%", 185, 25, GFXFF);

      tft.setFreeFont(FSB9);
      tft.fillRect(0, 67, 159, 65, TFT_BLACK);
      tft.drawString("Pressure", 35, 67, GFXFF);
      tft.setFreeFont(FSB12);
      tft.drawString(String(weatherData.pressure) + " hPa", 15, 92, GFXFF);

      tft.setFreeFont(FSB9);
      tft.fillRect(161, 67, 159, 65, TFT_BLACK);
      tft.drawString("Light", 215, 67, GFXFF);
      tft.setFreeFont(FSB12);
      tft.drawString(String(weatherData.light) + " lux", 195, 92, GFXFF);

      if (millis() - thingSpeakLastUpdate > THING_SPEAK_WRITE_INTERVAL)
      {
        thingSpeakLastUpdate = millis();

        sendWeatherDataToThingSpeak();
      }
    }
  }
}

void sendWeatherDataToThingSpeak()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    ThingSpeak.setField(1, weatherData.ts);
    ThingSpeak.setField(2, weatherData.temp);
    ThingSpeak.setField(3, weatherData.pressure);
    ThingSpeak.setField(4, weatherData.humidity);
    ThingSpeak.setField(5, weatherData.light);
    ThingSpeak.setField(6, weatherData.watherTemp);
    ThingSpeak.setField(7, weatherData.rain);
    ThingSpeak.setField(8, weatherData.groundHum);

    // write to the ThingSpeak channel
    int x = ThingSpeak.writeFields(SECRET_CH_ID, SECRET_WRITE_APIKEY);
    if (x == 200)
    {
      Serial.println("Channel update successful.");
    }
    else
    {
      Serial.println("Problem updating channel. HTTP error code " + String(x));
    }
  }
}

void initWiFi()
{
  WiFi.onEvent(WiFiEvent);
  WiFi.mode(WIFI_STA);

  Serial.println("");
  Serial.println("Connecting to WIFI");
  if (WiFi.SSID() == "")
  {
    WiFi.begin(SECRET_SSID, SECRET_PASS);
  }
  else
  {
    WiFi.begin();
  }
}

void initTft()
{
  tft.begin();
  tft.setTouch(touchCalibration);
  tft.setRotation(3);
  tft.fillScreen(TFT_WHITE);
}

void WiFiEvent(WiFiEvent_t event)
{
  //Serial.printf("[WiFi-event] event: %d\n", event);

  switch (event)
  {
  case SYSTEM_EVENT_WIFI_READY:
    //Serial.println("WiFi interface ready");
    break;
  case SYSTEM_EVENT_SCAN_DONE:
    Serial.println("Completed scan for access points");
    break;
  case SYSTEM_EVENT_STA_START:
    //Serial.println("WiFi client started");
    break;
  case SYSTEM_EVENT_STA_STOP:
    Serial.println("WiFi clients stopped");
    break;
  case SYSTEM_EVENT_STA_CONNECTED:
    Serial.println("Connected to access point");
    break;
  case SYSTEM_EVENT_STA_DISCONNECTED:
    Serial.println("Disconnected from WiFi access point");
    WiFi.reconnect();
    break;
  case SYSTEM_EVENT_STA_AUTHMODE_CHANGE:
    Serial.println("Authentication mode of access point has changed");
    break;
  case SYSTEM_EVENT_STA_GOT_IP:
    Serial.print("Obtained IP address: ");
    Serial.println(WiFi.localIP());
    ThingSpeak.begin(client);
    break;
  case SYSTEM_EVENT_STA_LOST_IP:
    Serial.println("Lost IP address and IP address is reset to 0");
    break;
  case SYSTEM_EVENT_STA_WPS_ER_SUCCESS:
    Serial.println("WiFi Protected Setup (WPS): succeeded in enrollee mode");
    break;
  case SYSTEM_EVENT_STA_WPS_ER_FAILED:
    Serial.println("WiFi Protected Setup (WPS): failed in enrollee mode");
    break;
  case SYSTEM_EVENT_STA_WPS_ER_TIMEOUT:
    Serial.println("WiFi Protected Setup (WPS): timeout in enrollee mode");
    break;
  case SYSTEM_EVENT_STA_WPS_ER_PIN:
    Serial.println("WiFi Protected Setup (WPS): pin code in enrollee mode");
    break;
  case SYSTEM_EVENT_AP_START:
    Serial.println("WiFi access point started");
    break;
  case SYSTEM_EVENT_AP_STOP:
    Serial.println("WiFi access point  stopped");
    break;
  case SYSTEM_EVENT_AP_STACONNECTED:
    Serial.println("Client connected");
    break;
  case SYSTEM_EVENT_AP_STADISCONNECTED:
    Serial.println("Client disconnected");
    break;
  case SYSTEM_EVENT_AP_STAIPASSIGNED:
    Serial.println("Assigned IP address to client");
    break;
  case SYSTEM_EVENT_AP_PROBEREQRECVED:
    Serial.println("Received probe request");
    break;
  case SYSTEM_EVENT_GOT_IP6:
    Serial.println("IPv6 is preferred");
    break;
  case SYSTEM_EVENT_ETH_START:
    Serial.println("Ethernet started");
    break;
  case SYSTEM_EVENT_ETH_STOP:
    Serial.println("Ethernet stopped");
    break;
  case SYSTEM_EVENT_ETH_CONNECTED:
    Serial.println("Ethernet connected");
    break;
  case SYSTEM_EVENT_ETH_DISCONNECTED:
    Serial.println("Ethernet disconnected");
    break;
  case SYSTEM_EVENT_ETH_GOT_IP:
    Serial.println("Obtained IP address");
    break;
  }
}

void listenSIM800()
{
  if (SIM800.available())
  {
    String response = SIM800.readString();
    response.trim();
    Serial.println(response);

    // USSD Handler
    if (response.startsWith("+CUSD:"))
    {
      // Check balance
      if (response.indexOf("Balans") > -1)
      {
        String msgBalance = response.substring(response.indexOf("Balans") + 7);
        msgBalance = msgBalance.substring(0, msgBalance.indexOf("hrn") - 1);
        currentBalance = msgBalance.toFloat();
        Serial.println("USSD: " + String(currentBalance));
      }
    }
  }

  if (Serial.available())
  {
    SIM800.write(Serial.read());
  }
}

void sendATCommand(String cmd)
{
  Serial.println(cmd);
  SIM800.println(cmd);
}