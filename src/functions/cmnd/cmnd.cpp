#include "cmnd.h"
#include <config.h>
#include "functions/stat/stat.h"

int light(int lightID, bool state);
int cycle(unsigned int timeInSeconds, char *segment[]);

int cycleID = 0;
unsigned long cycleInterval = 0;
unsigned long cycleStart = 0;
bool cycleFlag = false;

void cmnd(char *segment[], const size_t seg_len, const char *payload) {
    if (seg_len < 4) return;

    int status = -1;

    if(strcmp(segment[2], "charger") == 0) {
        if(seg_len >= 5 && strcmp(segment[4], "cycle") == 0) {
            status = cycle(atoi(payload), segment);
        } else {
            status = charger(atoi(segment[3]), atoi(payload));
        }
    } else if(strcmp(segment[2], "light") == 0) {
        if(strcmp(segment[3], "all") == 0) {
            status = 0;
            for(int i = 0; i < (totalPins-pinOffset); i++) {
                int tempStatus = light(i, atoi(payload));
                if(tempStatus != 0) status = -1;
            }
        } else {
            status = light(atoi(segment[3]), atoi(payload));
        }
    } else {
        return; // unrecognised group — don't ack a topic we ignored
    }

    statAck(segment, seg_len, status == 0);
}


int charger(int chargerID, bool state) {

    if(chargerID >= pinOffset) {
        Serial.println("ChargerID can not be greater than pinOffset");
        return -1;
    }

    if(state) {
        digitalWrite(chargerPin[chargerID], HIGH);
        relayState[chargerID] = 1;
        Serial.printf("Charger %d (GPIO %d) -> ON\n", chargerID, chargerPin[chargerID]);
    } else if(!state) {
        digitalWrite(chargerPin[chargerID], LOW);
        relayState[chargerID] = 0;
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
        relayState[pinOffset + lightID] = 1;
        Serial.printf("Light %d (GPIO %d) -> ON\n", lightID, lightPin[lightID]);
    } else if(!state) {
        digitalWrite(lightPin[lightID], LOW);
        relayState[pinOffset + lightID] = 0;
        Serial.printf("Light %d (GPIO %d) -> OFF\n", lightID, lightPin[lightID]);
    }

    return 0;
}

int cycle(unsigned int timeInSeconds, char *segment[]) {
    int chargerID = atoi(segment[3]);
    int s = charger(chargerID, false);
    if (s != 0) return s;

    cycleFlag = true;
    cycleID = chargerID;
    cycleInterval = timeInSeconds*1000;
    cycleStart = millis();
    Serial.printf("Cycle started: charger %d, interval %lu ms\n", cycleID, cycleInterval);

    return 0;
}