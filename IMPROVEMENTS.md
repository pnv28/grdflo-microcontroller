# Improvements & Issues — Ranked by Urgency

> This is not a criticism list. The code is well-structured for a first embedded project.
> These are things to fix before deployment or things that will eventually bite you.

---

## Tier 1 — Fix These Now (actual bugs)

---

### 1. `segment[1]` is accessed without checking if it exists

> fixed

**File:** `src/mqttManager/mqttManager.cpp`

**The original problem:**

After tokenising the topic, the code accessed `segment[1]` directly with no check that `tokenCount >= 2` first. A topic with only one level (e.g., `"test"` — the boot-announce topic the device was subscribed to) would make `segment[1]` an uninitialised pointer — undefined behaviour, likely a crash.

**Resolution:** The routing block is now live with `if (tokenCount < 3) return;` as the first check in `MQTT_EVENT_DATA`, executed before any `segment[]` access. The `< 3` threshold guards both `segment[1]` (device ID) and `segment[2]` (sub-topic) in one check.

---

### 2. `topic` variable shadows the global

> **Non-issue — moot.** This was filed against a global `const char* topic`, but the variable in `config.h` / `config.cpp` was always named `testTopic`, not `topic`. There is no global named `topic` for the local `char topic[...]` in `MQTT_EVENT_DATA` to shadow. The item is kept for historical reference.

**File:** `src/mqttManager/mqttManager.cpp`

**Original concern:** The local `char topic[event->topic_len + 1]` inside `MQTT_EVENT_DATA` was believed to shadow a global `const char* topic`. The routing block is now live and the local variable is active. In practice there is no shadowing because the only related global in `config.h` is `testTopic`, a different name entirely. No rename to `topicBuf` is needed.

---

### 3. `esp_mqtt_client_init()` return value is not checked

**File:** `src/mqttManager/mqttManager.cpp`, line 97

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

**File:** `src/wifiUtils/initWiFiConnection.cpp`, lines 4–6

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

### 4a. `MQTT_EVENT_DATA` forwards every message to `cmd()` with no device-ID filter

> fixed

**File:** `src/mqttManager/mqttManager.cpp`, line 66

**The problem:**

The structured routing block (tokenisation, `segment[1] == username` check, dispatch into `cmnd/stat/tele`) is currently commented out. The active line in `MQTT_EVENT_DATA` is:

```cpp
cmd(payload);
```

This runs on **every** incoming message, regardless of topic, regardless of which client published it. On the dev topic `"test"` (which this device is also subscribed to), any payload from any source — including stale retained messages or messages from another bench device — will be acted on. Payloads like `"reboot"` will hard-reset the device; `"rp"`/`"bp"` will toggle indicator GPIOs.

That is acceptable for bench work, but the moment this firmware sees a shared production broker without the device-ID filter, behaviour becomes unpredictable.

**The fix:**

Re-enable the structured routing block above the `cmd(payload)` call (and apply item #2 when you do — rename the local `topic` to `topicBuf`). Once routing is live, the `cmd(payload)` line should be removed entirely or gated behind a debug-only subtopic — it predates the structured router and is not part of the production message flow.

**Resolution:** The structured tokenisation + dispatch block is now live in `MQTT_EVENT_DATA`. Topics are tokenised on `/`, `segment[1]` is verified against `username` (or the literal `"all"` for broadcast), and dispatch runs on `segment[0]` — `cmnd → cmnd()` and `conf → conf()`. The legacy `cmd(payload)` call site is wrapped in a `/* DEPRECIATED */` block and no longer reached.

---

## Tier 2 — Fix Before First Real Deployment

---

### 5. `MQTT_EVENT_ERROR` logs nothing useful

**File:** `src/mqttManager/mqttManager.cpp`, line 71

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

### 6. `lightPin[10]` and `chargerPin[10]` are `extern` declared but never defined

> fixed

**File:** `src/config.h`, `src/config.cpp`

Fixed by replacing the fixed-size `int[10]` arrays with **dynamically allocated pointers**:

```cpp
// config.h
extern int *chargerPin;
extern int *lightPin;

// config.cpp
chargerPin = new int[pinOffset];
lightPin   = new int[totalPins - pinOffset];
```

A fixed size of `[10]` would have been the wrong approach anyway — the actual number of pins in each group is only known at boot time after reading `pinOffset` and `totalPins` from NVS. Dynamic allocation sizes the arrays exactly to what the device needs, with no wasted memory.

---

### 7. `LED_PIN` is defined in `cmd.cpp` instead of `config.h`

> fixed

**File:** `src/config.h`

`#define LED_PIN 10` has been moved into `config.h` alongside `RED_PIN` and `BLUE_PIN`, where all hardware pin constants belong.

---

### 8. `cmd.cpp` uses a chain of `if` instead of `else if`

**File:** `src/cmd.cpp`, lines 7–42

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

**File:** `src/mqttManager/mqttManager.cpp`, lines 46 and 64 (both now active)

**The problem:**

```cpp
char topic[event->topic_len + 1];    // size known only at runtime — ACTIVE
char payload[event->data_len + 1];   // size known only at runtime — ACTIVE
```

Both VLAs are now live (the routing block is uncommented). VLAs are allocated on the **stack** at runtime. In standard C++ they are not legal (GCC allows them as an extension). On embedded systems, the stack is small. The ESP-IDF MQTT client caps incoming message size at the buffer size (default 1024 bytes), so in practice you won't overflow — but it is fragile because:

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

**File:** `src/wifiUtils/initWiFiConnection.cpp`, lines 9–13

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

### 16. Boot-time online announce still uses a hardcoded topic and label

**Partially resolved.** The per-device subscriptions (`cmnd/<username>/#`, `conf/<username>/#`) are already built dynamically from `username` in `MQTT_EVENT_CONNECTED` — that part is done. What remains:

```cpp
// config.cpp
const char* testTopic = "test";   // ← still hardcoded

// mqttManager.cpp MQTT_EVENT_CONNECTED
esp_mqtt_client_enqueue(client, testTopic, "GF-KD1-Test --> Online", ...);
```

Two things still need updating:
1. The online announce topic (`testTopic = "test"`) should become `stat/<username>/online` to match the `stat/` namespace and pair with the future LWT.
2. The announce payload (`"GF-KD1-Test --> Online"`) is a hardcoded label — it should use `username` from NVS so every device identifies itself correctly to the broker.

---

### 17. No telemetry loop

The `tele` namespace is planned but nothing is published yet. A basic telemetry interval (free heap, WiFi RSSI, uptime) is very useful in production to detect slow memory leaks or degraded WiFi signal before they become outages. This can be added into the existing 60-second `loop()` interval once the MQTT topic structure is finalised.

---

### 18. `MQTT_EVENT_ERROR` could re-enable the error counter

> fixed

The `globalErrorCounter` mechanism is now fully active:
- Incremented in `MQTT_EVENT_ERROR`
- Reset to `0` in `MQTT_EVENT_CONNECTED`
- Checked in `loop()`: `if(globalErrorCounter >= 5) ESP.restart()`

Combined with proper error logging from item #5 above, the device will now self-heal after 5 unrecovered MQTT errors between 60-second heartbeats.

---

### 19. `cmnd()` accesses `segment[3]` without checking `seg_len >= 4`

> fixed

**File:** `src/functions/cmnd/cmnd.cpp`, lines 25–30

**The problem:**

```cpp
if(strcmp(segment[2], "charger") == 0) {
    status = charger(atoi(segment[3]), atoi(payload));
}
```

The upstream guard in `mqtt_event_handler` is `if (tokenCount < 3) return;` — which only ensures `segment[0..2]` exist. `cmnd()` then reads `segment[3]` unconditionally. A topic like `cmnd/<id>/charger` (three segments) would pass the gate and then read `segment[3]`, which is uninitialised stack — undefined behaviour, likely crash or, worse, silent wrong-channel writes.

**The fix:**

Either tighten the upstream guard to `< 4` before dispatching into `cmnd()`/`stat()`/`tele()` (the channel index is universal to all three), or add a local guard:

```cpp
void cmnd(char *segment[], const size_t seg_len, const char *payload) {
    if (seg_len < 4) return;
    // ...
}
```

The upstream-guard approach is cleaner because the channel index is part of the contract for all three root namespaces.

**Resolution:** A local `if (seg_len < 4) return;` guard is now at the top of `cmnd()`. The upstream guard in `mqttManager.cpp` had to stay at `< 3` so 3-segment `conf/<dev>/health` etc. can reach the conf handler — so the `< 4` check lives inside `cmnd()` itself.

---

### 20. `charger()` / `light()` don't reject negative channel IDs

**File:** `src/functions/cmnd/cmnd.cpp`, lines 34–64

**The problem:**

```cpp
int charger(int chargerID, bool state) {
    if(chargerID >= pinOffset) { ... return -1; }
    // ...
    digitalWrite(chargerPin[chargerID], HIGH);
}
```

`chargerID` comes from `atoi(segment[3])`, which returns `int`. `atoi("-1")` returns `-1`, `atoi("garbage")` returns `0`. The upper-bound check (`>= pinOffset`) catches over-range values but a negative ID slips through and indexes before the start of the heap array — silent memory corruption.

**The fix:**

Add a lower-bound check, or take `unsigned int`:

```cpp
int charger(int chargerID, bool state) {
    if(chargerID < 0 || chargerID >= pinOffset) { ... return -1; }
    // ...
}
```

Same applies to `light()`.

---

### 21. `atoi(payload)` only parses numeric `0`/`1` — won't handle `"on"`/`"off"`/`"true"`/`"false"`

**File:** `src/functions/cmnd/cmnd.cpp`, lines 26, 29

**The problem:**

```cpp
status = charger(atoi(segment[3]), atoi(payload));
```

`atoi("on")`, `atoi("off")`, `atoi("ON")`, `atoi("true")`, `atoi("false")` all return `0`. So sending `cmnd/<id>/charger/0 on` would silently turn the relay **off**, not on, because the payload parses as `0`. This is a footgun the moment any upstream sends a non-numeric command.

Decide the payload contract up front and document it. Two options:

```cpp
// Option A — strict numeric only
bool state = (strcmp(payload, "1") == 0);

// Option B — accept common boolean spellings
bool state = (strcmp(payload, "1") == 0 ||
              strcasecmp(payload, "on") == 0 ||
              strcasecmp(payload, "true") == 0);
```

Either is fine — pick one and stick to it. Document the chosen format in `DOCUMENTATION.md` alongside the topic structure.

---

### 22. `MQTT_EVENT_DATA` doesn't handle fragmented (multi-chunk) messages

**File:** `src/mqttManager/mqttManager.cpp`, lines 32–69

**The problem:**

ESP-MQTT delivers messages larger than the receive buffer in **multiple** `MQTT_EVENT_DATA` events. Each chunk carries `event->current_data_offset` and `event->total_data_len`. The current code treats every chunk as a complete message, so a single oversized payload would be dispatched once per chunk, with each call seeing only a partial payload.

In practice this doesn't bite today (commands are short, broker is small), but it's a correctness landmine the moment someone publishes a large config blob or JSON telemetry response.

**The fix:**

Drop messages that arrive fragmented, or buffer them. The simplest safe option:

```cpp
case MQTT_EVENT_DATA: {
    if (event->current_data_offset != 0 || event->data_len != event->total_data_len) {
        Serial.println("Fragmented MQTT message — dropping");
        break;
    }
    // ...existing handling...
}
```

---

## Tier 2 — Fix Before First Real Deployment (continued)

---

### 23. `cmnd.cpp` uses a chain of `if` instead of `else if`

> fixed

**File:** `src/functions/cmnd/cmnd.cpp`, lines 25–30

Same issue as item #8 in `cmd.cpp`. `charger` and `light` are mutually exclusive — once one matches, the other should not be evaluated. Use `else if`:

```cpp
if(strcmp(segment[2], "charger") == 0)      { ... }
else if(strcmp(segment[2], "light") == 0)   { ... }
```

**Resolution:** The `charger` and `light` branches in `cmnd()` are now chained with `else if`. An unrecognised `segment[2]` falls into a default `else { return; }` that exits **without** acking — consistent with the rule of not confirming a topic the firmware silently dropped.

---

### 24. `cmnd()` assigns to `status` but never uses or reports it

> fixed

**File:** `src/functions/cmnd/cmnd.cpp`, lines 23–30

```cpp
int status;
if(strcmp(segment[2], "charger") == 0) {
    status = charger(...);   // ← assigned
}
if(strcmp(segment[2], "light") == 0) {
    status = light(...);     // ← assigned
}
// status is never read
```

`charger()` and `light()` return `-1` on out-of-range input and `0` on success. The caller throws that away, so an out-of-range relay command fails silently — the operator on the other end of the broker has no way to know their command was rejected.

When the `stat` namespace is wired up, `cmnd()` should publish the result to `stat/<id>/<group>/<channel>` so the operator gets confirmation. Either that, or remove `status` entirely if you genuinely don't care.

**Resolution:** `status` is now initialised to `-1`, set by whichever sub-handler runs (`charger()` / `light()` / `cycle()`), and passed into `statAck(segment, seg_len, status == 0)` at the end of `cmnd()`. The operator now gets a `stat/<dev>/ack` with `"ok": true|false` for every actioned command. (For the `light/all` broadcast form, `status` stays at `-1` if any single channel fails so the ack reports `ok: false`.)

---

### 25. `mqttPublish()` discards the return value of `esp_mqtt_client_enqueue()`

**File:** `src/mqttManager/mqttManager.cpp`, line 107

```cpp
void mqttPublish(const char* pubTopic, const char *message, ...) {
    esp_mqtt_client_enqueue(client, pubTopic, message, 0, QoS, retain, store);
}
```

`esp_mqtt_client_enqueue()` returns the message ID on success and `-1` on failure (queue full, client not connected with `store=false`, etc.). Silent failure here means a "stat published" code path may not have published anything — and you'll never know.

At minimum log the failure; ideally surface it to the caller:

```cpp
int mqttPublish(...) {
    int msg_id = esp_mqtt_client_enqueue(client, pubTopic, message, 0, QoS, retain, store);
    if (msg_id < 0) Serial.printf("Publish failed for %s\n", pubTopic);
    return msg_id;
}
```

---

### 26. `MQTT_EVENT_CONNECTED` publishes a hardcoded `"GF-KD1-Test --> Online"` to literal `"test"`

**File:** `src/mqttManager/mqttManager.cpp`, lines 20–21

```cpp
esp_mqtt_client_enqueue(client, topic, "GF-KD1-Test --> Online", 0, 0, 0, true);
esp_mqtt_client_subscribe(client, topic, 2);
```

Two problems bundled together:

1. The online announcement is a hardcoded device label (`"GF-KD1-Test"`) — it doesn't use `username` from NVS, so every device will report the same identity to the broker.
2. The status payload is a free-text string, not JSON. The planned `stat`/`tele` layer is structured. This online message should match that format (e.g. `{"status":"online"}`) and live on a per-device topic like `stat/<id>/availability`.

Related to items #16 (dynamic topic) and #29 (Last Will payload format).

---

### 27. Subscribe QoS is 2 but publish QoS is 0 — inconsistent and overbuilt

**File:** `src/mqttManager/mqttManager.cpp`, lines 20–21

```cpp
esp_mqtt_client_enqueue(client, topic, "...", 0, 0, 0, true);  // QoS 0
esp_mqtt_client_subscribe(client, topic, 2);                   // QoS 2
```

QoS 2 (exactly-once) is the heaviest tier — four-way handshake per message, broker has to track inflight state. For relay control, QoS 1 (at-least-once) is the standard choice: cheaper, and the relay-control state is idempotent (writing HIGH twice is the same as once).

Pick one consistent QoS per direction and document it. Recommended: QoS 1 for both, unless you have a specific reason for QoS 2.

---

### 28. `ca_cert` and `brokerUri` are hardcoded in firmware

**File:** `src/config.cpp`, lines 4–23, 27

WiFi/MQTT credentials and the per-device pin layout are NVS-provisioned, but the CA certificate and broker URI are baked into the binary. That means:

- Rotating the CA (which will happen eventually — the current one expires 2036-05-26) requires reflashing every device in the field.
- You can't point a device at a staging broker without a separate firmware build.
- A device migrating between environments needs new firmware.

Move both into NVS (e.g. namespace `mqttCfg`, keys `brokerUri` and `caCert`) and read them in `getDeviceSpecificConfig()`. The CA cert is ~1 KB and fits comfortably in NVS. This pairs naturally with item #15 (NVS encryption) — once encrypted, this is the right place for trust material.

---

### 29. Last Will Testament is commented out

**File:** `src/mqttManager/mqttManager.cpp`, lines 90–95

```cpp
// mqtt_cfg.session.last_will.topic   = "test";
// mqtt_cfg.session.last_will.msg     = "{\"status\":\"offline\"}";
// ...
```

Without an LWT, when a device loses power or its WiFi link the broker has no way to tell the system the device is gone — subscribers see the last retained "online" message forever. LWT is the standard MQTT mechanism for this; the broker publishes the will message when it detects the dead client.

Two things to fix when enabling:
1. The will topic must be per-device (using `username`), not literal `"test"` — same lifetime concern as item #16.
2. The retain flag on the LWT should be true and the online publish at `MQTT_EVENT_CONNECTED` should also be true with the opposite payload (`{"status":"online"}`), so subscribers can read the current state at any time. This is the standard "availability topic" pattern.

---

### 30. No NTP time sync — TLS handshake may fail or accept invalid certs

**File:** `src/main.cpp`, `setup()`

The device connects over `mqtts://` (TLS), which validates the broker's certificate against the CA. Certificate validation includes `notBefore`/`notAfter` checks against the current system clock. After cold boot the ESP32's clock starts at 1970 — well before any cert's `notBefore` — and the TLS stack may either reject the connection or skip the time check silently (depending on mbedTLS config).

Right now the device "just works" because ESP-MQTT's mbedTLS likely has time validation disabled by default, but that means a man-in-the-middle could present an expired cert and it would be accepted.

**The fix:** Initialise SNTP after WiFi connects and before `initMqtt()`:

```cpp
configTime(0, 0, "pool.ntp.org", "time.google.com");
// wait briefly for time to be set, or skip and let MQTT retry
```

---

## Tier 3 — Good Habits / Code Quality (continued)

---

### 31. `cycle()` is forward-declared but never implemented or called

> fixed

**File:** `src/functions/cmnd/cmnd.cpp`, line 5

```cpp
int cycle(unsigned int timeInSeconds);
```

Declared at file scope, no definition, no call site. It's a TODO stub that should either be implemented or removed. Dead forward declarations confuse future readers (and tools like clangd will flag them).

**Resolution:** `cycle()` is fully implemented. Signature is now `int cycle(unsigned int timeInSeconds, char *segment[])`. It parses `chargerID` from `segment[3]`, calls `charger(chargerID, false)` (returning early if that returns non-zero), then arms the `cycleFlag` / `cycleID` / `cycleStart` / `cycleInterval` globals (with `cycleInterval = timeInSeconds * 1000`). The re-enable is owned by `loop()` in `main.cpp`, which watches the timestamp and calls `charger(cycleID, true)` when it fires. Single-slot bookkeeping limitation is still tracked separately — see the cycle entries in `DOCUMENTATION.md` Section 15.

---

### 32. `mqttManager.cpp` includes `ArduinoJson.h` but never uses it

> fixed

**File:** `src/mqttManager/mqttManager.cpp`, line 5

```cpp
#include "ArduinoJson.h"
```

No `JsonDocument`, no `serializeJson`, no `deserializeJson` calls anywhere in this file. Drop the include until JSON handling actually moves into this file. (Once `stat`/`tele` start producing JSON payloads, the include will belong wherever they're built — not necessarily here.)

**Resolution:** The `bblanchon/ArduinoJson@7.4.2` `lib_deps` entry has been removed entirely from both `platformio.ini` environments, and the include has been dropped. The `stat/` payloads are small fixed-shape JSON strings built directly with `snprintf`, so a full JSON library is not needed today.

---

### 33. `else if(!state)` in `charger()` / `light()` is redundant

**File:** `src/functions/cmnd/cmnd.cpp`, lines 41–45, 57–61

```cpp
if(state) {
    digitalWrite(chargerPin[chargerID], HIGH);
} else if(!state) {
    digitalWrite(chargerPin[chargerID], LOW);
}
```

`state` is `bool`. There are exactly two possible values. The `else if(!state)` is a tautology and the compiler may even warn on it. Just `else`:

```cpp
if(state) digitalWrite(chargerPin[chargerID], HIGH);
else      digitalWrite(chargerPin[chargerID], LOW);
```

Or shorter:

```cpp
digitalWrite(chargerPin[chargerID], state ? HIGH : LOW);
```

---

### 34. `for(counter; ...)` with empty initializer in `config.cpp`

**File:** `src/config.cpp`, line 91

```cpp
for(counter; counter < (65 + totalPins); counter++) {
```

`for(counter; ...)` is legal C++ but `counter` here is a statement that does nothing — it's neither an initializer nor a reset. Most compilers warn (`-Wunused-value`). The intent is "continue from where the previous loop left off", which is fine — just write it explicitly:

```cpp
for(; counter < (65 + totalPins); counter++) {
```

---

### 35. Magic literal `65` (ASCII `'A'`) in NVS key generation

**File:** `src/config.cpp`, lines 74, 91

```cpp
for(counter = 65; counter < (65+pinOffset); counter++) {
    tmp[0] = (char)counter;
```

`65` is the ASCII code for `'A'`. The CSV documented in `DOCUMENTATION.md` uses keys `A`, `B`, `C`, ... — the code happens to know that because someone hardcoded `65`. Write `'A'` instead:

```cpp
for(counter = 'A'; counter < ('A' + pinOffset); counter++) {
    tmp[0] = (char)counter;
```

This breaks the link between "ASCII trivia" and "what these loops mean".

---

### 36. `u8_t` vs `uint8_t` for `pinOffset` / `totalPins`

**File:** `src/config.h`, lines 26–27; `src/config.cpp`, lines 36–37

```cpp
extern u8_t pinOffset;
extern u8_t totalPins;
```

`u8_t` is the lwIP typedef (defined in `arch/cc.h` because Arduino pulls in lwIP). It works, but the standard portable type is `uint8_t` from `<stdint.h>`. Mixing them across the codebase invites the kind of subtle build break that only shows up when you swap the network stack. Use `uint8_t`.

---

### 37. `checkWiFiStatus()` does `disconnect()` + `begin()` instead of `WiFi.reconnect()`

**File:** `src/wifiUtils/checkWiFiStatus.cpp`, lines 5–8

```cpp
if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost, reconnecting...");
    WiFi.disconnect();
    WiFi.begin(ssid, password);
}
```

`WiFi.disconnect()` followed by `WiFi.begin()` is heavier than necessary — it tears the connection state down and then starts a fresh association handshake. `WiFi.reconnect()` reuses the cached credentials and triggers an immediate re-association. With `setAutoReconnect(true)` already set in `initWiFiConnection.cpp`, this path arguably shouldn't even fire — the WiFi driver is already retrying in the background.

If you keep the explicit check, prefer:

```cpp
if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi lost, reconnecting...");
    WiFi.reconnect();
}
```

---

### 38. `delay(3000)` in `setup()` with unexplained purpose

**File:** `src/main.cpp`, line 30

```cpp
delay(3000); // waiting for everything to initialise
```

"Everything" is vague. After `getDeviceSpecificConfig()` returns, NVS is closed and pinModes are set — there's nothing obvious for the device to wait on before `initWiFiConnection()`. A 3-second blocking delay in `setup()` adds 3 seconds to every cold boot for no clear reason.

Either remove it, or replace it with a comment naming the *specific* thing it's waiting for (e.g. "USB-CDC enumeration on host before serial output is visible") — and prefer a non-blocking wait if it's just for serial visibility.

---

### 39. MQTT credentials are stored as pointers into `String` internals

**File:** `src/mqttManager/mqttManager.cpp`, lines 85–87

```cpp
mqtt_cfg.credentials.client_id = username.c_str();
mqtt_cfg.credentials.username = username.c_str();
mqtt_cfg.credentials.authentication.password = password.c_str();
```

`esp_mqtt_client_init()` stores these pointers internally. They point into the `String` object's heap buffer. If `username` or `password` is ever reassigned (e.g. once `conf` is implemented and can update credentials at runtime), the underlying buffer can be reallocated and ESP-MQTT will hold a dangling pointer — security-relevant: it could end up sending garbage or freed memory as the broker auth.

Currently safe because both are set once in `setup()` and never touched again, but the constraint isn't expressed anywhere in code. Either:
- Document the invariant ("these Strings must outlive the MQTT client and never be reassigned"), or
- Copy into `char[]` buffers under your own control before passing to ESP-MQTT.

---

## Tier 4 — Future / Think About These Later (continued)

---

### 40. Consider `WiFi.setSleep(false)` for control-path responsiveness

**File:** `src/wifiUtils/initWiFiConnection.cpp`

The ESP32 WiFi modem enters power-save mode by default, which adds ~100ms of latency to incoming packets (the modem wakes up on the next beacon). For a relay-control device where an operator presses a button and expects the relay to click, that latency is noticeable.

For mains-powered devices (which these are — they're driving relays), the power savings aren't relevant. `WiFi.setSleep(false)` before `WiFi.begin()` would trade idle current for snappier control. Worth measuring on real hardware before deciding.

---

### 41. No rate limiting on relay state changes

A misbehaving (or malicious) publisher on the broker could spam `cmnd/<id>/charger/0 1` and `cmnd/<id>/charger/0 0` in a tight loop, flipping the relay at MQTT throughput. Mechanical relays have a finite cycle life — a few million operations typically — and rapid cycling damages contacts.

Once the `cmnd` handler is live, consider a minimum interval between toggles for the same channel (e.g. 100ms) and drop or queue commands that arrive faster.

---

## Summary Table

| # | Issue | File | Severity |
|---|-------|------|----------|
| 1 | `segment[1]` out-of-bounds access | `mqttManager.cpp` | ~~Dormant — guard in staged code~~ **fixed** |
| 2 | `topic` variable shadowing | `mqttManager.cpp` | ~~Dormant but not yet fixed~~ **moot** — global is `testTopic` not `topic`, no shadowing |
| 3 | MQTT init return value not checked | `mqttManager.cpp:97` | **Fix now** |
| 4 | `WiFi.persistent` called after `WiFi.begin` | `initWiFiConnection.cpp:4` | ~~**Fix now**~~ **fixed** |
| 4a | `MQTT_EVENT_DATA` forwards every message to `cmd()` (no device-ID filter) | `mqttManager.cpp:66` | ~~**Fix now** (re-enable structured routing)~~ **fixed** |
| 5 | MQTT errors log nothing useful | `mqttManager.cpp:71` | Before deployment |
| 6 | `lightPin/chargerPin` declared but not defined | `config.h:28–29` | ~~Before deployment~~ **fixed** |
| 7 | `LED_PIN` not in `config.h` | `cmd.cpp:3` | ~~Before deployment~~ **fixed** |
| 8 | `if` chain should be `else if` | `cmd.cpp:7–42` | Before deployment |
| 9 | Missing `\n` in heap print | `main.cpp:42` | Before deployment |
| 10 | `Preferences` should be a local variable | `config.cpp:41` | ~~Before deployment~~ **fixed** |
| 11 | VLA stack allocation | `mqttManager.cpp:46` (topic) and `:64` (payload) — **both now active** | Good habit |
| 12 | WDT add return value unchecked | `main.cpp:26` | Good habit |
| 13 | Blocking WiFi loop doesn't reset WDT | `initWiFiConnection.cpp:9` | Good habit |
| 14 | No OTA update mechanism | — | Future |
| 15 | NVS not encrypted | — | Future |
| 16 | Boot announce still uses hardcoded topic + label | `mqttManager.cpp:22` (announce), `config.cpp` (`testTopic`) | Partially done — subscriptions are dynamic; announce topic/payload still hardcoded |
| 17 | No telemetry publishing | — | Future |
| 18 | Error counter not re-enabled | — | ~~Future~~ **fixed** |
| 19 | `cmnd()` accesses `segment[3]` without `seg_len >= 4` check | `cmnd.cpp:25` | ~~**Fix now**~~ **fixed** |
| 20 | `charger()` / `light()` don't reject negative channel IDs | `cmnd.cpp:34,50` | **Fix now** |
| 21 | `atoi(payload)` won't parse `"on"`/`"off"`/`"true"`/`"false"` | `cmnd.cpp:26,29` | **Fix now** (define payload contract) |
| 22 | `MQTT_EVENT_DATA` doesn't handle fragmented messages | `mqttManager.cpp:32` | **Fix now** |
| 23 | `cmnd.cpp` chained `if` should be `else if` | `cmnd.cpp:25–30` | ~~Before deployment~~ **fixed** |
| 24 | `status` assigned but never reported back | `cmnd.cpp:23–30` | ~~Before deployment~~ **fixed** |
| 25 | `mqttPublish()` discards `esp_mqtt_client_enqueue()` return | `mqttManager.cpp:107` | Before deployment |
| 26 | Hardcoded `"GF-KD1-Test --> Online"` published to literal `"test"` | `mqttManager.cpp:20–21` | Before deployment |
| 27 | Subscribe QoS 2 vs publish QoS 0 inconsistency | `mqttManager.cpp:20–21` | Before deployment |
| 28 | `ca_cert` and `brokerUri` hardcoded in firmware | `config.cpp:4–27` | Before deployment |
| 29 | Last Will Testament is commented out | `mqttManager.cpp:90–95` | Before deployment |
| 30 | No NTP / time sync before TLS handshake | `main.cpp` | Before deployment |
| 31 | `cycle()` forward-declared but never implemented | `cmnd.cpp:5` | ~~Good habit~~ **fixed** |
| 32 | `ArduinoJson.h` included but unused | `mqttManager.cpp:5` | ~~Good habit~~ **fixed** |
| 33 | `else if(!state)` is redundant (bool only has 2 values) | `cmnd.cpp:41–45,57–61` | Good habit |
| 34 | `for(counter; ...)` empty initializer statement | `config.cpp:91` | Good habit |
| 35 | Magic `65` instead of `'A'` in NVS key generation | `config.cpp:74,91` | Good habit |
| 36 | `u8_t` (lwIP) instead of `uint8_t` for pin counts | `config.h:26–27` | Good habit |
| 37 | `checkWiFiStatus()` should use `WiFi.reconnect()` | `checkWiFiStatus.cpp:5` | Good habit |
| 38 | `delay(3000)` in `setup()` has unexplained purpose | `main.cpp:30` | Good habit |
| 39 | MQTT credentials are `String::c_str()` pointers — fragile if reassigned | `mqttManager.cpp:85–87` | Good habit |
| 40 | Consider `WiFi.setSleep(false)` for control-path responsiveness | `initWiFiConnection.cpp` | Future |
| 41 | No rate limiting on relay state changes | `cmnd.cpp` | Future |
