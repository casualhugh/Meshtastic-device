#include "power.h"
#include "NodeDB.h"
#include "PowerFSM.h"
#include "configuration.h"
#include "main.h"
#include "sleep.h"
#include "utils.h"


// Copy of the base class defined in axp20x.h.
// I'd rather not inlude axp20x.h as it brings Wire dependency.
class HasBatteryLevel
{
  public:
    /**
     * Battery state of charge, from 0 to 100 or -1 for unknown
     */
    virtual int getBattPercentage() { return -1; }

    /**
     * The raw voltage of the battery or NAN if unknown
     */
    virtual float getBattVoltage() { return NAN; }

    /**
     * return true if there is a battery installed in this unit
     */
    virtual bool isBatteryConnect() { return false; }

    virtual bool isVBUSPlug() { return false; }
    virtual bool isChargeing() { return false; }
};

bool pmu_irq = false;

Power *power;

using namespace meshtastic;

#ifndef AREF_VOLTAGE
#define AREF_VOLTAGE 3.3
#endif

/**
 * If this board has a battery level sensor, set this to a valid implementation
 */
static HasBatteryLevel *batteryLevel; // Default to NULL for no battery level sensor

/**
 * A simple battery level sensor that assumes the battery voltage is attached via a voltage-divider to an analog input
 */
class AnalogBatteryLevel : public HasBatteryLevel
{
    /**
     * Battery state of charge, from 0 to 100 or -1 for unknown
     *
     * FIXME - use a lipo lookup table, the current % full is super wrong
     */
    virtual int getBattPercentage() override
    {
        float v = getBattVoltage();

        if (v < noBatVolt)
            return -1; // If voltage is super low assume no battery installed

#ifndef NRF52_SERIES
        // This does not work on a RAK4631 with battery connected
        if (v > chargingVolt)
            return 0; // While charging we can't report % full on the battery
#endif

        return clamp((int)(100 * (v - emptyVolt) / (fullVolt - emptyVolt)), 0, 100);
    }

    /**
     * The raw voltage of the batteryin millivolts or NAN if unknown
     */
    virtual float getBattVoltage() override
    {

#ifndef ADC_MULTIPLIER
#define ADC_MULTIPLIER 2.0
#endif

#ifdef BATTERY_PIN
        // Override variant or default ADC_MULTIPLIER if we have the override pref
        float operativeAdcMultiplier = config.power.adc_multiplier_override > 0
                                           ? config.power.adc_multiplier_override
                                           : ADC_MULTIPLIER;
        // Do not call analogRead() often.
        const uint32_t min_read_interval = 5000;
        if (millis() - last_read_time_ms > min_read_interval) {
            last_read_time_ms = millis();
            uint32_t raw = analogRead(BATTERY_PIN);
            float scaled;
#ifndef VBAT_RAW_TO_SCALED
            scaled = 1000.0 * operativeAdcMultiplier * (AREF_VOLTAGE / 1024.0) * raw + 300;
#else
            scaled = VBAT_RAW_TO_SCALED(raw); // defined in variant.h
#endif
            // DEBUG_MSG("battery gpio %d raw val=%u scaled=%u\n", BATTERY_PIN, raw, (uint32_t)(scaled));
            last_read_value = scaled;
            return scaled;
        } else {
            return last_read_value;
        }
#else
        return NAN;
#endif
    }

    /**
     * return true if there is a battery installed in this unit
     */
    virtual bool isBatteryConnect() override { return getBattPercentage() != -1; }

    /// If we see a battery voltage higher than physics allows - assume charger is pumping
    /// in power
    virtual bool isVBUSPlug() override { return getBattVoltage() > chargingVolt; }

    /// Assume charging if we have a battery and external power is connected.
    /// we can't be smart enough to say 'full'?
    virtual bool isChargeing() override { return isBatteryConnect() && isVBUSPlug(); }

  private:
    /// If we see a battery voltage higher than physics allows - assume charger is pumping
    /// in power

    /// For heltecs with no battery connected, the measured voltage is 2204, so raising to 2230 from 2100
    const float fullVolt = 4200, emptyVolt = 3270, chargingVolt = 4210, noBatVolt = 2230;
    float last_read_value = 0.0;
    uint32_t last_read_time_ms = 0;
};

AnalogBatteryLevel analogLevel;

Power::Power() : OSThread("Power")
{
    statusHandler = {};
    low_voltage_counter = 0;
}

bool Power::analogInit()
{
#ifdef BATTERY_PIN
    DEBUG_MSG("Using analog input %d for battery level\n", BATTERY_PIN);

    // disable any internal pullups
    pinMode(BATTERY_PIN, INPUT);

#ifndef NO_ESP32
    // ESP32 needs special analog stuff
    adcAttachPin(BATTERY_PIN);
#endif
#ifdef NRF52_SERIES
#ifdef VBAT_AR_INTERNAL
    analogReference(VBAT_AR_INTERNAL);
#else
    analogReference(AR_INTERNAL); // 3.6V
#endif
#endif

#ifndef BATTERY_SENSE_RESOLUTION_BITS
#define BATTERY_SENSE_RESOLUTION_BITS 10
#endif

    // adcStart(BATTERY_PIN);
    analogReadResolution(BATTERY_SENSE_RESOLUTION_BITS); // Default of 12 is not very linear. Recommended to use 10 or 11
                                                         // depending on needed resolution.
    batteryLevel = &analogLevel;
    return true;
#else
    return false;
#endif
}

bool Power::setup()
{
    bool found = analogInit();
    enabled = found;
    low_voltage_counter = 0;

    return found;
}

void Power::shutdown()
{
}

/// Reads power status to powerStatus singleton.
//
// TODO(girts): move this and other axp stuff to power.h/power.cpp.
void Power::readPowerStatus()
{
    if (batteryLevel) {
        bool hasBattery = batteryLevel->isBatteryConnect();
        int batteryVoltageMv = 0;
        int8_t batteryChargePercent = 0;
        if (hasBattery) {
            batteryVoltageMv = batteryLevel->getBattVoltage();
            // If the AXP192 returns a valid battery percentage, use it
            if (batteryLevel->getBattPercentage() >= 0) {
                batteryChargePercent = batteryLevel->getBattPercentage();
            } else {
                // If the AXP192 returns a percentage less than 0, the feature is either not supported or there is an error
                // In that case, we compute an estimate of the charge percent based on maximum and minimum voltages defined in
                // power.h
                batteryChargePercent =
                    clamp((int)(((batteryVoltageMv - BAT_MILLIVOLTS_EMPTY) * 1e2) / (BAT_MILLIVOLTS_FULL - BAT_MILLIVOLTS_EMPTY)),
                          0, 100);
            }
        }

        // Notify any status instances that are observing us
        const PowerStatus powerStatus2 =
            PowerStatus(hasBattery ? OptTrue : OptFalse, batteryLevel->isVBUSPlug() ? OptTrue : OptFalse,
                        batteryLevel->isChargeing() ? OptTrue : OptFalse, batteryVoltageMv, batteryChargePercent);
        DEBUG_MSG("Battery: usbPower=%d, isCharging=%d, batMv=%d, batPct=%d\n", powerStatus2.getHasUSB(),
                  powerStatus2.getIsCharging(), powerStatus2.getBatteryVoltageMv(), powerStatus2.getBatteryChargePercent());
        newStatus.notifyObservers(&powerStatus2);

// If we have a battery at all and it is less than 10% full, force deep sleep if we have more than 3 low readings in a row
        // If we have a battery at all and it is less than 10% full, force deep sleep
        if (powerStatus2.getHasBattery() && !powerStatus2.getHasUSB() && batteryLevel->getBattVoltage() < MIN_BAT_MILLIVOLTS)
            powerFSM.trigger(EVENT_LOW_BATTERY);
    } else {
        // No power sensing on this board - tell everyone else we have no idea what is happening
        const PowerStatus powerStatus3 = PowerStatus(OptUnknown, OptUnknown, OptUnknown, -1, -1);
        newStatus.notifyObservers(&powerStatus3);
    }
}

int32_t Power::runOnce()
{
    readPowerStatus();

    // Only read once every 20 seconds once the power status for the app has been initialized
    return (statusHandler && statusHandler->isInitialized()) ? (1000 * 20) : RUN_SAME;
}

