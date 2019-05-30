#include "secrets.h"
#include "config.h"
#include <ArduinoJson.h>
#include <WiFi.h>
#include <FS.h>
#include <SPIFFS.h>
#include "SD.h"
#include <HTTPClient.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFSEditor.h>
#include <Time.h>
#include <Update.h>
#include "src/Chronos/src/Chronos.h"

SPIClass spiSD(HSPI);
AsyncWebServer server(HTTP_PORT);
AsyncWebSocket ws("/ws");
HardwareSerial HC12(1);
HardwareSerial SIM800(2);
WiFiClient client;
DefineCalendarType(Calendar, CALENDAR_MAX_NUM_EVENTS);
Calendar MyCalendar;
AsyncWebHandler *spiffsEditorHandler;
AsyncWebHandler *sdEditorHandler;
TaskHandle_t OWMHandler;

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
  unsigned int OWMMainId = 0;
  float OWMRain3h = 0;
} weatherData;

unsigned long HC12LastUpdate = 0;
unsigned long calendarLastCheck = 0;
unsigned long RTCLastSync = 0;
unsigned long balanceLastCheck = CHECK_BALANCE_INTERVAL;
unsigned long GSMStatusLastCheck = CHECK_STATUS_INTERVAL;
unsigned long flowPrevTime = 0;
unsigned long saveStatisticPrevTime = 0;

int currentBalance = NULL;
byte CREGCode = 0;
int GSMSignal = 0;
String GSMPhoneNumber;
bool shouldReboot = false;
bool manualIrrigationRunning = false;
volatile int flowPulses = 0;

float flowCalibrationFactor = FLOW_SENSOR_CALIBRATION;
float totalLitres = 0.0;
float currentDayLitres = 0.0;
float currentMonthLitres = 0.0;
float currentFlow = 0.0;

void setup()
{
  initPins();
  initSerial();
  initSD();
  loadCalendarFromSD();
  loadManualIrrigationFromSD();
  initSPIFFS();
  initWiFi();
  initWebServer();
  xTaskCreatePinnedToCore(
      getWeatherFromOpenWeatherMap,
      "OWM",
      1000,
      NULL,
      0,
      &OWMHandler,
      0);
}

void loop()
{
  if (shouldReboot)
  {
    Serial.println("Rebooting...");
    delay(100);
    ESP.restart();
  }
  syncRTC();
  listenSIM800();
  checkCalendar();
  listenRadio();
  checkBalance();
  checkGSMStatus();
  flowCalculate();
  saveStatistic();
}

void initPins()
{
  pinMode(LOAD_RELAY_1, OUTPUT);
  pinMode(LOAD_RELAY_2, OUTPUT);
  pinMode(LOAD_MOSFET_1, OUTPUT);
  pinMode(LOAD_MOSFET_2, OUTPUT);
  digitalWrite(LOAD_RELAY_1, LOW);
  digitalWrite(LOAD_RELAY_2, LOW);
  digitalWrite(LOAD_MOSFET_1, LOW);
  digitalWrite(LOAD_MOSFET_2, LOW);
  pinMode(FLOW_SENSOR_PIN, INPUT);
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

void saveStatistic()
{
  if ((millis() - saveStatisticPrevTime) > SAVE_STATISTIC_INTERVAL)
  {
    Serial.println("");
    Serial.print("Saving statistics...");
    unsigned long start = millis();

    Chronos::DateTime nowTime(Chronos::DateTime::now());

    char fileName[30];
    sprintf(fileName, WATER_STATISTIC_FILE, nowTime.year(), nowTime.month());

    saveStatisticPrevTime = millis();

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
      Serial.println(F("Failed to create water statistic file"));
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

    Serial.println("OK");
    Serial.print("Saving time = ");
    Serial.print(millis() - start);
    Serial.print("ms");
    Serial.println("");
  }
}

void sendWaterInfoToWS()
{
  DynamicJsonDocument waterInfo(128);
  waterInfo["command"] = "getWaterInfo";

  JsonObject data = waterInfo.createNestedObject("data");

  data["flow"] = round(currentFlow * 10) / 10.0;
  data["curDay"] = currentDayLitres;
  data["curMonth"] = currentMonthLitres;

  sendDocumentToWs(waterInfo);
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
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html");
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

  if (slots.size() != MyCalendar.numRecurring())
  {
    loadCalendarFromSD();
    sendSlotsToWS();
  }

  data["total"] = CALENDAR_MAX_NUM_EVENTS - 1;
  data["occupied"] = MyCalendar.numRecurring();

  sendDocumentToWs(response);
}

void sendSysInfoToWS()
{
  DynamicJsonDocument sysInfo(1024);
  sysInfo["command"] = "getSysInfo";

  JsonObject data = sysInfo.createNestedObject("data");
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

  JsonObject gsm = data.createNestedObject("gsm");
  gsm["balance"] = currentBalance;
  gsm["CREGCode"] = CREGCode;
  gsm["signal"] = GSMSignal;
  gsm["phone"] = GSMPhoneNumber;

  sendDocumentToWs(sysInfo);
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
      Serial.println(F("Failed to create schedule"));
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

  File scheduleFile = SD.open(SCHEDULE_FILE_NAME, FILE_WRITE);
  if (!scheduleFile)
  {
    Serial.println(F("Failed to create schedule"));

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
    Serial.println(F("Failed to create schedule"));

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
  byte evId = MyCalendar.numRecurring();

  if (MyCalendar.numRecurring() >= CALENDAR_MAX_NUM_EVENTS - 1 && !isEditEvent)
  {
    return;
  }

  if (isEditEvent)
  {
    evId = eventId.as<int>();

    Serial.println("");
    Serial.print("Edit schedule #");
    Serial.println(evId);
    eventData.remove("skipUntil");
    eventData["enabled"] = MyCalendar.isEnabled(evId);
    MyCalendar.removeAll(evId);
  }
  else
  {
    Serial.println("");
    Serial.print("Add schedule");
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
      Serial.println(F("Failed to create schedule"));
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

  Serial.println("Event id:");
  Serial.println(evId);

  switch (periodicity)
  {
  case Periodicity::HOURLY:
  {
    Serial.println("HOURLY");
    byte minute = eventData["minute"];
    byte second = eventData["second"];

    eventSaved = MyCalendar.add(Chronos::Event(evId, Chronos::Mark::Hourly(minute, second), Chronos::Span::Minutes(duration), _channels, isEnabled));
  }
  break;
  case Periodicity::EVERY_X_HOUR:
  {
    Serial.println("EVERY_X_HOUR");
    byte hours = eventData["hours"];
    byte minute = eventData["minute"];
    byte second = eventData["second"];

    eventSaved = MyCalendar.add(Chronos::Event(evId, Chronos::Mark::EveryXHours(hours, minute, second), Chronos::Span::Minutes(duration), _channels, isEnabled));
  }
  break;
  case Periodicity::DAILY:
  {
    Serial.println("DAILY");
    byte hour = eventData["hour"];
    byte minute = eventData["minute"];

    eventSaved = MyCalendar.add(Chronos::Event(evId, Chronos::Mark::Daily(hour, minute), Chronos::Span::Minutes(duration), _channels, isEnabled));
  }
  break;
  case Periodicity::EVERY_X_DAYS:
  {
    Serial.println("EVERY_X_DAYS");
    byte days = eventData["days"];
    byte hour = eventData["hour"];
    byte minute = eventData["minute"];

    eventSaved = MyCalendar.add(Chronos::Event(evId, Chronos::Mark::EveryXDays(days, hour, minute), Chronos::Span::Minutes(duration), _channels, isEnabled));
  }
  break;
  case Periodicity::WEEKLY:
  {
    Serial.println("WEEKLY");
    byte dayOfWeek = eventData["dayOfWeek"];
    byte hour = eventData["hour"];
    byte minute = eventData["minute"];

    eventSaved = MyCalendar.add(Chronos::Event(evId, Chronos::Mark::Weekly(dayOfWeek, hour, minute), Chronos::Span::Minutes(duration), _channels, isEnabled));
  }
  break;
  case Periodicity::MONTHLY:
  {
    Serial.println("MONTHLY");
    byte dayOfMonth = eventData["dayOfMonth"];
    byte hour = eventData["hour"];
    byte minute = eventData["minute"];

    eventSaved = MyCalendar.add(Chronos::Event(evId, Chronos::Mark::Monthly(dayOfMonth, hour, minute), Chronos::Span::Minutes(duration), _channels, isEnabled));
  }
  break;
  }

  MyCalendar.skipEvent(evId, skipUntil);

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
    WSTextAll(MANUAL_IRRIGATION_STATUS_TRUE);
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

    DynamicJsonDocument root(512);
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
          Serial.println("Error set event enabled");
        }
        sendSlotsToWS();
      }
      else if (command == "getSysInfo")
      {
        sendSysInfoToWS();
      }
      else if (command == "getWaterInfo")
      {
        sendWaterInfoToWS();
      }
      else if (command == "skipEvent")
      {
        skipEvent(root["data"]["evId"]);
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

  updateFromFS(SD);
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
  HC12.begin(BAUD_RATE_RADIO, SERIAL_8N1, HC_12_RX, HC_12_TX);
  SIM800.begin(BAUD_RATE);
  sendATCommand("AT");

  bool _channels[CHANNELS_COUNT] = {false};
  processRemoteChannels(_channels);
}

void checkCalendar()
{
  bool _channels[CHANNELS_COUNT] = {false};

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
        Serial.println("");
        Serial.print("**** Event: ");
        Serial.print((int)occurrenceList[i].id);
        Serial.print(": ");
        (Chronos::DateTime::now() - occurrenceList[i].finish).printTo(Serial);
        Serial.println("");

        if ((int)occurrenceList[i].id == MANUAL_IRRIGATION_EVENT_ID)
        {
          //Manual irrigation
          WSTextAll(MANUAL_IRRIGATION_STATUS_TRUE);
          if ((Chronos::DateTime::now() - occurrenceList[i].finish) <= 1)
          {
            stopManualIrrigation();
          }
        }

        for (byte n = 0; n < CHANNELS_COUNT; n++)
        {
          _channels[n] = occurrenceList[i].channels[n];
        }
      }
    }

    digitalWrite(LOAD_RELAY_1, _channels[0] ? HIGH : LOW);
    digitalWrite(LOAD_RELAY_2, _channels[1] ? HIGH : LOW);
    digitalWrite(LOAD_MOSFET_1, _channels[2] ? HIGH : LOW);
    digitalWrite(LOAD_MOSFET_2, _channels[3] ? HIGH : LOW);

    processRemoteChannels(_channels);

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
        }
      }

      sendDocumentToWs(ongoing);

      if (numNext)
      {
        DynamicJsonDocument next(4000);
        next["command"] = "nextEvents";
        JsonArray nextData = next.createNestedArray("data");
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

        sendDocumentToWs(next);
      }
    }
  }
}

void processRemoteChannels(bool *_channels)
{
  // Process remote channels
  for (byte n = 4; n < CHANNELS_COUNT; n++)
  {
    DynamicJsonDocument remoteChannel(128);
    remoteChannel["ch"] = n;
    remoteChannel["st"] = _channels[n] ? 1 : 0;
    serializeJson(remoteChannel, HC12);
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

void getWeatherFromOpenWeatherMap(void *parameter)
{
  for (;;)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      HTTPClient http;
      http.begin(OWM_ENDPOINT);
      int httpCode = http.GET();

      if (httpCode > 0)
      {
        String payload = http.getString();
        Serial.println(httpCode);
        Serial.println(payload);

        DynamicJsonDocument OWMDocument(512);
        deserializeJson(OWMDocument, payload);
        if (!OWMDocument.isNull())
        {
          weatherData.OWMMainId = OWMDocument["weather"]["id"].as<int>();
          weatherData.OWMRain3h = OWMDocument["rain"]["3h"].as<float>();
        }
        delay(30000);
      }
      else
      {
        delay(5000);
      }
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
    Chronos::DateTime::now().printTo(Serial);
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

bool dateIsValid()
{
  return year() >= 2019;
}

void syncRTC()
{
  //If current year < 2019 then update RTC every 2 sec else update RTC every RTC_SYNC_INTERVAL
  if (millis() - RTCLastSync >= (!dateIsValid() ? 2000 : RTC_SYNC_INTERVAL))
  {
    sendATCommand("AT+CCLK?");
    RTCLastSync = millis();
  }
}

void checkBalance()
{
  if (millis() - balanceLastCheck >= CHECK_BALANCE_INTERVAL)
  {
    sendATCommand("AT+CUSD=1,\"*111#\"");
    balanceLastCheck = millis();
  }
}

void checkGSMStatus()
{
  if (millis() - GSMStatusLastCheck >= CHECK_STATUS_INTERVAL)
  {
    sendATCommand("AT+CREG?");
    sendATCommand("AT+CSQ");
    sendATCommand("AT+CNUM");

    GSMStatusLastCheck = millis();
  }
}

void listenSIM800()
{
  if (Serial.available())
  {
    SIM800.write(Serial.read());
  }

  if (SIM800.available())
  {
    String response;
    response = SIM800.readStringUntil('\n');
    Serial.println(response);

    // USSD Handler
    if (response.indexOf("+CUSD:") > -1)
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

    // GSM RTC Handler
    if (response.indexOf("+CCLK:") > -1)
    {
      String cclk;
      cclk.reserve(32);
      cclk = response.substring(response.indexOf("+CCLK:") + 8);
      uint8_t year = cclk.substring(0, 2).toInt();
      uint8_t month = cclk.substring(3, 5).toInt();
      uint8_t day = cclk.substring(6, 8).toInt();
      uint8_t hour = cclk.substring(9, 11).toInt();
      uint8_t minute = cclk.substring(12, 14).toInt();
      uint8_t second = cclk.substring(15, 17).toInt();
      setTime(hour, minute, second, day, month, year);
      Serial.println("RTC synchronized. New time is: ");
      Chronos::DateTime::now().printTo(Serial);
    }

    if (response.indexOf("+CREG:") > -1)
    {
      CREGCode = response.substring(response.indexOf("+CREG:") + 9).toInt();
    }

    if (response.indexOf("+CSQ:") > -1)
    {
      String csq;
      csq.reserve(12);
      csq = response.substring(response.indexOf("+CSQ:") + 6);
      GSMSignal = csq.substring(0, 2).toInt() * 2;
    }

    if (response.indexOf("+CNUM:") > -1)
    {
      String cnum;
      cnum.reserve(36);
      cnum = response.substring(response.indexOf("+CNUM:") + 11);
      GSMPhoneNumber.reserve(12);
      GSMPhoneNumber = cnum.substring(0, 12);
    }
  }
}

void sendATCommand(String cmd)
{
  Serial.println(cmd);
  SIM800.println(cmd);
}