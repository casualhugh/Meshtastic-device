// #define BUTTON_NEED_PULLUP // if set we need to turn on the internal CPU pullup during sleep

#define I2C_SDA 21
#define I2C_SCL 22

#define BUTTON_PIN 36     // The middle button GPIO on the T-Beam
#define BUTTON_PIN_2 38 // Alternate GPIO for an external button if needed. Does anyone use this? It is not documented anywhere.

// TTGO uses a common pinout for their SX1262 vs RF95 modules - both can be enabled and we will probe at runtime for RF95 and if
// not found then probe for SX1262
#define USE_RF95

#define RF95_SCK 7
#define RF95_MISO 19
#define RF95_MOSI 8
#define RF95_NSS 15
#define LORA_DIO0 13 // a No connect on the SX1262 module
#define LORA_RESET 4
#define LORA_DIO3    // Not connected on PCB, but internally on the TTGO SX1262, if DIO3 is high the TXCO is enabled
#define GPS_RX_PIN 25
#define GPS_TX_PIN 26

#define SCREEN_BL 20
#define DC_PIN  12
#define CS_PIN 27
#define SCK_PIN RF95_SCK
#define MOSI_PIN RF95_MOSI
#define MISO_PIN RF95_MISO
#define RST_PIN 14
#define PULLUP_LCD 2
#define BATTERY_PIN 35

