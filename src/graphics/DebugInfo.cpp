#include "configuration.h"

#include "OLEDDisplay.h"

#include "GPS.h"
#include "MeshService.h"
#include "NodeDB.h"
#include "Screen.h"
#include "gps/GeoCoord.h"
#include "gps/RTC.h"
#include "graphics/images.h"
#include "main.h"
#include "mesh-pb-constants.h"
#include "mesh/Channels.h"
#include "mesh/generated/deviceonly.pb.h"
#include "modules/TextMessageModule.h"
#include "modules/CannedMessageModule.h"
#include "sleep.h"
#include "target_specific.h"
#include "utils.h"
#include "heading.h"
#include <TinyGPS++.h>
#ifndef NO_ESP32
#include "esp_task_wdt.h"
#include "main.h"
#include "ESP32OTA.h"
//#include "mesh/http/WiFiAPClient.h"
#endif



using namespace meshtastic; /** @todo remove */

extern bool loadProto(const char *filename, size_t protoSize, size_t objSize, const pb_msgdesc_t *fields, void *dest_struct);

namespace graphics
{

// This means the *visible* area (sh1106 can address 132, but shows 128 for example)
#define IDLE_FRAMERATE 1 // in fps
#define COMPASS_DIAM 44

// DEBUG
#define NUM_EXTRA_FRAMES 3 // text message and debug frame
// if defined a pixel will blink to show redraws
// #define SHOW_REDRAWS

// This image definition is here instead of images.h because it's modified dynamically by the drawBattery function
// uint8_t imgBattery[16] = {0xFF, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0xE7, 0x3C};

// Threshold values for the GPS lock accuracy bar display
uint32_t dopThresholds[5] = {2000, 1000, 500, 200, 100};

// Stores the last 4 of our hardware ID, to make finding the device for pairing easier
static char ourId[5];

// GeoCoord object for the screen
GeoCoord geoCoord;

#ifdef SHOW_REDRAWS
static bool heartbeat = false;
#endif

#define SCREEN_WIDTH 240
#define SCREEN_HEIGHT 240

#define FONT_SMALL 1
#define FONT_MEDIUM 2
#define FONT_LARGE 3
#define FONT_HEIGHT_SMALL 10
#define FONT_HEIGHT_MEDIUM 20
#define FONT_HEIGHT_LARGE 30
#define getStringCenteredX(s) ((SCREEN_WIDTH - display->getStringWidth(s)) / 2)

#ifndef SCREEN_TRANSITION_MSECS
#define SCREEN_TRANSITION_MSECS 300
#endif





// Draw nodes status
static void drawNodes(OLEDDisplay *display, int16_t x, int16_t y, NodeStatus *nodeStatus)
{
    char usersString[20];
    sprintf(usersString, "%d/%d", nodeStatus->getNumOnline(), nodeStatus->getNumTotal());
    //display->drawFastImage(x, y, 8, 8, imgUser);
    display->drawString(x, y + display->getHeight() / 2, usersString);
}

// Draw GPS status summary
static void drawGPS(OLEDDisplay *display, int16_t x, int16_t y, const GPSStatus *gps)
{
    if (config.position.fixed_position) {
        // GPS coordinates are currently fixed
        display->drawString(x + SCREEN_WIDTH/2, y + 40, "Fixed GPS");
        return;
    }
    if (!gps->getIsConnected()) {
        display->drawString(x + SCREEN_WIDTH/2, y + 40, "No GPS");
        return;
    }
    //display->drawFastImage(x, y, 6, 8, gps->getHasLock() ? imgPositionSolid : imgPositionEmpty);
    if (!gps->getHasLock()) {
        display->drawString(x + SCREEN_WIDTH/2, y + 50,  "No sats");
        return;
    } else {
        char satsString[3];
        uint8_t bar[2] = {0};

        // Draw DOP signal bars
        for (int i = 0; i < 5; i++) {
            if (gps->getDOP() <= dopThresholds[i])
                bar[0] = ~((1 << (5 - i)) - 1);
            else
                bar[0] = 0b10000000;
            bar[1] = bar[0];
            display->drawFastImage(x + 9 + (i * 2), y, 2, 8, bar);
        }

        // Draw satellite image
        //display->drawFastImage(x + 24, y, 8, 8, imgSatellite);

        // Draw the number of satellites
        sprintf(satsString, "%u", gps->getNumSatellites());
        display->drawString(x + SCREEN_WIDTH/2, y + 50, satsString);
    }
}

static void drawGPSAltitude(OLEDDisplay *display, int16_t x, int16_t y, const GPSStatus *gps)
{
    String displayLine = "";
    if (!gps->getIsConnected() && !config.position.fixed_position) {
        displayLine = "No GPS Module";
        //display->drawString(x + SCREEN_WIDTH/2, y + FONT_HEIGHT_SMALL, displayLine);
    } else if (!gps->getHasLock() && !config.position.fixed_position) {
        displayLine = "No GPS Lock";
        //display->drawString(x + SCREEN_WIDTH/2, y + FONT_HEIGHT_SMALL, displayLine);
    } else {
        geoCoord.updateCoords(int32_t(gps->getLatitude()), int32_t(gps->getLongitude()), int32_t(gps->getAltitude()));
        displayLine = "Altitude: " + String(geoCoord.getAltitude()) + "m";
        display->drawString(x + SCREEN_WIDTH/2, y + FONT_HEIGHT_SMALL * 4, displayLine);
    }
}

// Draw GPS status coordinates
static void drawGPScoordinates(OLEDDisplay *display, int16_t x, int16_t y, const GPSStatus *gps)
{
    auto gpsFormat = config.display.gps_format;
    String displayLine = "";

    if (!gps->getIsConnected() && !config.position.fixed_position) {
        displayLine = "No GPS Module";
        display->drawString(x + SCREEN_WIDTH/2, y + FONT_HEIGHT_SMALL, displayLine);
    } else if (!gps->getHasLock() && !config.position.fixed_position) {
        displayLine = "No GPS Lock";
        display->drawString(x + SCREEN_WIDTH/2, y + FONT_HEIGHT_SMALL, displayLine);
    } else {

        if (gpsFormat != Config_DisplayConfig_GpsCoordinateFormat_GpsFormatDMS) {
            char coordinateLine[22];
            geoCoord.updateCoords(int32_t(gps->getLatitude()), int32_t(gps->getLongitude()), int32_t(gps->getAltitude()));
            if (gpsFormat == Config_DisplayConfig_GpsCoordinateFormat_GpsFormatDec) { // Decimal Degrees
                sprintf(coordinateLine, "%f %f", geoCoord.getLatitude() * 1e-7, geoCoord.getLongitude() * 1e-7);
            } else if (gpsFormat == Config_DisplayConfig_GpsCoordinateFormat_GpsFormatUTM) { // Universal Transverse Mercator
                sprintf(coordinateLine, "%2i%1c %06u %07u", geoCoord.getUTMZone(), geoCoord.getUTMBand(),
                        geoCoord.getUTMEasting(), geoCoord.getUTMNorthing());
            } else if (gpsFormat == Config_DisplayConfig_GpsCoordinateFormat_GpsFormatMGRS) { // Military Grid Reference System
                sprintf(coordinateLine, "%2i%1c %1c%1c %05u %05u", geoCoord.getMGRSZone(), geoCoord.getMGRSBand(),
                        geoCoord.getMGRSEast100k(), geoCoord.getMGRSNorth100k(), geoCoord.getMGRSEasting(),
                        geoCoord.getMGRSNorthing());
            } else if (gpsFormat == Config_DisplayConfig_GpsCoordinateFormat_GpsFormatOLC) { // Open Location Code
                geoCoord.getOLCCode(coordinateLine);
            } else if (gpsFormat == Config_DisplayConfig_GpsCoordinateFormat_GpsFormatOSGR) { // Ordnance Survey Grid Reference
                if (geoCoord.getOSGRE100k() == 'I' || geoCoord.getOSGRN100k() == 'I') // OSGR is only valid around the UK region
                    sprintf(coordinateLine, "%s", "Out of Boundary");
                else
                    sprintf(coordinateLine, "%1c%1c %05u %05u", geoCoord.getOSGRE100k(), geoCoord.getOSGRN100k(),
                            geoCoord.getOSGREasting(), geoCoord.getOSGRNorthing());
            }

            // If fixed position, display text "Fixed GPS" alternating with the coordinates.
            if (config.position.fixed_position) {
                if ((millis() / 10000) % 2) {
                    display->drawString(x + SCREEN_WIDTH/2, y + FONT_HEIGHT_SMALL * 3, coordinateLine);
                } else {
                    display->drawString(x + SCREEN_WIDTH/2, y + FONT_HEIGHT_SMALL * 3, "Fixed GPS");
                }
            } else {
                display->drawString(x + SCREEN_WIDTH/2, y + FONT_HEIGHT_SMALL * 3, coordinateLine);
            }

        } else {
            char latLine[22];
            char lonLine[22];
            sprintf(latLine, "%2i° %2i' %2u\" %1c", geoCoord.getDMSLatDeg(), geoCoord.getDMSLatMin(), geoCoord.getDMSLatSec(),
                    geoCoord.getDMSLatCP());
            sprintf(lonLine, "%3i° %2i' %2u\" %1c", geoCoord.getDMSLonDeg(), geoCoord.getDMSLonMin(), geoCoord.getDMSLonSec(),
                    geoCoord.getDMSLonCP());
            display->drawString(x + SCREEN_WIDTH / 2, y + FONT_HEIGHT_SMALL * 3, latLine);
            display->drawString(x + SCREEN_WIDTH / 2, y + FONT_HEIGHT_SMALL * 4, lonLine);
        }
    }
}

void saveChannel(char* buf, uint8_t length){

}
void saveMode(char* buf, uint8_t length){

}
void saveCannedMsgOne(char* buf, uint8_t length){
    buf[length] = '|';
    cannedMessageModule->handleSetCannedMessageModulePart1(buf);
}

void saveCannedMsgTwo(char* buf, uint8_t length){
     buf[length] = '|';
    cannedMessageModule->handleSetCannedMessageModulePart2(buf);
}

void saveCannedMsgThree(char* buf, uint8_t length){
     buf[length] = '|';
    cannedMessageModule->handleSetCannedMessageModulePart3(buf);
}

void saveCannedMsgFour(char* buf, uint8_t length){
     buf[length] = '|';
    cannedMessageModule->handleSetCannedMessageModulePart4(buf);
}

void resetCannedMsgs(char* buf, uint8_t length){
    nodeDB.installDefaultDeviceState();
    nodeDB.saveToDisk();
    
    char str[20];
    memset(str, '\0', 20);
    sprintf(str, "Okay");
    saveCannedMsgOne(str, strlen(str));
    memset(str, '\0', 20);
    sprintf(str, "Looking 4 U");
    saveCannedMsgTwo(str, strlen(str));
    memset(str, '\0', 20);
    sprintf(str, "Come 2 me");
    saveCannedMsgThree(str, strlen(str));
    memset(str, '\0', 20);
    sprintf(str, "On my way");
    saveCannedMsgFour(str, strlen(str));
    nodeDB.saveToDisk();
    delay(100);
    ESP.restart();
}

void enableDebugMenu(char* buf, uint8_t length){
    forceSoftAP = ota_setup();
}
void restart(char* buf, uint8_t length){
    delay(500);
    ESP.restart();
}

void saveLongName(char* buf, uint8_t length){
    User new_owner = owner;
    memcpy(new_owner.long_name,
        buf, 
        length+1);
    buf[2] = '\0';
    memcpy(new_owner.short_name,
        buf, 
        3        
        );
    adminModule->handleSetOwner(new_owner);
}


void saveShortName(char* buf, uint8_t length){
    User new_owner = owner;
    
    memcpy(new_owner.long_name,
        buf, 
        length+1);
    buf[2] = '\0';
    memcpy(new_owner.short_name,
        buf, 
        3        
        );
    adminModule->handleSetOwner(new_owner);
}

void DebugInfo::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    displayedNodeNum = 0; // Not currently showing a node pane

    display->setFont(FONT_SMALL);

    // The coordinates define the left starting point of the text
    display->setTextAlignment(TEXT_ALIGN_CENTER);

    char channelStr[30];
    {
        concurrency::LockGuard guard(&lock);
        auto chName = channels.getPrimaryName();
        snprintf(channelStr, sizeof(channelStr), "%s", chName);
    }

    // Display power status
    // if (powerStatus->getHasBattery())
    //     drawBattery(display, x, y, imgBattery, powerStatus);
    // else if (powerStatus->knowsUSB())
    //     display->drawFastImage(x, y + 2, 16, 8, powerStatus->getHasUSB() ? imgUSB : imgPower);
    // Display nodes status
    drawNodes(display, x + SCREEN_WIDTH/2, y, nodeStatus);
    // Display GPS status
    drawGPS(display, x, y, gpsStatus);

    // // Draw the channel name
    // display->drawString(x + SCREEN_WIDTH/2, y + FONT_HEIGHT_SMALL + (SCREEN_HEIGHT/2), channelStr);
    // // Draw our hardware ID to assist with bluetooth pairing
    // display->drawFastImage(x + SCREEN_WIDTH - (10) - display->getStringWidth(ourId), y + FONT_HEIGHT_SMALL + (SCREEN_HEIGHT/2), 8, 8, imgInfo);
    // display->drawString(x + SCREEN_WIDTH/2, y + FONT_HEIGHT_SMALL * 3 + (SCREEN_HEIGHT/2), ourId);

    // Draw any log messages
    //display->drawLogBuffer(x, y + (FONT_HEIGHT_SMALL * 2));
    
}

void DebugInfo::drawFrameInfo(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    if (state->frameState != SELECTED){
            
        display->setFont(FONT_LARGE);
        display->drawString(SCREEN_WIDTH/2, y + SCREEN_HEIGHT/2, String("System Info"));
        return;
    }
    
    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    
    char batStr[20];
    if (powerStatus->getHasBattery()) {
        int batV = powerStatus->getBatteryVoltageMv() / 1000;
        int batCv = (powerStatus->getBatteryVoltageMv() % 1000) / 10;

        snprintf(batStr, sizeof(batStr), "Bat %01d.%02dV %3d%% %c%c", batV, batCv, powerStatus->getBatteryChargePercent(),
                 powerStatus->getIsCharging() ? '+' : ' ', powerStatus->getHasUSB() ? 'U' : ' ');
    } else {
        snprintf(batStr, sizeof(batStr), "USB");
    }
    char mode[10];

    switch (config.lora.modem_preset) {
    case Config_LoRaConfig_ModemPreset_ShortSlow:
        sprintf(mode, "%s", "SSlow");
        break;
    case Config_LoRaConfig_ModemPreset_ShortFast:
        sprintf(mode, "%s", "SFast");
        break;
    case Config_LoRaConfig_ModemPreset_MidSlow:
        sprintf(mode, "%s", "MSlow");
        break;
    case Config_LoRaConfig_ModemPreset_MidFast:
        sprintf(mode, "%s", "MFast");
        break;
    case Config_LoRaConfig_ModemPreset_LongFast:
        sprintf(mode, "%s", "LFast");
        break;
    case Config_LoRaConfig_ModemPreset_LongSlow:
        sprintf(mode, "%s", "LSlow");
        break;
    case Config_LoRaConfig_ModemPreset_VLongSlow:
        sprintf(mode, "%s", "VLSlow");
        break;
    default:
        sprintf(mode, "%s", "Custom");
        break;
    }


    char channelStr[40];
    {
        concurrency::LockGuard guard(&lock);
        auto chName = channels.getPrimaryName();
        snprintf(channelStr, sizeof(channelStr), "%s - %dms - %2.0f%%", chName, airTime->airtimeLastPeriod(TX_LOG), airTime->channelUtilizationPercent());
    }

    char usersString[20];
    sprintf(usersString, "Nodes: %d/%d", nodeStatus->getNumOnline(), nodeStatus->getNumTotal());
    uint32_t currentMillis = millis();
    uint32_t seconds = currentMillis / 1000;
    uint32_t minutes = seconds / 60;
    uint32_t hours = minutes / 60;
    uint32_t days = hours / 24;
    char uptime_letter;
    uint32_t uptime_number = days;
    if (days >= 2){
        uptime_number = days;
        uptime_letter = 'd';
    } else if (hours >= 2){
        uptime_number = hours;
        uptime_letter = 'h';
    } else if (minutes >= 1){
        uptime_number = minutes;
        uptime_letter = 'm';
    } else{
        uptime_number = seconds;
        uptime_letter = 's';    
    }
    char uptimebuf[20];
    snprintf(uptimebuf, sizeof(uptimebuf), "Uptime: %02d %c", uptime_number, uptime_letter);
    
    char timebuf[20];
    
    uint32_t rtc_sec = getValidTime(RTCQuality::RTCQualityDevice);
    if (rtc_sec > 0) {
        long hms = rtc_sec % SEC_PER_DAY;
        // hms += tz.tz_dsttime * SEC_PER_HOUR;
        // hms -= tz.tz_minuteswest * SEC_PER_MIN;
        // mod `hms` to ensure in positive range of [0...SEC_PER_DAY)
        hms = (hms + SEC_PER_DAY) % SEC_PER_DAY;

        // Tear apart hms into h:m:s
        int hour = hms / SEC_PER_HOUR;
        int min = (hms % SEC_PER_HOUR) / SEC_PER_MIN;
        int sec = (hms % SEC_PER_HOUR) % SEC_PER_MIN; // or hms % SEC_PER_MIN
        snprintf(timebuf, sizeof(timebuf), "%02d:%02d:%02d", hour, min, sec);
    } else {
        snprintf(timebuf, sizeof(timebuf), "00:00:00");
    }

    char gpsStatbuf[10];
    char posbuf[20];
    if (!gpsStatus->getIsConnected()) {
        snprintf(gpsStatbuf, sizeof(gpsStatbuf), "%s", "No GPS");
    }
    //display->drawFastImage(x, y, 6, 8, gps->getHasLock() ? imgPositionSolid : imgPositionEmpty);
    if (!gpsStatus->getHasLock()) {
        snprintf(gpsStatbuf, sizeof(gpsStatbuf), "%s", "No sats");
        sprintf(posbuf, "%s", "No Position");
    } else {
        uint8_t bar[2] = {0};

        // Draw DOP signal bars
        for (int i = 0; i < 5; i++) {
            if (gpsStatus->getDOP() <= dopThresholds[i])
                bar[0] = ~((1 << (5 - i)) - 1);
            else
                bar[0] = 0b10000000;
             bar[1] = bar[0];
            display->drawFastImage(x + SCREEN_WIDTH/2 - 10, y + 15, 2, 8, bar);
        }

        // Draw satellite image
        display->drawFastImage(x + SCREEN_WIDTH/2,  y + 30, 8, 8, imgSatellite);

        // Draw the number of satellites
        sprintf(gpsStatbuf, "Sats: %u", gpsStatus->getNumSatellites());
        sprintf(posbuf, "%f, %f", geoCoord.getLatitude() * 1e-7, geoCoord.getLongitude() * 1e-7);
    }
    uint16_t myHeading = heading->getHeading(); //estimatedHeading(DegD(op.latitude_i), DegD(op.longitude_i));
    
    char headingbuf[12]; 
    sprintf(headingbuf, "%s - %d", TinyGPSPlus::cardinal((float)myHeading * DEG_TO_RAD), myHeading);
    

    char *to_display[] = {
        timebuf,
        batStr,
        mode,
        channelStr,
        usersString,
        uptimebuf,
        gpsStatbuf,
        posbuf,
        headingbuf
    };
    int lines = 9;
    int center = SCREEN_WIDTH/2;
    int height = y + SCREEN_HEIGHT/2 - (lines * 5);
    for (int line = 0; line < lines; line++){
        if (0 < height && height < SCREEN_HEIGHT){
            display->drawString(center, height, String(to_display[line]));
        }
        height += 10;
    }
}


void DebugInfo::drawFrameSettings(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    if (state->currentSetting != -1){
        drawFrameSetting(display, state, x, y);
    } else {
        if (state->frameState == SELECTED){
            state->maxIndex = NUM_SETTINGS;
            
            display->setFont(FONT_SMALL);
            display->setTextAlignment(TEXT_ALIGN_CENTER);

            int center = SCREEN_WIDTH/2;
            for (int line = 0; line < NUM_SETTINGS; line++){
                if (line == state->currentIndex && state->frameState == SELECTED){
                    display->setFont(FONT_MEDIUM);
                } else {
                    display->setFont(FONT_SMALL);
                }
                int height = y + SCREEN_HEIGHT/2 + (line - state->maxIndex/2) * 20;
                if (state->frameState == SELECTED){
                    height = y + SCREEN_HEIGHT/2 + (line - state->currentIndex) * 20;
                }
                if (0 < height && height < SCREEN_HEIGHT){
                display->drawString(center, height, String(settings[line]));
                }
            }
        } else {
            display->setFont(FONT_LARGE);
            display->drawString(SCREEN_WIDTH/2, y + SCREEN_HEIGHT/2, String("Settings"));
        }
    }
}

void DebugInfo::drawFrameSetting(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    if (state->currentSetting >= 0 && state->currentSetting < NUM_SETTINGS){
        switch(setting_type[state->currentSetting]){
            case PREDEFINED:
                if (state->currentSetting == 1){
                    setName(display, state, x, y);
                } else {
                    resetFrame(display, state, x, y);
                }
                break;
            case WORD:
                drawWordSetting(display, state, x, y);
                break;
            case BOOL:
                
                break;
        }

    }
}



void DebugInfo::setName(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y){
    char* names[4] = {
        "Hugh\0",
        "Connor\0",
        "Caitlin\0",
        "Loz\0"
    };
    int name_lengths[4] = {
        5, 7, 8, 4
    };
    if (state->action == SELECT){
        char buffer[15];
        sprintf(buffer, "%s", names[state->currentIndex]);
        saveShortName(buffer, strlen(buffer));
        state->currentIndex = state->currentSetting;
        state->currentSetting = -1;
        state->action = NOACTION;
    } else if (state->action == SAVE){
        state->action = NOACTION;
        state->currentIndex = state->currentSetting;
        state->currentSetting = -1;
        return;
    }
    state->maxIndex = 4;
    for(int i = 0; i < 4; i++){
        if (i == state->currentIndex){
            display->setFont(FONT_LARGE);
        } else {
            display->setFont(FONT_MEDIUM);
        }
        int height = SCREEN_HEIGHT/2 + (i - state->currentIndex) * 30;
        display->drawString(x + SCREEN_WIDTH/2, y + height, String(names[i]));
    }   
}

void DebugInfo::resetFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y){
    if (state->action == SELECT){
        // Reseting the canned messages and node info
        settingSaves[state->currentSetting]("nothing", 10);
        state->action = NOACTION;
        state->currentIndex = state->currentSetting;
        state->currentSetting = -1;
        return;
    } else if (state->action == SAVE){
        // Actually wanting to go back
        state->action = NOACTION;
        state->currentIndex = state->currentSetting;
        state->currentSetting = -1;
    }
    display->setFont(FONT_MEDIUM);
    char question[21];
    sprintf(question, "%s?", settings[state->currentSetting]);
    display->drawString(x + SCREEN_WIDTH/2, 40, question);
    display->setFont(FONT_SMALL);
    display->drawString(x + 40, SCREEN_HEIGHT / 4, "Cancel");
    display->drawString(x + 40, 3 * SCREEN_HEIGHT / 4, "Confirm");
    
}

void DebugInfo::drawWordSetting(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y){
    if (state->action == SELECT){
        tempStore[currentLetter++] = alphabet[state->currentIndex];
        state->action = NOACTION;
    } else if (state->action == SAVE){
        // Would save here
        if (currentLetter > 0){
            settingSaves[state->currentSetting](tempStore, currentLetter);
        }
        memset(tempStore, '\0', 64);
        currentLetter = 0;
        state->action = NOACTION;
        state->currentIndex = state->currentSetting;
        state->currentSetting = -1;
        state->maxIndex = 1;
        return;
    }
    if (state->maxIndex != LETTERS_SIZE){
        state->maxIndex = LETTERS_SIZE;
        memset(tempStore, '\0', 64);
    }
    display->setFont(FONT_MEDIUM);
    display->drawString(x + SCREEN_WIDTH/2, 40, String(settings[state->currentSetting]));
    if (currentLetter > 0){
        display->drawString(x + SCREEN_WIDTH/2, 60, String(tempStore));
    }
    for(int i = 0; i < 26; i++){
        if (i == state->currentIndex){
            display->setFont(FONT_LARGE);
        } else {
            display->setFont(FONT_MEDIUM);
        }
        int height = SCREEN_HEIGHT/2 + (i - state->currentIndex) * 30 + 20;
        if (90 < height && height < SCREEN_HEIGHT){
            display->drawString(x + SCREEN_WIDTH/2, y + height, String(alphabet[i]));
        }
    }   
}

void DebugInfo::doSwitchSetting(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y){
    if (state->action == SELECT){
        tempStore[currentLetter++] = alphabet[state->currentIndex];
        state->action = NOACTION;
    } else if (state->action == SAVE){
        // Would save here
        if (currentLetter > 0){
            settingSaves[state->currentSetting](tempStore, currentLetter);
        }
        memset(tempStore, '\0', 64);
        currentLetter = 0;
        state->action = NOACTION;
        state->currentIndex = state->currentSetting;
        state->currentSetting = -1;
        state->maxIndex = 1;
        return;
    }
    if (state->maxIndex != LETTERS_SIZE){
        state->maxIndex = LETTERS_SIZE;
        memset(tempStore, '\0', 64);
    }
    display->setFont(FONT_MEDIUM);
    display->drawString(x + SCREEN_WIDTH/2, 40, String(settings[state->currentSetting]));
    if (currentLetter > 0){
        display->drawString(x + SCREEN_WIDTH/2, 60, String(tempStore));
    }
    for(int i = 0; i < 26; i++){
        if (i == state->currentIndex){
            display->setFont(FONT_LARGE);
        } else {
            display->setFont(FONT_MEDIUM);
        }
        int height = SCREEN_HEIGHT/2 + (i - state->currentIndex) * 30 + 20;
        if (90 < height && height < SCREEN_HEIGHT){
            display->drawString(x + SCREEN_WIDTH/2, y + height, String(alphabet[i]));
        }
    }   
}

void DebugInfo::drawFrameTestNode(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    if (state->frameState == SELECTED){
        drawFrameText(display, state, x, y);
    } else {
        display->setFont(FONT_SMALL);
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        int lines = 4;
        const char *to_display[] = {
            "Name here",
            "20m",
            "60 seconds ago",
            "-5dB - NNE - 200"
        };
        int center = SCREEN_WIDTH/2;
        for (int line = 0; line < lines; line++){
            if (line == 0 || line == 1){
                display->setFont(FONT_MEDIUM);
            } else {
                display->setFont(FONT_SMALL);
            }
            int height = y + SCREEN_HEIGHT/2 + (line - 1) * 20;
            if (0 < height && height < SCREEN_HEIGHT){
            display->drawString(center, height, String(to_display[line]));
            }
        }
        uint16_t degree = heading->getHeading();
        uint16_t radius = 110;
        uint16_t thickness = 10;
        uint16_t width = 5;
        /*
        if (distance <= 20){
            thickness = -5.33 * distance + 116.66;
            if (thickness > 90){
                thickness = 180;
            }
        } 
        */ 
        
        display->drawArc(SCREEN_WIDTH/2, 
                        SCREEN_HEIGHT/2, 
                        radius - thickness /2 , 
                        radius + thickness /2, 
                        degree - width, 
                        degree + width);
    }
}

void DebugInfo::drawFrameText(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    cannedMessageModule->currentMessageIndex = state->currentIndex;
    state->maxIndex = 4;
    
    if (state->currentSetting != 0){
        state->currentSetting = 0;
    }
    if (state->action == SELECT || state->action == SAVE){
        if (state->action == SELECT){
            cannedMessageModule->eventSelect();
        }
        state->action = NOACTION;
        state->frameState = FIXED;
        state->currentIndex = 0;
        state->currentSetting = -1;
        return;
    }   
    
    
    display->setFont(FONT_MEDIUM);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    int lines = 3;
    const char *to_display[] = {
        cannedMessageModule->getPrevMessage(),
        cannedMessageModule->getCurrentMessage(),
        cannedMessageModule->getNextMessage()
    };
    int center = SCREEN_WIDTH/2;
    for (int line = 0; line < lines; line++){
        if (line == 1){
            display->setFont(FONT_LARGE);
        } else {
            display->setFont(FONT_MEDIUM);
        }
        int height = y + SCREEN_HEIGHT/2 + (line - 1) * 40;
        if (0 < height && height < SCREEN_HEIGHT){
          display->drawString(center, height, String(to_display[line]));
        }
    }
    
}

}

