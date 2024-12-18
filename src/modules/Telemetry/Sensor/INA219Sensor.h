#include "configuration.h"

#if HAS_TELEMETRY && !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR

#include "../mesh/generated/meshtastic/telemetry.pb.h"
#include "TelemetrySensor.h"
#include "VoltageSensor.h"
#include <Adafruit_INA219.h>
#include <INA226_WE.h>
class INA219Sensor : public TelemetrySensor, VoltageSensor
{
  private:
    // TODO(hugh): Replace me with the INA226 library
    // Adafruit_INA219 ina219;
    INA226_WE ina226;

  protected:
    virtual void setup() override;

  public:
    INA219Sensor();
    virtual int32_t runOnce() override;
    virtual bool getMetrics(meshtastic_Telemetry *measurement) override;
    virtual uint16_t getBusVoltageMv() override;
};

#endif