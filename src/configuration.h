/*

TTGO T-BEAM Tracker for The Things Network

Copyright (C) 2018 by Xose Pérez <xose dot perez at gmail dot com>

This code requires LMIC library by Matthijs Kooijman
https://github.com/matthijskooijman/arduino-lmic

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#pragma once
#include <Arduino.h>
// -----------------------------------------------------------------------------
// Version
// -----------------------------------------------------------------------------

// If app version is not specified we assume we are not being invoked by the build script
#ifndef APP_VERSION
#error APP_VERSION must be set by the build environment
#endif

// FIXME: This is still needed by the Bluetooth Stack and needs to be replaced by something better. Remnant of the old versioning system.
#ifndef HW_VERSION
#define HW_VERSION "1.0"
#endif
//#define NO_SCREEN

// -----------------------------------------------------------------------------
// Configuration
// -----------------------------------------------------------------------------

// If we are using the JTAG port for debugging, some pins must be left free for that (and things like GPS have to be disabled)
// we don't support jtag on the ttgo - access to gpio 12 is a PITA
#define REQUIRE_RADIO true // If true, we will fail to start if the radio is not found

/// Convert a preprocessor name into a quoted string
#define xstr(s) str(s)
#define str(s) #s

/// Convert a preprocessor name into a quoted string and if that string is empty use "unset"
#define optstr(s) (xstr(s)[0] ? xstr(s) : "")

//
// Standard definitions for ESP32 targets
//
#define HAS_WIFI
#define PIN_GPS_WAKE 27//forceon
#define GPS_SERIAL_NUM 1

//#define WANT_WIFI


// -----------------------------------------------------------------------------
// Feature toggles
// -----------------------------------------------------------------------------

// Disable use of the NTP library and related features
#define DISABLE_NTP

// Disable the welcome screen and allow 
#define DISABLE_WELCOME_UNSET

// -----------------------------------------------------------------------------
// OLED & Input
// -----------------------------------------------------------------------------

#define SSD1306_ADDRESS 0x3C
#define ST7567_ADDRESS 0x3F

// The SH1106 controller is almost, but not quite, the same as SSD1306
// Define this if you know you have that controller or your "SSD1306" misbehaves.
//#define USE_SH1106

// Flip the screen upside down by default as it makes more sense on T-BEAM
// devices. Comment this out to not rotate screen 180 degrees.
//#define SCREEN_FLIP_VERTICALLY

// Define if screen should be mirrored left to right
// #define SCREEN_MIRROR

// The m5stack I2C Keyboard (also RAK14004)
#define CARDKB_ADDR 0x5F

// The older M5 Faces I2C Keyboard
#define FACESKB_ADDR 0x88

// -----------------------------------------------------------------------------
// SENSOR
// -----------------------------------------------------------------------------
#define BME_ADDR 0x76
#define BME_ADDR_ALTERNATE 0x77
#define MCP9808_ADDR 0x18
#define INA_ADDR 0x40
#define INA_ADDR_ALTERNATE 0x41

// -----------------------------------------------------------------------------
// GPS
// -----------------------------------------------------------------------------

#define GPS_BAUDRATE 9600

#ifndef GPS_THREAD_INTERVAL
#define GPS_THREAD_INTERVAL 100
#endif

#if defined(TBEAM_V10)
// This string must exactly match the case used in release file names or the android updater won't work
#define HW_VENDOR HardwareModel_TBEAM
#elif defined(TBEAM_V07)
#define HW_VENDOR HardwareModel_TBEAM0p7
#endif
#include "variant.h"
#include "RF95Configuration.h"
#include "DebugConfiguration.h"
