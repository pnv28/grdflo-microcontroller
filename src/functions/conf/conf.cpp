#include "conf.h"
#include "functions/stat/stat.h"
#include <Preferences.h>

void conf(char *segment[], const size_t seg_len, const char *payload) {
    if(seg_len < 3) return;
    if(strcmp(segment[1], "all") == 0) return;

    if(strcmp(segment[2], "health") == 0) {
        statHealth();
        return;
    }

    if(strcmp(segment[2], "state") == 0) {
        statState();
        return;
    }

    if(strcmp(segment[2], "reboot") == 0) {
        ESP.restart();
        return;
    }

    if(strcmp(segment[2], "edit") == 0) {
        if (seg_len < 5) return;

        bool ok = false;
        Preferences pref;
        if(strcmp(segment[3], "creds") == 0) {
            pref.begin("creds", false);
            if(pref.isKey(segment[4])) {
                ok = (pref.putString(segment[4], payload) > 0);
            }
            pref.end();
        }

        statAck(segment, seg_len, ok);
        return;
    }
}
