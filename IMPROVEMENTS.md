# Improvements & Issues — Ranked by Urgency

> This is not a criticism list. The code is well-structured for a first embedded project.
> These are things to fix before deployment or things that will eventually bite you.

---

## Tier 1 — Fix These Now (actual bugs)

---

### 1. `segment[1]` is accessed without checking if it exists

**File:** `src/mqttManager/mqttManager.cpp`, line 56

**The problem:**

After tokenising the topic, you access `segment[1]` directly:

```cpp
if(strcmp(segment[1], username.c_str()) == 0) {
```

But there is no check that `tokenCount >= 2` before doing this. If the received topic has only one level (e.g., `"test"` — which is **exactly the topic you are currently subscribed to**), then `segment[1]` is an uninitialised pointer. Reading it is undefined behaviour — it will likely crash the device.

Right now this doesn't crash only because the routing calls are commented out, but the `strcmp` itself already reads `segment[1]`.

**The fix:**

Add a guard before that line:

```cpp
if(tokenCount < 2) {
    Serial.println("Topic too short to route");
    break;
}
if(strcmp(segment[1], username.c_str()) == 0) { ...
```

---

### 2. `topic` variable shadows the global

**File:** `src/mqttManager/mqttManager.cpp`, line 36

**The problem:**

You already know this one. Inside `MQTT_EVENT_DATA`:

```cpp
char topic[event->topic_len + 1];   // ← local variable named "topic"
```

This shadows the global `const char* topic` from `config.h`. The global is still correctly used in `MQTT_EVENT_CONNECTED` (lines 20–21), but inside `MQTT_EVENT_DATA`, any reference to `topic` now means the local char array, not the global pointer. Currently harmless because you don't use the global inside that case block — but it is confusing and will cause a subtle bug when you eventually build the real subscription topic from `username`.

**The fix:**

Rename the local variable:

```cpp
char topicBuf[event->topic_len + 1];
memcpy(topicBuf, event->topic, event->topic_len);
topicBuf[event->topic_len] = '\0';
// and then strtok_r on topicBuf
```

---

### 3. `esp_mqtt_client_init()` return value is not checked

**File:** `src/mqttManager/mqttManager.cpp`, line 95

**The problem:**

```cpp
client = esp_mqtt_client_init(&mqtt_cfg);
esp_mqtt_client_register_event(client, ...);   // ← crashes if client is NULL
esp_mqtt_client_start(client);                 // ← crashes if client is NULL
```

If `esp_mqtt_client_init` fails (e.g., out of memory, bad config), it returns `NULL`. The next two calls then dereference `NULL` and the device hard-faults. You will never see the error message — just a crash.

**The fix:**

```cpp
client = esp_mqtt_client_init(&mqtt_cfg);
if (client == NULL) {
    Serial.println("MQTT client init failed — rebooting");
    delay(100);
    ESP.restart();
}
```

---

### 4. `WiFi.persistent()` and `setAutoReconnect()` are called after `WiFi.begin()`

> fixed

**File:** `lib/wifiUtils/src/initWiFiConnection.cpp`, lines 4–6

**The problem:**

```cpp
WiFi.begin(ssid, password);       // ← called first
WiFi.setAutoReconnect(true);      // ← called after
WiFi.persistent(false);           // ← called after
```

Both of these should be set **before** `WiFi.begin()`. Specifically, `WiFi.persistent(false)` is supposed to prevent the WiFi library from saving credentials to flash — but if `WiFi.begin()` runs first, it may have already written them. The auto-reconnect setting may also not apply cleanly to the already-started connection attempt.

**The fix:**

```cpp
WiFi.persistent(false);
WiFi.setAutoReconnect(true);
WiFi.begin(ssid, password);
```

---

## Tier 2 — Fix Before First Real Deployment

---

### 5. `MQTT_EVENT_ERROR` logs nothing useful

**File:** `src/mqttManager/mqttManager.cpp`, line 70

**The problem:**

```cpp
case MQTT_EVENT_ERROR:
    Serial.println("MQTT_EVENT_ERROR");
    break;
```

The event carries an `error_handle` struct with the actual reason — TLS handshake failure, broker rejected the connection, network timeout, etc. Without logging it, you will spend a long time staring at "MQTT_EVENT_ERROR" in the serial monitor with no idea what went wrong.

**The fix:**

```cpp
case MQTT_EVENT_ERROR:
    Serial.println("MQTT_EVENT_ERROR");
    if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
        Serial.printf("  Transport error: %d\n", event->error_handle->esp_transport_sock_errno);
    }
    if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
        Serial.printf("  Connection refused, code: %d\n", event->error_handle->connect_return_code);
    }
    break;
```

---

### 6. `lightPin[10]` and `chargePin[10]` are `extern` declared but never defined

> fixed

**File:** `src/config.h`, `src/config.cpp`

Fixed by replacing the fixed-size `int[10]` arrays with **dynamically allocated pointers**:

```cpp
// config.h
extern int *chargePin;
extern int *lightPin;

// config.cpp
chargePin = new int[pinOffset];
lightPin  = new int[16 - pinOffset];
```

A fixed size of `[10]` would have been the wrong approach anyway — the actual number of pins in each group is only known at boot time after reading `pinOffset` from NVS. Dynamic allocation sizes the arrays exactly to what the device needs, with no wasted memory.

---

### 7. `LED_PIN` is defined in `cmd.cpp` instead of `config.h`

> fixed

**File:** `src/config.h`

`#define LED_PIN 10` has been moved into `config.h` alongside `RED_PIN` and `BLUE_PIN`, where all hardware pin constants belong.

---

### 8. `cmd.cpp` uses a chain of `if` instead of `else if`

**File:** `src/cmd.cpp`, lines 8–43

**The problem:**

```cpp
if(strcmp(payload, "reboot") == 0) { ... }
if(strcmp(payload, "red") == 0) { ... }
if(strcmp(payload, "blue") == 0) { ... }
// etc.
```

Every single condition is evaluated even after a match is found. On a desktop machine this is harmless. On a microcontroller, you're doing unnecessary `strcmp` calls on every message. More importantly, it means if you ever have overlapping conditions, multiple branches can fire.

**The fix:** Use `else if` so evaluation stops at the first match:

```cpp
if(strcmp(payload, "reboot") == 0) { ... }
else if(strcmp(payload, "red") == 0) { ... }
else if(strcmp(payload, "blue") == 0) { ... }
// etc.
```

---

### 9. Missing newline in the heap print

**File:** `src/main.cpp`, line 42

**The problem:**

```cpp
Serial.printf("Free heap: %d bytes", ESP.getFreeHeap());
```

No `\n` at the end. The next serial output (e.g., from `checkWiFiStatus` or the MQTT event handler) runs directly onto the same line in the serial monitor. After a few minutes the logs become unreadable.

**The fix:** Add `\n`:

```cpp
Serial.printf("Free heap: %d bytes\n", ESP.getFreeHeap());
```

---

### 10. `Preferences prefs` is a global but only used once

> fixed

**File:** `src/config.cpp`, line 4

**The problem:**

```cpp
Preferences prefs;   // ← global, lives in RAM for the entire lifetime of the device
```

`prefs` is only ever used inside `getDeviceSpecificConfig()`, which runs once during `setup()`. After `prefs.end()` is called, it's just sitting in RAM doing nothing. On a device with 400KB of RAM this isn't catastrophic, but it's a bad habit.

**The fix:** Make it a local variable inside the function:

```cpp
void getDeviceSpecificConfig() {
    Preferences prefs;
    prefs.begin("creds", true);
    // ...
    prefs.end();
}
```

---

## Tier 3 — Good Habits / Code Quality

---

### 11. VLA (Variable Length Array) stack allocation for topic and payload

**File:** `src/mqttManager/mqttManager.cpp`, lines 36 and 52

**The problem:**

```cpp
char topic[event->topic_len + 1];    // size known only at runtime
char payload[event->data_len + 1];   // size known only at runtime
```

VLAs are allocated on the **stack** at runtime. In standard C++ they are not even legal (GCC allows them as an extension). On embedded systems, the stack is small. The ESP-IDF MQTT client caps incoming message size at the buffer size (default 1024 bytes), so in practice you won't overflow — but it is fragile because:

- It relies on an implicit cap you don't control from this code.
- If that cap is ever changed, you get a silent stack overflow.

**The fix:** Use fixed-size buffers with an explicit size check:

```cpp
const size_t MAX_TOPIC_LEN = 256;
const size_t MAX_PAYLOAD_LEN = 512;

if (event->topic_len >= MAX_TOPIC_LEN || event->data_len >= MAX_PAYLOAD_LEN) {
    Serial.println("Message too large, dropping");
    break;
}

char topicBuf[MAX_TOPIC_LEN];
char payload[MAX_PAYLOAD_LEN];
```

---

### 12. `esp_task_wdt_add()` return value not checked

**File:** `src/main.cpp`, line 26

**The problem:**

```cpp
esp_task_wdt_add(NULL);
```

If this fails (e.g., the task is already subscribed from a previous boot cycle without a clean reset), the call returns an error code that is silently ignored. The watchdog is then not actually protecting the main task and you won't know.

**The fix:**

```cpp
esp_err_t wdt_err = esp_task_wdt_add(NULL);
if (wdt_err != ESP_OK) {
    Serial.printf("WDT add failed: %d\n", wdt_err);
}
```

---

### 13. The `initWiFiConnection` blocking loop doesn't reset the WDT

**File:** `lib/wifiUtils/src/initWiFiConnection.cpp`, lines 9–13

**The problem:**

The blocking connection loop runs for up to 10 seconds (20 attempts × 500ms delay). The WDT is set to 30 seconds and is armed before this function is called, so right now it's fine — 10 seconds is well within the timeout.

But: if you ever increase `attempts` or the delay (say, to give the router more time), you might silently exceed the 30-second WDT, causing a reboot during WiFi connect — which would cause an infinite reboot loop.

**The fix:** Add a WDT reset inside the loop:

```cpp
while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    esp_task_wdt_reset();   // ← keep the WDT happy during the wait
    delay(500);
    Serial.print(".");
    attempts++;
}
```

---

## Tier 4 — Future / Think About These Later

---

### 14. No OTA (Over-The-Air) firmware update

This is the most important missing piece for any real deployment.

Right now, updating firmware on a deployed device means physical access — plugging in USB, flashing manually. For a grid monitoring device in the field, that's not realistic.

The ESP32 has built-in OTA support (`esp_ota_ops.h`). The typical pattern for IoT devices is: the device receives an MQTT command with a URL, downloads the new firmware binary from an HTTP server, writes it to the inactive OTA partition, and reboots into it. The `cmnd` handler (once implemented) would be the natural place to trigger this.

This is not urgent now, but should be planned before any device leaves your hands.

---

### 15. NVS partition is not encrypted

The credentials stored in NVS (WiFi password, MQTT password) are currently sitting in flash in plaintext. Anyone with physical access to the chip can dump the flash with `esptool.py` in under 30 seconds.

The ESP32 supports **NVS encryption** using a per-device key that is burned into eFuses. Once eFuses are burned, the key cannot be read out. This is the right solution for production hardware.

Not urgent during development, but something to plan for before devices go to customers or field installations.

---

### 16. The `topic` global will need to become dynamic

Currently:

```cpp
const char* topic = "test";
```

The real design subscribes to `cmnd/<device_id>/#` and publishes to `stat/<device_id>/...`. The topic needs to be constructed at runtime from `username` (which comes from NVS and isn't known at compile time). The current hardcoded `topic` is a dev placeholder — remember to replace it when you wire up the real subscription in `MQTT_EVENT_CONNECTED`.

---

### 17. No telemetry loop

The `tele` namespace is planned but nothing is published yet. A basic telemetry interval (free heap, WiFi RSSI, uptime) is very useful in production to detect slow memory leaks or degraded WiFi signal before they become outages. This can be added into the existing 60-second `loop()` interval once the MQTT topic structure is finalised.

---

### 18. `MQTT_EVENT_ERROR` could re-enable the error counter

The `globalErrorCount` mechanism (currently commented out everywhere) was a good idea. Combined with proper error logging from item #5 above, you could re-enable it: increment on `MQTT_EVENT_ERROR`, reset on `MQTT_EVENT_CONNECTED`, and reboot if it exceeds a threshold. This makes the device self-healing in the field when the broker has transient issues.

---

## Summary Table

| # | Issue | File | Severity |
|---|-------|------|----------|
| 1 | `segment[1]` out-of-bounds access | `mqttManager.cpp:56` | **Fix now** |
| 2 | `topic` variable shadowing | `mqttManager.cpp:36` | **Fix now** |
| 3 | MQTT init return value not checked | `mqttManager.cpp:95` | **Fix now** |
| 4 | `WiFi.persistent` called after `WiFi.begin` | `initWiFiConnection.cpp:4` | **Fix now** |
| 5 | MQTT errors log nothing useful | `mqttManager.cpp:70` | Before deployment |
| 6 | `lightPin/chargePin` declared but not defined | `config.h:25` | ~~Before deployment~~ **fixed** |
| 7 | `LED_PIN` not in `config.h` | `cmd.cpp:3` | ~~Before deployment~~ **fixed** |
| 8 | `if` chain should be `else if` | `cmd.cpp:8` | Before deployment |
| 9 | Missing `\n` in heap print | `main.cpp:42` | Before deployment |
| 10 | `Preferences` should be a local variable | `config.cpp:4` | Before deployment |
| 11 | VLA stack allocation | `mqttManager.cpp:36,52` | Good habit |
| 12 | WDT add return value unchecked | `main.cpp:26` | Good habit |
| 13 | Blocking WiFi loop doesn't reset WDT | `initWiFiConnection.cpp:9` | Good habit |
| 14 | No OTA update mechanism | — | Future |
| 15 | NVS not encrypted | — | Future |
| 16 | `topic` global needs to become dynamic | `config.cpp:30` | Future |
| 17 | No telemetry publishing | — | Future |
| 18 | Error counter not re-enabled | — | Future |
