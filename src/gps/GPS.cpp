#include "GPS.h"
#include "NodeDB.h"
#include "RTC.h"
#include "configuration.h"
#include "sleep.h"
#include <assert.h>

// If we have a serial GPS port it will not be null
HardwareSerial _serial_gps_real(GPS_SERIAL_NUM);
HardwareSerial *GPS::_serial_gps = &_serial_gps_real;

GPS *gps;

/// Multiple GPS instances might use the same serial port (in sequence), but we can
/// only init that port once.
static bool didSerialInit;

bool GPS::getACK(uint8_t c, uint8_t i) {
  uint8_t b;
  uint8_t ack = 0;
  const uint8_t ackP[2] = {c, i};
  uint8_t buf[10] = {0xB5, 0x62, 0x05, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00};
  unsigned long startTime = millis();

  for (int j = 2; j < 6; j++) {
    buf[8] += buf[j];
    buf[9] += buf[8];
  }

  for (int j = 0; j < 2; j++) {
    buf[6 + j] = ackP[j];
    buf[8] += buf[6 + j];
    buf[9] += buf[8];
  }

  while (1) {
    if (ack > 9) {
      return true;
    }
    if (millis() - startTime > 1000) {
      return false;
    }
    if (_serial_gps->available()) {
      b = _serial_gps->read();
      if (b == buf[ack]) {
        ack++;
      }
      else {
        ack = 0;
      }
    }
  }
}

bool GPS::setupGPS()
{
    if (_serial_gps && !didSerialInit) {
        didSerialInit = true;
        _serial_gps->begin(GPS_BAUDRATE, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
        _serial_gps->setRxBufferSize(2048); // the default is 256

    }

    return true;
}

bool GPS::setup()
{
    // Master power for the GPS
    #ifdef PIN_GPS_EN
        digitalWrite(PIN_GPS_EN, 1);
        pinMode(PIN_GPS_EN, OUTPUT);
    #endif

    #ifdef PIN_GPS_RESET
        digitalWrite(PIN_GPS_RESET, 1); // assert for 10ms
        pinMode(PIN_GPS_RESET, OUTPUT);
        delay(10);
        digitalWrite(PIN_GPS_RESET, 0);
    #endif

    setAwake(true); // Wake GPS power before doing any init
    bool ok = setupGPS();

    if (ok) {
        notifySleepObserver.observe(&notifySleep);
        notifyDeepSleepObserver.observe(&notifyDeepSleep);
    }

    return ok;
}

GPS::~GPS()
{
    // we really should unregister our sleep observer
    notifySleepObserver.unobserve(&notifySleep);
    notifyDeepSleepObserver.unobserve(&notifyDeepSleep);
}

bool GPS::hasLock()
{
    return hasValidLocation;
}

bool GPS::hasFlow()
{
    return hasGPS;
}

// Allow defining the polarity of the WAKE output.  default is active high
#ifndef GPS_WAKE_ACTIVE
#define GPS_WAKE_ACTIVE 1
#endif

void GPS::wake()
{
    #ifdef PIN_GPS_WAKE
        //digitalWrite(PIN_GPS_WAKE, GPS_WAKE_ACTIVE);
        //pinMode(PIN_GPS_WAKE, OUTPUT);
    #endif
    delay(10);
    //ss.println("$PMTK353,1,1,1,0,0*2A");
}

void GPS::sleep()
{
#ifdef PIN_GPS_WAKE
    //digitalWrite(PIN_GPS_WAKE, GPS_WAKE_ACTIVE ? 0 : 1);
    //pinMode(PIN_GPS_WAKE, OUTPUT);
    //ss.println("$PMTK161,0*28"); // Standby
    //BACKUP ss.println("$PMTK225,4*2F"0;
#endif
}

/// Record that we have a GPS
void GPS::setConnected()
{
    if (!hasGPS) {
        hasGPS = true;
        shouldPublish = true;
    }
}

void GPS::setNumSatellites(uint8_t n)
{
    if (n != numSatellites) {
        numSatellites = n;
        shouldPublish = true;
    }
}

/**
 * Switch the GPS into a mode where we are actively looking for a lock, or alternatively switch GPS into a low power mode
 *
 * calls sleep/wake
 */
void GPS::setAwake(bool on)
{
    if (!wakeAllowed && on) {
        DEBUG_MSG("Inhibiting because !wakeAllowed\n");
        on = false;
    }

    if (isAwake != on) {
        DEBUG_MSG("WANT GPS=%d\n", on);
        if (on) {
            lastWakeStartMsec = millis();
            wake();
        } else {
            lastSleepStartMsec = millis();
            sleep();
        }

        isAwake = on;
    }
}

/** Get how long we should stay looking for each aquisition in msecs
 */
uint32_t GPS::getWakeTime() const
{
    uint32_t t = config.position.gps_attempt_time;

    if (t == UINT32_MAX)
        return t; // already maxint

    if (t == 0)
        t = (config.device.role == Config_DeviceConfig_Role_Router)
                ? 5 * 60
                : 15 * 60; // Allow up to 15 mins for each attempt (probably will be much
                           // less if we can find sats) or less if a router

    t *= 1000; // msecs

    return t;
}

/** Get how long we should sleep between aqusition attempts in msecs
 */
uint32_t GPS::getSleepTime() const
{
    uint32_t t = config.position.gps_update_interval;
    bool gps_disabled = config.position.gps_disabled;

    if (gps_disabled)
        t = UINT32_MAX; // Sleep forever now

    if (t == UINT32_MAX)
        return t; // already maxint

    if (t == 0) // default - unset in preferences
        t = (config.device.role == Config_DeviceConfig_Role_Router) ? 24 * 60 * 60
                                                                                   : 2 * 60; // 2 mins or once per day for routers

    t *= 1000;

    return t;
}

void GPS::publishUpdate()
{
    if (shouldPublish) {
        shouldPublish = false;

        // In debug logs, identify position by @timestamp:stage (stage 2 = publish)
        DEBUG_MSG("publishing pos@%x:2, hasVal=%d, GPSlock=%d\n", p.pos_timestamp, hasValidLocation, hasLock());

        // Notify any status instances that are observing us
        const meshtastic::GPSStatus status = meshtastic::GPSStatus(hasValidLocation, isConnected(), p);
        newStatus.notifyObservers(&status);
    }
}

int32_t GPS::runOnce()
{
    if (whileIdle()) {
        // if we have received valid NMEA claim we are connected
        setConnected();
    } else {
    }

    // If we are overdue for an update, turn on the GPS and at least publish the current status
    uint32_t now = millis();

    auto sleepTime = getSleepTime();
    if (!isAwake && sleepTime != UINT32_MAX && (now - lastSleepStartMsec) > sleepTime) {
        // We now want to be awake - so wake up the GPS
        setAwake(true);
    }

    // While we are awake
    if (isAwake) {
        // DEBUG_MSG("looking for location\n");
        if ((now - lastWhileActiveMsec) > 5000) {
            lastWhileActiveMsec = now;
            whileActive();
        }

        // If we've already set time from the GPS, no need to ask the GPS
        bool gotTime = (getRTCQuality() >= RTCQualityGPS);
        if (!gotTime && lookForTime()) { // Note: we count on this && short-circuiting and not resetting the RTC time
            gotTime = true;
            shouldPublish = true;
        }

        bool gotLoc = lookForLocation();
        if (gotLoc && !hasValidLocation) { // declare that we have location ASAP
            DEBUG_MSG("hasValidLocation RISING EDGE\n");
            hasValidLocation = true;
            shouldPublish = true;
        }

        // We've been awake too long - force sleep
        now = millis();
        auto wakeTime = getWakeTime();
        bool tooLong = wakeTime != UINT32_MAX && (now - lastWakeStartMsec) > wakeTime;

        // Once we get a location we no longer desperately want an update
        // DEBUG_MSG("gotLoc %d, tooLong %d, gotTime %d\n", gotLoc, tooLong, gotTime);
        if ((gotLoc && gotTime) || tooLong) {

            if (tooLong) {
                // we didn't get a location during this ack window, therefore declare loss of lock
                if (hasValidLocation) {
                    DEBUG_MSG("hasValidLocation FALLING EDGE (last read: %d)\n", gotLoc);
                }
                p = Position_init_default;
                hasValidLocation = false;
            }

            setAwake(false);
            shouldPublish = true; // publish our update for this just finished acquisition window
        }
    }

    // If state has changed do a publish
    publishUpdate();

    // 9600bps is approx 1 byte per msec, so considering our buffer size we never need to wake more often than 200ms
    // if not awake we can run super infrquently (once every 5 secs?) to see if we need to wake.
    return isAwake ? GPS_THREAD_INTERVAL : 5000;
}

void GPS::forceWake(bool on)
{
    if (on) {
        DEBUG_MSG("Allowing GPS lock\n");
        // lastSleepStartMsec = 0; // Force an update ASAP
        wakeAllowed = true;
    } else {
        wakeAllowed = false;

        // Note: if the gps was already awake, we DO NOT shut it down, because we want to allow it to complete its lock
        // attempt even if we are in light sleep.  Once the attempt succeeds (or times out) we'll then shut it down.
        // setAwake(false);
    }
}

/// Prepare the GPS for the cpu entering deep or light sleep, expect to be gone for at least 100s of msecs
int GPS::prepareSleep(void *unused)
{
    DEBUG_MSG("GPS prepare sleep!\n");
    forceWake(false);

    return 0;
}

/// Prepare the GPS for the cpu entering deep or light sleep, expect to be gone for at least 100s of msecs
int GPS::prepareDeepSleep(void *unused)
{
    DEBUG_MSG("GPS deep sleep!\n");

    // For deep sleep we also want abandon any lock attempts (because we want minimum power)
    setAwake(false);

    return 0;
}

#include "NMEAGPS.h"

GPS *createGps()
{

    if (GPS::_serial_gps) {
        // Some boards might have only the TX line from the GPS connected, in that case, we can't configure it at all.  Just
        // assume NMEA at 9600 baud.
        GPS *new_gps = new NMEAGPS();
        new_gps->setup();
        return new_gps;
    }
    return nullptr;
}
