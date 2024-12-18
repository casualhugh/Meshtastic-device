#include "configuration.h"

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "INA219Sensor.h"
#include "TelemetrySensor.h"
#include <Adafruit_INA219.h>

#ifndef INA219_MULTIPLIER
#define INA219_MULTIPLIER 1.0f
#endif

INA219Sensor::INA219Sensor() : TelemetrySensor(meshtastic_TelemetrySensorType_INA219, "INA219") {

    ina226 = INA226_WE(0x40);
}

int32_t INA219Sensor::runOnce()
{
    LOG_INFO("Init sensor: %s", sensorName);
    if (!hasSensor()) {
        return DEFAULT_SENSOR_MINIMUM_WAIT_TIME_BETWEEN_READS;
    }
    if (!ina226.init()) {
        ina226 = INA226_WE(0x40);
        status = ina226.init();
    } else {
        status = ina226.init();
    }
    return initI2CSensor();
}

void INA219Sensor::setup() {
    ina226.setMeasureMode(TRIGGERED);
}

bool INA219Sensor::getMetrics(meshtastic_Telemetry *measurement)
{
    measurement->variant.environment_metrics.has_voltage = true;
    measurement->variant.environment_metrics.has_current = true;

    ina226.startSingleMeasurement();
    ina226.readAndClearFlags();
    // float shuntVoltage_mV = ina226.getShuntVoltage_mV();
    // float power_mW = ina226.getBusPower();
    measurement->variant.environment_metrics.voltage = ina226.getBusVoltage_V();
    measurement->variant.environment_metrics.current = ina226.getCurrent_mA();
    return true;
}

uint16_t INA219Sensor::getBusVoltageMv()
{
    ina226.startSingleMeasurement();
    ina226.readAndClearFlags();
    return lround(ina226.getBusVoltage_V() * 1000);
}

#endif