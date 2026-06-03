#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// GPIO Pin Definition
#define RED_PIN 21
#define BLUE_PIN 21 // i know it is bricked
#define LED_PIN 10

// Max Segment
#define MAX_SEGMENT 6

// note i might have fried gpio pin 21

extern const char* ca_cert;
extern unsigned int globalErrorCounter;

extern  String ssid;
extern  String wifiPassword;

extern const char* brokerUri;
extern const char* testTopic;
extern  String username;
extern  String password;

extern u8_t pinOffset;
extern u8_t totalPins;
extern int *chargerPin;
extern int *lightPin;

extern unsigned long cycleInterval;
extern unsigned long cycleStart;
extern bool cycleFlag;

void getDeviceSpecificConfig();

#endif