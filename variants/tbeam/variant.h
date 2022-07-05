// #define BUTTON_NEED_PULLUP // if set we need to turn on the internal CPU pullup during sleep

#define I2C_SDA 21
#define I2C_SCL 22

#define BUTTON_PIN 39     // The middle button GPIO on the T-Beam
#define BUTTON_PIN_2 36 // Alternate GPIO for an external button if needed. Does anyone use this? It is not documented anywhere.
#define EXT_NOTIFY_OUT 13 // Default pin to use for Ext Notify Module.

#define LED_INVERTED 1
#define LED_PIN 4 // Newer tbeams (1.1) have an extra led on GPIO4

// TTGO uses a common pinout for their SX1262 vs RF95 modules - both can be enabled and we will probe at runtime for RF95 and if
// not found then probe for SX1262
#define USE_RF95


#define LORA_DIO0 13 // a No connect on the SX1262 module
#define LORA_RESET 4
#define LORA_DIO3    // Not connected on PCB, but internally on the TTGO SX1262, if DIO3 is high the TXCO is enabled
#define GPS_RX_PIN 16
#define GPS_TX_PIN 17