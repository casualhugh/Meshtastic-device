/**
 * @brief This class enables on the fly software and hardware setup.
 *        It will contain all command messages to change internal settings.
 */
#ifndef COMMAND_H
#define COMMAND_H
enum class Cmd {
        INVALID,
        SET_ON,
        SET_OFF,
        ON_PRESS_UP_SINGLE,
        ON_PRESS_UP_LONG,
        ON_PRESS_UP_ALT,
        ON_PRESS_DOWN_SINGLE,
        ON_PRESS_DOWN_LONG,
        ON_PRESS_DOWN_ALT,
        START_BLUETOOTH_PIN_SCREEN,
        START_FIRMWARE_UPDATE_SCREEN,
        STOP_BLUETOOTH_PIN_SCREEN,
        STOP_BOOT_SCREEN,
        PRINT,
        START_SHUTDOWN_SCREEN,
};
#endif