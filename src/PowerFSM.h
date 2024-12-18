#pragma once

#include "configuration.h"

// See sw-design.md for documentation
typedef enum {
    EVENT_PRESS,
    EVENT_WAKE_TIMER,
    EVENT_RECEIVED_PACKET, 
    EVENT_PACKET_FOR_PHONE, 
    EVENT_RECEIVED_MSG, 
    EVENT_BOOT,
    EVENT_BLUETOOTH_PAIR,
    EVENT_NODEDB_UPDATED,    // NodeDB has a big enough change that we think you should turn on the screen
    EVENT_CONTACT_FROM_PHONE, // the phone just talked to us over bluetooth
    EVENT_LOW_BATTERY,       // Battery is critically low, go to sleep
    EVENT_SERIAL_CONNECTED,
    EVENT_SERIAL_DISCONNECTED,
    EVENT_POWER_CONNECTED,
    EVENT_POWER_DISCONNECTED,
    EVENT_FIRMWARE_UPDATE, // We just received a new firmware update packet from the phone
    EVENT_SHUTDOWN,        // force a full shutdown now (not just sleep)
    EVENT_INPUT,          // input broker wants something, we need to wake up and enable screen
    EVENT_PRESS_ALT
} EventTypes;

#if EXCLUDE_POWER_FSM
class FakeFsm
{
  public:
    void trigger(int event)
    {
        if (event == EVENT_SERIAL_CONNECTED) {
            serialConnected = true;
        } else if (event == EVENT_SERIAL_DISCONNECTED) {
            serialConnected = false;
        }
    };
    bool getState() { return serialConnected; };

  private:
    bool serialConnected = false;
};
extern FakeFsm powerFSM;
void PowerFSM_setup();

#else
#include <Fsm.h>
extern Fsm powerFSM;
extern State stateON, statePOWER, stateSERIAL, stateDARK;

void PowerFSM_setup();
#endif