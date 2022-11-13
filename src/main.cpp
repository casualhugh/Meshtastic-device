#include "GPS.h"
#include "MeshRadio.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "airtime.h"

#include "configuration.h"
#include "error.h"
#include "power.h"
// #include "rom/rtc.h"
//#include "DSRRouter.h"
#include "ReliableRouter.h"
#include "debug.h"
#include "FSCommon.h"
#include "RTC.h"
#include "SPILock.h"
#include "OSThread.h"
#include "Periodic.h"
#include "debug/i2cScan.h"
#include "graphics/Screen.h"
#include "main.h"
#include "modules/Modules.h"
#include "shutdown.h"
#include "sleep.h"
#include "target_specific.h"
#include <Wire.h>
#include "heading.h"
// #include <driver/rtc_io.h>
#ifdef WANT_WIFI
#include "mesh/http/WiFiAPClient.h"
#endif
#ifndef NO_ESP32
#ifdef WANT_WIFI
#include "mesh/http/WebServer.h"
#endif

#include "esp32/ESP32Bluetooth.h"


#endif

#ifdef WANT_WIFI
#if defined(HAS_WIFI) || defined(PORTDUINO)
#include "mesh/wifi/WiFiServerAPI.h"

#endif
#endif
#define WIFI_UPDATES
#ifdef WIFI_UPDATES
#include "ESP32OTA.h"
#endif

#include "RF95Interface.h"

#include "ButtonThread.h"
#include "PowerFSMThread.h"

using namespace concurrency;

// We always create a screen object, but we only init it if we find the hardware
graphics::Screen *screen;

// Global power status
meshtastic::PowerStatus *powerStatus = new meshtastic::PowerStatus();

// Global GPS status
meshtastic::GPSStatus *gpsStatus = new meshtastic::GPSStatus();

// Global Node status
meshtastic::NodeStatus *nodeStatus = new meshtastic::NodeStatus();

/// The I2C address of our display (if found)
uint8_t screen_found;
uint8_t screen_model;

// The I2C address of the RTC Module (if found)
uint8_t rtc_found;


uint32_t serialSinceMsec;


// Array map of sensor types (as array index) and i2c address as value we'll find in the i2c scan
uint8_t nodeTelemetrySensorsMap[12] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };

Router *router = NULL; // Users of router don't care what sort of subclass implements that API

const char *getDeviceName()
{
    uint8_t dmac[6];

    getMacAddr(dmac);

    // Meshtastic_ab3c or Shortname_abcd
    static char name[20];
    sprintf(name, "%02x%02x", dmac[4], dmac[5]);
    // if the shortname exists and is NOT the new default of ab3c, use it for BLE name.
    if ((owner.short_name != NULL) && (owner.short_name != name)) {
        sprintf(name, "%s_%02x%02x", owner.short_name, dmac[4], dmac[5]);
    } else {
        sprintf(name, "Meshtastic_%02x%02x", dmac[4], dmac[5]);
    }
    return name;
}

uint32_t timeLastPowered = 0;

bool ButtonThread::shutdown_on_long_stop = false;

static OSThread *powerFSMthread;
static ButtonThread *buttonThread;
uint32_t ButtonThread::longPressTime = 0;

RadioInterface *rIf = NULL;

/**
 * Some platforms (nrf52) might provide an alterate version that supresses calling delay from sleep.
 */
__attribute__((weak, noinline)) bool loopCanSleep()
{
    return true;
}

#ifdef WIFI_UPDATES
    int forceSoftAP = 0;

#endif

void setup()
{
    #ifdef BUTTON_PIN
    // If the button is connected to GPIO 12, don't enable the ability to use
    // meshtasticAdmin on the device.
    pinMode(BUTTON_PIN, INPUT);
    #endif
    #ifdef BUTTON_PIN_2
        // If the button is connected to GPIO 12, don't enable the ability to use
        // meshtasticAdmin on the device.
        pinMode(BUTTON_PIN_2, INPUT);
    #endif
     #ifdef WIFI_UPDATES
    if (digitalRead(BUTTON_PIN) && digitalRead(BUTTON_PIN_2)) {
        if (wifi_setup()){
            forceSoftAP = 3;
            DEBUG_MSG("Setting forceSoftAP = 1\n");
            return;
        }
       
    }
    #endif
    concurrency::hasBeenSetup = true;
    #ifdef DEBUG_PORT
        if (!config.device.serial_disabled) {
            consoleInit(); // Set serial baud rate and init our mesh console
        }
    #endif

    serialSinceMsec = millis();
    DEBUG_MSG("\n\n//\\ E S H T /\\ S T / C\n\n");
    initDeepSleep();

    #ifdef VEXT_ENABLE
        pinMode(VEXT_ENABLE, OUTPUT);
        digitalWrite(VEXT_ENABLE, 0); // turn on the display power
    #endif
    


    // BUTTON_PIN is pulled low by a 12k resistor .
    #ifdef WIFI_UPDATES
    if (digitalRead(BUTTON_PIN)) {
        forceSoftAP = 1;
        DEBUG_MSG("Setting forceSoftAP = 1\n");
    } else if (digitalRead(BUTTON_PIN_2)){
        forceSoftAP = 2;
        DEBUG_MSG("Setting forceSoftAP = 2\n");
    }
    #endif
    OSThread::setup();

    fsInit();

    // router = new DSRRouter();
    router = new ReliableRouter();

    Wire.begin(I2C_SDA, I2C_SCL);

    scanI2Cdevice();
    heading = new Heading(&Wire);
    heading->init();
    // Buttons & LED
    buttonThread = new ButtonThread();

    // Hello
    DEBUG_MSG("Meshtastic hwvendor=%d, swver=%s\n", HW_VENDOR, optstr(APP_VERSION));

#ifndef NO_ESP32
    // Don't init display if we don't have one or we are waking headless due to a timer event
    if (wakeCause == ESP_SLEEP_WAKEUP_TIMER)
        screen_found = 0; // forget we even have the hardware

    esp32Setup();
#endif

    // We do this as early as possible because this loads preferences from flash
    // but we need to do this after main cpu iniot (esp32setup), because we need the random seed set
    nodeDB.init();

    // Currently only the tbeam has a PMU
    power = new Power();
    power->setStatusHandler(powerStatus);
    powerStatus->observe(&power->newStatus);
    power->setup(); // Must be after status handler is installed, so that handler gets notified of the initial configuration
    // Init our SPI controller (must be before screen and lora)
    initSPI();
    // ESP32
    SPI.begin(RF95_SCK, RF95_MISO, RF95_MOSI, RF95_NSS);
    SPI.setFrequency(4000000);

    // Initialize the screen first so we can show the logo while we start up everything else.
    screen = new graphics::Screen();

    readFromRTC(); // read the main CPU RTC at first (in case we can't get GPS time)
    
    Serial.println("init gps");
    gps = createGps();
    if (gps)
        gpsStatus->observe(&gps->newStatus);
    else
        DEBUG_MSG("Warning: No GPS found - running without GPS\n");
    nodeStatus->observe(&nodeDB.newStatus);

    service.init();

    // Now that the mesh service is created, create any modules
    setupModules();

        // Don't call screen setup until after nodedb is setup (because we need
        // the current region name)
    screen->setup();


    screen->print("Started...\n");

    // We have now loaded our saved preferences from flash

    // ONCE we will factory reset the GPS for bug #327
    if (gps && !devicestate.did_gps_reset) {
        DEBUG_MSG("GPS FactoryReset requested\n");
        if (gps->factoryReset()) { // If we don't succeed try again next time
            devicestate.did_gps_reset = true;
            nodeDB.saveToDisk();
        }
    }

    // radio init MUST BE AFTER service.init, so we have our radio config settings (from nodedb init)

#if defined(RF95_IRQ)
    if (!rIf) {
        rIf = new RF95Interface(RF95_NSS, RF95_IRQ, RF95_RESET, SPI);
        if (!rIf->init()) {
            DEBUG_MSG("Warning: Failed to find RF95 radio\n");
            delete rIf;
            rIf = NULL;
        } else {
            DEBUG_MSG("RF95 Radio init succeeded, using RF95 radio\n");
        }
    }
#endif


    // Initialize Wifi
    #ifdef WANT_WIFI
    initWifi(forceSoftAP);
    #ifndef NO_ESP32
        // Start web server thread.
        webServerThread = new WebServerThread();
    #endif
    #endif
    
    
   

    // Start airtime logger thread.
    airTime = new AirTime();

    if (!rIf)
        RECORD_CRITICALERROR(CriticalErrorCode_NoRadio);
    else {
        router->addInterface(rIf);

        // Calculate and save the bit rate to myNodeInfo
        // TODO: This needs to be added what ever method changes the channel from the phone.
        myNodeInfo.bitrate = (float(Constants_DATA_PAYLOAD_LEN) / (float(rIf->getPacketTime(Constants_DATA_PAYLOAD_LEN)))) * 1000;
        DEBUG_MSG("myNodeInfo.bitrate = %f bytes / sec\n", myNodeInfo.bitrate);
    }

    // This must be _after_ service.init because we need our preferences loaded from flash to have proper timeout values
    PowerFSM_setup(); // we will transition to ON in a couple of seconds, FIXME, only do this for cold boots, not waking from SDS
    powerFSMthread = new PowerFSMThread();
    #ifdef WIFI_UPDATES
    if (hwReason != RTCWDT_BROWN_OUT_RESET && forceSoftAP == 1){
        Serial.println("Doing wifisetup");
        forceSoftAP = wifi_setup();
        Serial.println("Done wifisetup");
    } else if (hwReason != RTCWDT_BROWN_OUT_RESET && forceSoftAP == 2){
        forceSoftAP = ota_setup();
    }
    #endif
    // setBluetoothEnable(false); we now don't start bluetooth until we enter the proper state
    setCPUFast(false); // 80MHz is fine for our slow peripherals
}

uint32_t rebootAtMsec;   // If not zero we will reboot at this time (used to reboot shortly after the update completes)
uint32_t shutdownAtMsec; // If not zero we will shutdown at this time (used to shutdown from python or mobile client)

// If a thread does something that might need for it to be rescheduled ASAP it can set this flag
// This will supress the current delay and instead try to run ASAP.
bool runASAP;

void loop()
{
    runASAP = false;

    // heap_caps_check_integrity_all(true); // FIXME - disable this expensive check

    esp32Loop();
    powerCommandsCheck();

    // For debugging
    // if (rIf) ((RadioLibInterface *)rIf)->isActivelyReceiving();

    #ifdef DEBUG_STACK
        static uint32_t lastPrint = 0;
        if (millis() - lastPrint > 10 * 1000L) {
            lastPrint = millis();
            meshtastic::printThreadInfo("main");
        }
    #endif

    // TODO: This should go into a thread handled by FreeRTOS.
    // handleWebResponse();
    if (forceSoftAP != 3){
        service.loop();

        long delayMsec = mainController.runOrDelay();

        /* if (mainController.nextThread && delayMsec)
            DEBUG_MSG("Next %s in %ld\n", mainController.nextThread->ThreadName.c_str(),
                    mainController.nextThread->tillRun(millis())); */

        // We want to sleep as long as possible here - because it saves power
        if (!runASAP && loopCanSleep()) {
            // if(delayMsec > 100) DEBUG_MSG("sleeping %ld\n", delayMsec);
            mainDelay.delay(delayMsec);
        }
    }
    // if (didWake) DEBUG_MSG("wake!\n");
    #ifdef WIFI_UPDATES
    
    if ((hwReason != RTCWDT_BROWN_OUT_RESET && forceSoftAP == 1) || forceSoftAP == 3){
        server.handleClient();
    } else if (hwReason != RTCWDT_BROWN_OUT_RESET && forceSoftAP == 2){
        ArduinoOTA.handle();
    }
    #endif
}
