#include "cmnd.h"

int charger(int chargerID, bool state);
int light(int lightID, bool state);
int cycle(unsigned int timeInSeconds);

/* 
assuming max_seg is 6 [ 0 - 5 ]

rough scratch board for self, while working, helps me visualize

i know that segment[0] is command, I know that segment[1] is device id, both are being checkedin mqtt manager.

So, segment[2] has to be either charger or light


will implement cycle later

*/

void cmnd(char *segment[], const size_t seg_len, const char *payload) {

    int status;

    if(strcmp(segment[2], "charger") == 0) {
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
    } else if(!state) {
        digitalWrite(chargerPin[chargerID], LOW);
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
    } else if(!state) {
        digitalWrite(lightPin[lightID], LOW);
    }

    return 0;
}