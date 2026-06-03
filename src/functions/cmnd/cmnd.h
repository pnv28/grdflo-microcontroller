#ifndef CMND_H
#define CMND_H

#include <Arduino.h>

void cmnd(char *segment[], const size_t seg_len, const char *payload);
int charger(int chargerID, bool state);

extern int cycleID;

#endif