#pragma once
#include <Arduino.h>
#include <Wire.h>
#include <LSM303AGR_ACC_Sensor.h>
#include <LSM303AGR_MAG_Sensor.h>
#include "Observer.h"


#define DECLINATION -5.55 // declination (in degrees) in Cluj-Napoca (Romania).
#define I2C_ADDRESS 0x30

class Heading
{
  public:
    Heading(TwoWire *dev_i2c);
    void init();
    bool isEnabled();
    void enable();
    void disable();
    uint16_t getHeading();

  protected:
    LSM303AGR_ACC_Sensor* acc;
    LSM303AGR_MAG_Sensor* mag;
    
    int32_t a[3];
    int32_t m[3];
    bool enabled;
    double calibration_matrix[3][3] = 
    {
      {0.969, 0.024, -0.023},
      {0.024, 1.026, -0.023},
      {-0.023, -0.023, 1.007}  
    };
    double bias[3] = 
    {
      -376.50,
      188.00,
      -52.50
    };  
    int32_t maxes[3] = {
      0,
      0,
      0
    };
    int32_t mins[3] = {
      2147483647,
      2147483647,
      2147483647
    };
    int32_t count = 0;
    float last_values[10];
    float getAverage(float last);
    void recalculate_bias();
    void transformation(float uncalibrated_values[3], float* calibrated_values);
};

extern Heading *heading;
