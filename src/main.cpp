#include "config.h"

#include <WiFi.h>
#include "esp_task_wdt.h"

#include "cmd.h"
#include "wifiUtils/wifiUtils.h"
#include "statusManager/statusManager.h"

#include "mqttManager/mqttManager.h"
#include "functions/cmnd/cmnd.h"
#include "functions/stat/stat.h"


esp_task_wdt_config_t wdt_cfg = {
  .timeout_ms     = 30000,
  .idle_core_mask = 0,
  .trigger_panic  = true
};

unsigned long prevMillis = 0;
const unsigned long interval = 60000;

void setup() {
  statusHandler(STATE_BOOT);
  Serial.begin(115200);

  Serial.printf("BOOT TIME FREE HEAP: %d\n", ESP.getFreeHeap());

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
  // cycleStart = currMillis;

  if(cycleFlag) {
    if((currMillis - cycleStart) >= cycleInterval) {
      Serial.printf("Cycle fired: charger %d re-enabling after %lu ms\n", cycleID, cycleInterval);
      charger(cycleID, true);
      cycleFlag = false;
    }
  }

  if((currMillis - prevMillis) >= interval) {
    prevMillis = currMillis;
    Serial.printf("[%lu]Free heap: %d bytes\n", currMillis, ESP.getFreeHeap());

    if(globalErrorCounter >= 5) {
      ESP.restart();
    }
    statHealth();
    checkWiFiStatus(ssid.c_str(), wifiPassword.c_str()); 
  }
}
