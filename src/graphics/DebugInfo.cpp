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
#include "sleep.h"
#include "target_specific.h"
#include "utils.h"

#ifndef NO_ESP32
#include "esp_task_wdt.h"
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
uint8_t imgBattery[16] = {0xFF, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81, 0xE7, 0x3C};

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

// Draw power bars or a charging indicator on an image of a battery, determined by battery charge voltage or percentage.
static void drawBattery(OLEDDisplay *display, int16_t x, int16_t y, uint8_t *imgBuffer, const PowerStatus *powerStatus)
{
    // static const uint8_t powerBar[3] = {0x81, 0xBD, 0xBD};
    // static const uint8_t lightning[8] = {0xA1, 0xA1, 0xA5, 0xAD, 0xB5, 0xA5, 0x85, 0x85};
    // // Clear the bar area on the battery image
    // for (int i = 1; i < 14; i++) {
    //     imgBuffer[i] = 0x81;
    // }
    // // If charging, draw a charging indicator
    // if (powerStatus->getIsCharging()) {
    //     memcpy(imgBuffer + 3, lightning, 8);
    //     // If not charging, Draw power bars
    // } else {
    //     for (int i = 0; i < 4; i++) {
    //         if (powerStatus->getBatteryChargePercent() >= 25 * i)
    //             memcpy(imgBuffer + 1 + (i * 3), powerBar, 3);
    //     }
    // }
    // display->drawFastImage(x, y, 16, 8, imgBuffer);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(FONT_HEIGHT_SMALL);
    char battString[5];
    if (powerStatus->getIsCharging()) {
        display->drawString(x + SCREEN_WIDTH/2, y + FONT_HEIGHT_SMALL, "Charging");
    } else {
        
        sprintf(battString, "%u %%", powerStatus->getBatteryChargePercent());
        display->drawString(x + SCREEN_WIDTH/2, y + FONT_HEIGHT_SMALL, battString); 
    }
}

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
            // bar[1] = bar[0];
            //display->drawFastImage(x + 9 + (i * 2), y, 2, 8, bar);
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

void DebugInfo::drawFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    displayedNodeNum = 0; // Not currently showing a node pane

    display->setFont(FONT_SMALL);

    // The coordinates define the left starting point of the text
    display->setTextAlignment(TEXT_ALIGN_CENTER);

    char channelStr[20];
    {
        concurrency::LockGuard guard(&lock);
        auto chName = channels.getPrimaryName();
        snprintf(channelStr, sizeof(channelStr), "%s", chName);
    }

    // Display power status
    if (powerStatus->getHasBattery())
        drawBattery(display, x, y, imgBattery, powerStatus);
    else if (powerStatus->knowsUSB())
        display->drawFastImage(x, y + 2, 16, 8, powerStatus->getHasUSB() ? imgUSB : imgPower);
    // Display nodes status
    drawNodes(display, x + SCREEN_WIDTH/2, y, nodeStatus);
    // Display GPS status
    drawGPS(display, x, y, gpsStatus);

    // Draw the channel name
    display->drawString(x + SCREEN_WIDTH/2, y + FONT_HEIGHT_SMALL + (SCREEN_HEIGHT/2), channelStr);
    // Draw our hardware ID to assist with bluetooth pairing
    display->drawFastImage(x + SCREEN_WIDTH - (10) - display->getStringWidth(ourId), y + FONT_HEIGHT_SMALL + (SCREEN_HEIGHT/2), 8, 8, imgInfo);
    display->drawString(x + SCREEN_WIDTH/2, y + FONT_HEIGHT_SMALL * 3 + (SCREEN_HEIGHT/2), ourId);

    // Draw any log messages
    //display->drawLogBuffer(x, y + (FONT_HEIGHT_SMALL * 2));
    
}

void DebugInfo::drawFrameSettings(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    displayedNodeNum = 0; // Not currently showing a node pane

    display->setFont(FONT_SMALL);

    // The coordinates define the left starting point of the text
    display->setTextAlignment(TEXT_ALIGN_CENTER);

    char batStr[20];
    if (powerStatus->getHasBattery()) {
        int batV = powerStatus->getBatteryVoltageMv() / 1000;
        int batCv = (powerStatus->getBatteryVoltageMv() % 1000) / 10;

        snprintf(batStr, sizeof(batStr), "B %01d.%02dV %3d%% %c%c", batV, batCv, powerStatus->getBatteryChargePercent(),
                 powerStatus->getIsCharging() ? '+' : ' ', powerStatus->getHasUSB() ? 'U' : ' ');

        // Line 1
        display->drawString(x + SCREEN_WIDTH/2, y + (SCREEN_HEIGHT/2) - FONT_HEIGHT_SMALL*3, batStr);
    } else {
        // Line 1
        display->drawString(x + SCREEN_WIDTH/2, y + (SCREEN_HEIGHT/2) - FONT_HEIGHT_SMALL*3, String("USB"));
    }

    auto mode = "";

    switch (config.lora.modem_preset) {
    case Config_LoRaConfig_ModemPreset_ShortSlow:
        mode = "ShortSlow";
        break;
    case Config_LoRaConfig_ModemPreset_ShortFast:
        mode = "ShortFast";
        break;
    case Config_LoRaConfig_ModemPreset_MidSlow:
        mode = "MediumSlow";
        break;
    case Config_LoRaConfig_ModemPreset_MidFast:
        mode = "MediumFast";
        break;
    case Config_LoRaConfig_ModemPreset_LongFast:
        mode = "LongFast";
        break;
    case Config_LoRaConfig_ModemPreset_LongSlow:
        mode = "LongSlow";
        break;
    case Config_LoRaConfig_ModemPreset_VLongSlow:
        mode = "VLongSlow";
        break;
    default:
        mode = "Custom";
        break;
    }

    display->drawString(x + SCREEN_WIDTH/2, y + (SCREEN_HEIGHT/2) - FONT_HEIGHT_SMALL*3, mode);

    // Line 2
    uint32_t currentMillis = millis();
    uint32_t seconds = currentMillis / 1000;
    uint32_t minutes = seconds / 60;
    uint32_t hours = minutes / 60;
    uint32_t days = hours / 24;
    // currentMillis %= 1000;
    // seconds %= 60;
    // minutes %= 60;
    // hours %= 24;

    // Show uptime as days, hours, minutes OR seconds
    String uptime;
    if (days >= 2)
        uptime += String(days) + "d ";
    else if (hours >= 2)
        uptime += String(hours) + "h ";
    else if (minutes >= 1)
        uptime += String(minutes) + "m ";
    else
        uptime += String(seconds) + "s ";

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

        char timebuf[9];
        snprintf(timebuf, sizeof(timebuf), "%02d:%02d:%02d", hour, min, sec);
        uptime += timebuf;
    }

    display->drawString(x + SCREEN_WIDTH/2, y - FONT_HEIGHT_SMALL * 1 + (SCREEN_HEIGHT/2), uptime);

    // Display Channel Utilization
    char chUtil[13];
    sprintf(chUtil, "ChUtil %2.0f%%", airTime->channelUtilizationPercent());
    display->drawString(x + SCREEN_WIDTH/2, y + (SCREEN_HEIGHT/2), chUtil);

    // Line 3
    if (config.display.gps_format !=
        Config_DisplayConfig_GpsCoordinateFormat_GpsFormatDMS) // if DMS then don't draw altitude
        drawGPSAltitude(display, x, y + (SCREEN_HEIGHT/2), gpsStatus);

    // Line 4
    drawGPScoordinates(display, x, y + (SCREEN_HEIGHT/2), gpsStatus);

    /* Display a heartbeat pixel that blinks every time the frame is redrawn */
#ifdef SHOW_REDRAWS
    if (heartbeat)
        display->setPixel(0, 0);
    heartbeat = !heartbeat;
#endif
}
}