#include "mywifi.h"
#include "GPS.h"
#include "MeshRadio.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "ReliableRouter.h"
#include "airtime.h"
#include "configuration.h"
#include "error.h"
#include "power.h"
// #include "debug.h"
#include "FSCommon.h"
#include "RTC.h"
#include "SPILock.h"
#include "concurrency/OSThread.h"
#include "concurrency/Periodic.h"
#include "detect/ScanI2C.h"
#include "detect/ScanI2CTwoWire.h"
#include "detect/axpDebug.h"
#include "detect/einkScan.h"
// #include "graphics/RAKled.h"
#include "graphics/Screen.h"
#include "main.h"
#include "mesh/generated/meshtastic/config.pb.h"
#include "modules/Modules.h"
#include "shutdown.h"
#include "sleep.h"
#include "target_specific.h"
#include <Wire.h>
#include <memory>
#include <utility>
// #include <driver/rtc_io.h>

#include "mesh/eth/ethClient.h"
#include "mesh/http/WiFiAPClient.h"

#ifdef ARCH_ESP32
#include "mesh/http/WebServer.h"
#include "nimble/NimbleBluetooth.h"
NimbleBluetooth *nimbleBluetooth;
#endif

#if HAS_WIFI
#include "mesh/api/WiFiServerAPI.h"
#endif

#include "mqtt/MQTT.h"

#include "LLCC68Interface.h"
#include "RF95Interface.h"
#include "SX1262Interface.h"
#include "SX1268Interface.h"
#include "SX1280Interface.h"

#if HAS_BUTTON
#include "ButtonThread.h"
#endif
#include "PowerFSMThread.h"

#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL)
#include "AccelerometerThread.h"
#include "MagnotometerThread.h"
// #include "AmbientLightingThread.h"
#endif

using namespace concurrency;

// We always create a screen object, but we only init it if we find the hardware
graphics::Screen *screen;

// Global power status
meshtastic::PowerStatus *powerStatus = new meshtastic::PowerStatus();

// Global GPS status
meshtastic::GPSStatus *gpsStatus = new meshtastic::GPSStatus();

// Global Node status
meshtastic::NodeStatus *nodeStatus = new meshtastic::NodeStatus();

// Scan for I2C Devices

/// The I2C address of our display (if found)
ScanI2C::DeviceAddress screen_found = ScanI2C::ADDRESS_NONE; // Todo(hugh): screen is spi not i2c

// The I2C address of the cardkb or RAK14004 (if found)
ScanI2C::DeviceAddress cardkb_found = ScanI2C::ADDRESS_NONE; // Todo(hugh): Dont have this
// 0x02 for RAK14004, 0x00 for cardkb, 0x10 for T-Deck
uint8_t kb_model;

// The I2C address of the RTC Module (if found)
ScanI2C::DeviceAddress rtc_found = ScanI2C::ADDRESS_NONE;
// The I2C address of the Accelerometer (if found)
ScanI2C::DeviceAddress accelerometer_found = ScanI2C::ADDRESS_NONE;
// The I2C address of the Accelerometer (if found)
ScanI2C::DeviceAddress magnotometer_found = ScanI2C::ADDRESS_NONE;
// The I2C address of the RGB LED (if found)
ScanI2C::FoundDevice rgb_found = ScanI2C::FoundDevice(ScanI2C::DeviceType::NONE, ScanI2C::ADDRESS_NONE);

#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL)
ATECCX08A atecc;
#endif

bool isVibrating = false;

bool eink_found = true;

uint32_t serialSinceMsec;

bool pmu_found;

// Array map of sensor types with i2c address and wire as we'll find in the i2c scan
std::pair<uint8_t, TwoWire *> nodeTelemetrySensorsMap[_meshtastic_TelemetrySensorType_MAX + 1] = {};

Router *router = NULL; // Users of router don't care what sort of subclass implements that API

const char *getDeviceName()
{
    uint8_t dmac[6];

    getMacAddr(dmac);

    // Meshtastic_ab3c or Shortname_abcd
    static char name[20];
    snprintf(name, sizeof(name), "%02x%02x", dmac[4], dmac[5]);
    // if the shortname exists and is NOT the new default of ab3c, use it for BLE name.
    if ((owner.short_name != NULL) && (strcmp(owner.short_name, name) != 0))
    {
        snprintf(name, sizeof(name), "%s_%02x%02x", owner.short_name, dmac[4], dmac[5]);
    }
    else
    {
        snprintf(name, sizeof(name), "Meshtastic_%02x%02x", dmac[4], dmac[5]);
    }
    return name;
}

static int32_t ledBlinker()
{
    static bool ledOn;
    ledOn ^= 1;

    setLed(ledOn);

    // have a very sparse duty cycle of LED being on, unless charging, then blink 0.5Hz square wave rate to indicate that
    return powerStatus->getIsCharging() ? 1000 : (ledOn ? 1 : 1000);
}

uint32_t timeLastPowered = 0;

#if HAS_BUTTON
bool ButtonThread::shutdown_on_long_stop = false;
#endif

static Periodic *ledPeriodic;
static OSThread *powerFSMthread;
#if HAS_BUTTON
static OSThread *buttonThread;
uint32_t ButtonThread::longPressTime = 0;
#endif
static OSThread *accelerometerThread;
static OSThread *magnotometerThread;
// static OSThread *ambientLightingThread;
SPISettings spiSettings(4000000, MSBFIRST, SPI_MODE0);

RadioInterface *rIf = NULL;

/**
 * Some platforms (nrf52) might provide an alterate version that suppresses calling delay from sleep.
 */
__attribute__((weak, noinline)) bool loopCanSleep()
{
    return true;
}

void setup()
{

    concurrency::hasBeenSetup = true;
    meshtastic_Config_DisplayConfig_OledType screen_model =
        meshtastic_Config_DisplayConfig_OledType::meshtastic_Config_DisplayConfig_OledType_OLED_AUTO;
    OLEDDISPLAY_GEOMETRY screen_geometry = GEOMETRY_128_64;

#ifdef DEBUG_PORT
    consoleInit(); // Set serial baud rate and init our mesh console
#endif

    serialSinceMsec = millis();

    LOG_INFO("\n\n//\\ E S H T /\\ S T / C\n\n");

    initDeepSleep();

#ifdef RESET_OLED
    pinMode(RESET_OLED, OUTPUT);
    digitalWrite(RESET_OLED, 1);
#endif

#ifdef BUTTON_PIN
#ifdef ARCH_ESP32

    // If the button is connected to GPIO 12, don't enable the ability to use
    // meshtasticAdmin on the device.
    pinMode(config.device.button_gpio ? config.device.button_gpio : BUTTON_PIN, INPUT);

#endif
#endif
#ifdef BUTTON_PIN_2
#ifdef ARCH_ESP32

    // If the button is connected to GPIO 12, don't enable the ability to use
    // meshtasticAdmin on the device.
    pinMode(BUTTON_PIN_2, INPUT);

#endif
#endif

    OSThread::setup();

    ledPeriodic = new Periodic("Blink", ledBlinker);

    fsInit();

#ifdef I2C_SDA
    Wire.begin(I2C_SDA, I2C_SCL);
#endif

#ifdef PIN_LCD_RESET
    pinMode(PIN_LCD_RESET, OUTPUT);
    digitalWrite(PIN_LCD_RESET, 0);
    delay(1);
    digitalWrite(PIN_LCD_RESET, 1);
    delay(1);
#endif

    // Currently only the tbeam has a PMU
    // PMU initialization needs to be placed before i2c scanning
    power = new Power();
    power->setStatusHandler(powerStatus);
    powerStatus->observe(&power->newStatus);
    power->setup(); // Must be after status handler is installed, so that handler gets notified of the initial configuration

    // We need to scan here to decide if we have a screen for nodeDB.init() and because power has been applied to
    // accessories
    auto i2cScanner = std::unique_ptr<ScanI2CTwoWire>(new ScanI2CTwoWire());

    LOG_INFO("Scanning for i2c devices...\n");

#ifdef I2C_SDA
    Wire.begin(I2C_SDA, I2C_SCL);
    i2cScanner->scanPort(ScanI2C::I2CPort::WIRE);
#endif

    auto i2cCount = i2cScanner->countDevices();
    if (i2cCount == 0)
    {
        LOG_INFO("No I2C devices found\n");
    }
    else
    {
        LOG_INFO("%i I2C devices found\n", i2cCount);
    }

#ifdef ARCH_ESP32
    // Don't init display if we don't have one or we are waking headless due to a timer event
    if (wakeCause == ESP_SLEEP_WAKEUP_TIMER)
    {
        LOG_DEBUG("suppress screen wake because this is a headless timer wakeup");
        i2cScanner->setSuppressScreen();
    }
#endif

    auto screenInfo = i2cScanner->firstScreen();
    screen_found = screenInfo.type != ScanI2C::DeviceType::NONE ? screenInfo.address : ScanI2C::ADDRESS_NONE;

    if (screen_found.port != ScanI2C::I2CPort::NO_I2C)
    {
        switch (screenInfo.type)
        {
        case ScanI2C::DeviceType::SCREEN_SH1106:
            screen_model = meshtastic_Config_DisplayConfig_OledType::meshtastic_Config_DisplayConfig_OledType_OLED_SH1106;
            break;
        case ScanI2C::DeviceType::SCREEN_SSD1306:
            screen_model = meshtastic_Config_DisplayConfig_OledType::meshtastic_Config_DisplayConfig_OledType_OLED_SSD1306;
            break;
        case ScanI2C::DeviceType::SCREEN_ST7567:
        case ScanI2C::DeviceType::SCREEN_UNKNOWN:
        default:
            screen_model = meshtastic_Config_DisplayConfig_OledType::meshtastic_Config_DisplayConfig_OledType_OLED_AUTO;
        }
    }

#define UPDATE_FROM_SCANNER(FIND_FN)
    pmu_found = i2cScanner->exists(ScanI2C::DeviceType::PMU_AXP192_AXP2101); 

    /*
     * There are a bunch of sensors that have no further logic than to be found and stuffed into the
     * nodeTelemetrySensorsMap singleton. This wraps that logic in a temporary scope to declare the temporary field
     * "found".
     */

#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL)
    auto acc_info = i2cScanner->firstAccelerometer();
    accelerometer_found = acc_info.type != ScanI2C::DeviceType::NONE ? acc_info.address : accelerometer_found;
    LOG_DEBUG("acc_info = %i\n", acc_info.type);
#endif

    auto mag_info = i2cScanner->firstMagnotometer();
    magnotometer_found = mag_info.type != ScanI2C::DeviceType::NONE ? mag_info.address : magnotometer_found;
    LOG_DEBUG("mag_info = %i\n", mag_info.type);

#define STRING(S) #S

#define SCANNER_TO_SENSORS_MAP(SCANNER_T, PB_T)                                            \
    {                                                                                      \
        auto found = i2cScanner->find(SCANNER_T);                                          \
        if (found.type != ScanI2C::DeviceType::NONE)                                       \
        {                                                                                  \
            nodeTelemetrySensorsMap[PB_T].first = found.address.address;                   \
            nodeTelemetrySensorsMap[PB_T].second = i2cScanner->fetchI2CBus(found.address); \
            LOG_DEBUG("found i2c sensor %s\n", STRING(PB_T));                              \
        }                                                                                  \
    }
    SCANNER_TO_SENSORS_MAP(ScanI2C::DeviceType::INA219, meshtastic_TelemetrySensorType_INA219)

    SCANNER_TO_SENSORS_MAP(ScanI2C::DeviceType::LSM303_ACC, meshtastic_TelemetrySensorType_QMI8658)
    SCANNER_TO_SENSORS_MAP(ScanI2C::DeviceType::LSM303_MAG, meshtastic_TelemetrySensorType_QMC5883L)
    // SCANNER_TO_SENSORS_MAP(ScanI2C::DeviceType::MCP9808, meshtastic_TelemetrySensorType_MCP9808)
    // SCANNER_TO_SENSORS_MAP(ScanI2C::DeviceType::MCP9808, meshtastic_TelemetrySensorType_MCP9808)
    // SCANNER_TO_SENSORS_MAP(ScanI2C::DeviceType::SHT31, meshtastic_TelemetrySensorType_SHT31)
    // SCANNER_TO_SENSORS_MAP(ScanI2C::DeviceType::SHTC3, meshtastic_TelemetrySensorType_SHTC3)
    // SCANNER_TO_SENSORS_MAP(ScanI2C::DeviceType::LPS22HB, meshtastic_TelemetrySensorType_LPS22)
    // SCANNER_TO_SENSORS_MAP(ScanI2C::DeviceType::QMC6310, meshtastic_TelemetrySensorType_QMC6310)
    // SCANNER_TO_SENSORS_MAP(ScanI2C::DeviceType::QMI8658, meshtastic_TelemetrySensorType_QMI8658)
    // SCANNER_TO_SENSORS_MAP(ScanI2C::DeviceType::QMC5883L, meshtastic_TelemetrySensorType_QMC5883L)
    // SCANNER_TO_SENSORS_MAP(ScanI2C::DeviceType::PMSA0031, meshtastic_TelemetrySensorType_PMSA003I)

    i2cScanner.reset();

    // Hello
    LOG_INFO("Meshtastic hwvendor=%d, swver=%s\n", HW_VENDOR, optstr(APP_VERSION));

#ifdef ARCH_ESP32
    esp32Setup();
#endif
    // We do this as early as possible because this loads preferences from flash
    // but we need to do this after main cpu init (esp32setup), because we need the random seed set
    nodeDB.init();

    // If we're taking on the repeater role, use flood router
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_REPEATER)
        router = new FloodingRouter();
    else
        router = new ReliableRouter();

#if HAS_BUTTON
    // Buttons. Moved here cause we need NodeDB to be initialized
    buttonThread = new ButtonThread();
#endif
    // fixed screen override?
    if (config.display.oled != meshtastic_Config_DisplayConfig_OledType_OLED_AUTO)
        screen_model = config.display.oled;

#if !defined(ARCH_PORTDUINO) && !defined(ARCH_STM32WL)
    if (acc_info.type != ScanI2C::DeviceType::NONE)
    {
        config.display.wake_on_tap_or_motion = true;
        moduleConfig.external_notification.enabled = true;
        accelerometerThread = new AccelerometerThread(acc_info.type);
    }
    magnotometerThread = new MagnotometerThread();
#endif

    // Init our SPI controller (must be before screen and lora)
    initSPI();
    // ESP32
    SPI.begin(RF95_SCK, RF95_MISO, RF95_MOSI, RF95_NSS);
    LOG_WARN("SPI.begin(SCK=%d, MISO=%d, MOSI=%d, NSS=%d)\n", RF95_SCK, RF95_MISO, RF95_MOSI, RF95_NSS);
    SPI.setFrequency(4000000);

    // Initialize the screen first so we can show the logo while we start up everything else.
    screen = new graphics::Screen(screen_found, screen_model, screen_geometry);

    readFromRTC(); // read the main CPU RTC at first (in case we can't get GPS time)

    gps = GPS::createGps();
    if (gps)
    {
        gpsStatus->observe(&gps->newStatus);
    }
    else
    {
        LOG_DEBUG("Running without GPS.\n");
    }
    nodeStatus->observe(&nodeDB.newStatus);

    service.init();

    // Now that the mesh service is created, create any modules
    setupModules();

// Do this after service.init (because that clears error_code)
#ifdef HAS_PMU
    if (!pmu_found)
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_NO_AXP192); // Record a hardware fault for missing hardware
#endif

// Don't call screen setup until after nodedb is setup (because we need
// the current region name)
#if defined(ST7735_CS) || defined(USE_EINK) || defined(ILI9341_DRIVER) || defined(ST7789_CS)
    screen->setup(); // TODO(hugh): Rewrite the screen setup for my driver
#endif

    screen->print("Started...\n");

#ifdef HW_SPI1_DEVICE // TODO(hugh): Figure out if this is needed
    LockingArduinoHal *RadioLibHAL = new LockingArduinoHal(SPI1, spiSettings);
#else // HW_SPI1_DEVICE
    LockingArduinoHal *RadioLibHAL = new LockingArduinoHal(SPI, spiSettings);
#endif

#if defined(USE_SX1262)
    if (!rIf)
    {
        rIf = new SX1262Interface(RadioLibHAL, SX126X_CS, SX126X_DIO1, SX126X_RESET, SX126X_BUSY);
        if (!rIf->init())
        {
            LOG_WARN("Failed to find SX1262 radio\n");
            delete rIf;
            rIf = NULL;
        }
        else
        {
            LOG_INFO("SX1262 Radio init succeeded, using SX1262 radio\n");
        }
    }
#endif
    // check if the radio chip matches the selected region

    if ((config.lora.region == meshtastic_Config_LoRaConfig_RegionCode_LORA_24) && (!rIf->wideLora()))
    {
        LOG_WARN("Radio chip does not support 2.4GHz LoRa. Reverting to unset.\n");
        config.lora.region = meshtastic_Config_LoRaConfig_RegionCode_UNSET;
        nodeDB.saveToDisk(SEGMENT_CONFIG);
        if (!rIf->reconfigure())
        {
            LOG_WARN("Reconfigure failed, rebooting\n");
            screen->startRebootScreen();
            rebootAtMsec = millis() + 5000;
        }
    }

    mqttInit(); // Todo(hugh): Is this needed?

#ifndef ARCH_PORTDUINO
    // Initialize Wifi
    // initWifi();

    // Initialize Ethernet
    // initEthernet(); // Todo(hugh): Is this needed?
#endif

#ifdef ARCH_ESP32
    // Start web server thread.
    // webServerThread = new WebServerThread();
#endif

#ifdef ARCH_PORTDUINO
    initApiServer(TCPPort);
#endif

    // Start airtime logger thread.
    airTime = new AirTime();

    if (!rIf)
        RECORD_CRITICALERROR(meshtastic_CriticalErrorCode_NO_RADIO);
    else
    {
        router->addInterface(rIf);

        // Log bit rate to debug output
        LOG_DEBUG("LoRA bitrate = %f bytes / sec\n", (float(meshtastic_Constants_DATA_PAYLOAD_LEN) /
                                                      (float(rIf->getPacketTime(meshtastic_Constants_DATA_PAYLOAD_LEN)))) *
                                                         1000);
    }

    // This must be _after_ service.init because we need our preferences loaded from flash to have proper timeout values
    PowerFSM_setup(); // we will transition to ON in a couple of seconds, FIXME, only do this for cold boots, not waking from SDS
    powerFSMthread = new PowerFSMThread();
    // setupOTA("WhereU1.1", mySSID, myPASSWORD);
    setCPUFast(false); // 80MHz is fine for our slow peripherals
}

uint32_t rebootAtMsec;   // If not zero we will reboot at this time (used to reboot shortly after the update completes)
uint32_t shutdownAtMsec; // If not zero we will shutdown at this time (used to shutdown from python or mobile client)

// If a thread does something that might need for it to be rescheduled ASAP it can set this flag
// This will suppress the current delay and instead try to run ASAP.
bool runASAP;

extern meshtastic_DeviceMetadata getDeviceMetadata()
{
    meshtastic_DeviceMetadata deviceMetadata;
    strncpy(deviceMetadata.firmware_version, optstr(APP_VERSION), sizeof(deviceMetadata.firmware_version));
    deviceMetadata.device_state_version = DEVICESTATE_CUR_VER;
    deviceMetadata.canShutdown = pmu_found || HAS_CPU_SHUTDOWN;
    deviceMetadata.hasBluetooth = HAS_BLUETOOTH;
    deviceMetadata.hasWifi = HAS_WIFI;
    deviceMetadata.hasEthernet = HAS_ETHERNET;
    deviceMetadata.role = config.device.role;
    deviceMetadata.position_flags = config.position.position_flags;
    deviceMetadata.hw_model = HW_VENDOR;
    deviceMetadata.hasRemoteHardware = moduleConfig.remote_hardware.enabled;
    return deviceMetadata;
}

void loop()
{
    runASAP = false;

    // axpDebugOutput.loop();

    // heap_caps_check_integrity_all(true); // FIXME - disable this expensive check

#ifdef ARCH_ESP32
    esp32Loop();
#endif
    powerCommandsCheck();

    // For debugging
    // if (rIf) ((RadioLibInterface *)rIf)->isActivelyReceiving();

#ifdef DEBUG_STACK
    static uint32_t lastPrint = 0;
    if (millis() - lastPrint > 10 * 1000L)
    {
        lastPrint = millis();
        meshtastic::printThreadInfo("main");
    }
#endif

    // TODO: This should go into a thread handled by FreeRTOS.
    // handleWebResponse();

    service.loop();

    long delayMsec = mainController.runOrDelay();

    /* if (mainController.nextThread && delayMsec)
        LOG_DEBUG("Next %s in %ld\n", mainController.nextThread->ThreadName.c_str(),
                  mainController.nextThread->tillRun(millis())); */
    // wifiRunOnce();
    // We want to sleep as long as possible here - because it saves power
    if (!runASAP && loopCanSleep())
    {
        // if(delayMsec > 100) LOG_DEBUG("sleeping %ld\n", delayMsec);
        mainDelay.delay(delayMsec);
    }
    // if (didWake) LOG_DEBUG("wake!\n");
}