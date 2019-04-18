/* Serial configuration */
#define BAUD_RATE 9600
#define HC_12_RX 26
#define HC_12_TX 27
#define HC_12_UPDATE_INTERVAL 2000
#define PRINTLN(...)  Serial.println(__VA_ARGS__)

/* LCD configuration */
#define TFT_MISO 19 // 21 on LCD
#define TFT_MOSI 23 // 19 on LCD
#define TFT_SCLK 18 // 23 on LCD
#define TFT_CS 15   // 24 on LCD
#define TFT_DC 2    // 15 on LCD
#define TFT_RST 4   // 13 on LCD
#define TOUCH_CS 25 // 26 on LCD

/* Pins for load control */
#define LOAD_RELAY_1 35  // Relay 1
#define LOAD_RELAY_2 33  // Relay 2
#define LOAD_MOSFET_1 32 // Mosfet 1
#define LOAD_MOSFET_2 13 // Mosfet 2

/* Thing Speak configuration */
#define THING_SPEAK_WRITE_INTERVAL 900000

/* RTC pins */
#define RTC_SQW_PIN 34

/* Calendar configuration */
#define CALENDAR_OCCURRENCES_LIST_SIZE 25
#define CALENDAR_MAX_NUM_EVENTS 25
#define CALENDAR_CHECK_INTERVAL 1000
#define ZONES_COUNT 4
#define SCHEDULE_FILE_NAME "/schedule.json"

/* Web server configuration */
#define HTTP_PORT 80

/* SD card configuration */
#define SD_SCK 14
#define SD_MISO 33
#define SD_MOSI 32
#define SD_CS 13
#define SCHEDULE_FILE_SIZE 8000

/* WS messages */
#define MANUAL_IRRIGATION_STATUS_TRUE "{\"command\":\"manualIrrigation\",\"status\":true}"
#define MANUAL_IRRIGATION_STATUS_FALSE "{\"command\":\"manualIrrigation\",\"status\":false}"
#define MANUAL_IRRIGATION_STOP "{\"command\":\"stopManualIrrigation\"}"
#define SCHEDULE_ADD_EDIT "{\"command\":\"addOrEditSchedule\"}"