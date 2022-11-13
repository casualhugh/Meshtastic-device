#include "PowerFSM.h"
#include "RadioLibInterface.h"
#include "OSThread.h"
#include "configuration.h"
#include "graphics/Screen.h"
#include "power.h"
#include <OneButton.h>
#include "commands.h"
// enum {
//     PRESS_UP = 1,
//     PRESS_DOWN = 2
// };
// enum{
//     SINGLE_PRESS = 1,
//     LONG_PRESS = 2,
//     ALT_PRESS = 3,
//     UP = 4,
//     DOWN = 5
// };

namespace concurrency
{
/**
 * Watch a GPIO and if we get an IRQ, wake the main thread.
 * Use to add wake on button press
 */
void wakeOnIrq(int irq, int mode)
{
    attachInterrupt(
        irq,
        [] {
            BaseType_t higherWake = 0;
            mainDelay.interruptFromISR(&higherWake);
        },
        FALLING);
}

class ButtonThread : public concurrency::OSThread
{
// Prepare for button presses
#ifdef BUTTON_PIN
    OneButton button1;
#endif
#ifdef BUTTON_PIN_2
    OneButton button2;
#endif

    static bool shutdown_on_long_stop;

  public:
    static uint32_t longPressTime;
    // callback returns the period for the next callback invocation (or 0 if we should no longer be called)
    ButtonThread() : OSThread("Button")
    {
#ifdef BUTTON_PIN
        button1 = OneButton(BUTTON_PIN, false, false);
        button1.attachClick(button1Pressed);
        button1.attachDuringLongPress(button1PressedLong);
        button1.attachDoubleClick(button1DoublePressed);
        button1.attachMultiClick(button1MultiPressed);
        button1.attachLongPressStart(button1PressedLongStart);
        button1.attachLongPressStop(button1PressedLongStop);
        wakeOnIrq(BUTTON_PIN, RISING);
#endif
#ifdef BUTTON_PIN_2
        button2 = OneButton(BUTTON_PIN_2, false, false);
        button2.attachClick(button2Pressed);
        button2.attachDuringLongPress(button2PressedLong);
        button2.attachDoubleClick(button2DoublePressed);
        button2.attachMultiClick(button2MultiPressed);
        button2.attachLongPressStart(button2PressedLongStart);
        button2.attachLongPressStop(button2PressedLongStop);
        wakeOnIrq(BUTTON_PIN_2, RISING);
#endif
    }

  protected:
    /// If the button is pressed we suppress CPU sleep until release
    int32_t runOnce() override
    {
        canSleep = true; // Assume we should not keep the board awake
        button1.tick();
        canSleep &= button1.isIdle();

#ifdef BUTTON_PIN_2
        button2.tick();
        canSleep &= button2.isIdle();
#endif
        // if (!canSleep) DEBUG_MSG("Supressing sleep!\n");
        // else DEBUG_MSG("sleep ok\n");

        return 5;
    }

  private:
    static void button1Pressed()
    {
        DEBUG_MSG("press 1 !\n");
        screen->onPress(Cmd::ON_PRESS_UP_SINGLE);
        #ifdef BUTTON_PIN
                if ((BUTTON_PIN != moduleConfig.canned_message.inputbroker_pin_press) ||
                    !moduleConfig.canned_message.enabled) {
                    powerFSM.trigger(EVENT_PRESS);
                }
        #endif

    }
    static void button1PressedLong()
    {
        // DEBUG_MSG("Long press 1 !\n");
        
        //screen->adjustBrightness();
        // // If user button is held down for 5 seconds, shutdown the device.
        // if ((millis() - longPressTime > 5 * 1000) && (longPressTime > 0)) {

        // } else {
        //     // DEBUG_MSG("Long press %u\n", (millis() - longPressTime));
        // }
    }

    static void button1DoublePressed()
    {
        screen->onPress(Cmd::ON_PRESS_UP_ALT);
        // Doesnt do anything lol disablePin();
        
    }

    static void button1MultiPressed()
    {
        
        // Bluetooth disconnect and restart clearNVS();
    }

    static void button1PressedLongStart()
    {
        if (millis() > 30 * 1000) {
            DEBUG_MSG("Long press 1 start!\n");
            longPressTime = millis();
        }
    }

    static void button1PressedLongStop()
    {
        screen->onPress(Cmd::ON_PRESS_UP_LONG);
        // if (millis() > 30 * 1000) {
        //     DEBUG_MSG("Long press stop!\n");
        //     longPressTime = 0;
        //     if (shutdown_on_long_stop) {
        //         delay(3000);
        //         power->shutdown();
        //     }
        // }
    }

    static void button2Pressed()
    {
        DEBUG_MSG("press 2!\n");
        screen->onPress(Cmd::ON_PRESS_DOWN_SINGLE);
        // #ifdef BUTTON_PIN
        //         if ((BUTTON_PIN != moduleConfig.canned_message.inputbroker_pin_press) ||
        //             !moduleConfig.canned_message.enabled) {
        //             powerFSM.trigger(EVENT_PRESS);
        //         }
        // #endif

    }
    static void button2PressedLong()
    {
        // DEBUG_MSG("Long press!\n");
        
        //DEBUG_MSG("Long press 2 %u\n", (millis() - longPressTime));
        
    }

    static void button2DoublePressed()
    {
        screen->onPress(Cmd::ON_PRESS_DOWN_ALT);
        // Doesnt do anything lol disablePin();
        
    }

    static void button2MultiPressed()
    {
        // Bluetooth disconnect and restart clearNVS();
    }

    static void button2PressedLongStart()
    {
        if (millis() > 30 * 1000) {
            //DEBUG_MSG("Long press 1 start!\n");
            longPressTime = millis();
        }
    }

    static void button2PressedLongStop()
    {
        screen->onPress(Cmd::ON_PRESS_DOWN_LONG);
        if (millis() > 30 * 1000) {
            DEBUG_MSG("Long press stop!\n");
            longPressTime = 0;
        }
    }
};

} // namespace concurrency