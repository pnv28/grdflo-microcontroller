#include "wifiUtils.h"

void initWiFiConnection(const char *ssid, const char *password) {
    WiFi.begin(ssid, password);
    WiFi.setAutoReconnect(true);
    WiFi.persistent(false);
    Serial.println("Trying to connect to WiFi");
    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("WiFi failed, rebooting...");
        ESP.restart();
    }

    Serial.printf("\nConnected to %s\n", ssid);
    Serial.println(WiFi.localIP());
}