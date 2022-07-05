#include "../configuration.h"
#include "../main.h"
#include <Wire.h>
#include "mesh/generated/telemetry.pb.h"

#ifndef NO_WIRE
uint16_t getRegisterValue(uint8_t address, uint8_t reg, uint8_t length) {
    uint16_t value = 0x00;
    Wire.beginTransmission(address);
    Wire.write(reg);
    Wire.endTransmission();
    delay(20);
    Wire.requestFrom(address, length);
    DEBUG_MSG("Wire.available() = %d\n", Wire.available());
    if (Wire.available() == 2) {
        // Read MSB, then LSB
        value = (uint16_t)Wire.read() << 8;  
        value |= Wire.read();
    } else if (Wire.available()) {
        value = Wire.read();
    }
    return value;
}

uint8_t oled_probe(byte addr)
{
    uint8_t r = 0;
    uint8_t r_prev = 0;
    uint8_t c = 0;
    uint8_t o_probe = 0;
    do {
        r_prev = r;
        Wire.beginTransmission(addr);
        Wire.write(0x00);
        Wire.endTransmission();
        Wire.requestFrom((int)addr, 1);
        if (Wire.available()) {
            r = Wire.read();
        }
        r &= 0x0f;

        if (r == 0x08 || r == 0x00) {
            o_probe = 2; // SH1106
        } else if ( r == 0x03 || r == 0x06 || r == 0x07) {
            o_probe = 1; // SSD1306
        }
        c++;
    } while ((r != r_prev) && (c < 4));
    DEBUG_MSG("0x%x subtype probed in %i tries \n", r, c);
    return o_probe;
}

void scanI2Cdevice(void)
{
    byte err, addr;
    uint16_t registerValue = 0x00;
    int nDevices = 0;
    for (addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        err = Wire.endTransmission();
        if (err == 0) {
            DEBUG_MSG("I2C device found at address 0x%x\n", addr);

            nDevices++;
        if (addr == BME_ADDR || addr == BME_ADDR_ALTERNATE) {
            registerValue = getRegisterValue(addr, 0xD0, 1); // GET_ID
            if (registerValue == 0x61) {
                DEBUG_MSG("BME-680 sensor found at address 0x%x\n", (uint8_t)addr);
                nodeTelemetrySensorsMap[TelemetrySensorType_BME680] = addr;
            } else if (registerValue == 0x60) {
                DEBUG_MSG("BME-280 sensor found at address 0x%x\n", (uint8_t)addr);
                nodeTelemetrySensorsMap[TelemetrySensorType_BME280] = addr;
            }
        }
        if (addr == INA_ADDR || addr == INA_ADDR_ALTERNATE) {
            registerValue = getRegisterValue(addr, 0xFE, 2);
            DEBUG_MSG("Register MFG_UID: 0x%x\n", registerValue);
            if (registerValue == 0x5449) {
                DEBUG_MSG("INA260 sensor found at address 0x%x\n", (uint8_t)addr);
                nodeTelemetrySensorsMap[TelemetrySensorType_INA260] = addr;
            } else { // Assume INA219 if INA260 ID is not found
                DEBUG_MSG("INA219 sensor found at address 0x%x\n", (uint8_t)addr);
                nodeTelemetrySensorsMap[TelemetrySensorType_INA219] = addr;
            }
        }
        if (addr == MCP9808_ADDR) {
            nodeTelemetrySensorsMap[TelemetrySensorType_MCP9808] = addr;
            DEBUG_MSG("MCP9808 sensor found at address 0x%x\n", (uint8_t)addr);
        }
        } else if (err == 4) {
            DEBUG_MSG("Unknow error at address 0x%x\n", addr);
        }
    }

    if (nDevices == 0)
        DEBUG_MSG("No I2C devices found\n");
    else
        DEBUG_MSG("%i I2C devices found\n",nDevices);
}
#else
void scanI2Cdevice(void) {}
#endif
