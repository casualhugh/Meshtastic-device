#include "configuration.h"
#include "graphics/Screen.h"
#include "main.h"
#include "power.h"

void powerCommandsCheck()
{
    if (rebootAtMsec && millis() > rebootAtMsec) {
        DEBUG_MSG("Rebooting\n");
#ifndef NO_ESP32
        ESP.restart();
#elif NRF52_SERIES
        NVIC_SystemReset();
#else
        DEBUG_MSG("FIXME implement reboot for this platform");
#endif
    }


    if (shutdownAtMsec && millis() > shutdownAtMsec) {
        DEBUG_MSG("Shutting down from admin command\n");
    }
}
