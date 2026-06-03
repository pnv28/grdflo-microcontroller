#include "conf.h"
#include "functions/stat/stat.h"
#include <Preferences.h>

/*
seg[0] = conf, seg[1] = device id, seg[2] = subcommand.

subcommands:
  health                                  -> publish stat/<dev>/health
  state                                   -> publish stat/<dev>/state
  edit/<namespace>/<key>  payload=<value> -> NVS write, then stat/<dev>/ack
*/

void conf(char *segment[], const size_t seg_len, const char *payload) {
    if (seg_len < 3) return;

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
                pref.putString(segment[4], payload);
                ok = true;
            }
            pref.end();
        }

        statAck(segment, seg_len, ok);
        return;
    }
}
