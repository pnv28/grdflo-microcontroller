#include "wifiUtils.h"

unsigned long WiFiDiconnectSince = 0;

void checkWiFiStatus(const char *ssid, const char *password) {
    if (WiFi.status() != WL_CONNECTED) {
      statusHandler(STATE_WIFI_CONNECTING);
      Serial.println("WiFi lost, reconnecting...");
      WiFi.reconnect();
      WiFiDiconnectSince = millis();
      return;
    }
    WiFiDiconnectSince = 0;
}