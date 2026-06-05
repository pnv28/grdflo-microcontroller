#include "stat.h"
#include "mqttManager/mqttManager.h"
#include <WiFi.h>

void statHealth() {
    char payload[160];
    snprintf(payload, sizeof(payload),
        "{\"heap\":%u,\"rssi\":%d,\"uptime_ms\":%lu,\"err\":%u}",
        (unsigned)ESP.getFreeHeap(),
        (int)WiFi.RSSI(),
        (unsigned long)millis(),
        globalErrorCounter);

    String topic = "stat/" + username + "/health";
    mqttPublish(topic.c_str(), payload, 1, 1, true);
}

void statAck(const char *originTopic, bool ok) {
    char payload[192];
    snprintf(payload, sizeof(payload),
        "{\"topic\":\"%s\",\"ok\":%s,\"t\":%lu}",
        originTopic ? originTopic : "",
        ok ? "true" : "false",
        (unsigned long)millis());

    String topic = "stat/" + username + "/ack";
    mqttPublish(topic.c_str(), payload, 1, 0, true);
}

void statAck(char *segment[], size_t seg_len, bool ok) {
    String original;
    for (size_t i = 0; i < seg_len; i++) {
        if (i > 0) original += "/";
        original += segment[i];
    }
    statAck(original.c_str(), ok);
}

void statState() {
    // {"chargers":[..pinOffset..],"lights":[..totalPins-pinOffset..]}
    // Worst case: 16 entries * 2 chars + commas + keys ≈ 80; 256 is comfortable.
    char payload[256];
    int n = 0;

    n += snprintf(payload + n, sizeof(payload) - n, "{\"chargers\":[");
    for(int i = 0; i < pinOffset; i++) {
        n += snprintf(payload + n, sizeof(payload) - n,
                      "%s%d", i ? "," : "", relayState[i]);
    }
    n += snprintf(payload + n, sizeof(payload) - n, "],\"lights\":[");
    for(int i = pinOffset; i < totalPins; i++) {
        n += snprintf(payload + n, sizeof(payload) - n,
                      "%s%d", i == pinOffset ? "" : ",", relayState[i]);
    }
    snprintf(payload + n, sizeof(payload) - n, "]}");

    String topic = "stat/" + username + "/state";
    mqttPublish(topic.c_str(), payload, 1, 1, true);
}
