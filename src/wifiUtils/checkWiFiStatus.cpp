#include "wifiUtils.h"

void checkWiFiStatus(const char *ssid, const char *password) {
    if (WiFi.status() != WL_CONNECTED) {
      statusHandler(STATE_WIFI_CONNECTING);
      Serial.println("WiFi lost, reconnecting...");
      WiFi.disconnect();
      WiFi.begin(ssid, password);
    }
}