/* Serial configuration */
#define BAUD_RATE 9600
#define HC_12_RX 26
#define HC_12_TX 27
#define HC_12_UPDATE_INTERVAL 2000
#define PRINTLN(...)  Serial.println(__VA_ARGS__)

/* Pins for load control */
#define LOAD_RELAY_1 21  // Relay 1
#define LOAD_RELAY_2 22  // Relay 2
#define LOAD_MOSFET_1 18 // Mosfet 1
#define LOAD_MOSFET_2 19 // Mosfet 2

/* Thing Speak configuration */
#define THING_SPEAK_WRITE_INTERVAL 900000

/* RTC */
#define RTC_SYNC_INTERVAL 60000

/* Calendar configuration */
#define CALENDAR_OCCURRENCES_LIST_SIZE 25
#define CALENDAR_MAX_NUM_EVENTS 25
#define CALENDAR_CHECK_INTERVAL 1000
#define ZONES_COUNT 4
#define MANUAL_IRRIGATION_EVENT_ID CALENDAR_MAX_NUM_EVENTS
#define MANUAL_IRRIGATION_FILE_NAME "/manual.json"
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
#define MANUAL_IRRIGATION_STOP "{\"command\":\"stopManualIrrigation\"}"
#define SCHEDULE_ADD_EDIT "{\"command\":\"addOrEditSchedule\"}"