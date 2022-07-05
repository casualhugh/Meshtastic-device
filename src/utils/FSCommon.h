#pragma once

#include "configuration.h"

// Cross platform filesystem API

// ESP32 version
#include "LITTLEFS.h"
#define FSCom LITTLEFS
#define FSBegin() FSCom.begin(true)
#define FILE_O_WRITE "w"
#define FILE_O_READ "r"


void fsInit();
void listDir(const char * dirname, uint8_t levels);
void rmDir(const char * dirname);
