#include "wifiUtils.h"

unsigned long WiFiDisconnectSince = 0;

void checkWiFiStatus(const char *ssid, const char *password) {
    if (WiFi.status() != WL_CONNECTED) {
        statusHandler(STATE_WIFI_CONNECTING);
        Serial.println("WiFi lost, reconnecting...");
        WiFi.reconnect();
        if (WiFiDisconnectSince == 0) WiFiDisconnectSince = millis();
    } else {
        WiFiDisconnectSince = 0;
    }
}