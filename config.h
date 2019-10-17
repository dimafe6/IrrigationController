/* Pin definitions */
#define HC_12_RX 26
#define HC_12_TX 27
#define FLOW_SENSOR_PIN 34
#define LOAD_RELAY_1 16  // Relay 1
#define LOAD_RELAY_2 17  // Relay 2
#define LOAD_MOSFET_1 19 // Mosfet 1
#define LOAD_MOSFET_2 18 // Mosfet 2
#define SD_SCK 14
#define SD_MISO 33
#define SD_MOSI 32
#define SD_CS 13

/* Serial configuration */
#define BAUD_RATE 9600
#define BAUD_RATE_RADIO 9600
#define HC_12_UPDATE_INTERVAL 2000
#define PRINTLN(...)  Serial.println(__VA_ARGS__)

/* System */
#define WS_SYS_INFO_INTERVAL 5000
#define WS_WATER_INFO_INTERVAL 5000
#define SEND_LOGS_VIA_RADIO true

/* Flow sensor */
#define FLOW_SENSOR_CALIBRATION 6.5
#define FLOW_SENSOR_CALCULATION_INTERVAL 1000.0

/* Calendar configuration */
#define CALENDAR_OCCURRENCES_LIST_SIZE 25
#define CALENDAR_MAX_NUM_EVENTS 24
#define CALENDAR_TOTAL_NUM_EVENTS 25
#define CALENDAR_CHECK_INTERVAL 1000
#define MANUAL_IRRIGATION_EVENT_ID 25
#define MANUAL_IRRIGATION_FILE_NAME "/manual.json"
#define SCHEDULE_FILE_NAME "/schedule.json"
#define CHANNELS_FILE_NAME "/channels.json"
#define SETTINGS_FILE_NAME "/settings.json"

/* Web server configuration */
#define HTTP_PORT 80

/* SD card configuration */
#define SCHEDULE_FILE_SIZE 8000

/* Statistic */
#define SAVE_STATISTIC_INTERVAL 5000
#define WATER_STATISTIC_FILE "/water_statistic_%d_%d.json"
#define STATISTIC_FILE_SIZE 5000

/* Telegram */
#define SCAN_MESSAGES_INTERVAL 3000