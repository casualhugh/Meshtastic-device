#define HAS_GPS 1
#undef GPS_RX_PIN
#undef GPS_TX_PIN
#define GPS_RX_PIN 20
#define GPS_TX_PIN 4
#define GPS_FORCEON 13
// #define PIN_GPS_EN GPS_FORCEON // GPS power enable pin?
#define PIN_GPS_STANDBY GPS_FORCEON // Alternative check logic (note we need to send sleep to gps aswell as set this low)


// #define HAS_SDCARD
// #define SDCARD_USE_SPI1

// #define USE_SSD1306
#define ACC_INTERUPT 39
#define POWER_INTERUPT 34
#define TOUCH_INTERUPT 35

// #define LED_PIN 46
// #define LED_STATE_ON 0 // State when LED is litted
#define USERPREFS_SPLASH_TITLE "\\//HERE U"
#define BUTTON_PIN 36
#define BUTTON_PIN_ALT 38
// #define USE_RF95   // RFM95/SX127x
#define CANNED_MESSAGE_MODULE_ENABLE 1
// WaveShare Core1262-868M OK
// https://www.waveshare.com/wiki/Core1262-868M
#define USE_SX1262
#define BATTERY_PIN -1
#define ADC_CHANNEL ADC1_GPIO34_CHANNEL
#ifdef USE_SX1262
#define LORA_RESET 21
#define LORA_DIO1 22
#define LORA_SCK 7
#define LORA_MISO 19
#define LORA_MOSI 8
#define LORA_CS 18



#define SX126X_RESET LORA_RESET
#define SX126X_CS 5
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY -1
#endif


#define HAS_SCREEN 1
#define LCD_MISO LORA_MISO
#define LCD_SCK LORA_SCK
#define LCD_MOSI LORA_MOSI
#define LCD_CS 15
#define LCD_RST 2
#define LCD_DC 12
#define LCD_PSU 14
#define LCD_BL 27

// HAS_PMU (convert to use INA)
// ST7789 TFT LCD
#define ST7789_CS LCD_CS
#define ST7789_RS LCD_DC     // DC
#define ST7789_SDA LCD_MOSI // MOSI
#define ST7789_SCK LCD_SCK
#define ST7789_RESET LCD_RST
#define ST7789_MISO LCD_MISO
#define ST7789_BUSY -1
#define ST7789_BL LCD_BL
#define TFT_BL LCD_BL
#define ST7789_SPI_HOST SPI3_HOST
#define ST7789_BACKLIGHT_EN LCD_PSU // This turns on
#define SPI_FREQUENCY 40000000
#define SPI_READ_FREQUENCY 16000000
#define TFT_HEIGHT 240
#define TFT_WIDTH 240
#define TFT_OFFSET_X 0
#define TFT_OFFSET_Y 0
#define TFT_OFFSET_ROTATION 1
#define SCREEN_ROTATE
#define SCREEN_TRANSITION_FRAMERATE 15 // fps
#define TFT_MESH COLOR565(0xFF, 0xFF, 0xFF)
#define HAS_TOUCHSCREEN 0
// #define SCREEN_TOUCH_INT 16
// #define SCREEN_TOUCH_USE_I2C1
// #define TOUCH_I2C_PORT 1
// #define TOUCH_SLAVE_ADDRESS 0x40

#define SLEEP_TIME 180

#define I2C_SDA 25
#define I2C_SCL 26
