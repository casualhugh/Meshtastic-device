#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoOTA.h>
#define mySSID "Ebeling_Wireless_2.4Ghz"
#define myPASSWORD "99994929"

void setupOTA(const char *nameprefix, const char *ssid, const char *password);

void wifiRunOnce();