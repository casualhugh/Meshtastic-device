#pragma once

#include <map>
#include "GPSStatus.h"
#include "NodeStatus.h"
#include "PowerStatus.h"
#include "Screen.h"
#include "mesh/generated/telemetry.pb.h"

extern uint8_t screen_found;
extern uint8_t screen_model;
extern uint8_t rtc_found;
extern int forceSoftAP;
extern bool isCharging;
extern bool isUSBPowered;

extern uint8_t nodeTelemetrySensorsMap[12];

// Global Screen singleton.
extern graphics::Screen *screen;
// extern Observable<meshtastic::PowerStatus> newPowerStatus; //TODO: move this to main-esp32.cpp somehow or a helper class

// extern meshtastic::PowerStatus *powerStatus;
// extern meshtastic::GPSStatus *gpsStatus;
// extern meshtastic::NodeStatusHandler *nodeStatusHandler;

// Return a human readable string of the form "Meshtastic_ab13"
const char *getDeviceName();

extern uint32_t timeLastPowered;

extern uint32_t rebootAtMsec;
extern uint32_t shutdownAtMsec;

extern uint32_t serialSinceMsec;

// If a thread does something that might need for it to be rescheduled ASAP it can set this flag
// This will supress the current delay and instead try to run ASAP.
extern bool runASAP;

void esp32Setup(), esp32Loop(), clearBonds();
