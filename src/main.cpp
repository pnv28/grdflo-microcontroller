#include "config.h"

#include <WiFi.h>
#include "esp_task_wdt.h"

#include "cmd.h"
#include "wifiUtils.h"
#include "mqttManager/mqttManager.h"

esp_task_wdt_config_t wdt_cfg = {
  .timeout_ms     = 30000,
  .idle_core_mask = 0,
  .trigger_panic  = true
};

unsigned long prevMillis = 0;
const unsigned long interval = 60000;

void setup() {
  Serial.begin(115200);

  pinMode(RED_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);

  esp_task_wdt_reconfigure(&wdt_cfg);
  esp_task_wdt_add(NULL);

  getDeviceSpecificConfig();

  delay(3000); // waiting for everything to initialise
  initWiFiConnection(ssid.c_str(), wifiPassword.c_str());

  initMqtt();
}

void loop() {
  esp_task_wdt_reset();
  unsigned long currMillis = millis();

  if((currMillis - prevMillis) >= interval) {
    prevMillis = currMillis;
    Serial.printf("Free heap: %d bytes", ESP.getFreeHeap());
    // Serial.printf("Free heap: %d bytes | Error Count: %d\n", ESP.getFreeHeap(), globalErrorCount);


    checkWiFiStatus(ssid.c_str(), wifiPassword.c_str());

    // if(globalErrorCount >= 5) {
    //   Serial.println("Too many errors --> Rebooting");
    //   delay(100);
    //   ESP.restart();
    // }

  }
}
