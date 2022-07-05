// For OLED LCD
#define I2C_SDA 21
#define I2C_SCL 22


#define BATTERY_PIN 35 
#define ADC_MULTIPLIER 2.11 // 2.0 + 10% for correction of display undervoltage.


#define BUTTON_PIN 36
#define BUTTON_PIN_2 38  // If defined, this will be used for user button presses,

#define LORA_DIO0 13  // a No connect on the SX1262/SX1268 module
#define LORA_RESET 4 // RST for SX1276, and for SX1262/SX1268

#undef RF95_SCK
#define RF95_SCK 7
#undef RF95_MISO
#define RF95_MISO 19
#undef RF95_MOSI
#define RF95_MOSI 8
#undef RF95_NSS
#define RF95_NSS 15

// supported modules list
#define USE_RF95 // RFM95/SX127x

#define AC_INT 34
#define LCD_RST 14
#define LCD_DC 12
#define LCD_PSU 2
#define LCD_BL 20

