#include "cmnd.h"
#include <config.h>

int light(int lightID, bool state);
int cycle(unsigned int timeInSeconds, char *segment[]);

int cycleID = 0;
unsigned long cycleInterval = 0;
unsigned long cycleStart = 0;
bool cycleFlag = false;

void cmnd(char *segment[], const size_t seg_len, const char *payload) {

    int status;

    if(strcmp(segment[2], "charger") == 0) {

        if(seg_len >= 5 && strcmp(segment[4], "cycle") == 0) {
            cycle(atoi(payload), segment);
            return;
        }

        status = charger(atoi(segment[3]), atoi(payload));
    }
    if(strcmp(segment[2], "light") == 0) {
        status = light(atoi(segment[3]), atoi(payload));
    }
}


int charger(int chargerID, bool state) {

    if(chargerID >= pinOffset) {
        Serial.println("ChargerID can not be greater than pinOffset");
        return -1;
    }

    if(state) {
        digitalWrite(chargerPin[chargerID], HIGH);
        Serial.printf("Charger %d (GPIO %d) -> ON\n", chargerID, chargerPin[chargerID]);
    } else if(!state) {
        digitalWrite(chargerPin[chargerID], LOW);
        Serial.printf("Charger %d (GPIO %d) -> OFF\n", chargerID, chargerPin[chargerID]);
    }

    return 0;
}

int light(int lightID, bool state) {

    if(lightID >= (totalPins-pinOffset)) {
        Serial.println("PinID can not be greater than totalPins-pinOffset");
        return -1;
    }

    if(state) {
        digitalWrite(lightPin[lightID], HIGH);
        Serial.printf("Light %d (GPIO %d) -> ON\n", lightID, lightPin[lightID]);
    } else if(!state) {
        digitalWrite(lightPin[lightID], LOW);
        Serial.printf("Light %d (GPIO %d) -> OFF\n", lightID, lightPin[lightID]);
    }

    return 0;
}

int cycle(unsigned int timeInSeconds, char *segment[]) {
    cycleFlag = true;
    charger(atoi(segment[3]), false);
    cycleID = atoi(segment[3]);
    cycleInterval = timeInSeconds*1000;
    cycleStart = millis();
    Serial.printf("Cycle started: charger %d, interval %lu ms\n", cycleID, cycleInterval);

    return 0;
}