#include "secrets.h"
#include "config.h"
#include <ArduinoJson.h>
#include "ThingSpeak.h"
#include <WiFi.h>
#include <FS.h>
#include <SPIFFS.h>
#include "SD.h"
#include <ESPAsyncWebServer.h>
#include <SPIFFSEditor.h>
#include <Wire.h>
#include <RtcDS3231.h>
#include <Time.h>
#include <Update.h>
#include "src/Chronos/src/Chronos.h"

SPIClass spiSD(HSPI);
AsyncWebServer server(HTTP_PORT);
AsyncWebSocket ws("/ws");
HardwareSerial HC12(1);
HardwareSerial SIM800(2);
WiFiClient client;
RtcDS3231<TwoWire> RTC(Wire);
DefineCalendarType(Calendar, CALENDAR_MAX_NUM_EVENTS);
Calendar MyCalendar;
AsyncWebHandler *spiffsEditorHandler;
AsyncWebHandler *sdEditorHandler;

enum Periodicity
{
  ONCE = -1,
  HOURLY,
  EVERY_X_HOUR,
  DAILY,
  EVERY_X_DAYS,
  WEEKLY,
  MONTHLY
};

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
bool shouldReboot = false;
bool manualIrrigationRunning = false;

void setup()
{
  initSerial();
  initRtc();
  initSD();
  loadCalendarFromSD();
  initSPIFFS();
  initWiFi();
  initWebServer();
}

void loop()
{
  if (shouldReboot)
  {
    Serial.println("Rebooting...");
    delay(100);
    ESP.restart();
  }
  listenSIM800();
  checkCalendar();
  listenRadio();
}

void initWebServer()
{
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.serveStatic("/app.js", SPIFFS, "/app.js").setCacheControl("max-age=0");
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");

  server.on("/edit/sd", [](AsyncWebServerRequest *request) {
    server.removeHandler(spiffsEditorHandler);
    sdEditorHandler = &server.addHandler(new SPIFFSEditor(SD, SPIFFS_EDITOR_LOGIN, SPIFFS_EDITOR_PASS));
    request->redirect("/edit");
  });

  server.on("/edit/spiffs", [](AsyncWebServerRequest *request) {
    server.removeHandler(sdEditorHandler);
    spiffsEditorHandler = &server.addHandler(new SPIFFSEditor(SPIFFS, SPIFFS_EDITOR_LOGIN, SPIFFS_EDITOR_PASS));
    request->redirect("/edit");
  });

  server.onNotFound([](AsyncWebServerRequest *request) {
    request->send(404);
  });

  server.on("/update", HTTP_POST, [](AsyncWebServerRequest *request) {
    shouldReboot = !Update.hasError();
    AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", shouldReboot ? "OK" : "FAIL");
    response->addHeader("Connection", "close");
    request->send(response); }, [](AsyncWebServerRequest *request, String filename, size_t index, uint8_t *data, size_t len, bool final) {
    if(!index){
      Serial.printf("Update Start: %s\n", filename.c_str());
      if(!Update.begin((ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000)){
        Update.printError(Serial);
      }
    }
    if(!Update.hasError()){
      if(Update.write(data, len) != len){
        Update.printError(Serial);
      }
    }
    if(final){
      if(Update.end(true)){
        Serial.printf("Update Success");
        request->redirect("/");
      } else {
        Update.printError(Serial);
      }
    } });

  server.on("/reboot", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(200, "text/plain", "OK");
    response->addHeader("Connection", "close");
    request->send(response);
    shouldReboot = true;
  });

  server.begin();
}

void loadCalendarFromSD()
{
  DynamicJsonBuffer jsonBuffer(2048);
  File scheduleFile;

  if (!SD.exists(SCHEDULE_FILE_NAME))
  {
    return;
  }

  scheduleFile = SD.open(SCHEDULE_FILE_NAME, FILE_READ);
  if (!scheduleFile)
  {
    Serial.println(F("Failed to read schedule"));
    return;
  }

  //Read schedule to object
  JsonArray &schedule = jsonBuffer.parseArray(scheduleFile);
  scheduleFile.close();

  int evId = 0;
  for (JsonObject &eventData : schedule)
  {
    addEventToCalendar(evId, eventData);
  }
}

void sendSlotsToWS()
{
  if (!SD.exists(SCHEDULE_FILE_NAME))
  {
    return;
  }

  File scheduleFile = SD.open(SCHEDULE_FILE_NAME, FILE_READ);
  if (!scheduleFile)
  {
    Serial.println(F("Failed to read schedule"));
    return;
  }

  //Read schedule to object
  DynamicJsonBuffer jsonBuffer(2048);
  JsonArray &schedule = jsonBuffer.parseArray(scheduleFile);

  scheduleFile.close();

  DynamicJsonBuffer responseBuffer(1024);
  JsonObject &response = responseBuffer.createObject();
  response["command"] = "getSlots";
  JsonObject &data = response.createNestedObject("data");
  JsonArray &slots = data.createNestedArray("slots");

  for (JsonObject &eventData : schedule)
  {
    slots.add(eventData);
  }

  String json;
  json.reserve(2048);
  data["total"] = CALENDAR_MAX_NUM_EVENTS - 1;
  data["occupied"] = MyCalendar.numRecurring();
  response.printTo(json);

  ws.textAll(json);
}

void sendSysInfoToWS()
{
  DynamicJsonBuffer sysInfoBuf(512);
  JsonObject &sysInfo = sysInfoBuf.createObject();
  sysInfo["command"] = "getSysInfo";

  JsonObject &data = sysInfo.createNestedObject("data");
  JsonObject &wifi = data.createNestedObject("WiFi");
  wifi["SSID"] = WiFi.SSID();
  wifi_power_t power = WiFi.getTxPower();
  switch (power)
  {
  case WIFI_POWER_19_5dBm:
    wifi["power"] = "19.5dBm";
    break;
  case WIFI_POWER_19dBm:
    wifi["power"] = "19dBm";
    break;
  case WIFI_POWER_18_5dBm:
    wifi["power"] = "18.5dBm";
    break;
  case WIFI_POWER_17dBm:
    wifi["power"] = "17dBm";
    break;
  case WIFI_POWER_15dBm:
    wifi["power"] = "15dBm";
    break;
  case WIFI_POWER_13dBm:
    wifi["power"] = "13dBm";
    break;
  case WIFI_POWER_11dBm:
    wifi["power"] = "11dBm";
    break;
  case WIFI_POWER_8_5dBm:
    wifi["power"] = "8.5dBm";
    break;
  case WIFI_POWER_7dBm:
    wifi["power"] = "7dBm";
    break;
  case WIFI_POWER_5dBm:
    wifi["power"] = "5dBm";
    break;
  case WIFI_POWER_2dBm:
    wifi["power"] = "2dBm";
    break;
  case WIFI_POWER_MINUS_1dBm:
    wifi["power"] = "-1dBm";
    break;
  }
  wifi["RSSI"] = WiFi.RSSI();
  wifi["localIP"] = WiFi.localIP().toString();

  JsonObject &heap = data.createNestedObject("heap");
  heap["total"] = ESP.getHeapSize();
  heap["free"] = ESP.getFreeHeap();
  heap["min"] = ESP.getMinFreeHeap();
  heap["maxAlloc"] = ESP.getMaxAllocHeap();

  JsonObject &sdInfo = data.createNestedObject("SD");
  uint8_t cardType = SD.cardType();
  switch (cardType)
  {
  case CARD_NONE:
    sdInfo["type"] = "None";
    break;
  case CARD_MMC:
    sdInfo["type"] = "MMC";
    break;
  case CARD_SD:
    sdInfo["type"] = "SD";
    break;
  case CARD_SDHC:
    sdInfo["type"] = "SDHC";
    break;
  }

  char size[20];
  sprintf(size, "%lluMB", SD.totalBytes() / (1024 * 1024));
  sdInfo["total"] = size;
  sprintf(size, "%lluMB", SD.usedBytes() / (1024 * 1024));
  sdInfo["used"] = size;

  JsonObject &spiffsInfo = data.createNestedObject("SPIFFS");
  sprintf(size, "%dKB", SPIFFS.totalBytes() / 1024);
  spiffsInfo["total"] = size;
  sprintf(size, "%dKB", SPIFFS.usedBytes() / 1024);
  spiffsInfo["used"] = size;

  String json;
  json.reserve(512);

  sysInfo.printTo(json);
  ws.textAll(json);
}

Chronos::Zones getZonesFromJson(JsonArray &zones)
{
  struct Chronos::Zones _zones;

  for (int zone : zones)
  {
    if (zone == 1)
    {
      _zones.zone1 = true;
    }
    if (zone == 2)
    {
      _zones.zone2 = true;
    }
    if (zone == 3)
    {
      _zones.zone3 = true;
    }
    if (zone == 4)
    {
      _zones.zone4 = true;
    }
  }

  return _zones;
}

bool removeEvent(byte evId)
{
  DynamicJsonBuffer jsonBuffer(3000);
  File scheduleFile;
  String scheduleString = "[]";
  scheduleString.reserve(3000);

  //If schedule exists then open it
  if (SD.exists(SCHEDULE_FILE_NAME))
  {
    scheduleFile = SD.open(SCHEDULE_FILE_NAME, FILE_READ);
    if (!scheduleFile)
    {
      Serial.println(F("Failed to read schedule"));
      return false;
    }

    scheduleString = scheduleFile.readString();
    scheduleFile.close();

    if (!scheduleString.length())
    {
      return false;
    }
  }

  if (MyCalendar.remove(evId))
  {

    JsonArray &schedule = jsonBuffer.parseArray(scheduleString);
    scheduleFile = SD.open(SCHEDULE_FILE_NAME, FILE_WRITE);
    if (!scheduleFile)
    {
      Serial.println(F("Failed to create schedule"));
      return false;
    }

    JsonVariant element = schedule[evId];
    if (element.success())
    {
      schedule.remove(evId);
    }

    schedule.printTo(scheduleFile);
    scheduleFile.close();
  }

  /*char scheduleFileName[20];
  sprintf(scheduleFileName, "/schedule_%d.json", evId);
  if (MyCalendar.remove(evId))
  {
    if (SD.remove(scheduleFileName))
    {
      Serial.println("File deleted");
      uint8_t numEvents = MyCalendar.numEvents();
      if (evId <= numEvents)
      {
        Serial.println("Files reordering...");
        for (uint8_t i = evId; i <= numEvents; i++)
        {
          char newFilename[20];
          char oldFilename[20];
          sprintf(oldFilename, "/schedule_%d.json", i + 1);
          sprintf(newFilename, "/schedule_%d.json", i);
          Serial.println("Rename from: ");
          Serial.println(oldFilename);
          Serial.println("Rename to: ");
          Serial.println(newFilename);
          if (SD.exists(oldFilename))
          {
            SD.rename(oldFilename, newFilename);
          }
        }
        Serial.println("Reordering finished");
      }
      return true;
    }
    else
    {
      Serial.println("Delete failed");

      return false;
    }
  }
*/
  return false;
}

bool setEventEnabled(byte evId, bool enabled)
{
  DynamicJsonBuffer jsonBuffer(3000);
  File scheduleFile;
  String scheduleString = "[]";
  scheduleString.reserve(3000);

  //If schedule exists then open it
  if (SD.exists(SCHEDULE_FILE_NAME))
  {
    scheduleFile = SD.open(SCHEDULE_FILE_NAME, FILE_READ);
    if (!scheduleFile)
    {
      Serial.println(F("Failed to read schedule"));
      return false;
    }

    scheduleString = scheduleFile.readString();
    scheduleFile.close();

    if (!scheduleString.length())
    {
      return false;
    }
  }

  MyCalendar.setEnabled(evId, enabled);

  JsonArray &schedule = jsonBuffer.parseArray(scheduleString);
  scheduleFile = SD.open(SCHEDULE_FILE_NAME, FILE_WRITE);
  if (!scheduleFile)
  {
    Serial.println(F("Failed to create schedule"));
    return false;
  }

  JsonVariant element = schedule[evId];
  if (element.success())
  {
    schedule[evId]["enabled"] = enabled;
  }
  else
  {
    return false;
  }

  schedule.printTo(scheduleFile);
  scheduleFile.close();

  return true;
}

void stopManualIrrigation()
{
  /*removeEvent(0);
  loadCalendarFromSD();
  ws.textAll(MANUAL_IRRIGATION_STOP);*/
}

void addOrEditSchedule(JsonObject &eventData)
{
  if (MyCalendar.numRecurring() >= CALENDAR_MAX_NUM_EVENTS - 1)
  {
    return;
  }

  JsonVariant eventId = eventData["evId"];
  bool isEditEvent = eventId.success();
  byte evId = MyCalendar.numRecurring();

  if (isEditEvent)
  {
    evId = eventId.as<int>();

    Serial.println("");
    Serial.print("Edit schedule #");
    Serial.println(evId);
    eventData["enabled"] = MyCalendar.isEnabled(evId);
    MyCalendar.removeAll(evId);
  }
  else
  {
    Serial.println("");
    Serial.print("Add schedule");
    eventData["enabled"] = true; //New event always enabled
  }

  DynamicJsonBuffer jsonBuffer(3000);
  File scheduleFile;
  String scheduleString = "[]";
  scheduleString.reserve(3000);

  //If schedule exists then open it
  if (SD.exists(SCHEDULE_FILE_NAME))
  {
    scheduleFile = SD.open(SCHEDULE_FILE_NAME, FILE_READ);
    if (!scheduleFile)
    {
      Serial.println(F("Failed to read schedule"));
      return;
    }

    scheduleString = scheduleFile.readString();
    scheduleFile.close();
  }

  if (addEventToCalendar(evId, eventData))
  {
    JsonArray &schedule = jsonBuffer.parseArray(scheduleString);
    scheduleFile = SD.open(SCHEDULE_FILE_NAME, FILE_WRITE);
    if (!scheduleFile)
    {
      Serial.println(F("Failed to create schedule"));
      return;
    }

    JsonVariant element = schedule[evId];
    if (element.success())
    {
      schedule.set(evId, eventData);
    }
    else
    {
      schedule.add(eventData);
    }
    schedule.printTo(scheduleFile);
    scheduleFile.close();
  }

  // If edit event then need update from SD
  if (isEditEvent)
  {
    loadCalendarFromSD();
  }

  sendSlotsToWS();

  ws.textAll(SCHEDULE_ADD_EDIT);
}

bool addEventToCalendar(byte evId, JsonObject &eventData)
{
  int duration = eventData["duration"];
  struct Chronos::Zones _zones = getZonesFromJson(eventData["zones"]);
  byte periodicity = eventData["periodicity"];
  bool eventSaved = false;
  JsonVariant enabled = eventData["enabled"];
  JsonVariant evIdElem = eventData["evId"];

  if (!enabled.success())
  {
    eventData["enabled"] = true;
  }

  if (evIdElem.success())
  {
    eventData.remove("evId");
  }

  switch (periodicity)
  {
  case Periodicity::HOURLY:
  {
    Serial.println("HOURLY");
    byte minute = eventData["minute"];
    byte second = eventData["second"];

    eventSaved = MyCalendar.add(Chronos::Event(evId, Chronos::Mark::Hourly(minute, second), Chronos::Span::Minutes(duration), _zones));
  }
  break;
  case Periodicity::EVERY_X_HOUR:
  {
    Serial.println("EVERY_X_HOUR");
    byte hours = eventData["hours"];
    byte minute = eventData["minute"];
    byte second = eventData["second"];

    eventSaved = MyCalendar.add(Chronos::Event(evId, Chronos::Mark::EveryXHours(hours, minute, second), Chronos::Span::Minutes(duration), _zones));
  }
  break;
  case Periodicity::DAILY:
  {
    Serial.println("DAILY");
    byte hour = eventData["hour"];
    byte minute = eventData["minute"];

    eventSaved = MyCalendar.add(Chronos::Event(evId, Chronos::Mark::Daily(hour, minute), Chronos::Span::Minutes(duration), _zones));
  }
  break;
  case Periodicity::EVERY_X_DAYS:
  {
    Serial.println("EVERY_X_DAYS");
    byte days = eventData["days"];
    byte hour = eventData["hour"];
    byte minute = eventData["minute"];

    eventSaved = MyCalendar.add(Chronos::Event(evId, Chronos::Mark::EveryXDays(days, hour, minute), Chronos::Span::Minutes(duration), _zones));
  }
  break;
  case Periodicity::WEEKLY:
  {
    Serial.println("WEEKLY");
    byte dayOfWeek = eventData["dayOfWeek"];
    byte hour = eventData["hour"];
    byte minute = eventData["minute"];

    eventSaved = MyCalendar.add(Chronos::Event(evId, Chronos::Mark::Weekly(dayOfWeek, hour, minute), Chronos::Span::Minutes(duration), _zones));
  }
  break;
  case Periodicity::MONTHLY:
  {
    Serial.println("MONTHLY");
    byte dayOfMonth = eventData["dayOfMonth"];
    byte hour = eventData["hour"];
    byte minute = eventData["minute"];

    eventSaved = MyCalendar.add(Chronos::Event(evId, Chronos::Mark::Monthly(dayOfMonth, hour, minute), Chronos::Span::Minutes(duration), _zones));
  }
  break;
  }

  return eventSaved;
}

void onWsEvent(AsyncWebSocket *server, AsyncWebSocketClient *client, AwsEventType type, void *arg, uint8_t *data, size_t len)
{
  if (type == WS_EVT_DATA)
  {
    AwsFrameInfo *info = (AwsFrameInfo *)arg;
    String msg;
    msg.reserve(len);
    if (info->opcode == WS_TEXT)
    {
      for (size_t i = 0; i < info->len; i++)
      {
        msg.concat((char)data[i]);
      }
    }
    else
    {
      char buff[3];
      for (size_t i = 0; i < info->len; i++)
      {
        sprintf(buff, "%02x ", (uint8_t)data[i]);
        msg.concat(buff);
      }
    }
    Serial.println(msg);
    DynamicJsonBuffer jsonBuffer(1024);
    JsonObject &root = jsonBuffer.parseObject(msg);

    if (root.success())
    {
      String command = root["command"];
      if (command == "WiFiConfig")
      {
        root.printTo(Serial);
        const char *ssid = root["data"]["ssid"];
        const char *password = root["data"]["pass"];

        WiFi.begin(ssid, password);
        WiFi.reconnect();
      }
      else if (command == "getSlots")
      {
        sendSlotsToWS();
      }
      else if (command == "removeEvent")
      {
        removeEvent(root["data"]["evId"]);
        loadCalendarFromSD();
        sendSlotsToWS();
      }
      else if (command == "manualIrrigation")
      {
        /*int duration = root["data"]["duration"];
        struct Chronos::Zones _zones = getZonesFromJson(root["data"]["zones"]);
        File manualFile = SD.open("/schedule_0.json", FILE_WRITE);
        if (!manualFile)
        {
          Serial.println(F("Failed to start irrigation"));
          return;
        }

        MyCalendar.removeAll(0);
        if (MyCalendar.add(Chronos::Event(0, Chronos::DateTime::now(), Chronos::DateTime::now() + Chronos::Span::Minutes(duration), _zones)))
        {
          DynamicJsonBuffer buf(128);
          JsonObject &manual = buf.createObject();
          manual["from"] = Chronos::DateTime::now().asEpoch();
          manual["to"] = (Chronos::DateTime::now() + Chronos::Span::Minutes(duration)).asEpoch();
          manual["zones"] = root["data"]["zones"];
          manual["periodicity"] = -1;
          manual["duration"] = root["data"]["duration"];
          manual.printTo(manualFile);
          ws.textAll(MANUAL_IRRIGATION_STATUS_TRUE);
        }
        else
        {
          ws.textAll(MANUAL_IRRIGATION_STATUS_FALSE);
        }*/
      }
      else if (command == "stopManualIrrigation")
      {
        stopManualIrrigation();
      }
      else if (command == "addOrEditSchedule")
      {
        addOrEditSchedule(root["data"]);
      }
      else if (command == "setEventEnabled")
      {
        if (!setEventEnabled(root["data"]["evId"], root["data"]["enabled"]))
        {
          Serial.println("Error set event enabled");
        }
        //No need to update from SD
        sendSlotsToWS();
      }
      else if (command == "getSysInfo")
      {
        sendSysInfoToWS();
      }
    }
  }
}

void removeScheduleForAllZones()
{
  for (byte zone = 1; zone < 5; zone++)
  {
    MyCalendar.removeAll(zone);
  }
}

void performUpdate(Stream &updateSource, size_t updateSize)
{
  if (Update.begin(updateSize))
  {
    size_t written = Update.writeStream(updateSource);
    if (written == updateSize)
    {
      Serial.println("Written : " + String(written) + " successfully");
    }
    else
    {
      Serial.println("Written only : " + String(written) + "/" + String(updateSize) + ". Retry?");
    }
    if (Update.end())
    {
      Serial.println("OTA done!");
      if (Update.isFinished())
      {
        Serial.println("Update successfully completed. Rebooting.");
        shouldReboot = true;
      }
      else
      {
        Serial.println("Update not finished? Something went wrong!");
      }
    }
    else
    {
      Serial.println("Error Occurred. Error #: " + String(Update.getError()));
    }
  }
  else
  {
    Serial.println("Not enough space to begin OTA");
  }
}

void updateFromFS(fs::FS &fs)
{
  File updateBin = fs.open("/update.bin");
  if (updateBin)
  {
    if (updateBin.isDirectory())
    {
      Serial.println("Error, update.bin is not a file");
      updateBin.close();
      return;
    }

    size_t updateSize = updateBin.size();

    if (updateSize > 0)
    {
      Serial.println("Try to start update");
      performUpdate(updateBin, updateSize);
    }
    else
    {
      Serial.println("Error, file is empty");
    }

    updateBin.close();
    fs.remove("/update.bin");
  }
  else
  {
    Serial.println("No updates");
  }

  if (shouldReboot)
  {
    delay(500);
    ESP.restart();
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
  WiFi.setSleep(false);
}

void initSD()
{
  Serial.println("");
  spiSD.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, spiSD))
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

  //updateFromFS(SD);
}

void initSPIFFS()
{
  SPIFFS.begin(true);
  Serial.println("");
  Serial.printf("Total space: %dKB\n", SPIFFS.totalBytes() / (1024));
  Serial.printf("Used space: %dKB\n", SPIFFS.usedBytes() / (1024));
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

    /*Chronos::Event::Occurrence occurrenceList1[3];
    int numMon = MyCalendar.listNext(3, occurrenceList1, Chronos::DateTime::now());
    if (numMon)
    {
      Serial.println("");
      Serial.print("Next event: ");
      for (int i = 0; i < numMon; i++)
      {
        Serial.print((int)occurrenceList1[i].id);
        Serial.print(": ");
        occurrenceList1[i].start.printTo(Serial);
        occurrenceList1[i].finish.printTo(Serial);
        Serial.println("");
        Serial.println("Zones: ");

        Serial.print(occurrenceList1[i].zones.zone1);
        Serial.print(occurrenceList1[i].zones.zone2);
        Serial.print(occurrenceList1[i].zones.zone3);
        Serial.print(occurrenceList1[i].zones.zone4);

        Serial.println("");
      }
    }*/

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
        if ((int)occurrenceList[i].id == 0)
        {
          //Manual irrigation
          /*ws.textAll(MANUAL_IRRIGATION_STATUS_TRUE);
          if ((Chronos::DateTime::now() - occurrenceList[i].finish) <= 1)
          {
            stopManualIrrigation();
          }*/
        }
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
  answerString.reserve(1024);
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
      Serial.println("Problem updating channel");
    }
  }
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
    WiFi.reconnect();
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
    String response;
    response.reserve(512);
    response = SIM800.readString();
    response.trim();
    if (response != "")
    {
      Serial.println(response);
    }

    // USSD Handler
    if (response.startsWith("+CUSD:"))
    {
      // Check balance
      if (response.indexOf("Balans") > -1)
      {
        String msgBalance;
        msgBalance.reserve(128);
        msgBalance = response.substring(response.indexOf("Balans") + 7);
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