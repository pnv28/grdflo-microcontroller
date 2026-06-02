#include "cmnd.h"

int charger(int chargerID, bool state);
int light(int lightID, bool state);
int cycle(unsigned int timeInSeconds);

/* 
assuming max_seg is 6 [ 0 - 5 ]

rough scratch board for self, while working, helps me visualize

i know that segment[0] is command, I know that segment[1] is device id, both are being checkedin mqtt manager.

So, segment[2] has to be 

*/

void cmnd(char *segment[], const size_t seg_len, const char *payload) {

}