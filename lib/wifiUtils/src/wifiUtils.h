#ifndef WIFIUTILS_H
#define WIFIUTILS_H

#include <Arduino.h>
#include <WiFi.h>

void initWiFiConnection(const char *ssid, const char *password);
void checkWiFiStatus(const char *ssid, const char *password);

#endif