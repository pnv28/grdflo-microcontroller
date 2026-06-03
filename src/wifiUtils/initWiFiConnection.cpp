#include "wifiUtils.h"

void initWiFiConnection(const char *ssid, const char *password) {
    statusHandler(STATE_WIFI_CONNECTING);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);
    WiFi.begin(ssid, password);
    Serial.println("Trying to connect to WiFi");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() != WL_CONNECTED) {
        statusHandler(STATE_ERROR);
        Serial.println("WiFi failed, rebooting...");
        delay(5000);
        ESP.restart();
    }

    Serial.printf("\nConnected to %s\n", ssid);
    Serial.println(WiFi.localIP());
}