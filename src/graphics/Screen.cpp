#include "configuration.h"
#ifndef NO_SCREEN
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
#include "TinyGPS++.h"
#include "Heading.h"
#ifndef NO_ESP32
#include "esp_task_wdt.h"
#ifdef WANT_WIFI
    #include "mesh/http/WiFiAPClient.h"
#endif
#endif

using namespace meshtastic; /** @todo remove */
char ipaddress[17] = "\0";
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

// A text message frame + debug frame + all the node infos
static FrameCallback normalFrames[MAX_NUM_NODES + NUM_EXTRA_FRAMES];
static uint32_t targetFramerate = IDLE_FRAMERATE;
static char btPIN[16] = "888888";

// At some point, we're going to ask all of the modules if they would like to display a screen frame
// we'll need to hold onto pointers for the modules that can draw a frame.
std::vector<MeshModule *> moduleFrames;

// Stores the last 4 of our hardware ID, to make finding the device for pairing easier
static char ourId[5];
// This image definition is here instead of images.h because it's modified dynamically by the drawBattery function
uint8_t imgBattery[16] = {0xff, 0xfe, 0x80, 0x02, 0x80, 0x03, 0x80, 0x01, 0x80, 0x01, 0x80, 0x03, 0x80, 0x02, 0xff, 0xfe};

// OEM Config File
static const char *oemConfigFile = "/prefs/oem.proto";
OEMStore oemStore;

#ifdef SHOW_REDRAWS
static bool heartbeat = false;
#endif

static uint16_t displayWidth, displayHeight;

#define SCREEN_WIDTH displayWidth
#define SCREEN_HEIGHT displayHeight

#define FONT_SMALL 1
#define FONT_MEDIUM 2
#define FONT_LARGE 3

#define fontHeight(font) ((font)[1] + 1) // height is position 1

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
    static const uint8_t powerBar[4] = {0xc0, 0xc0, 0xc0, 0xc0};
    static const uint8_t lightning[8] = {0xA1, 0xA1, 0xA5, 0xAD, 0xB5, 0xA5, 0x85, 0x85};
    // Clear the bar area on the battery image
    for (int i = 1; i < 14; i++) {
        imgBuffer[i] = 0x81;
    }
    // If charging, draw a charging indicator
    if (powerStatus->getIsCharging()) {
        memcpy(imgBuffer + 3, lightning, 8);
        // If not charging, Draw power bars
    } else {
        // for (int i = 0; i < 4; i++) {
        //     if (powerStatus->getBatteryChargePercent() >= 25 * i){
        //         int x1 = (i + 1) * 3;
        //         for (int16_t j = 3; j < 4; j++)
        //         {
        //             for (int16_t i = x1; i < 2; i++)
        //             {
        //                 imgBuffer[i*j] = 1;
        //             }
        //         }
        //     }
        //         // memcpy(imgBuffer + 1 + (i * 3), powerBar, 4);
        // }
    }
    //display->drawFastImage(x, y, 16, 8, imgBuffer);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(FONT_SMALL);
    char battString[5];
    if (powerStatus->getIsCharging()) {
        // unable to detect if charging yet display->drawString(x + SCREEN_WIDTH/2, y + FONT_HEIGHT_SMALL, "Charging");
    } else {
        
        sprintf(battString, "%u %%", powerStatus->getBatteryChargePercent());
        display->drawString(x, y, battString); 
    }
}



/**
 * Draw the icon with extra info printed around the corners
 */
static void drawIconScreen(const char *upperMsg, OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // draw an xbm image.
    // Please note that everything that should be transitioned
    // needs to be drawn relative to x and y

    // draw centered icon left to right and centered above the one line of app text
    display->drawXbm(x + (SCREEN_WIDTH - icon_width) / 2, y + (SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM - icon_height) / 2 + 2,
                     icon_width, icon_height, (const uint8_t *)icon_bits);

    display->setFont(FONT_MEDIUM);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    const char *title = "WhereU AT";
    display->drawString(x + SCREEN_WIDTH/2, y + SCREEN_HEIGHT/2, title);
    display->setFont(FONT_SMALL);

    // Draw region in upper left
    if (upperMsg)
        display->drawString(x + SCREEN_WIDTH/2, y + FONT_HEIGHT_SMALL, upperMsg);

    // Draw version in upper right
    char buf[16];
    snprintf(buf, sizeof(buf), "%s",
             xstr(APP_VERSION_SHORT)); // Note: we don't bother printing region or now, it makes the string too long
    display->drawString(x + SCREEN_WIDTH/2, y + SCREEN_HEIGHT/2 + FONT_HEIGHT_MEDIUM, buf);
    // FIXME - draw serial # somewhere?
}

static void drawBootScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // Draw region in upper left
    const char *region = myRegion ? myRegion->name : NULL;
    drawIconScreen(region, display, state, x, y);
}

static void drawOEMIconScreen(const char *upperMsg, OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // draw an xbm image.
    // Please note that everything that should be transitioned
    // needs to be drawn relative to x and y

    // draw centered icon left to right and centered above the one line of app text
    display->drawXbm(x + (SCREEN_WIDTH - oemStore.oem_icon_width) / 2,
                     y + (SCREEN_HEIGHT - FONT_HEIGHT_MEDIUM - oemStore.oem_icon_height) / 2 + 2, oemStore.oem_icon_width,
                     oemStore.oem_icon_height, (const uint8_t *)oemStore.oem_icon_bits.bytes);

    switch (oemStore.oem_font) {
    case 0:
        display->setFont(FONT_SMALL);
        break;
    case 2:
        display->setFont(FONT_LARGE);
        break;
    default:
        display->setFont(FONT_MEDIUM);
        break;
    }

    display->setTextAlignment(TEXT_ALIGN_CENTER);
    const char *title = oemStore.oem_text;
    display->drawString(x + SCREEN_WIDTH/2, y + SCREEN_HEIGHT/2 - FONT_HEIGHT_MEDIUM, title);
    display->setFont(FONT_SMALL);

    // Draw region in upper left
    if (upperMsg)
        display->drawString(x + 0, y + 0, upperMsg);

    // Draw version in upper right
    char buf[16];
    snprintf(buf, sizeof(buf), "%s",
             xstr(APP_VERSION_SHORT)); // Note: we don't bother printing region or now, it makes the string too long
    display->drawString(x + SCREEN_WIDTH/2, y + SCREEN_HEIGHT/2, buf);

    // FIXME - draw serial # somewhere?
}

static void drawOEMBootScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    // Draw region in upper left
    const char *region = myRegion ? myRegion->name : NULL;
    drawOEMIconScreen(region, display, state, x, y);
}

// Used on boot when a certificate is being created
static void drawSSLScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(FONT_SMALL);
    display->drawString(x + SCREEN_WIDTH/2, y + SCREEN_HEIGHT/2 - FONT_HEIGHT_SMALL, "Creating SSL certificate");

#ifndef NO_ESP32
    yield();
    esp_task_wdt_reset();
#endif

    display->setFont(FONT_SMALL);
    if ((millis() / 1000) % 2) {
        display->drawString(x + SCREEN_WIDTH/2, y + SCREEN_HEIGHT/2 , "Please wait . . .");
    } else {
        display->drawString(x + SCREEN_WIDTH/2, y + SCREEN_HEIGHT/2, "Please wait . .  ");
    }
}

// Used when booting without a region set
static void drawWelcomeScreen(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{

    if ((millis() / 10000) % 2) {
        display->setFont(FONT_SMALL);

        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->drawString(x + SCREEN_WIDTH/2, y + SCREEN_HEIGHT/2 - FONT_HEIGHT_SMALL * 2, "//\\ E S H T /\\ S T / C");

        display->setTextAlignment(TEXT_ALIGN_CENTER);

        display->drawString(x + SCREEN_WIDTH/2, y + SCREEN_HEIGHT/2 - FONT_HEIGHT_SMALL, "Set the region using the");
        display->drawString(x + SCREEN_WIDTH/2, y + SCREEN_HEIGHT/2, "Meshtastic Android, iOS,");
        display->drawString(x + SCREEN_WIDTH/2, y + SCREEN_HEIGHT/2 + FONT_HEIGHT_SMALL, "Flasher or CLI client.");
    } else {
        display->setFont(FONT_SMALL);

        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->drawString(x + SCREEN_WIDTH/2, y + SCREEN_HEIGHT/2 - 20, "//\\ E S H T /\\ S T / C");

        display->setTextAlignment(TEXT_ALIGN_CENTER);

        display->drawString(x + SCREEN_WIDTH/2, y + SCREEN_HEIGHT/2 - FONT_HEIGHT_SMALL, "Visit meshtastic.org");
        display->drawString(x + SCREEN_WIDTH/2, y + SCREEN_HEIGHT/2, "for more information.");
    }

#ifndef NO_ESP32
    yield();
    esp_task_wdt_reset();
#endif
}

static void drawModuleFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    uint8_t module_frame;
    // there's a little but in the UI transition code
    // where it invokes the function at the correct offset
    // in the array of "drawScreen" functions; however,
    // the passed-state doesn't quite reflect the "current"
    // screen, so we have to detect it.
    if (state->frameState == IN_TRANSITION && state->transitionFrameRelationship == INCOMING) {
        // if we're transitioning from the end of the frame list back around to the first
        // frame, then we want this to be `0`
        module_frame = state->transitionFrameTarget;
    } else {
        // otherwise, just display the module frame that's aligned with the current frame
        module_frame = state->currentFrame;
        // DEBUG_MSG("Screen is not in transition.  Frame: %d\n\n", module_frame);
    }
    // DEBUG_MSG("Drawing Module Frame %d\n\n", module_frame);
    MeshModule &pi = *moduleFrames.at(module_frame);
    pi.drawFrame(display, state, x, y);
}

static void drawFrameBluetooth(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(FONT_MEDIUM);
    display->drawString(64 + x, y, "Bluetooth");

    display->setFont(FONT_SMALL);
    display->drawString(64 + x, FONT_HEIGHT_SMALL + y + 2, "Enter this code");

    display->setFont(FONT_LARGE);
    display->drawString(64 + x, 26 + y, btPIN);

    display->setFont(FONT_SMALL);
    char buf[30];
    const char *name = "Name: ";
    strcpy(buf, name);
    strcat(buf, getDeviceName());
    display->drawString(64 + x, 48 + y, buf);
}

static void drawFrameShutdown(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_CENTER);

    display->setFont(FONT_MEDIUM);
    display->drawString(64 + x, 26 + y, "Shutting down...");
}

static void drawFrameFirmware(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(FONT_MEDIUM);
    display->drawString(x + SCREEN_WIDTH/2, y + display->getHeight() / 2 - FONT_HEIGHT_MEDIUM, "Updating");

    display->setFont(FONT_SMALL);
    if ((millis() / 1000) % 2) {
        display->drawString(x + SCREEN_WIDTH/2, y + display->getHeight() / 2, "Please wait . . .");
    } else {
        display->drawString(x + SCREEN_WIDTH/2, y + display->getHeight() / 2, "Please wait . .  ");
    }

    // display->setFont(FONT_LARGE);
    // display->drawString(64 + x, 26 + y, btPIN);
}

/// Draw the last text message we received
static void drawCriticalFaultFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    displayedNodeNum = 0; // Not currently showing a node pane

    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_MEDIUM);

    char tempBuf[24];
    snprintf(tempBuf, sizeof(tempBuf), "Critical fault #%d", myNodeInfo.error_code);
    display->drawString(0 + x, 0 + y, tempBuf);
    display->setTextAlignment(TEXT_ALIGN_LEFT);
    display->setFont(FONT_SMALL);
    display->drawString(0 + x, FONT_HEIGHT_MEDIUM + y, "For help, please visit \nmeshtastic.org");
}

// Ignore messages orginating from phone (from the current node 0x0) unless range test or store and forward module are enabled
static bool shouldDrawMessage(const MeshPacket *packet)
{
    return packet->from != 0 && !moduleConfig.range_test.enabled &&
           !moduleConfig.store_forward.enabled;
}

/// Draw the last text message we received
static void drawTextMessageFrame(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    displayedNodeNum = 0; // Not currently showing a node pane

    MeshPacket &mp = devicestate.rx_text_message;
    NodeInfo *node = nodeDB.getNode(getFrom(&mp));
    // DEBUG_MSG("drawing text message from 0x%x: %s\n", mp.from,
    // mp.decoded.variant.data.decoded.bytes);

    // Demo for drawStringMaxWidth:
    // with the third parameter you can define the width after which words will
    // be wrapped. Currently only spaces and "-" are allowed for wrapping
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->setFont(FONT_MEDIUM);
    String sender = (node && node->has_user) ? node->user.long_name : "???";
    display->drawString(x + SCREEN_WIDTH/2, y + display->getHeight() / 2 - FONT_HEIGHT_MEDIUM, sender);
    display->setFont(FONT_SMALL);
    uint32_t time_ago = getValidTime(RTCQuality::RTCQualityDevice) - mp.rx_time;
    static char lastStr[20];
    if (time_ago < 120) // last 2 mins?
        snprintf(lastStr, sizeof(lastStr), "%u seconds ago", time_ago);
    else if (time_ago < 120 * 60) // last 2 hrs
        snprintf(lastStr, sizeof(lastStr), "%u minutes ago", time_ago / 60);
    else {
        uint32_t hours_in_month = 730;

        // Only show hours ago if it's been less than 6 months. Otherwise, we may have bad
        //   data.
        if ((time_ago / 60 / 60) < (hours_in_month * 6)) {
            snprintf(lastStr, sizeof(lastStr), "%u hours ago", time_ago / 60 / 60);
        } else {
            snprintf(lastStr, sizeof(lastStr), "A long time ago");
        }
    }
    // the max length of this buffer is much longer than we can possibly print
    static char tempBuf[96];
    snprintf(tempBuf, sizeof(tempBuf), "%s", mp.decoded.payload.bytes);

    display->drawString(x + SCREEN_WIDTH/2, y + display->getHeight() / 2, tempBuf);
    display->drawString(x + SCREEN_WIDTH/2, y + display->getHeight() / 2 + 30, lastStr);
}

namespace
{

/// A basic 2D point class for drawing
class Point
{
  public:
    float x, y;

    Point(float _x, float _y) : x(_x), y(_y) {}

    /// Apply a rotation around zero (standard rotation matrix math)
    void rotate(float radian)
    {
        float cos = cosf(radian), sin = sinf(radian);
        float rx = x * cos - y * sin, ry = x * sin + y * cos;

        x = rx;
        y = ry;
    }

    void translate(int16_t dx, int dy)
    {
        x += dx;
        y += dy;
    }

    void scale(float f)
    {
        x *= f;
        y *= f;
    }
};

} // namespace

static void drawLine(OLEDDisplay *d, const Point &p1, const Point &p2)
{
    d->drawLine(p1.x, p1.y, p2.x, p2.y);
}

/**
 * Given a recent lat/lon return a guess of the heading the user is walking on.
 *
 * We keep a series of "after you've gone 10 meters, what is your heading since
 * the last reference point?"
 */
static float estimatedHeading(double lat, double lon)
{
    static double oldLat, oldLon;
    static float b;

    if (oldLat == 0) {
        // just prepare for next time
        oldLat = lat;
        oldLon = lon;

        return b;
    }

    float d = GeoCoord::latLongToMeter(oldLat, oldLon, lat, lon);
    if (d < 10) // haven't moved enough, just keep current bearing
        return b;

    b = GeoCoord::bearing(oldLat, oldLon, lat, lon);
    oldLat = lat;
    oldLon = lon;

    return b;
}

/// Sometimes we will have Position objects that only have a time, so check for
/// valid lat/lon
static bool hasPosition(NodeInfo *n)
{
    return n->has_position && (n->position.latitude_i != 0 || n->position.longitude_i != 0);
}

/// We will skip one node - the one for us, so we just blindly loop over all
/// nodes
static size_t nodeIndex;
static int8_t prevFrame = -1;

// Draw the arrow pointing to a node's location
static void drawNodeHeading(OLEDDisplay *display, int16_t compassX, int16_t compassY, float headingRadian)
{
    Point tip(0.0f, 0.5f), tail(0.0f, -0.5f); // pointing up initially
    float arrowOffsetX = 0.2f, arrowOffsetY = 0.2f;
    Point leftArrow(tip.x - arrowOffsetX, tip.y - arrowOffsetY), rightArrow(tip.x + arrowOffsetX, tip.y - arrowOffsetY);

    Point *arrowPoints[] = {&tip, &tail, &leftArrow, &rightArrow};

    for (int i = 0; i < 4; i++) {
        arrowPoints[i]->rotate(headingRadian);
        arrowPoints[i]->scale(COMPASS_DIAM * 0.6);
        arrowPoints[i]->translate(compassX, compassY);
    }
    drawLine(display, tip, tail);
    drawLine(display, leftArrow, tip);
    drawLine(display, rightArrow, tip);
}

// Draw the compass heading
static void drawCompassHeading(OLEDDisplay *display, int16_t compassX, int16_t compassY, float myHeading)
{
    Point N1(-0.04f, -0.65f), N2(0.04f, -0.65f);
    Point N3(-0.04f, -0.55f), N4(0.04f, -0.55f);
    Point *rosePoints[] = {&N1, &N2, &N3, &N4};

    for (int i = 0; i < 4; i++) {
        rosePoints[i]->rotate(myHeading);
        rosePoints[i]->scale(-1 * COMPASS_DIAM);
        rosePoints[i]->translate(compassX, compassY);
    }
    drawLine(display, N1, N3);
    drawLine(display, N2, N4);
    drawLine(display, N1, N4);
}

int simpleBearingTo(double lat1, double lon1, double lat2, double lon2){
    double veryclose = 0.00001; // 1.11m
    double close = 0.0001; // 11.1m
    double mid = 0.001; // 111.1m
    int north_south = 0;
    int east_west = 0;

    if (lat1 < lat2){
        // 2 is north of 1
        north_south = 1;
    } else if (lat2 < lat1){
        // 2 is south of 1
        north_south = -1;
    }

    if (lon1 < lon2){
        // 2 is east of 1
        east_west = 1;
    } else if (lon2 < lon1){
        // 2 is west of 1
        east_west = -1;
    }

    double lat_difference = abs(lat1-lat2);
    if (lat_difference <= veryclose){
        north_south = 0;
    } else if (lat_difference <= close){
        north_south = north_south;
    } else if (lat_difference <= mid){
        north_south = north_south * 2;
    } else{
        north_south = north_south * 3;
    }

    double lon_difference = abs(lon1-lon2);
    if (lon_difference <= veryclose){
        east_west = 0;
    } else if (lon_difference <= close){
        east_west = east_west;
    } else if (lon_difference <= mid){
        east_west = east_west * 2;
    } else{
        east_west = east_west * 3;
    }

    
    return (int)(RAD_TO_DEG * atan2(east_west, north_south));
}


int altsimpleBearingTo(double lat1, double lon1, double lat2, double lon2){
    double veryclose = 0.00001; // 1.11m
    double close = 0.0001; // 11.1m
    double mid = 0.001; // 111.1m
    int north_south = 0;
    int east_west = 0;

    if (lat1 < lat2){
        // 2 is north of 1
        north_south = 1;
    } else if (lat2 < lat1){
        // 2 is south of 1
        north_south = -1;
    }

    if (lon1 < lon2){
        // 2 is east of 1
        east_west = 1;
    } else if (lon2 < lon1){
        // 2 is west of 1
        east_west = -1;
    }

    double lat_difference = abs(lat1-lat2);
    if (lat_difference <= veryclose){
        north_south = 0;
    } else if (lat_difference <= close){
        north_south = north_south;
    } else if (lat_difference <= mid){
        north_south = north_south * 2;
    } else{
        north_south = north_south * 3;
    }

    double lon_difference = abs(lon1-lon2);
    if (lon_difference <= veryclose){
        east_west = 0;
    } else if (lon_difference <= close){
        east_west = east_west;
    } else if (lon_difference <= mid){
        east_west = east_west * 2;
    } else{
        east_west = east_west * 3;
    }

    
    return (int)(RAD_TO_DEG * atan2(north_south, east_west));
}

/// Convert an integer GPS coords to a floating point
#define DegD(i) (i * 1e-7)

static void drawNodeInfo(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    if (state->frameState == SELECTED){
        screen->debug_info()->drawFrameText(display, state, x, y);
        return;
    }
    // We only advance our nodeIndex if the frame # has changed - because
    // drawNodeInfo will be called repeatedly while the frame is shown
    if (state->currentFrame != prevFrame) {
        prevFrame = state->currentFrame;

        nodeIndex = (nodeIndex + 1) % nodeDB.getNumNodes();
        NodeInfo *n = nodeDB.getNodeByIndex(nodeIndex);
        if (n->num == nodeDB.getNodeNum()) {
            // Don't show our node, just skip to next
            nodeIndex = (nodeIndex + 1) % nodeDB.getNumNodes();
            n = nodeDB.getNodeByIndex(nodeIndex);
        }
        displayedNodeNum = n->num;
    }

    NodeInfo *node = nodeDB.getNodeByIndex(nodeIndex);

    display->setFont(FONT_SMALL);

    // The coordinates define the left starting point of the text
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    const char* user = node->has_user ? node->user.long_name : "Unknown Name";
    char username[40];
    sprintf(username, "%s", user); 

    static char signalStr[20];
    snprintf(signalStr, sizeof(signalStr), "Signal: %d%%", clamp((int)((node->snr + 10) * 5), 0, 100));

    uint32_t agoSecs = sinceLastSeen(node);
    static char lastStr[20];
    if (agoSecs < 120) // last 2 mins?
        snprintf(lastStr, sizeof(lastStr), "%u seconds ago", agoSecs);
    else if (agoSecs < 120 * 60) // last 2 hrs
        snprintf(lastStr, sizeof(lastStr), "%u minutes ago", agoSecs / 60);
    else {
        uint32_t hours_in_month = 730;

        // Only show hours ago if it's been less than 6 months. Otherwise, we may have bad
        //   data.
        if ((agoSecs / 60 / 60) < (hours_in_month * 6)) {
            snprintf(lastStr, sizeof(lastStr), "%u hours ago", agoSecs / 60 / 60);
        } else {
            snprintf(lastStr, sizeof(lastStr), "A long time ago");
        }
    }

    static char distStr[20];
    strcpy(distStr, "? m"); // might not have location data
    NodeInfo *ourNode = nodeDB.getNode(nodeDB.getNodeNum());
    MeshPacket &mp = devicestate.rx_text_message;

    // coordinates for the center of the compass/circle
    bool hasNodeHeading = false;
    char headingbuf[25]; 
    float bearingToOther = -1;
    int16_t degree = 0;
    // float headingRadian = -1;
    if (ourNode && hasPosition(ourNode)) {
        Position &op = ourNode->position;
        
        int16_t myHeading = heading->getHeading();
        screen->displayingHeading = true;
        if (hasPosition(node)) {
            // display direction toward node
            hasNodeHeading = true;
            
            Position &p = node->position;
            float d =
                GeoCoord::latLongToMeter(DegD(p.latitude_i), DegD(p.longitude_i), DegD(op.latitude_i), DegD(op.longitude_i));
            bearingToOther = GeoCoord::bearing(DegD(p.latitude_i), DegD(p.longitude_i), DegD(op.latitude_i), DegD(op.longitude_i));
            int bearingToOtherDeg = bearingToOther * RAD_TO_DEG;
            if (d < 2000)
                snprintf(distStr, sizeof(distStr), "%.1fm %s", d, TinyGPSPlus::cardinal(bearingToOtherDeg));
            else
                snprintf(distStr, sizeof(distStr), "%.1fkm %s", d / 1000, TinyGPSPlus::cardinal(bearingToOtherDeg));
            
            // int simpleBearingToOther = simpleBearingTo(DegD(p.latitude_i), DegD(p.longitude_i), DegD(op.latitude_i), DegD(op.longitude_i));
            // int altSimpleBearingToOther = altsimpleBearingTo(DegD(p.latitude_i), DegD(p.longitude_i), DegD(op.latitude_i), DegD(op.longitude_i));
            
            // headingRadian = (float)bearingToOther + (float)myHeading * DEG_TO_RAD;
            // ,
            degree = myHeading - bearingToOtherDeg;
            sprintf(headingbuf, "%s A-%d R-%d",  
                    (int)(bearingToOtherDeg),
                    degree
            );
            
            degree = myHeading - bearingToOtherDeg;
            uint16_t radius = 110;
            uint16_t thickness = 10;
            uint16_t width = 5;
            uint16_t x1 = (float)radius * cos(bearingToOther) + SCREEN_WIDTH / 2;
            uint16_t y1 = (float)radius * sin(bearingToOther) + SCREEN_HEIGHT /2;
            display->setFont(FONT_SMALL);
            display->setTextAlignment(TEXT_ALIGN_CENTER);
            display->drawString(x1, y1, String("A"));
            degree =  bearingToOtherDeg - myHeading;
            display->drawArc(SCREEN_WIDTH/2, 
                                SCREEN_HEIGHT/2, 
                                radius - thickness /2 , 
                                radius + thickness /2, 
                                degree - width, 
                                degree + width);

        }
        uint16_t radius = 100;
        uint16_t x = (float)radius * cos(((float)myHeading - 90.0) * DEG_TO_RAD) + SCREEN_WIDTH / 2;
        uint16_t y = (float)radius * sin(((float)myHeading - 90.0) * DEG_TO_RAD) + SCREEN_HEIGHT /2;
        display->setFont(FONT_SMALL);
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->drawString(x, y, String("N"));
    }
    
    if (!hasNodeHeading){
        sprintf(headingbuf, "No heading");
    }

    display->setFont(FONT_SMALL);
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    int lines = 6;
    NodeInfo *msg_node = nodeDB.getNode(getFrom(&mp));
    static char tempBuf[30];
    memset(tempBuf, '\0', 30);
    if (ourNode->num == msg_node->num){
        // Demo for drawStringMaxWidth:
        // with the third parameter you can define the width after which words will
        // be wrapped. Currently only spaces and "-" are allowed for wrapping
        display->setTextAlignment(TEXT_ALIGN_CENTER);
        display->setFont(FONT_SMALL);

        // the max length of this buffer is much longer than we can possibly print
        snprintf(tempBuf, sizeof(tempBuf), "%s", mp.decoded.payload.bytes);      
    }
    char *to_display[] = {
        username,
        distStr,
        lastStr,
        signalStr,
        headingbuf,
        tempBuf
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
    

    
}

void screen_header(OLEDDisplay *display,  OLEDDisplayUiState* state)
{
    // Datetime
    uint32_t rtc_sec = getValidTime(RTCQuality::RTCQualityDevice);
    char timebuf[20];
    if (rtc_sec > 0) {
        long hms = rtc_sec % SEC_PER_DAY;
        // hms += tz.tz_dsttime * SEC_PER_HOUR;
        // hms -= tz.tz_minuteswest * SEC_PER_MIN;
        // mod `hms` to ensure in positive range of [0...SEC_PER_DAY)
        hms = (hms + SEC_PER_DAY) % SEC_PER_DAY;

        // Tear apart hms into h:m:s
        // Change me for timezones
        int hour = hms / SEC_PER_HOUR + 11;
        if (hour >= 24){
            hour -= 24;
        }
        int min = (hms % SEC_PER_HOUR) / SEC_PER_MIN;
        sprintf(timebuf, "%02d:%02d", hour, min);
        display->setFont(FONT_SMALL);
    }
    display->setTextAlignment(TEXT_ALIGN_CENTER);
    display->drawString(display->getWidth()/2, 10, timebuf);
    
    if (strlen(ipaddress) > 0) {
        display->drawString(display->getWidth()/2, 20, ipaddress);
    } else {
         if (powerStatus->getHasBattery())
            drawBattery(display,display->getWidth()/2, 20, imgBattery, powerStatus);
        // else if (powerStatus->knowsUSB())
        //     display->drawFastImage(display->getWidth()/2+20, 20 + 2, 16, 8, powerStatus->getHasUSB() ? imgUSB : imgPower);
        
        //drawBattery(display, display->getWidth()/2-20, 20, imgBattery, powerStatus);
        if (gpsStatus->getHasLock())
            display->drawFastImage(display->getWidth()/2-30, 20, 8, 8, imgSatellite);
    }

    
    // Satellite count
    // display->setTextAlignment(TEXT_ALIGN_RIGHT);
    // char buffer[10];
    // display->drawString(display->getWidth() - SATELLITE_IMAGE_WIDTH - 4, 2, itoa(gps.satellites.value(), buffer, 10));
    // display->drawXbm(display->getWidth() - SATELLITE_IMAGE_WIDTH, 0, SATELLITE_IMAGE_WIDTH, SATELLITE_IMAGE_HEIGHT, SATELLITE_IMAGE);
}

Screen::Screen() : OSThread("Screen"), cmdQueue(32), dispdev(), ui(&dispdev)
{
    cmdQueue.setReader(this);
}
// #endif
/**
 * Prepare the display for the unit going to the lowest power mode possible.  Most screens will just
 * poweroff, but eink screens will show a "I'm sleeping" graphic, possibly with a QR code
 */
void Screen::doDeepSleep()
{
    setOn(false);
}

void Screen::handleSetOn(bool on)
{
    if (!useDisplay)
        return;

    if (on != screenOn) {
        if (on) {
            DEBUG_MSG("Turning on screen\n");
            dispdev.displayOn();
            dispdev.setBrightness(brightness);
            enabled = true;
            setInterval(0); // Draw ASAP
            runASAP = true;
        } else {
            DEBUG_MSG("Turning off screen\n");
            dispdev.displayOff();
            enabled = false;
        }
        screenOn = on;
    }
}

void Screen::setup()
{
    // We don't set useDisplay until setup() is called, because some boards have a declaration of this object but the device
    // is never found when probing i2c and therefore we don't call setup and never want to do (invalid) accesses to this device.
    useDisplay = true;

    // Load OEM config from Proto file if existent
    loadProto(oemConfigFile, OEMStore_size, sizeof(oemConfigFile), OEMStore_fields, &oemStore);

    // Initialising the UI will init the display too.
    ui.init();

    displayWidth = dispdev.width();
    displayHeight = dispdev.height();

    ui.setTimePerTransition(SCREEN_TRANSITION_MSECS);

    ui.setIndicatorPosition(BOTTOM);
    // Defines where the first frame is located in the bar.
    ui.setIndicatorDirection(LEFT_RIGHT);
    ui.setFrameAnimation(SLIDE_UP);
    // Don't show the page swipe dots while in boot screen.
    ui.disableAllIndicators();
    // Store a pointer to Screen so we can get to it from static functions.
    ui.getUiState()->userData = this;

    // Set the utf8 conversion function
    dispdev.setFontTableLookupFunction(customFontTableLookup);

    // Add frames.
    static FrameCallback bootFrames[] = {drawBootScreen};
    static const int bootFrameCount = sizeof(bootFrames) / sizeof(bootFrames[0]);
    ui.setFrames(bootFrames, bootFrameCount);
    // No overlays.
    static OverlayCallback overlays[] = {screen_header};
    ui.setOverlays(overlays, 1);

    // Require presses to switch between frames.
    ui.disableAutoTransition();

    // Set up a log buffer with 3 lines, 32 chars each.
    dispdev.setLogBuffer(3, 32);

#ifdef SCREEN_MIRROR
    dispdev.mirrorScreen();
#elif defined(SCREEN_FLIP_VERTICALLY)
    dispdev.flipScreenVertically();
#endif

    // Get our hardware ID
    uint8_t dmac[6];
    getMacAddr(dmac);
    sprintf(ourId, "%02x%02x", dmac[4], dmac[5]);

    // Turn on the display.
    handleSetOn(true);

    // On some ssd1306 clones, the first draw command is discarded, so draw it
    // twice initially.
    ui.update();
    ui.update();
    serialSinceMsec = millis();

    // Subscribe to status updates
    powerStatusObserver.observe(&powerStatus->onNewStatus);
    gpsStatusObserver.observe(&gpsStatus->onNewStatus);
    nodeStatusObserver.observe(&nodeStatus->onNewStatus);
    if (textMessageModule)
        textMessageObserver.observe(textMessageModule);

    // Modules can notify screen about refresh
    MeshModule::observeUIEvents(&uiFrameEventObserver);
}

void Screen::forceDisplay()
{
    canvas->flush();
}

static uint32_t lastScreenTransition;

int32_t Screen::runOnce()
{
    // If we don't have a screen, don't ever spend any CPU for us.
    if (!useDisplay) {
        enabled = false;
        return RUN_SAME;
    }

    // Show boot screen for first 3 seconds, then switch to normal operation.
    // serialSinceMsec adjusts for additional serial wait time during nRF52 bootup
    static bool showingBootScreen = true;
    if (showingBootScreen && (millis() > (3000 + serialSinceMsec))) {
        DEBUG_MSG("Done with boot screen...\n");
        stopBootScreen();
        showingBootScreen = false;
    }

    // If we have an OEM Boot screen, toggle after 2,5 seconds
    if (strlen(oemStore.oem_text) > 0) {
        static bool showingOEMBootScreen = true;
        if (showingOEMBootScreen && (millis() > (2500 + serialSinceMsec))) {
            DEBUG_MSG("Switch to OEM screen...\n");
            // Change frames.
            static FrameCallback bootOEMFrames[] = {drawOEMBootScreen};
            static const int bootOEMFrameCount = sizeof(bootOEMFrames) / sizeof(bootOEMFrames[0]);
            ui.setFrames(bootOEMFrames, bootOEMFrameCount);
            ui.update();
            ui.update();
            showingOEMBootScreen = false;
        }
    }

#ifndef DISABLE_WELCOME_UNSET
    if (showingNormalScreen && config.lora.region == Config_LoRaConfig_RegionCode_Unset) {
        setWelcomeFrames();
    }
#endif

    // Process incoming commands.
    for (;;) {
        ScreenCmd cmd;
        if (!cmdQueue.dequeue(&cmd, 0)) {
            break;
        }
        switch (cmd.cmd) {
        case Cmd::SET_ON:
            handleSetOn(true);
            break;
        case Cmd::SET_OFF:
            handleSetOn(false);
            break;
        case Cmd::ON_PRESS_UP_SINGLE:
            handleOnPress(PRESS_UP);
            break;
        case Cmd::ON_PRESS_UP_ALT:
            handleOnPress(PRESS_UP);
            break;
        case Cmd::ON_PRESS_UP_LONG:
            handleLongPress(PRESS_UP);
            break;
        case Cmd::ON_PRESS_DOWN_SINGLE:
            handleOnPress(PRESS_DOWN);
            break;
        case Cmd::ON_PRESS_DOWN_ALT:
            handleOnPress(PRESS_DOWN);
            break;
        case Cmd::ON_PRESS_DOWN_LONG:
            handleLongPress(PRESS_DOWN);
            break;
        case Cmd::START_BLUETOOTH_PIN_SCREEN:
            handleStartBluetoothPinScreen(cmd.bluetooth_pin);
            break;
        case Cmd::START_FIRMWARE_UPDATE_SCREEN:
            handleStartFirmwareUpdateScreen();
            break;
        case Cmd::STOP_BLUETOOTH_PIN_SCREEN:
        case Cmd::STOP_BOOT_SCREEN:
            setFrames();
            break;
        case Cmd::PRINT:
            handlePrint(cmd.print_text);
            free(cmd.print_text);
            break;
        case Cmd::START_SHUTDOWN_SCREEN:
            handleShutdownScreen();
            break;
        default:
            DEBUG_MSG("BUG: invalid cmd\n");
        }
    }

    if (!screenOn) { // If we didn't just wake and the screen is still off, then
                     // stop updating until it is on again
        enabled = false;
        return 0;
    }

    // this must be before the frameState == FIXED check, because we always
    // want to draw at least one FIXED frame before doing forceDisplay
    ui.update();

    // Switch to a low framerate (to save CPU) when we are not in transition
    // but we should only call setTargetFPS when framestate changes, because
    // otherwise that breaks animations.
    if (targetFramerate != IDLE_FRAMERATE && (ui.getUiState()->frameState == FIXED || ui.getUiState()->frameState == SELECTED) && !displayingHeading) {
        // oldFrameState = ui.getUiState()->frameState;
        DEBUG_MSG("Setting idle framerate\n");
        targetFramerate = IDLE_FRAMERATE;

        #ifndef NO_ESP32
                setCPUFast(false); // Turn up the CPU to improve screen animations
        #endif

        ui.setTargetFPS(targetFramerate);
        forceDisplay();
    }
    displayingHeading = false;
    // While showing the bootscreen or Bluetooth pair screen all of our
    // standard screen switching is stopped.
    if (showingNormalScreen) {
        // standard screen loop handling here
        if (config.display.auto_screen_carousel_secs > 0 &&
            (millis() - lastScreenTransition) > (config.display.auto_screen_carousel_secs * 1000)) {
            DEBUG_MSG("LastScreenTransition exceeded %ums transitioning to next frame\n", (millis() - lastScreenTransition));
            handleOnPress(PRESS_DOWN);
        }
    }

    // DEBUG_MSG("want fps %d, fixed=%d\n", targetFramerate,
    // ui.getUiState()->frameState); If we are scrolling we need to be called
    // soon, otherwise just 1 fps (to save CPU) We also ask to be called twice
    // as fast as we really need so that any rounding errors still result with
    // the correct framerate
    return (1000 / targetFramerate);
}

void Screen::drawDebugInfoTrampoline(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    Screen *screen2 = reinterpret_cast<Screen *>(state->userData);
    screen2->debugInfo.drawFrame(display, state, x, y);
}

void Screen::drawDebugInfoSettingsTrampoline(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    Screen *screen2 = reinterpret_cast<Screen *>(state->userData);
    screen2->debugInfo.drawFrameSettings(display, state, x, y);
}

void Screen::drawInfoTrampoline(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    Screen *screen2 = reinterpret_cast<Screen *>(state->userData);
    screen2->debugInfo.drawFrameInfo(display, state, x, y);
}

void Screen::drawFakeNodeTrampoline(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    Screen *screen2 = reinterpret_cast<Screen *>(state->userData);
    screen2->debugInfo.drawFrameTestNode(display, state, x, y);
}


void Screen::drawDebugInfoWiFiTrampoline(OLEDDisplay *display, OLEDDisplayUiState *state, int16_t x, int16_t y)
{
    //Screen *screen2 = reinterpret_cast<Screen *>(state->userData);
    //screen2->debugInfo.drawFrameWiFi(display, state, x, y);
}

/* show a message that the SSL cert is being built
 * it is expected that this will be used during the boot phase */
void Screen::setSSLFrames()
{
    if (address_found) {
        // DEBUG_MSG("showing SSL frames\n");
        static FrameCallback sslFrames[] = {drawSSLScreen};
        ui.setFrames(sslFrames, 1);
        ui.update();
    }
}

/* show a message that the SSL cert is being built
 * it is expected that this will be used during the boot phase */
void Screen::setWelcomeFrames()
{
    if (address_found) {
        // DEBUG_MSG("showing Welcome frames\n");
        ui.disableAllIndicators();

        static FrameCallback welcomeFrames[] = {drawWelcomeScreen};
        ui.setFrames(welcomeFrames, 1);
        ui.update();
    }
}

// restore our regular frame list
void Screen::setFrames()
{
    DEBUG_MSG("showing standard frames\n");
    showingNormalScreen = true;

    moduleFrames = MeshModule::GetMeshModulesWithUIFrames();
    DEBUG_MSG("Showing %d module frames\n", moduleFrames.size());
    int totalFrameCount = MAX_NUM_NODES + NUM_EXTRA_FRAMES + moduleFrames.size();
    DEBUG_MSG("Total frame count: %d\n", totalFrameCount);

    // We don't show the node info our our node (if we have it yet - we should)
    size_t numnodes = nodeStatus->getNumTotal();
    if (numnodes > 0)
        numnodes--;

    size_t numframes = 0;

    // put all of the module frames first.
    // this is a little bit of a dirty hack; since we're going to call
    // the same drawModuleFrame handler here for all of these module frames
    // and then we'll just assume that the state->currentFrame value
    // is the same offset into the moduleFrames vector
    // so that we can invoke the module's callback
    for (auto i = moduleFrames.begin(); i != moduleFrames.end(); ++i) {
        normalFrames[numframes++] = drawModuleFrame;
    }

    DEBUG_MSG("Added modules.  numframes: %d\n", numframes);

    // If we have a critical fault, show it first
    if (myNodeInfo.error_code)
        normalFrames[numframes++] = drawCriticalFaultFrame;

    // If we have a text message - show it next, unless it's a phone message and we aren't using any special modules
    if (devicestate.has_rx_text_message && shouldDrawMessage(&devicestate.rx_text_message)) {
        normalFrames[numframes++] = drawTextMessageFrame;
    }

    // then all the nodes
    // We only show a few nodes in our scrolling list - because meshes with many nodes would have too many screens
    size_t numToShow = min(numnodes, 4U);
    for (size_t i = 0; i < numToShow; i++)
        normalFrames[numframes++] = drawNodeInfo;

    // then the debug info
    //
    // Since frames are basic function pointers, we have to use a helper to
    // call a method on debugInfo object.
    //normalFrames[numframes++] = &Screen::drawDebugInfoTrampoline;

    // call a method on debugInfoScreen object (for more details)
    normalFrames[numframes++] = &Screen::drawDebugInfoSettingsTrampoline;
    normalFrames[numframes++] = &Screen::drawInfoTrampoline;
    //normalFrames[numframes++] = &Screen::drawFakeNodeTrampoline;
    #ifndef NO_ESP32
    #ifdef WANT_WIFI
        if (isWifiAvailable()) {
            // call a method on debugInfoScreen object (for more details)
            normalFrames[numframes++] = &Screen::drawDebugInfoWiFiTrampoline;
        }
    #endif
    #endif

    DEBUG_MSG("Finished building frames. numframes: %d\n", numframes);

    ui.setFrames(normalFrames, numframes);
    ui.enableAllIndicators();

    prevFrame = -1; // Force drawNodeInfo to pick a new node (because our list
                    // just changed)

    setFastFramerate(); // Draw ASAP
}

void Screen::handleStartBluetoothPinScreen(uint32_t pin)
{
    DEBUG_MSG("showing bluetooth screen\n");
    showingNormalScreen = false;

    static FrameCallback btFrames[] = {drawFrameBluetooth};

    snprintf(btPIN, sizeof(btPIN), "%06u", pin);

    ui.disableAllIndicators();
    ui.setFrames(btFrames, 1);
    setFastFramerate();
}

void Screen::handleShutdownScreen()
{
    DEBUG_MSG("showing shutdown screen\n");
    showingNormalScreen = false;

    static FrameCallback shutdownFrames[] = {drawFrameShutdown};

    ui.disableAllIndicators();
    ui.setFrames(shutdownFrames, 1);
    setFastFramerate();
}

void Screen::handleStartFirmwareUpdateScreen()
{
    DEBUG_MSG("showing firmware screen\n");
    showingNormalScreen = false;

    static FrameCallback btFrames[] = {drawFrameFirmware};

    ui.disableAllIndicators();
    ui.setFrames(btFrames, 1);
    setFastFramerate();
}

void Screen::blink()
{
    setFastFramerate();
    uint8_t count = 10;
    dispdev.setBrightness(254);
    while (count > 0) {
        dispdev.fillRect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
        dispdev.display();
        delay(50);
        dispdev.clear();
        dispdev.display();
        delay(50);
        count = count - 1;
    }
    dispdev.setBrightness(brightness);
}

void Screen::handlePrint(const char *text)
{
    // the string passed into us probably has a newline, but that would confuse the logging system
    // so strip it
    DEBUG_MSG("Screen: %.*s\n", strlen(text) - 1, text);
    if (!useDisplay || !showingNormalScreen)
        return;

    dispdev.print(text);
}

void Screen::handleOnPress(uint8_t dir)
{
    if (!screenOn){
        handleSetOn(true);
        return;
    }
    if (ui.getUiState()->frameState == FIXED) {
        if (dir == PRESS_DOWN){
            ui.nextFrame();
        } else {
            ui.previousFrame();
        }
        DEBUG_MSG("Setting LastScreenTransition\n");
        lastScreenTransition = millis();
    } else if (ui.getUiState()->frameState == SELECTED){
        DEBUG_MSG("Changing index\n");
        if (dir == PRESS_DOWN){
            ui.nextIndex();
        } else {
            ui.previousIndex();
        }
        lastScreenTransition = millis();
    }
    setFastFramerate();
}

void Screen::handleLongPress(uint8_t dir)
{
    OLEDDisplayUiState* state = ui.getUiState();
    if (!screenOn){
        handleSetOn(true);
        return;
    }
    if (state->frameState == FIXED) {
        if (dir == PRESS_DOWN){
            // Transition to selected
            state->frameState = SELECTED;
            state->currentIndex = 0;
            DEBUG_MSG("Switching to selected");
        
        } else {
            // Power down
            DEBUG_MSG("Powerdown display");
            handleSetOn(false);
            
        }
    } else if (state->frameState == SELECTED){
        if (dir == PRESS_DOWN){
            // Do some Select function
            // Enable/disable, word select using letters, menu select from predefined options 
            if (state->currentSetting == -1){
                // We are trying to enter the modification screen for a setting
                state->currentSetting = state->currentIndex;
                state->currentIndex = 0;
                DEBUG_MSG("Selecting a setting");
            } else {
                // We are trying to advance through the setting ie by selecting and saving
                // or chosing the next character
                DEBUG_MSG("Advancing in a setting");
                state->action = SELECT;
            }
        } else {
            if (state->currentSetting == -1){
                // No setting is selected to modify so we can transition to the main screen
                state->frameState = FIXED;
                state->currentIndex = 0;
                DEBUG_MSG("Going back to regular page");
            } else {
                // We are exiting the current setting so need to save it the frame will organise the exit
                state->action = SAVE;
                DEBUG_MSG("Saving setting");
            }

        }
    }
    setFastFramerate();
}

#ifndef SCREEN_TRANSITION_FRAMERATE
#define SCREEN_TRANSITION_FRAMERATE 30 // fps
#endif

void Screen::setFastFramerate()
{
    DEBUG_MSG("Setting fast framerate\n");

    // We are about to start a transition so speed up fps
    targetFramerate = SCREEN_TRANSITION_FRAMERATE;

#ifndef NO_ESP32
    setCPUFast(true); // Turn up the CPU to improve screen animations
#endif

    ui.setTargetFPS(targetFramerate);
    setInterval(0); // redraw ASAP
    runASAP = true;
}


// adjust Brightness cycle trough 1 to 254 as long as attachDuringLongPress is true
void Screen::adjustBrightness()
{
    if (brightness == 254) {
        brightness = 0;
    } else {
        brightness += 10;
    }
    int width = brightness / (254.00 / (SCREEN_WIDTH/2));
    dispdev.drawRect(SCREEN_WIDTH/4, 30, SCREEN_WIDTH/2, 4);
    dispdev.fillRect(SCREEN_WIDTH/4, 31, width, 2);
    dispdev.display();
    dispdev.setBrightness(brightness);
}

int Screen::handleStatusUpdate(const meshtastic::Status *arg)
{
    // DEBUG_MSG("Screen got status update %d\n", arg->getStatusType());
    switch (arg->getStatusType()) {
    case STATUS_TYPE_NODE:
        if (showingNormalScreen && nodeStatus->getLastNumTotal() != nodeStatus->getNumTotal()) {
            setFrames(); // Regen the list of screens
        }
        nodeDB.updateGUI = false;
        break;
    }

    return 0;
}

int Screen::handleTextMessage(const MeshPacket *packet)
{
    if (showingNormalScreen) {
        setFrames(); // Regen the list of screens (will show new text message)
    }

    return 0;
}

int Screen::handleUIFrameEvent(const UIFrameEvent *event)
{
    if (showingNormalScreen) {
        if (event->frameChanged) {
            setFrames(); // Regen the list of screens (will show new text message)
        } else if (event->needRedraw) {
            setFastFramerate();
            // TODO: We might also want switch to corresponding frame,
            //       but we don't know the exact frame number.
            // ui.switchToFrame(0);
        }
    }

    return 0;
}

} // namespace graphics
#endif // NO_SCREEN