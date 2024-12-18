#include "GPS.h"
#include "NodeDB.h"
#include "RTC.h"
#include "configuration.h"
#include "main.h" // pmu_found
#include "sleep.h"
#include "ubx.h"

#ifdef ARCH_PORTDUINO
#include "meshUtils.h"
#endif

#ifndef GPS_RESET_MODE
#define GPS_RESET_MODE HIGH
#endif

HardwareSerial _serial_gps_real(2);
HardwareSerial *GPS::_serial_gps = &_serial_gps_real;

GPS *gps = nullptr;

/// Multiple GPS instances might use the same serial port (in sequence), but we can
/// only init that port once.
static bool didSerialInit;

struct uBloxGnssModelInfo info;
uint8_t uBloxProtocolVersion;
#define GPS_SOL_EXPIRY_MS 5000 // in millis. give 1 second time to combine different sentences. NMEA Frequency isn't higher anyway
#define NMEA_MSG_GXGSA "GNGSA" // GSA message (GPGSA, GNGSA etc)

void GPS::UBXChecksum(uint8_t *message, size_t length)
{
    uint8_t CK_A = 0, CK_B = 0;

    // Calculate the checksum, starting from the CLASS field (which is message[2])
    for (size_t i = 2; i < length - 2; i++)
    {
        CK_A = (CK_A + message[i]) & 0xFF;
        CK_B = (CK_B + CK_A) & 0xFF;
    }

    // Place the calculated checksum values in the message
    message[length - 2] = CK_A;
    message[length - 1] = CK_B;
}

// Function to create a ublox packet for editing in memory
uint8_t GPS::makeUBXPacket(uint8_t class_id, uint8_t msg_id, uint8_t payload_size, const uint8_t *msg)
{
    // Construct the UBX packet
    UBXscratch[0] = 0xB5;         // header
    UBXscratch[1] = 0x62;         // header
    UBXscratch[2] = class_id;     // class
    UBXscratch[3] = msg_id;       // id
    UBXscratch[4] = payload_size; // length
    UBXscratch[5] = 0x00;

    UBXscratch[6 + payload_size] = 0x00; // CK_A
    UBXscratch[7 + payload_size] = 0x00; // CK_B

    for (int i = 0; i < payload_size; i++)
    {
        UBXscratch[6 + i] = pgm_read_byte(&msg[i]);
    }
    UBXChecksum(UBXscratch, (payload_size + 8));
    return (payload_size + 8);
}

GPS_RESPONSE GPS::getACK(const char *message, uint32_t waitMillis)
{
    uint8_t buffer[768] = {0};
    uint8_t b;
    int bytesRead = 0;
    uint32_t startTimeout = millis() + waitMillis;
    while (millis() < startTimeout)
    {
        if (_serial_gps->available())
        {
            b = _serial_gps->read();
#ifdef GPS_DEBUG
            LOG_DEBUG("%02X", (char *)buffer);
#endif
            buffer[bytesRead] = b;
            bytesRead++;
            if ((bytesRead == 767) || (b == '\r'))
            {
                if (strnstr((char *)buffer, message, bytesRead) != nullptr)
                {
#ifdef GPS_DEBUG
                    LOG_DEBUG("\r");
#endif
                    return GNSS_RESPONSE_OK;
                }
                else
                {
                    bytesRead = 0;
                }
            }
        }
    }
#ifdef GPS_DEBUG
    LOG_DEBUG("\n");
#endif
    return GNSS_RESPONSE_NONE;
}

GPS_RESPONSE GPS::getACK(uint8_t class_id, uint8_t msg_id, uint32_t waitMillis)
{
    uint8_t b;
    uint8_t ack = 0;
    const uint8_t ackP[2] = {class_id, msg_id};
    uint8_t buf[10] = {0xB5, 0x62, 0x05, 0x01, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00};
    uint32_t startTime = millis();
    const char frame_errors[] = "More than 100 frame errors";
    int sCounter = 0;

    for (int j = 2; j < 6; j++)
    {
        buf[8] += buf[j];
        buf[9] += buf[8];
    }

    for (int j = 0; j < 2; j++)
    {
        buf[6 + j] = ackP[j];
        buf[8] += buf[6 + j];
        buf[9] += buf[8];
    }

    while (millis() - startTime < waitMillis)
    {
        if (ack > 9)
        {
#ifdef GPS_DEBUG
            LOG_DEBUG("\n");
            LOG_INFO("Got ACK for class %02X message %02X in %d millis.\n", class_id, msg_id, millis() - startTime);
#endif
            return GNSS_RESPONSE_OK; // ACK received
        }
        if (_serial_gps->available())
        {
            b = _serial_gps->read();
            if (b == frame_errors[sCounter])
            {
                sCounter++;
                if (sCounter == 26)
                {
                    return GNSS_RESPONSE_FRAME_ERRORS;
                }
            }
            else
            {
                sCounter = 0;
            }
#ifdef GPS_DEBUG
            LOG_DEBUG("%02X", b);
#endif
            if (b == buf[ack])
            {
                ack++;
            }
            else
            {
                if (ack == 3 && b == 0x00)
                { // UBX-ACK-NAK message
#ifdef GPS_DEBUG
                    LOG_DEBUG("\n");
#endif
                    LOG_WARN("Got NAK for class %02X message %02X\n", class_id, msg_id);
                    return GNSS_RESPONSE_NAK; // NAK received
                }
                ack = 0; // Reset the acknowledgement counter
            }
        }
    }
#ifdef GPS_DEBUG
    LOG_DEBUG("\n");
    LOG_WARN("No response for class %02X message %02X\n", class_id, msg_id);
#endif
    return GNSS_RESPONSE_NONE; // No response received within timeout
}

/**
 * @brief
 * @note   New method, this method can wait for the specified class and message ID, and return the payload
 * @param  *buffer: The message buffer, if there is a response payload message, it will be returned through the buffer parameter
 * @param  size:    size of buffer
 * @param  requestedClass:  request class constant
 * @param  requestedID:     request message ID constant
 * @retval length of payload message
 */
int GPS::getACK(uint8_t *buffer, uint16_t size, uint8_t requestedClass, uint8_t requestedID, uint32_t waitMillis)
{
    uint16_t ubxFrameCounter = 0;
    uint32_t startTime = millis();
    uint16_t needRead;

    while (millis() - startTime < waitMillis)
    {
        if (_serial_gps->available())
        {
            int c = _serial_gps->read();
            switch (ubxFrameCounter)
            {
            case 0:
                // ubxFrame 'μ'
                if (c == 0xB5)
                {
                    ubxFrameCounter++;
                }
                break;
            case 1:
                // ubxFrame 'b'
                if (c == 0x62)
                {
                    ubxFrameCounter++;
                }
                else
                {
                    ubxFrameCounter = 0;
                }
                break;
            case 2:
                // Class
                if (c == requestedClass)
                {
                    ubxFrameCounter++;
                }
                else
                {
                    ubxFrameCounter = 0;
                }
                break;
            case 3:
                // Message ID
                if (c == requestedID)
                {
                    ubxFrameCounter++;
                }
                else
                {
                    ubxFrameCounter = 0;
                }
                break;
            case 4:
                // Payload length lsb
                needRead = c;
                ubxFrameCounter++;
                break;
            case 5:
                // Payload length msb
                needRead |= (c << 8);
                ubxFrameCounter++;
                // Check for buffer overflow
                if (needRead >= size)
                {
                    ubxFrameCounter = 0;
                    break;
                }
                if (_serial_gps->readBytes(buffer, needRead) != needRead)
                {
                    ubxFrameCounter = 0;
                }
                else
                {
                    // return payload length
#ifdef GPS_DEBUG
                    LOG_INFO("Got ACK for class %02X message %02X in %d millis.\n", requestedClass, requestedID,
                             millis() - startTime);
#endif
                    return needRead;
                }
                break;

            default:
                break;
            }
        }
    }
    // LOG_WARN("No response for class %02X message %02X\n", requestedClass, requestedID);
    return 0;
}

bool GPS::setup()
{
    if (!didSerialInit)
    {
        if (tx_gpio && gnssModel == GNSS_MODEL_UNKNOWN)
        {
            LOG_DEBUG("Probing for GPS at %d \n", serialSpeeds[speedSelect]);
            gnssModel = GNSS_MODEL_UNKNOWN; 
        }
        else
        {
            gnssModel = GNSS_MODEL_UNKNOWN;
        }

        didSerialInit = true;
    }

    notifyDeepSleepObserver.observe(&notifyDeepSleep);
    notifyGPSSleepObserver.observe(&notifyGPSSleep);
    return true;
}

GPS::~GPS()
{
    // we really should unregister our sleep observer
    notifyDeepSleepObserver.unobserve(&notifyDeepSleep);
    notifyGPSSleepObserver.observe(&notifyGPSSleep);
}

void GPS::setGPSPower(bool on, bool standbyOnly, uint32_t sleepTime)
{
    LOG_INFO("Setting GPS power=%d\n", on);
    if (on)
    {
        clearBuffer(); // drop any old data waiting in the buffer before re-enabling
        if (en_gpio)
            digitalWrite(en_gpio, on ? GPS_EN_ACTIVE : !GPS_EN_ACTIVE); // turn this on if defined, every time
    }
    isInPowersave = !on;
    if (!standbyOnly && en_gpio != 0 &&
        !(HW_VENDOR == meshtastic_HardwareModel_RAK4631 && upDownInterruptImpl1))
    {
        LOG_DEBUG("GPS powerdown using GPS_EN_ACTIVE\n");
        digitalWrite(en_gpio, on ? GPS_EN_ACTIVE : !GPS_EN_ACTIVE);
        return;
    }

#ifdef PIN_GPS_STANDBY // Specifically the standby pin for L76K and clones
    if (on)
    {
        LOG_INFO("Waking GPS");
        digitalWrite(PIN_GPS_STANDBY, 1);
        pinMode(PIN_GPS_STANDBY, OUTPUT);
        return;
    }
    else
    {
        LOG_INFO("GPS entering sleep");
        // notifyGPSSleep.notifyObservers(NULL);
        digitalWrite(PIN_GPS_STANDBY, 0);
        pinMode(PIN_GPS_STANDBY, OUTPUT);
        return;
    }
#endif
    // Note(hugh)L all ublox stuff
    // if (!on)
    // {
    //     if (gnssModel == GNSS_MODEL_UBLOX)
    //     {
    //         uint8_t msglen;
    //         LOG_DEBUG("Sleep Time: %i\n", sleepTime);
    //         for (int i = 0; i < 4; i++)
    //         {
    //             gps->_message_PMREQ[0 + i] = sleepTime >> (i * 8); // Encode the sleep time in millis into the packet
    //         }
    //         msglen = gps->makeUBXPacket(0x02, 0x41, 0x08, gps->_message_PMREQ);
    //         gps->_serial_gps->write(gps->UBXscratch, msglen);
    //     }
    // }
    // else
    // {
    //     if (gnssModel == GNSS_MODEL_UBLOX)
    //     {
    //         gps->_serial_gps->write(0xFF);
    //         clearBuffer(); // This often returns old data, so drop it
    //     }
    // }
}

/// Record that we have a GPS
void GPS::setConnected()
{
    if (!hasGPS)
    {
        hasGPS = true;
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
    if (isAwake != on)
    {
        LOG_DEBUG("WANT GPS=%d\n", on);
        isAwake = on;
        if (!enabled)
        { // short circuit if the user has disabled GPS
            setGPSPower(false, false, 0);
            return;
        }

        if (on)
        {
            lastWakeStartMsec = millis();
        }
        else
        {
            lastSleepStartMsec = millis();
            if (GPSCycles == 1)
            { // Skipping initial lock time, as it will likely be much longer than average
                averageLockTime = lastSleepStartMsec - lastWakeStartMsec;
            }
            else if (GPSCycles > 1)
            {
                averageLockTime += ((int32_t)(lastSleepStartMsec - lastWakeStartMsec) - averageLockTime) / (int32_t)GPSCycles;
            }
            GPSCycles++;
            LOG_DEBUG("GPS Lock took %d, average %d\n", (lastSleepStartMsec - lastWakeStartMsec) / 1000, averageLockTime / 1000);
        }
        if ((int32_t)getSleepTime() - averageLockTime >
            15 * 60 * 1000)
        { // 15 minutes is probably long enough to make a complete poweroff worth it.
            setGPSPower(on, false, getSleepTime() - averageLockTime);
        }
        else if ((int32_t)getSleepTime() - averageLockTime > 10000)
        { // 10 seconds is enough for standby
#ifdef GPS_UC6580
            setGPSPower(on, false, getSleepTime() - averageLockTime);
#else
            setGPSPower(on, true, getSleepTime() - averageLockTime);
#endif
        }
        else if (averageLockTime > 20000)
        {
            averageLockTime -= 1000; // eventually want to sleep again.
        }
    }
}

/** Get how long we should stay looking for each acquisition in msecs
 */
uint32_t GPS::getWakeTime() const
{
    uint32_t t = config.position.gps_attempt_time;

    if (t == UINT32_MAX)
        return t; // already maxint
    return t * 1000;
}

/** Get how long we should sleep between aqusition attempts in msecs
 */
uint32_t GPS::getSleepTime() const
{
    uint32_t t = config.position.gps_update_interval;

    // We'll not need the GPS thread to wake up again after first acq. with fixed position.
    if (!config.position.gps_enabled || config.position.fixed_position)
        t = UINT32_MAX; // Sleep forever now

    if (t == UINT32_MAX)
        return t; // already maxint

    return t * 1000;
}

void GPS::publishUpdate()
{
    if (shouldPublish)
    {
        shouldPublish = false;

        // In debug logs, identify position by @timestamp:stage (stage 2 = publish)
        LOG_DEBUG("publishing pos@%x:2, hasVal=%d, Sats=%d, GPSlock=%d\n", p.timestamp, hasValidLocation, p.sats_in_view,
                  hasLock());

        // Notify any status instances that are observing us
        const meshtastic::GPSStatus status = meshtastic::GPSStatus(hasValidLocation, isConnected(), isPowerSaving(), p);
        newStatus.notifyObservers(&status);
        if (config.position.gps_enabled)
            positionModule->handleNewPosition();
    }
}

int32_t GPS::runOnce()
{
    if (!GPSInitFinished)
    {
        if (!_serial_gps)
            return disable();
        if (!setup())
            return 2000; // Setup failed, re-run in two seconds

        // We have now loaded our saved preferences from flash
        if (config.position.gps_enabled == false)
        {
            return disable();
        }
        // ONCE we will factory reset the GPS for bug #327
        if (!devicestate.did_gps_reset)
        {
            LOG_WARN("GPS FactoryReset requested\n");
            if (gps->factoryReset())
            { // If we don't succeed try again next time
                devicestate.did_gps_reset = true;
                nodeDB.saveToDisk(SEGMENT_DEVICESTATE);
            }
        }
        GPSInitFinished = true;
    }

    // Repeaters have no need for GPS
    if (config.device.role == meshtastic_Config_DeviceConfig_Role_REPEATER)
    {
        return disable();
    }

    if (whileIdle())
    {
        // if we have received valid NMEA claim we are connected
        setConnected();
    }
    else
    {
        // Note(hugh): All ublox stuff
        // if ((config.position.gps_enabled == 1) && (gnssModel == GNSS_MODEL_UBLOX))
        // {
        //     // reset the GPS on next bootup
        //     if (devicestate.did_gps_reset && (millis() - lastWakeStartMsec > 60000) && !hasFlow())
        //     {
        //         LOG_DEBUG("GPS is not communicating, trying factory reset on next bootup.\n");
        //         devicestate.did_gps_reset = false;
        //         nodeDB.saveDeviceStateToDisk();
        //         return disable(); // Stop the GPS thread as it can do nothing useful until next reboot.
        //     }
        // }
    }
    // At least one GPS has a bad habit of losing its mind from time to time
    if (rebootsSeen > 2)
    {
        rebootsSeen = 0;
        gps->factoryReset();
    }

    // If we are overdue for an update, turn on the GPS and at least publish the current status
    uint32_t now = millis();
    uint32_t timeAsleep = now - lastSleepStartMsec;

    auto sleepTime = getSleepTime();
    if (!isAwake && (sleepTime != UINT32_MAX) &&
        ((timeAsleep > sleepTime) || (isInPowersave && timeAsleep > (sleepTime - averageLockTime))))
    {
        // We now want to be awake - so wake up the GPS
        setAwake(true);
    }

    // While we are awake
    if (isAwake)
    {
        // LOG_DEBUG("looking for location\n");
        // If we've already set time from the GPS, no need to ask the GPS
        bool gotTime = (getRTCQuality() >= RTCQualityGPS);
        if (!gotTime && lookForTime())
        { // Note: we count on this && short-circuiting and not resetting the RTC time
            gotTime = true;
            shouldPublish = true;
        }

        bool gotLoc = lookForLocation();
        if (gotLoc && !hasValidLocation)
        { // declare that we have location ASAP
            LOG_DEBUG("hasValidLocation RISING EDGE\n");
            hasValidLocation = true;
            shouldPublish = true;
        }

        now = millis();
        auto wakeTime = getWakeTime();
        bool tooLong = wakeTime != UINT32_MAX && (now - lastWakeStartMsec) > wakeTime;

        // Once we get a location we no longer desperately want an update
        // LOG_DEBUG("gotLoc %d, tooLong %d, gotTime %d\n", gotLoc, tooLong, gotTime);
        if ((gotLoc && gotTime) || tooLong)
        {

            if (tooLong)
            {
                // we didn't get a location during this ack window, therefore declare loss of lock
                if (hasValidLocation)
                {
                    LOG_DEBUG("hasValidLocation FALLING EDGE (last read: %d)\n", gotLoc);
                }
                p = meshtastic_Position_init_default;
                hasValidLocation = false;
            }

            setAwake(false);
            shouldPublish = true; // publish our update for this just finished acquisition window
        }
    }

    // If state has changed do a publish
    publishUpdate();

    if (config.position.fixed_position == true && hasValidLocation)
        return disable(); // This should trigger when we have a fixed position, and get that first position

    // 9600bps is approx 1 byte per msec, so considering our buffer size we never need to wake more often than 200ms
    // if not awake we can run super infrquently (once every 5 secs?) to see if we need to wake.
    return isAwake ? GPS_THREAD_INTERVAL : 5000;
}

// clear the GPS rx buffer as quickly as possible
void GPS::clearBuffer()
{
    int x = _serial_gps->available();
    while (x--)
        _serial_gps->read();
}

/// Prepare the GPS for the cpu entering deep or light sleep, expect to be gone for at least 100s of msecs
int GPS::prepareDeepSleep(void *unused)
{
    LOG_INFO("GPS deep sleep!\n");

    setAwake(false);

    return 0;
}

GnssModel_t GPS::probe(int serialSpeed)
{
    if (_serial_gps->baudRate() != serialSpeed)
    {
        LOG_DEBUG("Setting Baud to %i\n", serialSpeed);
        _serial_gps->updateBaudRate(serialSpeed);
    }
#ifdef GPS_DEBUG
    for (int i = 0; i < 20; i++)
    {
        getACK("$GP", 200);
    }
#endif
    memset(&info, 0, sizeof(struct uBloxGnssModelInfo));
    uint8_t buffer[768] = {0};
    delay(100);

    // Close all NMEA sentences , Only valid for MTK platform
    _serial_gps->write("$PCAS03,0,0,0,0,0,0,0,0,0,0,,,0,0*02\r\n");
    delay(20);

    // Get version information
    clearBuffer();
    _serial_gps->write("$PCAS06,0*1B\r\n");
    if (getACK("$GPTXT,01,01,02,SW=", 500) == GNSS_RESPONSE_OK)
    {
        LOG_INFO("L76K GNSS init succeeded, using L76K GNSS Module\n");
        return GNSS_MODEL_MTK;
    }

    uint8_t cfg_rate[] = {0xB5, 0x62, 0x06, 0x08, 0x00, 0x00, 0x00, 0x00};
    UBXChecksum(cfg_rate, sizeof(cfg_rate));
    clearBuffer();
    _serial_gps->write(cfg_rate, sizeof(cfg_rate));
    // Check that the returned response class and message ID are correct
    GPS_RESPONSE response = getACK(0x06, 0x08, 750);
    if (response == GNSS_RESPONSE_NONE)
    {
        LOG_WARN("Failed to find UBlox & MTK GNSS Module using baudrate %d\n", serialSpeed);
        return GNSS_MODEL_UNKNOWN;
    }
    else if (response == GNSS_RESPONSE_FRAME_ERRORS)
    {
        LOG_INFO("UBlox Frame Errors using baudrate %d\n", serialSpeed);
    }
    else if (response == GNSS_RESPONSE_OK)
    {
        LOG_INFO("Found a UBlox Module using baudrate %d\n", serialSpeed);
    }
    // Note(hugh): This is all ublox stuff so doesnt concern our module
    // // tips: NMEA Only should not be set here, otherwise initializing Ublox gnss module again after
    // // setting will not output command messages in UART1, resulting in unrecognized module information
    // if (serialSpeed != 9600)
    // {
    //     // Set the UART port to 9600
    //     uint8_t _message_prt[] = {0xB5, 0x62, 0x06, 0x00, 0x14, 0x00, 0x01, 0x00, 0x00, 0x00, 0xD0, 0x08, 0x00, 0x00,
    //                               0x80, 0x25, 0x00, 0x00, 0x07, 0x00, 0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    //     UBXChecksum(_message_prt, sizeof(_message_prt));
    //     _serial_gps->write(_message_prt, sizeof(_message_prt));
    //     delay(500);
    //     serialSpeed = 9600;
    //     _serial_gps->updateBaudRate(serialSpeed);

    //     delay(200);
    // }

    // memset(buffer, 0, sizeof(buffer));
    // uint8_t _message_MONVER[8] = {
    //     0xB5, 0x62, // Sync message for UBX protocol
    //     0x0A, 0x04, // Message class and ID (UBX-MON-VER)
    //     0x00, 0x00, // Length of payload (we're asking for an answer, so no payload)
    //     0x00, 0x00  // Checksum
    // };
    // //  Get Ublox gnss module hardware and software info
    // UBXChecksum(_message_MONVER, sizeof(_message_MONVER));
    // clearBuffer();
    // _serial_gps->write(_message_MONVER, sizeof(_message_MONVER));

    // uint16_t len = getACK(buffer, sizeof(buffer), 0x0A, 0x04, 1200);
    // if (len)
    // {
    //     // LOG_DEBUG("monver reply size = %d\n", len);
    //     uint16_t position = 0;
    //     for (int i = 0; i < 30; i++)
    //     {
    //         info.swVersion[i] = buffer[position];
    //         position++;
    //     }
    //     for (int i = 0; i < 10; i++)
    //     {
    //         info.hwVersion[i] = buffer[position];
    //         position++;
    //     }

    //     while (len >= position + 30)
    //     {
    //         for (int i = 0; i < 30; i++)
    //         {
    //             info.extension[info.extensionNo][i] = buffer[position];
    //             position++;
    //         }
    //         info.extensionNo++;
    //         if (info.extensionNo > 9)
    //             break;
    //     }

    //     LOG_DEBUG("Module Info : \n");
    //     LOG_DEBUG("Soft version: %s\n", info.swVersion);
    //     LOG_DEBUG("Hard version: %s\n", info.hwVersion);
    //     LOG_DEBUG("Extensions:%d\n", info.extensionNo);
    //     for (int i = 0; i < info.extensionNo; i++)
    //     {
    //         LOG_DEBUG("  %s\n", info.extension[i]);
    //     }

    //     memset(buffer, 0, sizeof(buffer));

    //     // tips: extensionNo field is 0 on some 6M GNSS modules
    //     for (int i = 0; i < info.extensionNo; ++i)
    //     {
    //         if (!strncmp(info.extension[i], "MOD=", 4))
    //         {
    //             strncpy((char *)buffer, &(info.extension[i][4]), sizeof(buffer));
    //             // LOG_DEBUG("GetModel:%s\n", (char *)buffer);
    //             if (strlen((char *)buffer))
    //             {
    //                 LOG_INFO("UBlox GNSS init succeeded, using UBlox %s GNSS Module\n", (char *)buffer);
    //             }
    //             else
    //             {
    //                 LOG_INFO("UBlox GNSS init succeeded, using UBlox GNSS Module\n");
    //             }
    //         }
    //         else if (!strncmp(info.extension[i], "PROTVER", 7))
    //         {
    //             char *ptr = nullptr;
    //             memset(buffer, 0, sizeof(buffer));
    //             strncpy((char *)buffer, &(info.extension[i][8]), sizeof(buffer));
    //             LOG_DEBUG("Protocol Version:%s\n", (char *)buffer);
    //             if (strlen((char *)buffer))
    //             {
    //                 uBloxProtocolVersion = strtoul((char *)buffer, &ptr, 10);
    //                 LOG_DEBUG("ProtVer=%d\n", uBloxProtocolVersion);
    //             }
    //             else
    //             {
    //                 uBloxProtocolVersion = 0;
    //             }
    //         }
    //     }
    // }

    return GNSS_MODEL_UBLOX;
}


int calculateChecksum(const char* sentence) {
  int checksum = 0;
  for (int i = 1; sentence[i] != '\0'; i++) {
    checksum ^= sentence[i];
  }
  return checksum;
}

GPS *GPS::createGps()
{
    int8_t _rx_gpio = config.position.rx_gpio;
    int8_t _tx_gpio = config.position.tx_gpio;
    int8_t _en_gpio = config.position.gps_en_gpio;
#if defined(HAS_GPS) && !defined(ARCH_ESP32)
    _rx_gpio = 1; // We only specify GPS serial ports on ESP32. Otherwise, these are just flags.
    _tx_gpio = 1;
#endif
#if defined(GPS_RX_PIN)
    if (!_rx_gpio)
        _rx_gpio = GPS_RX_PIN;
#endif
#if defined(GPS_TX_PIN)
    if (!_tx_gpio)
        _tx_gpio = GPS_TX_PIN;
#endif
#if defined(PIN_GPS_EN)
    if (!_en_gpio)
        _en_gpio = PIN_GPS_EN;
#endif
    if (!_rx_gpio || !_serial_gps) // Configured to have no GPS at all
        return nullptr;

    GPS *new_gps = new GPS;
    new_gps->rx_gpio = _rx_gpio;
    new_gps->tx_gpio = _tx_gpio;
    new_gps->en_gpio = _en_gpio;

    if (_en_gpio != 0)
    {
        LOG_DEBUG("Setting %d to output.\n", _en_gpio);
        digitalWrite(_en_gpio, !GPS_EN_ACTIVE);
        pinMode(_en_gpio, OUTPUT);
    }

#ifdef PIN_GPS_PPS
    // pulse per second
    pinMode(PIN_GPS_PPS, INPUT);
#endif

// Currently disabled per issue #525 (TinyGPS++ crash bug)
// when fixed upstream, can be un-disabled to enable 3D FixType and PDOP
#ifndef TINYGPS_OPTION_NO_CUSTOM_FIELDS
    // see NMEAGPS.h
    gsafixtype.begin(reader, NMEA_MSG_GXGSA, 2);
    gsapdop.begin(reader, NMEA_MSG_GXGSA, 15);
    LOG_DEBUG("Using " NMEA_MSG_GXGSA " for 3DFIX and PDOP\n");
#endif

    new_gps->setGPSPower(true, false, 0);

#ifdef PIN_GPS_RESET
    digitalWrite(PIN_GPS_RESET, GPS_RESET_MODE); // assert for 10ms
    pinMode(PIN_GPS_RESET, OUTPUT);
    delay(10);
    digitalWrite(PIN_GPS_RESET, !GPS_RESET_MODE);
#endif
    new_gps->setAwake(true); // Wake GPS power before doing any init

    if (_serial_gps)
    {
#ifdef ARCH_ESP32
        // In esp32 framework, setRxBufferSize needs to be initialized before Serial
        _serial_gps->setRxBufferSize(SERIAL_BUFFER_SIZE); // the default is 256
#endif

        //  ESP32 has a special set of parameters vs other arduino ports
        LOG_DEBUG("Using GPIO%d for GPS RX\n", new_gps->rx_gpio);
        LOG_DEBUG("Using GPIO%d for GPS TX\n", new_gps->tx_gpio);
        _serial_gps->begin(GPS_BAUDRATE, SERIAL_8N1, new_gps->rx_gpio, new_gps->tx_gpio);
        // Send the PMTK353 command to enable all satellite systems
        _serial_gps->print("$PMTK353,1,1,1,0,1*"); // Enable GPS, GLONASS, Galileo, BDS

        // Calculate and append the checksum
        int checksum = calculateChecksum("$PMTK353,1,1,1,0,1");
        if (checksum < 16) {
            _serial_gps->print("0");
        }
        _serial_gps->println(checksum, HEX);

        /*
         * T-Beam-S3-Core will be preset to use gps Probe here, and other boards will not be changed first
         */
#if defined(GPS_UC6580)
        _serial_gps->updateBaudRate(115200);
#endif
    }
    return new_gps;
}

static int32_t toDegInt(RawDegrees d)
{
    int32_t degMult = 10000000; // 1e7
    int32_t r = d.deg * degMult + d.billionths / 100;
    if (d.negative)
        r *= -1;
    return r;
}

bool GPS::factoryReset()
{
#ifdef PIN_GPS_REINIT
    // The L76K GNSS on the T-Echo requires the RESET pin to be pulled LOW
    digitalWrite(PIN_GPS_REINIT, 0);
    pinMode(PIN_GPS_REINIT, OUTPUT);
    delay(150); // The L76K datasheet calls for at least 100MS delay
    digitalWrite(PIN_GPS_REINIT, 1);
#endif

    // send the UBLOX Factory Reset Command regardless of detect state, something is very wrong, just assume it's UBLOX.
    // Factory Reset
    byte _message_reset[] = {0xB5, 0x62, 0x06, 0x09, 0x0D, 0x00, 0xFF, 0xFB, 0x00, 0x00, 0x00,
                             0x00, 0x00, 0x00, 0xFF, 0xFF, 0x00, 0x00, 0x17, 0x2B, 0x7E};
    _serial_gps->write(_message_reset, sizeof(_message_reset));

    delay(1000);
    return true;
}

/**
 * Perform any processing that should be done only while the GPS is awake and looking for a fix.
 * Override this method to check for new locations
 *
 * @return true if we've acquired a new location
 */
bool GPS::lookForTime()
{
    auto ti = reader.time;
    auto d = reader.date;
    if (ti.isValid() && d.isValid())
    { // Note: we don't check for updated, because we'll only be called if needed
        /* Convert to unix time
The Unix epoch (or Unix time or POSIX time or Unix timestamp) is the number of seconds that have elapsed since January 1, 1970
(midnight UTC/GMT), not counting leap seconds (in ISO 8601: 1970-01-01T00:00:00Z).
*/
        struct tm t;
        t.tm_sec = ti.second();
        t.tm_min = ti.minute();
        t.tm_hour = ti.hour();
        t.tm_mday = d.day();
        t.tm_mon = d.month() - 1;
        t.tm_year = d.year() - 1900;
        t.tm_isdst = false;
        if (t.tm_mon > -1)
        {
            LOG_DEBUG("NMEA GPS time %02d-%02d-%02d %02d:%02d:%02d\n", d.year(), d.month(), t.tm_mday, t.tm_hour, t.tm_min,
                      t.tm_sec);
            perhapsSetRTC(RTCQualityGPS, t);
            return true;
        }
        else
            return false;
    }
    else
        return false;
}

/**
 * Perform any processing that should be done only while the GPS is awake and looking for a fix.
 * Override this method to check for new locations
 *
 * @return true if we've acquired a new location
 */
bool GPS::lookForLocation()
{
    // By default, TinyGPS++ does not parse GPGSA lines, which give us
    //   the 2D/3D fixType (see NMEAGPS.h)
    // At a minimum, use the fixQuality indicator in GPGGA (FIXME?)
    fixQual = reader.fixQuality();

#ifndef TINYGPS_OPTION_NO_STATISTICS
    if (reader.failedChecksum() > lastChecksumFailCount)
    {
        LOG_WARN("Warning, %u new GPS checksum failures, for a total of %u.\n", reader.failedChecksum() - lastChecksumFailCount,
                 reader.failedChecksum());
        lastChecksumFailCount = reader.failedChecksum();
    }
#endif

#ifndef TINYGPS_OPTION_NO_CUSTOM_FIELDS
    fixType = atoi(gsafixtype.value()); // will set to zero if no data
    // LOG_DEBUG("FIX QUAL=%d, TYPE=%d\n", fixQual, fixType);
#endif

    // check if GPS has an acceptable lock
    if (!hasLock())
        return false;

#ifdef GPS_EXTRAVERBOSE
    LOG_DEBUG("AGE: LOC=%d FIX=%d DATE=%d TIME=%d\n", reader.location.age(),
#ifndef TINYGPS_OPTION_NO_CUSTOM_FIELDS
              gsafixtype.age(),
#else
              0,
#endif
              reader.date.age(), reader.time.age());
#endif // GPS_EXTRAVERBOSE

    // check if a complete GPS solution set is available for reading
    //   tinyGPSDatum::age() also includes isValid() test
    // FIXME
    if (!((reader.location.age() < GPS_SOL_EXPIRY_MS) &&
#ifndef TINYGPS_OPTION_NO_CUSTOM_FIELDS
          (gsafixtype.age() < GPS_SOL_EXPIRY_MS) &&
#endif
          (reader.time.age() < GPS_SOL_EXPIRY_MS) && (reader.date.age() < GPS_SOL_EXPIRY_MS)))
    {
        LOG_WARN("SOME data is TOO OLD: LOC %u, TIME %u, DATE %u\n", reader.location.age(), reader.time.age(), reader.date.age());
        return false;
    }

    // Is this a new point or are we re-reading the previous one?
    if (!reader.location.isUpdated())
        return false;

    // We know the solution is fresh and valid, so just read the data
    auto loc = reader.location.value();

    // Bail out EARLY to avoid overwriting previous good data (like #857)
    if (toDegInt(loc.lat) > 900000000)
    {
#ifdef GPS_EXTRAVERBOSE
        LOG_DEBUG("Bail out EARLY on LAT %i\n", toDegInt(loc.lat));
#endif
        return false;
    }
    if (toDegInt(loc.lng) > 1800000000)
    {
#ifdef GPS_EXTRAVERBOSE
        LOG_DEBUG("Bail out EARLY on LNG %i\n", toDegInt(loc.lng));
#endif
        return false;
    }

    p.location_source = meshtastic_Position_LocSource_LOC_INTERNAL;

    // Dilution of precision (an accuracy metric) is reported in 10^2 units, so we need to scale down when we use it
#ifndef TINYGPS_OPTION_NO_CUSTOM_FIELDS
    p.HDOP = reader.hdop.value();
    p.PDOP = TinyGPSPlus::parseDecimal(gsapdop.value());
    // LOG_DEBUG("PDOP=%d, HDOP=%d\n", p.PDOP, p.HDOP);
#else
    // FIXME! naive PDOP emulation (assumes VDOP==HDOP)
    // correct formula is PDOP = SQRT(HDOP^2 + VDOP^2)
    p.HDOP = reader.hdop.value();
    p.PDOP = 1.41 * reader.hdop.value();
#endif

    // Discard incomplete or erroneous readings
    if (reader.hdop.value() == 0)
    {
        LOG_WARN("BOGUS hdop.value() REJECTED: %d\n", reader.hdop.value());
        return false;
    }

    p.latitude_i = toDegInt(loc.lat);
    p.longitude_i = toDegInt(loc.lng);

    p.altitude_geoidal_separation = reader.geoidHeight.meters();
    p.altitude_hae = reader.altitude.meters() + p.altitude_geoidal_separation;
    p.altitude = reader.altitude.meters();

    p.fix_quality = fixQual;
#ifndef TINYGPS_OPTION_NO_CUSTOM_FIELDS
    p.fix_type = fixType;
#endif

    // positional timestamp
    struct tm t;
    t.tm_sec = reader.time.second();
    t.tm_min = reader.time.minute();
    t.tm_hour = reader.time.hour();
    t.tm_mday = reader.date.day();
    t.tm_mon = reader.date.month() - 1;
    t.tm_year = reader.date.year() - 1900;
    t.tm_isdst = false;
    p.timestamp = mktime(&t);

    // Nice to have, if available
    if (reader.satellites.isUpdated())
    {
        p.sats_in_view = reader.satellites.value();
    }

    if (reader.course.isUpdated() && reader.course.isValid())
    {
        if (reader.course.value() < 36000)
        { // sanity check
            p.ground_track =
                reader.course.value() * 1e3; // Scale the heading (in degrees * 10^-2) to match the expected degrees * 10^-5
        }
        else
        {
            LOG_WARN("BOGUS course.value() REJECTED: %d\n", reader.course.value());
        }
    }

    if (reader.speed.isUpdated() && reader.speed.isValid())
    {
        p.ground_speed = reader.speed.kmph();
    }

    return true;
}

bool GPS::hasLock()
{
    // Using GPGGA fix quality indicator
    if (fixQual >= 1 && fixQual <= 5)
    {
#ifndef TINYGPS_OPTION_NO_CUSTOM_FIELDS
        // Use GPGSA fix type 2D/3D (better) if available
        if (fixType == 3 || fixType == 0) // zero means "no data received"
#endif
            return true;
    }

    return false;
}

bool GPS::hasFlow()
{
    return reader.passedChecksum() > 0;
}

bool GPS::whileIdle()
{
    int charsInBuf = 0;
    bool isValid = false;
    if (!isAwake)
    {
        clearBuffer();
        return isAwake;
    }
#ifdef SERIAL_BUFFER_SIZE
    if (_serial_gps->available() >= SERIAL_BUFFER_SIZE - 1)
    {
        LOG_WARN("GPS Buffer full with %u bytes waiting. Flushing to avoid corruption.\n", _serial_gps->available());
        clearBuffer();
    }
#endif
    // if (_serial_gps->available() > 0)
    // LOG_DEBUG("GPS Bytes Waiting: %u\n", _serial_gps->available());
    // First consume any chars that have piled up at the receiver
    while (_serial_gps->available() > 0)
    {
        int c = _serial_gps->read();
        
        UBXscratch[charsInBuf] = c;
#ifdef GPS_DEBUG
        LOG_DEBUG("%c", c);
#endif
        isValid |= reader.encode(c);
        if (charsInBuf > sizeof(UBXscratch) - 10 || c == '\r')
        {
            if (strnstr((char *)UBXscratch, "$GPTXT,01,01,02,u-blox ag - www.u-blox.com*50", charsInBuf))
            {
                rebootsSeen++;
            }
            charsInBuf = 0;
        }
        else
        {
            charsInBuf++;
        }
    }
    return isValid;
}
void GPS::enable()
{
    enabled = true;
    setInterval(GPS_THREAD_INTERVAL);
    setAwake(true);
}

int32_t GPS::disable()
{
    enabled = false;
    setInterval(INT32_MAX);
    setAwake(false);

    return INT32_MAX;
}