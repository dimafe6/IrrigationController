/* Serial configuration */
#define BAUD_RATE 9600
#define HC_12_RX 26
#define HC_12_TX 27
#define HC_12_UPDATE_INTERVAL 2000

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
#define CALENDAR_OCCURRENCES_LIST_SIZE 24
#define CALENDAR_MAX_NUM_EVENTS 24
#define CALENDAR_CHECK_INTERVAL 1000
#define CALENDAR_ZONE_1 1
#define CALENDAR_ZONE_2 2
#define CALENDAR_ZONE_3 3
#define CALENDAR_ZONE_4 4

/* Web server configuration */
#define HTTP_PORT 80
