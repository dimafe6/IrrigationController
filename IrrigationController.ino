#include "secrets.h"
#include "config.h"
#include <ArduinoJson.h>
#include "ThingSpeak.h"
#include <WiFi.h>
#include "SD.h"
#include <FS.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFSEditor.h>
#include <Wire.h>
#include <RtcDS3231.h>
#include <Time.h>
#include "src/Chronos/src/Chronos.h"

uint16_t touchCalibration[5] = {214, 3478, 258, 3520, 5};

SPIClass spiSD(HSPI);
AsyncWebServer server(HTTP_PORT);
AsyncWebSocket ws("/ws");
AsyncEventSource events("/events");
HardwareSerial HC12(1);
HardwareSerial SIM800(2);
WiFiClient client;
RtcDS3231<TwoWire> RTC(Wire);
DefineCalendarType(Calendar, CALENDAR_MAX_NUM_EVENTS);
Calendar MyCalendar;

struct WeatherData
{
  int temp = NULL;
  int pressure = NULL;
  int humidity = NULL;
  int light = NULL;
  int waterTemp = NULL;
  int rain = NULL;
  int groundHum = NULL;
} weatherData;

long thingSpeakLastUpdate;
long HC12LastUpdate;
long calendarLastCheck;

int currentBalance = NULL;

void setup()
{
  initSerial();
  initSD();
  initRtc();
  initWiFi();
  initWebServer();
}

void loop()
{
  checkCalendar();
  listenSIM800();
  listenRadio();
}

void initWebServer()
{
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.addHandler(&events);
  server.addHandler(new SPIFFSEditor(SD, SPIFFS_EDITOR_LOGIN, SPIFFS_EDITOR_PASS));
  server.rewrite("/wifi", "/wifi.html");

  server.serveStatic("/fa-solid-900.woff2", SD, "/fa-solid-900.woff2").setCacheControl("max-age=31536000");
  server.serveStatic("/fa-solid-900.ttf", SD, "/fa-solid-900.ttf").setCacheControl("max-age=31536000");
  server.serveStatic("/fa-solid-900.woff", SD, "/fa-solid-900.woff").setCacheControl("max-age=31536000");
  server.serveStatic("/glyphicons-halflings-regular.eot", SD, "/glyphicons-halflings-regular.eot").setCacheControl("max-age=31536000");
  server.serveStatic("/glyphicons-halflings-regular.svg", SD, "/glyphicons-halflings-regular.svg").setCacheControl("max-age=31536000");
  server.serveStatic("/glyphicons-halflings-regular.ttf", SD, "/glyphicons-halflings-regular.ttf").setCacheControl("max-age=31536000");
  server.serveStatic("/glyphicons-halflings-regular.woff", SD, "/glyphicons-halflings-regular.woff").setCacheControl("max-age=31536000");
  server.serveStatic("/glyphicons-halflings-regular.woff2", SD, "/glyphicons-halflings-regular.woff2").setCacheControl("max-age=31536000");
  server.serveStatic("/all.min.css", SD, "/all.min.css").setCacheControl("max-age=31536000");
  server.serveStatic("/bootstrap.min.css", SD, "/bootstrap.min.css").setCacheControl("max-age=31536000");
  server.serveStatic("/app.js", SD, "/app.js").setCacheControl("max-age=0");
  server.serveStatic("/bootstrap.min.js", SD, "/bootstrap.min.js").setCacheControl("max-age=31536000");
  server.serveStatic("/jquery.min.js", SD, "/jquery.min.js").setCacheControl("max-age=31536000");
  server.serveStatic("/", SD, "/").setDefaultFile("index.html");

  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404);
  });
  server.begin();
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
  if (type == WS_EVT_DATA)
  {
    //TODO: create ArduinoJson object from data
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    String msg = "";
    if (info->opcode == WS_TEXT)
    {
      for (size_t i = 0; i < info->len; i++)
      {
        msg += (char)data[i];
      }
    }
    else
    {
      char buff[3];
      for (size_t i = 0; i < info->len; i++)
      {
        sprintf(buff, "%02x ", (uint8_t)data[i]);
        msg += buff;
      }
    }

    DynamicJsonBuffer jsonBuffer(1024);
    JsonObject &root = jsonBuffer.parseObject(msg);

    if (root.success())
    {
      String command = root["command"];
      if (command == "WiFiConfig")
      {
        String answerString;
        root.printTo(Serial);
        const char *ssid = root["data"]["ssid"];
        const char *password = root["data"]["pass"];
        root.printTo(answerString);

        WiFi.begin(ssid, password);
        WiFi.reconnect();
      }
      else if (command == "manualIrrigation")
      {
        int duration = root["data"]["duration"];
        JsonArray &zones = root["data"]["zones"];
        for (int zone : zones)
        {
          MyCalendar.removeAll(zone);
          if (MyCalendar.add(Chronos::Event(zone, Chronos::DateTime::now(), Chronos::Span::Minutes(duration))))
          {
            //TODO: repeat this message untill irrigation finished
            ws.textAll("{\"command\":\"manualIrrigation\",\"status\":true}");
          }
          else
          {
            ws.textAll("{\"command\":\"manualIrrigation\",\"status\":false}");
          }
        }
      }
      else if (command == "stopManualIrrigation")
      {
        for (byte zone = 1; zone < 5; zone++)
        {
          MyCalendar.removeAll(zone);          
        }

        ws.textAll("{\"command\":\"stopManualIrrigation\"}");
      }
    }
  }
}

void initSD()
{
  //spiSD.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  //if (!SD.begin(SD_CS, spiSD))
  if (!SD.begin())
  {
    Serial.println("Card Mount Failed");
    return;
  }

  uint8_t cardType = SD.cardType();

  if (cardType == CARD_NONE)
  {
    Serial.println("No SD card attached");
    return;
  }

  Serial.print("SD Card Type: ");
  if (cardType == CARD_MMC)
  {
    Serial.println("MMC");
  }
  else if (cardType == CARD_SD)
  {
    Serial.println("SDSC");
  }
  else if (cardType == CARD_SDHC)
  {
    Serial.println("SDHC");
  }
  else
  {
    Serial.println("UNKNOWN");
  }

  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.printf("SD Card Size: %lluMB\n", cardSize);
  Serial.printf("Total space: %lluMB\n", SD.totalBytes() / (1024 * 1024));
  Serial.printf("Used space: %lluMB\n", SD.usedBytes() / (1024 * 1024));
}

void initSerial()
{
  Serial.begin(BAUD_RATE);
  HC12.begin(BAUD_RATE, SERIAL_8N1, HC_12_RX, HC_12_TX);
  SIM800.begin(BAUD_RATE);
  //sendATCommand("AT+CUSD=1,\"*111#\"");
}

void initRtc()
{
  RTC.Begin();
  RTC.Enable32kHzPin(false);
  RTC.SetSquareWavePin(DS3231SquareWavePin_ModeAlarmBoth);
  RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);

  if (!RTC.IsDateTimeValid())
  {
    Serial.println("RTC lost confidence in the DateTime!");
    RTC.SetDateTime(compiled);
  }

  if (!RTC.GetIsRunning())
  {
    Serial.println("RTC was not actively running, starting now");
    RTC.SetIsRunning(true);
  }

  RtcDateTime now = RTC.GetDateTime();
  if (now < compiled)
  {
    Serial.println("RTC is older than compile time!  (Updating DateTime)");
    RTC.SetDateTime(compiled);
  }

  setSyncProvider(getTime);
  Chronos::DateTime::now().printTo(Serial);

  Serial.println("");
  Serial.print("Free heap: ");
  Serial.print(ESP.getFreeHeap());
  Serial.println("");

  //MyCalendar.add(Chronos::Event(CALENDAR_ZONE_1, Chronos::Mark::EveryXDays(2, 15, 0), Chronos::Span::Seconds(10)));
  //MyCalendar.add(Chronos::Event(CALENDAR_ZONE_1, Chronos::Mark::EveryXHours(5, 30, 0), Chronos::Span::Seconds(10)));
  //MyCalendar.add(Chronos::Event(CALENDAR_ZONE_1, Chronos::Mark::Weekly(Chronos::Weekday::Friday, 2, 0), Chronos::Span::Seconds(10)));

  Serial.println("");
  Serial.print("Free heap: ");
  Serial.print(ESP.getFreeHeap());
  Serial.println("");
}

static time_t getTime()
{
  return RTC.GetDateTime().Epoch32Time();
}

void checkCalendar()
{
  if (millis() - calendarLastCheck >= CALENDAR_CHECK_INTERVAL)
  {
    calendarLastCheck = millis();

    Chronos::Event::Occurrence occurrenceList1[10];
    int numMon = MyCalendar.listNext(10, occurrenceList1, Chronos::DateTime::now());
    if (numMon)
    {
      Serial.println("");
      Serial.print("Next event: ");
      for (int i = 0; i < numMon; i++)
      {
        Serial.print((int)occurrenceList1[i].id);
        Serial.print(": ");
        occurrenceList1[i].finish.printTo(Serial);
        Serial.println("");
      }
    }

    Chronos::Event::Occurrence occurrenceList[CALENDAR_OCCURRENCES_LIST_SIZE];
    int numOngoing = MyCalendar.listOngoing(CALENDAR_OCCURRENCES_LIST_SIZE, occurrenceList, Chronos::DateTime::now());
    if (numOngoing)
    {
      for (int i = 0; i < numOngoing; i++)
      {
        Serial.println("");
        Serial.print("**** Event: ");
        Serial.print((int)occurrenceList[i].id);
        Serial.print(": ");
        (Chronos::DateTime::now() - occurrenceList[i].finish).printTo(Serial);
        Serial.println("");
      }
    }
  }
}

void updateWeatherData(int temp, int pressure, int humidity, int light, int waterTemp, int rain, int groundHum)
{
  if (NULL != temp && temp < 100)
  {
    weatherData.temp = temp;
  }
  if (NULL != pressure && pressure > 300 && pressure < 1100)
  {
    weatherData.pressure = pressure;
  }
  if (NULL != humidity && humidity < 101)
  {
    weatherData.humidity = humidity;
  }
  if (NULL != light && light < 50000)
  {
    weatherData.light = light;
  }
  if (NULL != waterTemp && waterTemp > 0 && waterTemp < 50)
  {
    weatherData.waterTemp = waterTemp;
  }
  if (NULL != rain && rain < 25000)
  {
    weatherData.rain = rain;
  }
  if (NULL != groundHum && groundHum < 25000)
  {
    weatherData.groundHum = groundHum;
  }

  String answerString;
  DynamicJsonBuffer jsonBuffer(1024);
  JsonObject &answer = jsonBuffer.createObject();
  answer["command"] = "weatherUpdate";
  JsonObject &data = answer.createNestedObject("data");
  data["temp"] = weatherData.temp;
  data["pressure"] = weatherData.pressure;
  data["humidity"] = weatherData.humidity;
  data["light"] = weatherData.light;
  data["waterTemp"] = weatherData.waterTemp;
  data["rain"] = weatherData.rain;
  data["groundHum"] = weatherData.groundHum;
  answer.printTo(answerString);

  ws.textAll(answerString.c_str());
}

void listenRadio()
{
  byte ch;
  String data;

  if (HC12.available())
  {
    if (millis() - HC12LastUpdate >= HC_12_UPDATE_INTERVAL)
    {
      DynamicJsonBuffer jsonBuffer(1024);
      JsonObject &root = jsonBuffer.parseObject(HC12);

      if (root.success())
      {
        int temp = root["t"];
        int pressure = root["p"];
        int humidity = root["h"];
        int light = root["l"];
        int waterTemp = root["wt"];
        int rain = root["r"];
        int groundHum = root["gh"];

        updateWeatherData(temp, pressure, humidity, light, waterTemp, rain, groundHum);
        HC12LastUpdate = millis();
        /*
        Serial.print("Temp: ");
        Serial.println(weatherData.temp);
        Serial.print("Pressure: ");
        Serial.println(weatherData.pressure);
        Serial.print("Humidity: ");
        Serial.println(weatherData.humidity);
        Serial.print("Light: ");
        Serial.println(weatherData.light);
        Serial.print("Wather temp: ");
        Serial.println(weatherData.waterTemp);
        Serial.print("Rain: ");
        Serial.println(weatherData.rain);
        Serial.print("Ground humidity: ");
        Serial.println(weatherData.groundHum);
*/
        if (millis() - thingSpeakLastUpdate > THING_SPEAK_WRITE_INTERVAL)
        {
          thingSpeakLastUpdate = millis();

          sendWeatherDataToThingSpeak(); //TODO: Run in second core
        }
      }
    }
  }
}

void sendWeatherDataToThingSpeak()
{
  if (WiFi.status() == WL_CONNECTED)
  {
    ThingSpeak.setField(2, weatherData.temp);
    ThingSpeak.setField(3, weatherData.pressure);
    ThingSpeak.setField(4, weatherData.humidity);
    ThingSpeak.setField(5, weatherData.light);
    ThingSpeak.setField(6, weatherData.waterTemp);
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
  WiFi.persistent(true);
  WiFi.setAutoConnect(true);
  WiFi.setAutoReconnect(true);

  WiFi.onEvent(WiFiEvent);
  WiFi.softAP("IrrigationController", SECRET_WEBSERVER_PASS);

  Serial.println("");
  Serial.println("Connecting to WIFI");
  WiFi.begin();
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
    Serial.println(WiFi.SSID());
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