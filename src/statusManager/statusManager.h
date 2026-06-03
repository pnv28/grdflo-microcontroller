#ifndef STATUSMANAGER_H
#define STATUSMANAGER_H

#define LED_PIN 10

#define STATE_BOOT 0
#define STATE_WIFI_CONNECTING 1
#define STATE_MQTT_CONNECTING 2
#define STATE_MQTT_DISCONNECTED 3
#define STATE_ALL_IS_WELL 4
#define STATE_ERROR 255

#include <Arduino.h>

void statusHandler(const u8_t statusCode);

#endif