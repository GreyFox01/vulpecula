#pragma once
#include <Arduino.h>
#define WIFI_MODE_STA 1
struct WiFiStub { void mode(int){} void disconnect(bool,bool){} };
static WiFiStub WiFi;
