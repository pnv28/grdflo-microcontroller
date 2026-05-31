#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// GPIO Pin Definition
#define RED_PIN 20
#define BLUE_PIN 5

// Max Segment
#define MAX_SEGMENT 6

// note i might have fried gpio pin 21

extern const char* ca_cert;

extern  String ssid;
extern  String wifiPassword;

extern const char* brokerUri;
extern const char* topic;
extern  String username;
extern  String password;

extern int lightPin[10];
extern int chargePin[10];

void getDeviceSpecificConfig();

#endif