#include "PowerFSM.h"
#include "concurrency/OSThread.h"
#include "configuration.h"
#include "main.h"
#include "power.h"
#include <Arduino.h>
#include <Wire.h>

#include <LSM303AGR_MAG_Sensor.h>
#define DEV_I2C Wire


#define MAG_CHECK_INTERVAL_MS 100

class LSM303 : public concurrency::OSThread
{
  public:
    LSM303(ScanI2C::DeviceType type = ScanI2C::DeviceType::NONE) : OSThread("LSM303")
    {
        if (magnotometer_found.port == ScanI2C::I2CPort::NO_I2C) {
            LOG_DEBUG("MagnotometerThread disabling due to no sensors found\n");
            disable();
            return;
        }
        LOG_DEBUG("MagnotometerThread initializing\n");
        
        // acc = new LSM303AGR_ACC_Sensor(&DEV_I2C);
        mag = new LSM303AGR_MAG_Sensor(&DEV_I2C);
        enabled = false;
        for (int i = 0; i < 3; i++) {
            a[i] = 0; 
            m[i] = 0;
        }
    }
    Observable<const meshtastic::MagnotometerStatus *> newStatus;
  protected:
  void publishUpdate(float heading)
  {
      // In debug logs, identify position by @timestamp:stage (stage 2 = publish)
      // LOG_DEBUG("publishing mag@%d\n", heading);

      // Notify any status instances that are observing us
      const meshtastic::MagnotometerStatus status = meshtastic::MagnotometerStatus(true, false, heading);
      newStatus.notifyObservers(&status);
  }
    int32_t runOnce() override
    {
        canSleep = true; // Assume we should not keep the board awake
        publishUpdate(getHeading());
        return MAG_CHECK_INTERVAL_MS;
    }
    ScanI2C::DeviceType mag_type;
    // TODO OBSERVE THE Sleep notifier
    int prepareDeepSleep(void *unused)
    {
        mag->Disable();
        enabled = false;
        return 0;
    }
    public:
    void init();
    bool isEnabled();
    void enable();
    // void disable();
    float getHeading();
    int32_t maxes[3] = {
      -2147483647,
      -2147483647,
      -2147483647
    };
    int32_t mins[3] = {
      2147483647,
      2147483647,
      2147483647
    };
  protected:
    // LSM303AGR_ACC_Sensor* acc;
    LSM303AGR_MAG_Sensor* mag;
    
    int32_t a[3];
    int32_t m[3];
    bool enabled = false;
    double calibration_matrix[3][3] = 
    {
      {0.969, 0.024, -0.023},
      {0.024, 1.026, -0.023},
      {-0.023, -0.023, 1.007}  
    };
    double bias[3] = 
    {
      0,
      0,
      0
    };  
    
    int32_t count = 0;
    float last_values[10];
    float getAverage(float last);
    void recalculate_bias();
    void transformation(float uncalibrated_values[3], float* calibrated_values);
};