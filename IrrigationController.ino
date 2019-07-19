#include "secrets.h"
#include "config.h"
#include <ArduinoJson.h> //6.11.0
#include <Wire.h>
#include <RtcDS3231.h> //2.3.3
#include <WiFi.h>
#include <FS.h>
#include <SPIFFS.h>
#include "SD.h"
#include <ESPAsyncWebServer.h> //a0d5c618ffdf976890a18a5e05e8ccd5c1ea90ed
#include <SPIFFSEditor.h>
#include <Time.h>
#include <Update.h>
#include "src/Chronos/src/Chronos.h"

SPIClass spiSD(HSPI);
AsyncWebServer server(HTTP_PORT);
AsyncWebSocket ws("/ws");
HardwareSerial HC12(1);
RtcDS3231<TwoWire> RTC(Wire);
WiFiClient client;
DefineCalendarType(Calendar, CALENDAR_TOTAL_NUM_EVENTS);
Calendar MyCalendar;
AsyncWebHandler *spiffsEditorHandler;
AsyncWebHandler *sdEditorHandler;
static AsyncClient *aClient = NULL;

enum Periodicity
{
  HOURLY,
  EVERY_X_HOUR,
  DAILY,
  EVERY_X_DAYS,
  WEEKLY,
  MONTHLY,
  ONCE
};

struct WeatherData
{
  volatile int temp = NULL;
  volatile int pressure = NULL;
  volatile int humidity = NULL;
  volatile int light = NULL;
  volatile int waterTemp = NULL;
  volatile int rain = NULL;
  volatile int groundHum = NULL;
} volatile weatherData;

volatile unsigned long HC12LastUpdate = 0;
unsigned long calendarLastCheck = 0;
unsigned long flowPrevTime = 0;
volatile unsigned long saveStatisticPrevTime = 0;
unsigned long sendSysInfoPrevTime = WS_SYS_INFO_INTERVAL;
unsigned long sendWaterInfoPrevTime = WS_WATER_INFO_INTERVAL;

bool shouldReboot = false;
volatile int flowPulses = 0;

float flowCalibrationFactor = FLOW_SENSOR_CALIBRATION;
volatile float totalLitres = 0.0;
volatile float currentDayLitres = 0.0;
volatile float currentMonthLitres = 0.0;
volatile float currentFlow = 0.0;

void setup()
{
  initPins();
  initSerial();
  initRtc();
  initSD();
  createChannelNamesIfNotExists();
  loadCalendarFromSD();
  loadManualIrrigationFromSD();
  initSPIFFS();
  initWiFi();
  initWebServer();
}

void loop()
{
  if (shouldReboot)
  {
    LOG("Rebooting...");
    delay(100);
    ESP.restart();
  }
  checkCalendar();
  flowCalculate();
  sendSysInfoToWS();
  sendWaterInfoToWS();
  listenRadio();
  saveStatistic();
}

void initPins()
{
  pinMode(LOAD_RELAY_1, OUTPUT);
  pinMode(LOAD_RELAY_2, OUTPUT);
  pinMode(LOAD_MOSFET_1, OUTPUT);
  pinMode(LOAD_MOSFET_2, OUTPUT);
  pinMode(FLOW_SENSOR_PIN, INPUT);
  digitalWrite(LOAD_RELAY_1, LOW);
  digitalWrite(LOAD_RELAY_2, LOW);
  digitalWrite(LOAD_MOSFET_1, LOW);
  digitalWrite(LOAD_MOSFET_2, LOW);
}

void LOG(const char *msg)
{
  char message[256];
  sprintf(message, "[%04u-%02u-%02u %02u:%02u:%02u]: %s", year(), month(), day(), hour(), minute(), second(), msg);
  Serial.println(message);
  char debugJson[512];
  sprintf(debugJson, "{\"command\":\"debug\", \"msg\":\"%s\"}", message);

  if (ws.count() > 0)
  {
    ws.textAll(debugJson);
  }

  if (SEND_LOGS_VIA_RADIO)
  {
    HC12.println(debugJson);
  }
}

void initRtc()
{
  RTC.Begin();
  RTC.Enable32kHzPin(false);
  RTC.SetSquareWavePin(DS3231SquareWavePin_ModeNone);
  RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);

  if (!RTC.IsDateTimeValid())
  {
    LOG("RTC lost confidence in the DateTime!");
    RTC.SetDateTime(compiled);
  }

  if (!RTC.GetIsRunning())
  {
    LOG("RTC was not actively running, starting now");
    RTC.SetIsRunning(true);
  }

  RtcDateTime now = RTC.GetDateTime();
  if (now < compiled)
  {
    LOG("RTC is older than compile time!  (Updating DateTime)");
    RTC.SetDateTime(compiled);
  }

  setSyncProvider(getTime);
  Chronos::DateTime::now().printTo(Serial);
}

static time_t getTime()
{
  return RTC.GetDateTime().Epoch32Time();
}

void initFlowSensor()
{
  attachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN), flowPulseCounter, FALLING);
}

void flowPulseCounter()
{
  flowPulses++;
}

void flowCalculate()
{
  if ((millis() - flowPrevTime) > FLOW_SENSOR_CALCULATION_INTERVAL)
  {
    detachInterrupt(digitalPinToInterrupt(FLOW_SENSOR_PIN));
    currentFlow = ((FLOW_SENSOR_CALCULATION_INTERVAL / (millis() - flowPrevTime)) * flowPulses) / flowCalibrationFactor;
    flowPrevTime = millis();
    float currentLitres = currentFlow / 60;
    totalLitres += currentLitres;
    flowPulses = 0;
    initFlowSensor();
  }
}

void createChannelNamesIfNotExists()
{
  if (!SD.exists(CHANNELS_FILE_NAME))
  {
    File channelsFile;
    channelsFile = SD.open(CHANNELS_FILE_NAME, FILE_WRITE);
    DynamicJsonDocument channelsDoc(4096);
    deserializeJson(channelsDoc, "[]");
    JsonArray arraySchedule = channelsDoc.as<JsonArray>();

    for (int i = 0; i < CHANNELS_COUNT; i++)
    {
      char name[10];
      switch (i)
      {
      case 0:
        sprintf(name, "Relay 1");
        break;
      case 1:
        sprintf(name, "Relay 2");
        break;
      case 2:
        sprintf(name, "Mosfet 1");
        break;
      case 3:
        sprintf(name, "Mosfet 2");
        break;
      default:
        sprintf(name, "RF %d", i - 3);
        break;
      }

      JsonObject channel = arraySchedule.createNestedObject();
      channel["id"] = i;
      channel["name"] = name;
    }

    serializeJson(channelsDoc, channelsFile);
    channelsFile.close();
  }
}

void sendChannelNamesToWS()
{
  if (ws.count() > 0)
  {
    if (!SD.exists(CHANNELS_FILE_NAME))
    {
      createChannelNamesIfNotExists();
    }

    File channelsFile;
    channelsFile = SD.open(CHANNELS_FILE_NAME, FILE_READ);
    DynamicJsonDocument channelsDoc(4096);
    DynamicJsonDocument response(5192);
    deserializeJson(channelsDoc, channelsFile);
    response["command"] = "getChannelNames";
    response["data"] = channelsDoc.as<JsonArray>();

    sendDocumentToWs(response);
  }
}

void saveChannelNames(const JsonArray &data)
{
  if (SD.exists(CHANNELS_FILE_NAME))
  {
    File channelsFile;
    channelsFile = SD.open(CHANNELS_FILE_NAME, FILE_WRITE);
    DynamicJsonDocument channelsDoc(4096);
    JsonArray docArray = channelsDoc.to<JsonArray>();
    docArray.set(data);

    serializeJson(channelsDoc, channelsFile);
    channelsFile.close();
    sendChannelNamesToWS();
  }
}

void saveStatistic()
{
  if ((millis() - saveStatisticPrevTime) > SAVE_STATISTIC_INTERVAL)
  {
    saveStatisticPrevTime = millis();

    Chronos::DateTime nowTime(Chronos::DateTime::now());

    char fileName[30];
    sprintf(fileName, WATER_STATISTIC_FILE, nowTime.year(), nowTime.month());

    File waterStatisticFile;
    DynamicJsonDocument waterStatistic(STATISTIC_FILE_SIZE);

    if (SD.exists(fileName))
    {
      waterStatisticFile = SD.open(fileName, FILE_READ);
      deserializeJson(waterStatistic, waterStatisticFile);
      waterStatisticFile.close();
    }

    if (waterStatistic.isNull())
    {
      deserializeJson(waterStatistic, "[]");
    }

    waterStatisticFile = SD.open(fileName, FILE_WRITE);
    if (!waterStatisticFile)
    {
      LOG("Failed to create water statistic file");
      return;
    }

    JsonArray arrayStatistic = waterStatistic.as<JsonArray>();
    int currentIndex = searchDocumentInArray(arrayStatistic, "d", nowTime.day());
    JsonObject currentDateStatistic = arrayStatistic[currentIndex];

    if (currentDateStatistic.isNull())
    {
      currentDateStatistic = arrayStatistic.createNestedObject();
    }

    float litres = currentDateStatistic["l"].as<float>();

    currentDayLitres = round((litres + totalLitres) * 100) / 100.0;
    currentDateStatistic["d"] = nowTime.day();
    currentDateStatistic["l"] = currentDayLitres;
    serializeJson(arrayStatistic, waterStatisticFile);

    currentMonthLitres = 0.0;
    for (JsonObject obj : arrayStatistic)
    {
      currentMonthLitres += obj["l"].as<float>();
    }

    waterStatisticFile.close();
    totalLitres = 0;
  }
}

void sendWaterInfoToWS()
{
  if (millis() - sendWaterInfoPrevTime >= WS_WATER_INFO_INTERVAL && ws.count() > 0)
  {
    sendWaterInfoPrevTime = millis();

    DynamicJsonDocument waterInfo(128);
    waterInfo["command"] = "getWaterInfo";

    JsonObject data = waterInfo.createNestedObject("data");

    data["flow"] = round(currentFlow * 10) / 10.0;
    data["curDay"] = currentDayLitres;
    data["curMonth"] = currentMonthLitres;

    sendDocumentToWs(waterInfo);
  }
}

int searchDocumentInArray(const JsonArray &arr, char *field, int search)
{
  int index = 0;
  for (JsonObject obj : arr)
  {
    if (obj[field] == search)
    {
      return index;
    }
    index++;
  }

  return index;
}

void WSTextAll(String msg)
{
  if (ws.count() > 0)
  {
    ws.textAll(msg);
  }
}

void initWebServer()
{
  ws.onEvent(onWsEvent);
  server.addHandler(&ws);
  server.serveStatic("/app.js", SPIFFS, "/app.js").setCacheControl("max-age=0");
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.htm");
  server.rewrite("/wifi", "/wifi.html");

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
      LOG("Update Start");
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
        LOG("Update Success");
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

void sendDocumentToWs(const JsonDocument &doc)
{
  if (ws.count() > 0)
  {
    String msg;
    serializeJson(doc, msg);
    ws.textAll(msg);
  }
}

bool openScheduleFromSD(DynamicJsonDocument &doc)
{
  File scheduleFile;

  if (!SD.exists(SCHEDULE_FILE_NAME))
  {
    return false;
  }

  scheduleFile = SD.open(SCHEDULE_FILE_NAME, FILE_READ);
  if (!scheduleFile)
  {
    PRINTLN(F("Failed to read schedule"));

    return false;
  }

  DeserializationError error = deserializeJson(doc, scheduleFile);
  if (error)
  {
    PRINTLN(F("deserializeJson() failed with code "));
    PRINTLN(error.c_str());

    return false;
  }

  scheduleFile.close();
}

void loadCalendarFromSD()
{
  DynamicJsonDocument schedule(SCHEDULE_FILE_SIZE);

  if (!openScheduleFromSD(schedule))
  {
    return;
  }

  PRINTLN(F("Schedule file is opened. Document size is:"));
  PRINTLN(measureJson(schedule));

  MyCalendar.clear();

  JsonArray arraySchedule = schedule.as<JsonArray>();
  int evId = 0;
  for (JsonObject eventData : arraySchedule)
  {
    addEventToCalendar(evId, eventData);
    evId++;
  }
}

void sendSlotsToWS()
{
  if (ws.count() > 0)
  {
    DynamicJsonDocument schedule(SCHEDULE_FILE_SIZE);

    if (!openScheduleFromSD(schedule))
    {
      return;
    }

    DynamicJsonDocument response(SCHEDULE_FILE_SIZE + 128);
    response["command"] = "getSlots";
    JsonObject data = response.createNestedObject("data");
    JsonArray slots = data.createNestedArray("slots");

    JsonArray arraySchedule = schedule.as<JsonArray>();
    for (JsonObject eventData : arraySchedule)
    {
      slots.add(eventData);
    }

    if (slots.size() != MyCalendar.numEvents())
    {
      loadCalendarFromSD();
      sendSlotsToWS();
    }

    data["total"] = CALENDAR_MAX_NUM_EVENTS;
    data["occupied"] = MyCalendar.numEvents();

    sendDocumentToWs(response);
  }
}

void sendSysInfoToWS()
{
  if (millis() - sendSysInfoPrevTime >= WS_SYS_INFO_INTERVAL && ws.count() > 0)
  {
    sendSysInfoPrevTime = millis();

    DynamicJsonDocument sysInfo(1024);
    sysInfo["command"] = "getSysInfo";

    JsonObject data = sysInfo.createNestedObject("data");
    char datestring[20];
    sprintf(datestring, "%04u-%02u-%02u %02u:%02u:%02u", year(), month(), day(), hour(), minute(), second());
    data["time"] = datestring;
    JsonObject wifi = data.createNestedObject("WiFi");
    wifi["SSID"] = WiFi.SSID();
    wifi["RSSI"] = WiFi.RSSI();
    wifi["localIP"] = WiFi.localIP().toString();

    JsonObject heap = data.createNestedObject("heap");
    heap["total"] = ESP.getHeapSize();
    heap["free"] = ESP.getFreeHeap();
    heap["min"] = ESP.getMinFreeHeap();
    heap["maxAlloc"] = ESP.getMaxAllocHeap();

    JsonObject sdInfo = data.createNestedObject("SD");
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

    JsonObject spiffsInfo = data.createNestedObject("SPIFFS");
    sprintf(size, "%dKB", SPIFFS.totalBytes() / 1024);
    spiffsInfo["total"] = size;
    sprintf(size, "%dKB", SPIFFS.usedBytes() / 1024);
    spiffsInfo["used"] = size;

    sendDocumentToWs(sysInfo);
  }
}

void getChannelsFromJson(const JsonArray &arr, bool *_channels)
{
  for (int channel : arr)
  {
    _channels[channel] = true;
  }
}

bool removeEvent(int evId)
{
  DynamicJsonDocument schedule(SCHEDULE_FILE_SIZE);

  if (!openScheduleFromSD(schedule))
  {
    return false;
  }

  if (MyCalendar.remove(evId))
  {
    calendarLastCheck = 0;
    File scheduleFile = SD.open(SCHEDULE_FILE_NAME, FILE_WRITE);
    if (!scheduleFile)
    {
      LOG("Failed to create schedule");
      return false;
    }

    if (!schedule[evId].isNull())
    {
      schedule.remove(evId);
    }

    serializeJson(schedule, scheduleFile);
    scheduleFile.close();
  }

  return false;
}

bool setEventEnabled(byte evId, bool enabled)
{
  DynamicJsonDocument schedule(SCHEDULE_FILE_SIZE);

  if (!openScheduleFromSD(schedule))
  {
    return false;
  }

  MyCalendar.setEnabled(evId, enabled);
  calendarLastCheck = 0;

  if (!schedule[evId].isNull())
  {
    schedule[evId]["enabled"] = enabled;
  }
  else
  {
    return false;
  }

  schedule[evId].remove("skipUntil");
  MyCalendar.skipEvent(evId, Chronos::DateTime(1970, 1, 1));

  File scheduleFile = SD.open(SCHEDULE_FILE_NAME, FILE_WRITE);
  if (!scheduleFile)
  {
    LOG("Failed to create schedule");

    return false;
  }

  serializeJson(schedule, scheduleFile);
  scheduleFile.close();

  return true;
}

bool skipEvent(byte evId)
{
  DynamicJsonDocument schedule(SCHEDULE_FILE_SIZE);

  if (!openScheduleFromSD(schedule))
  {
    return false;
  }

  Chronos::DateTime finish = MyCalendar.closestFinish(evId);
  MyCalendar.skipEvent(evId, finish);

  calendarLastCheck = 0;

  if (!schedule[evId].isNull())
  {
    schedule[evId]["skipUntil"] = finish.asEpoch();
  }
  else
  {
    return false;
  }

  File scheduleFile = SD.open(SCHEDULE_FILE_NAME, FILE_WRITE);
  if (!scheduleFile)
  {
    LOG("Failed to create schedule");

    return false;
  }

  serializeJson(schedule, scheduleFile);
  scheduleFile.close();

  return true;
}

void loadManualIrrigationFromSD()
{
  File manualFile;

  if (!SD.exists(MANUAL_IRRIGATION_FILE_NAME))
  {
    return;
  }

  manualFile = SD.open(MANUAL_IRRIGATION_FILE_NAME, FILE_READ);
  if (!manualFile)
  {
    PRINTLN(F("Failed to read manual irrigation file"));

    return;
  }

  DynamicJsonDocument manual(128);

  DeserializationError error = deserializeJson(manual, manualFile);
  if (error)
  {
    PRINTLN(F("deserializeJson() failed with code "));
    PRINTLN(error.c_str());

    return;
  }

  manualFile.close();

  JsonObject eventData = manual.as<JsonObject>();

  int duration = eventData["duration"];
  bool _channels[CHANNELS_COUNT] = {false};
  getChannelsFromJson(eventData["channels"], _channels);
  Chronos::EpochTime from = eventData["from"];
  Chronos::EpochTime to = eventData["to"];

  MyCalendar.remove(MANUAL_IRRIGATION_EVENT_ID);

  // Expired event
  if (to < Chronos::DateTime::now().asEpoch())
  {
    SD.remove(MANUAL_IRRIGATION_FILE_NAME);
    return;
  }

  MyCalendar.add(Chronos::Event(MANUAL_IRRIGATION_EVENT_ID, Chronos::DateTime(from), Chronos::DateTime(to), _channels));
}

void stopManualIrrigation()
{
  MyCalendar.remove(MANUAL_IRRIGATION_EVENT_ID);
  calendarLastCheck = 0;
  SD.remove(MANUAL_IRRIGATION_FILE_NAME);
  WSTextAll(MANUAL_IRRIGATION_STOP);
}

void addOrEditSchedule(const JsonObject &eventData)
{
  JsonVariant eventId = eventData["evId"];
  bool isEditEvent = !eventId.isNull();
  byte evId = MyCalendar.numEvents();

  if (MyCalendar.numEvents() >= CALENDAR_MAX_NUM_EVENTS && !isEditEvent)
  {
    return;
  }

  if (isEditEvent)
  {
    evId = eventId.as<int>();

    char tmp[32];
    sprintf(tmp, "Edit schedule #%d", evId);
    LOG(tmp);
    eventData.remove("skipUntil");
    MyCalendar.skipEvent(evId, Chronos::DateTime(1970, 1, 1));
    eventData["enabled"] = MyCalendar.isEnabled(evId);
    MyCalendar.removeAll(evId);
  }
  else
  {
    LOG("Add schedule");
    eventData["enabled"] = true; //New event always enabled
  }

  DynamicJsonDocument schedule(SCHEDULE_FILE_SIZE);

  openScheduleFromSD(schedule);

  if (addEventToCalendar(evId, eventData))
  {
    if (schedule.isNull())
    {
      deserializeJson(schedule, "[]");
    }

    File scheduleFile = SD.open(SCHEDULE_FILE_NAME, FILE_WRITE);
    if (!scheduleFile)
    {
      LOG("Failed to create schedule");
      return;
    }

    if (!schedule[evId].isNull())
    {
      schedule[evId] = eventData;
    }
    else
    {
      schedule.add(eventData);
    }

    serializeJson(schedule, scheduleFile);
    scheduleFile.close();
  }

  calendarLastCheck = 0;

  sendSlotsToWS();

  WSTextAll(SCHEDULE_ADD_EDIT);
}

bool addEventToCalendar(byte evId, const JsonObject &eventData)
{
  int duration = eventData["duration"];
  bool _channels[CHANNELS_COUNT] = {false};
  getChannelsFromJson(eventData["channels"], _channels);
  byte periodicity = eventData["periodicity"];
  bool eventSaved = false;
  JsonVariant enabled = eventData["enabled"];
  JsonVariant evIdElem = eventData["evId"];
  bool isSkip = !eventData["skipUntil"].isNull();
  Chronos::EpochTime skipUntilTime = eventData["skipUntil"];
  Chronos::DateTime skipUntil = Chronos::DateTime(skipUntilTime);

  if (enabled.isNull())
  {
    eventData["enabled"] = true;
  }

  if (!evIdElem.isNull())
  {
    eventData.remove("evId");
  }

  bool isEnabled = enabled.as<bool>();

  switch (periodicity)
  {
  case Periodicity::HOURLY:
  {
    byte minute = eventData["minute"];
    byte second = eventData["second"];

    eventSaved = MyCalendar.add(Chronos::Event(evId, Chronos::Mark::Hourly(minute, second), Chronos::Span::Minutes(duration), _channels, isEnabled));
  }
  break;
  case Periodicity::EVERY_X_HOUR:
  {
    byte hours = eventData["hours"];
    byte minute = eventData["minute"];
    byte second = eventData["second"];

    eventSaved = MyCalendar.add(Chronos::Event(evId, Chronos::Mark::EveryXHours(hours, minute, second), Chronos::Span::Minutes(duration), _channels, isEnabled));
  }
  break;
  case Periodicity::DAILY:
  {
    byte hour = eventData["hour"];
    byte minute = eventData["minute"];

    eventSaved = MyCalendar.add(Chronos::Event(evId, Chronos::Mark::Daily(hour, minute), Chronos::Span::Minutes(duration), _channels, isEnabled));
  }
  break;
  case Periodicity::EVERY_X_DAYS:
  {
    byte days = eventData["days"];
    byte hour = eventData["hour"];
    byte minute = eventData["minute"];

    eventSaved = MyCalendar.add(Chronos::Event(evId, Chronos::Mark::EveryXDays(days, hour, minute), Chronos::Span::Minutes(duration), _channels, isEnabled));
  }
  break;
  case Periodicity::WEEKLY:
  {
    byte dayOfWeek = eventData["dayOfWeek"];
    byte hour = eventData["hour"];
    byte minute = eventData["minute"];

    eventSaved = MyCalendar.add(Chronos::Event(evId, Chronos::Mark::Weekly(dayOfWeek, hour, minute), Chronos::Span::Minutes(duration), _channels, isEnabled));
  }
  break;
  case Periodicity::MONTHLY:
  {
    byte dayOfMonth = eventData["dayOfMonth"];
    byte hour = eventData["hour"];
    byte minute = eventData["minute"];

    eventSaved = MyCalendar.add(Chronos::Event(evId, Chronos::Mark::Monthly(dayOfMonth, hour, minute), Chronos::Span::Minutes(duration), _channels, isEnabled));
  }
  case Periodicity::ONCE:
  {
    int year = eventData["year"];
    byte month = eventData["month"];
    byte day = eventData["day"];
    byte hour = eventData["hour"];
    byte minute = eventData["minute"];
    byte second = eventData["second"];

    eventSaved = MyCalendar.add(Chronos::Event(evId, Chronos::DateTime(year, month, day, hour, minute, second), Chronos::Span::Minutes(duration), _channels, isEnabled));
  }
  break;
  }

  if (isSkip)
  {
    //MyCalendar.skipEvent(evId, skipUntil);
    Chronos::DateTime finish = MyCalendar.closestFinish(evId);
    MyCalendar.skipEvent(evId, finish);

    calendarLastCheck = 0;
  }

  return eventSaved;
}

void addManualEventToCalendar(const JsonObject &eventData)
{
  int duration = eventData["duration"];
  bool _channels[CHANNELS_COUNT] = {false};
  getChannelsFromJson(eventData["channels"], _channels);

  File manualFile = SD.open(MANUAL_IRRIGATION_FILE_NAME, FILE_WRITE);
  if (!manualFile)
  {
    Serial.println(F("Failed to start irrigation"));
    return;
  }

  MyCalendar.remove(MANUAL_IRRIGATION_EVENT_ID);
  if (MyCalendar.add(Chronos::Event(MANUAL_IRRIGATION_EVENT_ID, Chronos::DateTime::now(), Chronos::DateTime::now() + Chronos::Span::Minutes(duration), _channels)))
  {
    DynamicJsonDocument manual(128);
    manual["from"] = Chronos::DateTime::now().asEpoch();
    manual["to"] = (Chronos::DateTime::now() + Chronos::Span::Minutes(duration)).asEpoch();
    manual["channels"] = eventData["channels"];
    manual["duration"] = eventData["duration"];

    serializeJson(manual, manualFile);
    manualFile.close();
    calendarLastCheck = 0;
  }
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

    DynamicJsonDocument root(5192);
    deserializeJson(root, msg);
    if (!root.isNull())
    {
      String command = root["command"];
      if (command == "WiFiConfig")
      {
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
        sendSlotsToWS();
      }
      else if (command == "manualIrrigation")
      {
        addManualEventToCalendar(root["data"]);
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
          LOG("Error set event enabled");
        }
        sendSlotsToWS();
      }
      else if (command == "skipEvent")
      {
        if (MyCalendar.isRecurring(root["data"]["evId"]))
        {
          skipEvent(root["data"]["evId"]);
        }
        else
        {
          removeEvent(root["data"]["evId"]);
          sendSlotsToWS();
        }
      }
      else if (command == "getChannelNames")
      {
        sendChannelNamesToWS();
      }
      else if (command == "saveChannelNames")
      {
        saveChannelNames(root["data"]);
      }
      else if (command == "setTime")
      {
        JsonObject data = root["data"];
        RtcDateTime newTime = RtcDateTime(data["year"], data["month"], data["day"], data["hour"], data["minute"], data["second"]);
        RTC.SetDateTime(newTime);
        setTime(RTC.GetDateTime().Epoch32Time());
        Chronos::DateTime::now().printTo(Serial);

        calendarLastCheck = 0;
      }
    }
  }
}

void performUpdate(Stream &updateSource, size_t updateSize)
{
  if (Update.begin(updateSize))
  {
    size_t written = Update.writeStream(updateSource);
    if (written == updateSize)
    {
      LOG("Written successfully");
    }
    else
    {
      LOG("Written not completed");
    }
    if (Update.end())
    {
      LOG("OTA done!");
      if (Update.isFinished())
      {
        LOG("Update successfully completed. Rebooting.");
        shouldReboot = true;
      }
      else
      {
        LOG("Update not finished? Something went wrong!");
      }
    }
    else
    {
      LOG("Error Occurred");
    }
  }
  else
  {
    LOG("Not enough space to begin OTA");
  }
}

void updateFromFS(fs::FS &fs)
{
  File updateBin = fs.open("/update.bin");
  if (updateBin)
  {
    if (updateBin.isDirectory())
    {
      LOG("Error, update.bin is not a file");
      updateBin.close();
      return;
    }

    size_t updateSize = updateBin.size();

    if (updateSize > 0)
    {
      LOG("Try to start update");
      performUpdate(updateBin, updateSize);
    }
    else
    {
      LOG("Error, file is empty");
    }

    updateBin.close();
    fs.remove("/update.bin");
  }
  else
  {
    LOG("No updates");
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
  pinMode(SD_MOSI, INPUT_PULLUP);
  spiSD.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS, spiSD))
  {
    LOG("Card Mount Failed");
    return;
  }

  uint8_t cardType = SD.cardType();

  if (cardType == CARD_NONE)
  {
    LOG("No SD card attached");
    return;
  }

  if (cardType == CARD_MMC)
  {
    LOG("SD Card Type: MMC");
  }
  else if (cardType == CARD_SD)
  {
    LOG("SD Card Type: SDSC");
  }
  else if (cardType == CARD_SDHC)
  {
    LOG("SD Card Type: SDHC");
  }
  else
  {
    LOG("SD Card Type: UNKNOWN");
  }

  uint64_t cardSize = SD.cardSize() / (1024 * 1024);

  char cs[32];
  sprintf(cs, "SD Card Size: %lluMB\n", cardSize);
  LOG(cs);

  char ts[32];
  sprintf(ts, "Total space: %lluMB\n", SD.totalBytes() / (1024 * 1024));
  LOG(ts);

  char us[32];
  sprintf(us, "Used space: %lluMB\n", SD.usedBytes() / (1024 * 1024));
  LOG(us);

  updateFromFS(SD);
}

void initSPIFFS()
{
  SPIFFS.begin(true);
  char ts[32];
  sprintf(ts, "Total space: %dKB\n", SPIFFS.totalBytes() / (1024));
  LOG(ts);

  char us[32];
  sprintf(us, "Used space: %dKB\n", SPIFFS.usedBytes() / (1024));
  LOG(us);
}

void initSerial()
{
  Serial.begin(BAUD_RATE);
  HC12.begin(BAUD_RATE_RADIO, SERIAL_8N1, HC_12_RX, HC_12_TX);
}

void checkCalendar()
{
  bool currentChannelsState[CHANNELS_COUNT] = {false};

  if (dateIsValid() && (millis() - calendarLastCheck >= CALENDAR_CHECK_INTERVAL))
  {
    calendarLastCheck = millis();

    Chronos::Event::Occurrence nextList[CHANNELS_COUNT];
    int numNext = MyCalendar.listNext(CHANNELS_COUNT, 1, nextList, Chronos::DateTime::now());

    Chronos::Event::Occurrence occurrenceList[CALENDAR_OCCURRENCES_LIST_SIZE];
    int numOngoing = MyCalendar.listOngoing(CALENDAR_OCCURRENCES_LIST_SIZE, occurrenceList, Chronos::DateTime::now());

    if (numOngoing)
    {
      for (int i = 0; i < numOngoing; i++)
      {
        Serial.printf("**** Event %d\n", (int)occurrenceList[i].id);
        (Chronos::DateTime::now() - occurrenceList[i].finish).printTo(Serial);
        Serial.println("");

        if ((int)occurrenceList[i].id == MANUAL_IRRIGATION_EVENT_ID)
        {
          if ((Chronos::DateTime::now() - occurrenceList[i].finish) <= 1)
          {
            stopManualIrrigation();
          }
        }

        // If the current event is not recurring and is overdue then delete it
        if (MyCalendar.isOverdue((int)occurrenceList[i].id))
        {
          removeEvent((int)occurrenceList[i].id);
          sendSlotsToWS();
        }

        for (byte n = 0; n < CHANNELS_COUNT; n++)
        {
          currentChannelsState[n] = currentChannelsState[n] ?: occurrenceList[i].channels[n];
        }
      }
    }

    digitalWrite(LOAD_RELAY_1, currentChannelsState[0] ? HIGH : LOW);
    digitalWrite(LOAD_RELAY_2, currentChannelsState[1] ? HIGH : LOW);
    digitalWrite(LOAD_MOSFET_1, currentChannelsState[2] ? HIGH : LOW);
    digitalWrite(LOAD_MOSFET_2, currentChannelsState[3] ? HIGH : LOW);

    if (ws.count() > 0)
    {
      DynamicJsonDocument ongoing(4000);
      ongoing["command"] = "ongoingEvents";
      JsonArray data = ongoing.createNestedArray("data");

      if (numOngoing)
      {
        for (int i = 0; i < numOngoing; i++)
        {
          JsonObject occurrence = data.createNestedObject();
          JsonArray ocurenceChannels = occurrence.createNestedArray("channels");
          for (byte n = 0; n < CHANNELS_COUNT; n++)
          {
            ocurenceChannels.add(occurrenceList[i].channels[n] ? 1 : 0);
          }
          occurrence["from"] = occurrenceList[i].start.asEpoch();
          occurrence["to"] = occurrenceList[i].finish.asEpoch();
          occurrence["elapsed"] = (Chronos::DateTime::now() - occurrenceList[i].finish).totalSeconds();
          occurrence["evId"] = occurrenceList[i].id;
          occurrence["isManual"] = (int)occurrenceList[i].id == MANUAL_IRRIGATION_EVENT_ID;
        }
      }

      sendDocumentToWs(ongoing);

      DynamicJsonDocument next(4000);
      next["command"] = "nextEvents";
      JsonArray nextData = next.createNestedArray("data");

      if (numNext)
      {
        for (int i = 0; i < numNext; i++)
        {
          JsonObject nextOccurrence = nextData.createNestedObject();
          JsonArray nextOcurenceChannels = nextOccurrence.createNestedArray("channels");
          for (byte n = 0; n < CHANNELS_COUNT; n++)
          {
            nextOcurenceChannels.add(nextList[i].channels[n] ? 1 : 0);
          }
          nextOccurrence["from"] = nextList[i].start.asEpoch();
          nextOccurrence["to"] = nextList[i].finish.asEpoch();
          nextOccurrence["elapsed"] = (nextList[i].start - Chronos::DateTime::now()).totalSeconds();
          nextOccurrence["evId"] = nextList[i].id;
        }
      }

      sendDocumentToWs(next);
    }

    processRemoteChannels(currentChannelsState);
  }
}

void processRemoteChannels(bool *currentChannelsState)
{
  String data = "ch";
  // Process remote channels. 4 channels reserved for channels in PCB
  for (byte channel = 4; channel < CHANNELS_COUNT; channel++)
  {
    data += currentChannelsState[channel] ? '1' : '0';
  }
  data += ';';

  HC12.println(data);
}

void listenRadio()
{
  if (HC12.available())
  {
    if (millis() - HC12LastUpdate >= HC_12_UPDATE_INTERVAL)
    {
      DynamicJsonDocument root(1024);
      deserializeJson(root, HC12);

      if (!root.isNull())
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
      }
    }
  }
}

void updateWeatherData(volatile int temp, volatile int pressure, volatile int humidity, volatile int light, volatile int waterTemp, volatile int rain, volatile int groundHum)
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

  if (ws.count() > 0)
  {
    DynamicJsonDocument answer(1024);
    answer["command"] = "weatherUpdate";
    JsonObject data = answer.createNestedObject("data");
    data["temp"] = weatherData.temp;
    data["pressure"] = weatherData.pressure;
    data["humidity"] = weatherData.humidity;
    data["light"] = weatherData.light;
    data["waterTemp"] = weatherData.waterTemp;
    data["rain"] = weatherData.rain;
    data["groundHum"] = weatherData.groundHum;
    sendDocumentToWs(answer);
  }
}

void WiFiEvent(WiFiEvent_t event)
{
  switch (event)
  {
  case SYSTEM_EVENT_WIFI_READY:
    //LOG("WiFi interface ready");
    break;
  case SYSTEM_EVENT_SCAN_DONE:
    LOG("Completed scan for access points");
    break;
  case SYSTEM_EVENT_STA_START:
    //LOG("WiFi client started");
    break;
  case SYSTEM_EVENT_STA_STOP:
    LOG("WiFi clients stopped");
    break;
  case SYSTEM_EVENT_STA_CONNECTED:
    LOG("Connected to access point");
    break;
  case SYSTEM_EVENT_STA_DISCONNECTED:
    LOG("Disconnected from WiFi access point");
    WiFi.reconnect();
    break;
  case SYSTEM_EVENT_STA_AUTHMODE_CHANGE:
    LOG("Authentication mode of access point has changed");
    break;
  case SYSTEM_EVENT_STA_GOT_IP:
    char tmp[64];
    sprintf(tmp, "Obtained IP address: %s", WiFi.localIP().toString());
    LOG(tmp);
    break;
  case SYSTEM_EVENT_STA_LOST_IP:
    LOG("Lost IP address and IP address is reset to 0");
    WiFi.reconnect();
    break;
  case SYSTEM_EVENT_STA_WPS_ER_SUCCESS:
    LOG("WiFi Protected Setup (WPS): succeeded in enrollee mode");
    break;
  case SYSTEM_EVENT_STA_WPS_ER_FAILED:
    LOG("WiFi Protected Setup (WPS): failed in enrollee mode");
    break;
  case SYSTEM_EVENT_STA_WPS_ER_TIMEOUT:
    LOG("WiFi Protected Setup (WPS): timeout in enrollee mode");
    break;
  case SYSTEM_EVENT_STA_WPS_ER_PIN:
    LOG("WiFi Protected Setup (WPS): pin code in enrollee mode");
    break;
  case SYSTEM_EVENT_AP_START:
    LOG("WiFi access point started");
    break;
  case SYSTEM_EVENT_AP_STOP:
    LOG("WiFi access point  stopped");
    break;
  case SYSTEM_EVENT_AP_STACONNECTED:
    LOG("Client connected");
    break;
  case SYSTEM_EVENT_AP_STADISCONNECTED:
    LOG("Client disconnected");
    break;
  case SYSTEM_EVENT_AP_STAIPASSIGNED:
    LOG("Assigned IP address to client");
    break;
  case SYSTEM_EVENT_AP_PROBEREQRECVED:
    LOG("Received probe request");
    break;
  case SYSTEM_EVENT_GOT_IP6:
    LOG("IPv6 is preferred");
    break;
  case SYSTEM_EVENT_ETH_START:
    LOG("Ethernet started");
    break;
  case SYSTEM_EVENT_ETH_STOP:
    LOG("Ethernet stopped");
    break;
  case SYSTEM_EVENT_ETH_CONNECTED:
    LOG("Ethernet connected");
    break;
  case SYSTEM_EVENT_ETH_DISCONNECTED:
    LOG("Ethernet disconnected");
    break;
  case SYSTEM_EVENT_ETH_GOT_IP:
    LOG("Obtained IP address");
    break;
  }
}

bool dateIsValid()
{
  RtcDateTime compiled = RtcDateTime(__DATE__, __TIME__);
  return year() >= compiled.Year();
}