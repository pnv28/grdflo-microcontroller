#include <Arduino.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <esp_task_wdt.h>

#include "provision.h"
#include "statusManager/statusManager.h"
#include "config.h"

static constexpr uint32_t BANNER_INTERVAL_MS = 1000;

static constexpr size_t   MAX_LINE_LEN       = 1024;

static constexpr int      GPIO_MAX           = 21;


static void sendErr(const char* reason) {
    Serial.print("<<ERR ");
    Serial.print(reason);
    Serial.println(">>");
}

static void handleLine(const String& line) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, line);
    if (err) {
        sendErr(err.c_str());
        return;
    }

    const char* _ssid    = doc["wifi_ssid"];
    const char* _wifiPwd = doc["wifi_pass"];
    const char* _devId   = doc["dev_id"];
    const char* _mqttPwd = doc["mqtt_pass"];
    if (_ssid    == nullptr) { sendErr("missing wifi_ssid"); return; }
    if (_wifiPwd == nullptr) { sendErr("missing wifi_pass"); return; }
    if (_devId   == nullptr) { sendErr("missing dev_id");    return; }
    if (_mqttPwd == nullptr) { sendErr("missing mqtt_pass"); return; }
    if (strlen(_ssid)    == 0) { sendErr("empty wifi_ssid"); return; }
    if (strlen(_wifiPwd) == 0) { sendErr("empty wifi_pass"); return; }
    if (strlen(_devId)   == 0) { sendErr("empty dev_id");    return; }
    if (strlen(_mqttPwd) == 0) { sendErr("empty mqtt_pass"); return; }

    if (!doc["pin_offset"].is<int>()) { sendErr("missing pin_offset"); return; }
    if (!doc["pin_total"].is<int>())  { sendErr("missing pin_total");  return; }
    int _offset = doc["pin_offset"].as<int>();
    int _total  = doc["pin_total"].as<int>();
    if (_total  < 1 || _total > 16)   { sendErr("pin_total out of range (1..16)"); return; }
    if (_offset < 0 || _offset > _total) { sendErr("pin_offset out of range (0..pin_total)"); return; }

    JsonObject pinMapObj = doc["pin_map"];
    if (pinMapObj.isNull()) { sendErr("missing pin_map"); return; }

    uint8_t _pinMap[16];
    for (int i = 0; i < 16; i++) _pinMap[i] = 255;

    char key[2] = {0};
    for (int i = 0; i < _total; i++) {
        key[0] = 'A' + i;
        JsonVariant v = pinMapObj[key];
        if (v.isNull() || !v.is<int>()) {
            sendErr("pin_map missing or non-integer slot");
            return;
        }
        int gpio = v.as<int>();
        if (gpio < 0 || gpio > GPIO_MAX) {
            sendErr("pin_map GPIO out of range (0..21)");
            return;
        }
        _pinMap[i] = (uint8_t) gpio;
    }

    Preferences prefs;

    prefs.begin("creds", false);
    prefs.putString("wifi_ssid", _ssid);
    prefs.putString("wifi_pass", _wifiPwd);
    prefs.putString("dev_id",    _devId);
    prefs.putString("mqtt_pass", _mqttPwd);
    prefs.end();

    prefs.begin("pinDistribution", false);
    prefs.putUChar("offset",   (uint8_t) _offset);
    prefs.putUChar("totalPin", (uint8_t) _total);
    prefs.end();

    prefs.begin("pinMapping", false);
    for (int i = 0; i < _total; i++) {
        key[0] = 'A' + i;
        prefs.putUChar(key, _pinMap[i]);
    }
    prefs.end();

    Serial.println("<<OK v1>>");
    Serial.flush();
    delay(5000);
    ESP.restart();
}


void provision() {
    statusHandler(STATE_PROVISION);
    Serial.println("<<GRDFLO-PROVISION-READY v1>>");

    String buf;
    buf.reserve(MAX_LINE_LEN);
    uint32_t lastBanner = millis();

    while (true) {
        uint32_t now = millis();
        if (now - lastBanner >= BANNER_INTERVAL_MS) {
            Serial.println("<<GRDFLO-PROVISION-READY v1>>");
            lastBanner = now;
        }

        while (Serial.available() > 0) {
            char c = (char) Serial.read();
            if (c == '\r') continue;              
            if (c == '\n') {
                if (buf.length() > 0) {
                    handleLine(buf);             
                    buf = "";
                }
            } else if (buf.length() < MAX_LINE_LEN) {
                buf += c;
            } else {
                sendErr("line too long");
                buf = "";
                while (Serial.available() > 0 && Serial.read() != '\n') {}
            }
        }

        esp_task_wdt_reset();

        delay(10); 
    }
}

void handleSerialPort() {
    static char buf[48];
    static uint8_t len = 0;
    while (Serial.available() > 0) {
        char c = Serial.read();
        if (c == '\r') continue;
        if (c == '\n') {
            buf[len] = '\0';
            if (len && strstr(buf, "GRDFLO-WHOAMI"))
                Serial.printf("<<GRDFLO-INITIALISED v1 dev=%s>>\n", username.c_str());
            len = 0;
        } else if (len < sizeof(buf) - 1) buf[len++] = c;
        else len = 0;
    }
}