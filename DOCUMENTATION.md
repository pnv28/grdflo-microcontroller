# GridFlow Microcontroller Firmware — Documentation

> **Status: Work in progress.** The core WiFi + MQTT infrastructure is functional. The `cmnd`, `stat`, and `tele` message handlers are stubs yet to be implemented.

---

## Table of Contents

1. [Project Overview](#1-project-overview)
2. [Hardware](#2-hardware)
3. [Project Structure](#3-project-structure)
4. [Build Configuration — `platformio.ini`](#4-build-configuration--platformioini)
5. [Global Configuration — `config.h` / `config.cpp`](#5-global-configuration--configh--configcpp)
6. [Entry Point — `main.cpp`](#6-entry-point--maincpp)
7. [WiFi Utilities — `lib/wifiUtils/`](#7-wifi-utilities--libwifiutils)
8. [MQTT Manager — `src/mqttManager/`](#8-mqtt-manager--srcmqttmanager)
9. [Topic Routing — `src/functions/`](#9-topic-routing--srcfunctions)
10. [Debug Command Handler — `src/cmd.cpp`](#10-debug-command-handler--srccmdcpp)
11. [Architecture — How It All Fits Together](#11-architecture--how-it-all-fits-together)
12. [NVS Provisioning](#12-nvs-provisioning)
13. [MQTT Topic Structure](#13-mqtt-topic-structure)
14. [What Is Not Yet Implemented](#14-what-is-not-yet-implemented)

---

## 1. Project Overview

This is the firmware for a **GridFlow (GrdFlo) IoT node** running on an **ESP32-C3** microcontroller. The device:

- Connects to a WiFi network.
- Connects to the GridFlow MQTT broker (`emqx.internal.grdflo.com`) over **TLS** (port 8883), authenticating with a device-specific username and password.
- Receives commands and publishes status/telemetry through a structured MQTT topic hierarchy inspired by [Tasmota](https://tasmota.github.io/docs/): `cmnd/`, `stat/`, `tele/`.
- Stores all sensitive credentials (WiFi SSID, WiFi password, device ID, MQTT password) in **ESP32 NVS** (Non-Volatile Storage), so they never appear in the firmware binary itself.

---

## 2. Hardware

### Target Board

**ESP32-C3-DevKitM-1** — a development board based on the ESP32-C3 SoC (RISC-V, single-core, 2.4 GHz WiFi + Bluetooth 5).

### GPIO Pin Assignments

| Constant    | GPIO | Purpose                                   |
|-------------|------|-------------------------------------------|
| `LED_PIN`   | 10   | Onboard addressable RGB LED (WS2812-type) |
| `RED_PIN`   | 20   | External red indicator LED / output       |
| `BLUE_PIN`  | 5    | External blue indicator LED / output      |

> **Important quirk noted in the code:** The onboard RGB LED on the ESP32-C3-DevKitM-1 uses a **GRB** byte order instead of the usual RGB. The `rgbLedWrite()` call in `cmd.cpp` accounts for this — the second argument is Green, third is Red, fourth is Blue.

> **Hardware note from the author:** GPIO 21 may have been damaged (`// note i might have fried gpio pin 21`).

---

## 3. Project Structure

```
grdflo-microcontroller/
│
├── platformio.ini                  # PlatformIO build + board configuration
│
├── src/                            # Main application source
│   ├── main.cpp                    # setup() and loop() — firmware entry point
│   ├── config.h                    # Global constants, pin defs, extern declarations
│   ├── config.cpp                  # Global variable definitions + NVS credential loading
│   ├── cmd.h                       # Declaration for the debug cmd() function
│   ├── cmd.cpp                     # Debug command handler (LED/pin control via MQTT payload)
│   │
│   ├── mqttManager/
│   │   ├── mqttManager.h           # Public API: initMqtt(), mqttPublish()
│   │   └── mqttManager.cpp         # MQTT client setup, event handler, topic tokeniser
│   │
│   └── functions/                  # Handlers for each MQTT root topic
│       ├── cmnd/
│       │   ├── cmnd.h              # Declaration for cmnd() handler
│       │   └── cmnd.cpp            # [STUB] Processes incoming cmnd/<id>/... messages
│       ├── stat/
│       │   └── stat.h              # Declaration for stat() handler — no .cpp yet
│       └── tele/
│           └── tele.h              # Declaration for tele() handler — no .cpp yet
│
├── lib/                            # Project-private libraries (compiled separately by PlatformIO)
│   └── wifiUtils/
│       └── src/
│           ├── wifiUtils.h         # Declarations for WiFi helpers
│           ├── initWiFiConnection.cpp  # Blocking WiFi connect with reboot-on-failure
│           └── checkWiFiStatus.cpp     # Periodic WiFi health check + reconnect
│
├── include/                        # (Empty — reserved for shared project headers)
└── test/                           # (Empty — reserved for unit tests)
```

---

## 4. Build Configuration — `platformio.ini`

```ini
[env:esp32-c3-devkitm-1]
platform = https://github.com/pioarduino/platform-espressif32/releases/download/stable/platform-espressif32.zip
board = esp32-c3-devkitm-1
framework = arduino
monitor_speed = 115200
board_build.partitions = min_spiffs.csv
lib_deps = bblanchon/ArduinoJson@7.4.2
build_flags =
    -DARDUINO_USB_CDC_ON_BOOT=1
    -DARDUINO_USB_MODE=1
```

### Key points

| Setting | Explanation |
|---|---|
| `platform = ...pioarduino...` | Uses the **pioarduino** fork of the ESP32 PlatformIO platform, which tracks newer ESP-IDF releases than the official Espressif platform package. |
| `board = esp32-c3-devkitm-1` | Selects the ESP32-C3-DevKitM-1 board profile (pin maps, flash size, etc.). |
| `framework = arduino` | Uses the Arduino-on-ESP-IDF layer. You can call standard `Arduino.h` functions AND drop into raw ESP-IDF APIs (like `esp_mqtt_client_*`). |
| `monitor_speed = 115200` | Serial monitor baud rate — must match `Serial.begin(115200)` in `main.cpp`. |
| `board_build.partitions = min_spiffs.csv` | Selects a partition table that allocates **minimal space for SPIFFS** (the filesystem partition), giving the application partition more flash. Useful here because no filesystem is used — credentials live in NVS instead. |
| `lib_deps = bblanchon/ArduinoJson@7.4.2` | Pins ArduinoJson to version 7.4.2. Used in `mqttManager.cpp` (included but not yet actively used for payload parsing — planned for the `cmnd`/`stat`/`tele` handlers). |
| `-DARDUINO_USB_CDC_ON_BOOT=1` | Enables the USB CDC (virtual COM port) on boot. The ESP32-C3-DevKitM-1 has a built-in USB-to-UART bridge via USB CDC; this flag activates it so `Serial.print()` output is visible over the USB cable without a separate UART adapter. |
| `-DARDUINO_USB_MODE=1` | Puts the USB peripheral in CDC/ACM mode (standard serial over USB). |

---

## 5. Global Configuration — `config.h` / `config.cpp`

These two files together define all the **global state** of the device.

### `config.h` — Declarations

```cpp
#define RED_PIN  20
#define BLUE_PIN  5
#define MAX_SEGMENT  6
```

- `RED_PIN` / `BLUE_PIN`: GPIO numbers for two external digital-output pins (e.g., indicator LEDs or relay signals).
- `MAX_SEGMENT`: The maximum number of `/`-separated segments the firmware will parse from an incoming MQTT topic string. A topic like `cmnd/device123/power/channel/1` has 5 segments. Setting this to 6 gives a reasonable upper bound without unbounded allocation.

The header also `extern`-declares every global variable that other `.cpp` files need to access:

```cpp
extern const char* ca_cert;       // TLS root certificate (PEM format)
extern String ssid;               // WiFi network name
extern String wifiPassword;       // WiFi password
extern const char* brokerUri;     // MQTT broker URI
extern const char* topic;         // Default MQTT topic (temporary / dev)
extern String username;           // Device ID — used as MQTT client ID and username
extern String password;           // MQTT password for this device
extern int lightPin[10];          // (declared, not yet used)
extern int chargePin[10];         // (declared, not yet used)
```

`lightPin` and `chargePin` are declared but not defined or used yet — they are reserved for future hardware control (presumably light circuits and charging circuits that this GridFlow node will manage).

### `config.cpp` — Definitions and NVS Loading

#### `ca_cert`

A **hardcoded PEM-encoded X.509 certificate** for the GridFlow internal Root CA (`GridFlow-RootCA`). This is the certificate the broker's TLS certificate is signed by. The firmware embeds it so it can verify the broker's identity without relying on any public CA store.

- Issuer/Subject: `GridFlow-RootCA`, country `NP`
- Valid: 2026-05-29 to 2036-05-26
- This is a **self-signed CA certificate** (CA:true), meaning GridFlow operates its own private PKI.

#### `brokerUri`

```cpp
const char* brokerUri = "mqtts://emqx.internal.grdflo.com:8883";
```

- `mqtts://` — MQTT over TLS.
- `emqx.internal.grdflo.com` — internal (private network) EMQX broker.
- Port `8883` — the standard MQTTS port.

#### `topic`

```cpp
const char* topic = "test";
```

Temporary development topic. The real per-device topic structure (see [Section 13](#13-mqtt-topic-structure)) will replace this.

#### `getDeviceSpecificConfig()`

This is the most important function in this file. It reads **device-specific secrets out of NVS** (the ESP32's Non-Volatile Storage — a key-value store that survives power cycles and firmware updates).

```cpp
void getDeviceSpecificConfig() {
    prefs.begin("creds", true);     // Open NVS namespace "creds" in read-only mode

    ssid         = prefs.getString("wifi_ssid", "readError");
    wifiPassword = prefs.getString("wifi_pass", "readError");
    username     = prefs.getString("dev_id",    "readError");
    password     = prefs.getString("mqtt_pass", "readError");

    prefs.end();

    // If any key was missing, abort and reboot
    if(ssid == "readError" || wifiPassword == "readError" || ...)  {
        Serial.println("Could not get value from NVS — rebooting...");
        delay(100);
        ESP.restart();
    }
}
```

**Why NVS?** Hardcoding credentials in the firmware binary is a security risk — the binary can be extracted from flash and read. By storing secrets in a separate NVS partition (which can be encrypted with ESP32's NVS encryption feature), every device can have unique credentials without rebuilding the firmware.

**NVS namespace and keys:**

| NVS Key       | Loaded into     | Meaning                                    |
|---------------|-----------------|--------------------------------------------|
| `wifi_ssid`   | `ssid`          | WiFi network name                          |
| `wifi_pass`   | `wifiPassword`  | WiFi password                              |
| `dev_id`      | `username`      | Device's unique ID (also MQTT client ID)   |
| `mqtt_pass`   | `password`      | MQTT password for this device on the broker|

**Failure behaviour:** If any key returns the fallback `"readError"` (meaning the key doesn't exist or the namespace wasn't provisioned), the device prints a message and **hard-reboots**. This is intentional — a device with no credentials cannot function, and a reboot loop makes the misconfiguration obvious.

> The `nvs.csv` and `nvs.bin` files used to flash these credentials are intentionally **gitignored** (they contain real secrets). See [Section 12](#12-nvs-provisioning).

---

## 6. Entry Point — `main.cpp`

This is the standard Arduino-style entry point with `setup()` (runs once on boot) and `loop()` (runs repeatedly).

### Watchdog Timer Setup

```cpp
esp_task_wdt_config_t wdt_cfg = {
  .timeout_ms     = 30000,
  .idle_core_mask = 0,
  .trigger_panic  = true
};
```

The **Task Watchdog Timer (TWDT)** is the firmware's self-recovery mechanism. If `loop()` ever gets stuck for more than **30 seconds** without calling `esp_task_wdt_reset()`, the watchdog fires and the device **panics and reboots**. `trigger_panic = true` means it does a full ESP32 panic (prints a stack trace to serial) rather than a silent reset — useful for debugging.

`esp_task_wdt_reconfigure()` applies this config, and `esp_task_wdt_add(NULL)` subscribes the current task (the Arduino main task) to the watchdog.

### `setup()`

Runs once on power-on / reset. Execution order matters here:

1. **`Serial.begin(115200)`** — Start serial output for debugging.
2. **`pinMode(RED_PIN/BLUE_PIN, OUTPUT)`** — Configure the two GPIO pins as digital outputs.
3. **`esp_task_wdt_reconfigure() + esp_task_wdt_add(NULL)`** — Arm the watchdog.
4. **`getDeviceSpecificConfig()`** — Load WiFi/MQTT credentials from NVS. Reboots on failure.
5. **`delay(3000)`** — A 3-second pause giving hardware (radios, etc.) time to initialise before making network calls.
6. **`initWiFiConnection(ssid, wifiPassword)`** — Connect to WiFi. Blocks until connected or reboots after 20 failed attempts.
7. **`initMqtt()`** — Start the MQTT client. After this, the device is fully connected and the MQTT event handler takes over.

### `loop()`

Runs continuously after `setup()`. It is intentionally minimal:

```cpp
void loop() {
  esp_task_wdt_reset();           // Pet the watchdog — must be called at least every 30 s
  unsigned long currMillis = millis();

  if((currMillis - prevMillis) >= interval) {   // Every 60 seconds:
    prevMillis = currMillis;
    Serial.printf("Free heap: %d bytes", ESP.getFreeHeap());    // Log heap health
    checkWiFiStatus(ssid.c_str(), wifiPassword.c_str());        // Check/repair WiFi
  }
}
```

The `millis()` / `prevMillis` pattern is a **non-blocking interval timer** — it avoids using `delay()` in the loop, which would block the watchdog reset from being called. Every 60 seconds:

- The free heap size is printed to serial (useful for detecting memory leaks over time).
- `checkWiFiStatus()` is called to detect and recover from WiFi disconnections.

**Commented-out code in `loop()`:**
- A global error counter (`globalErrorCount`) was being tracked and would trigger a reboot after 5 errors. This is currently disabled — the variable definition is also commented out in `config.cpp`.

---

## 7. WiFi Utilities — `lib/wifiUtils/`

This is a **project-private library** (placed under `lib/` so PlatformIO compiles it as a separate static library). It provides two functions.

### `initWiFiConnection(ssid, password)` — `initWiFiConnection.cpp`

Blocking WiFi connection used at startup.

```
WiFi.begin(ssid, password)
WiFi.setAutoReconnect(true)    ← driver-level auto-reconnect
WiFi.persistent(false)         ← do NOT save credentials to flash
```

- **`setAutoReconnect(true)`**: Tells the ESP32 WiFi driver to automatically attempt reconnection when the link drops, without any application-layer intervention. This is the primary reconnection mechanism.
- **`WiFi.persistent(false)`**: Prevents the WiFi library from writing the SSID/password to flash on every `WiFi.begin()` call. Since credentials come from NVS already, writing them to a second flash region is redundant and wastes flash write cycles.

The function then polls `WiFi.status()` in a loop, waiting up to 10 seconds (20 attempts × 500 ms). If WiFi does not connect in time, the device **reboots** — there is no point continuing without network connectivity.

On success, the connected SSID and assigned IP address are printed to serial.

### `checkWiFiStatus(ssid, password)` — `checkWiFiStatus.cpp`

Called from `loop()` every 60 seconds as an **application-level WiFi watchdog**.

```cpp
void checkWiFiStatus(const char *ssid, const char *password) {
    if (WiFi.status() != WL_CONNECTED) {
      WiFi.disconnect();
      WiFi.begin(ssid, password);   // Kick a fresh connect attempt
    }
}
```

This is a safety net on top of `setAutoReconnect(true)`. If the driver-level reconnect failed or got stuck, this manually forces a fresh `WiFi.begin()`.

---

## 8. MQTT Manager — `src/mqttManager/`

This is the heart of the firmware's connectivity layer. It uses the **ESP-IDF native MQTT client** (`esp_mqtt_client_*`) directly — not the Arduino PubSubClient library. The native client is more capable: it supports MQTT 5, QoS 0/1/2, TLS out of the box, message queuing (outbox), and runs on its own FreeRTOS task.

### `mqttManager.h` — Public API

```cpp
void initMqtt();
void mqttPublish(const char* pubTopic, const char *message, const int QoS, const int retain, const bool store);
```

- `initMqtt()`: Call once from `setup()`. Configures and starts the MQTT client.
- `mqttPublish()`: Used by the rest of the application to send messages. The commented-out `mqttSubscribe()` suggests dynamic subscription was considered but subscriptions are currently hardcoded inside `MQTT_EVENT_CONNECTED`.

### `mqttManager.cpp` — Implementation

#### `client`

```cpp
esp_mqtt_client_handle_t client;
```

A global handle to the MQTT client instance. Stored at file scope so both `mqtt_event_handler` and `mqttPublish` can access it.

---

#### `initMqtt()`

Builds the MQTT client configuration struct and starts the client:

```cpp
esp_mqtt_client_config_t mqtt_cfg = {};
mqtt_cfg.session.keepalive                        = 20;          // Send PINGREQ every 20 s
mqtt_cfg.broker.address.uri                       = brokerUri;   // mqtts://...
mqtt_cfg.credentials.client_id                    = username;    // Device ID
mqtt_cfg.credentials.username                     = username;    // Same — device ID
mqtt_cfg.credentials.authentication.password      = password;    // MQTT password
mqtt_cfg.broker.verification.certificate          = ca_cert;     // GridFlow Root CA (TLS)
```

Key design decisions:
- **`client_id` == `username`**: The device ID serves double duty — it uniquely identifies the MQTT session AND authenticates it. The EMQX broker is presumably configured with ACL rules that restrict each client ID to only its own topics.
- **`keepalive = 20`**: The client sends a PINGREQ to the broker every 20 seconds of inactivity. The broker will disconnect a client that goes silent for longer than `keepalive × 1.5 = 30 s`. This aligns with the watchdog timeout.
- **`ca_cert`**: Passed directly to the ESP-IDF TLS stack. The stack verifies the broker's certificate chain against this CA. If verification fails, the connection is refused — no MITM possible.

**Commented-out Last Will block:**
```cpp
// mqtt_cfg.session.last_will.topic   = "test";
// mqtt_cfg.session.last_will.msg     = "{\"status\":\"offline\"}";
// mqtt_cfg.session.last_will.qos     = 1;
// mqtt_cfg.session.last_will.retain  = 1;
```
This is the **MQTT Last Will and Testament (LWT)**. When configured, the broker automatically publishes this message if the client disconnects unexpectedly (power loss, crash, network failure). It's a standard IoT pattern for device presence detection. It's commented out for now because the final topic structure isn't locked in yet.

---

#### `mqtt_event_handler()`

This is a **FreeRTOS event handler callback** — it runs on the MQTT client's internal task whenever an MQTT event occurs. The four handled events:

**`MQTT_EVENT_CONNECTED`**

```cpp
esp_mqtt_client_enqueue(client, topic, "GF-KD1-Test --> Online", 0, 0, 0, true);
esp_mqtt_client_subscribe(client, topic, 2);
```

When the client successfully connects to the broker:
1. Publishes an "Online" announcement to `topic` (QoS 0, retained — the `true` at the end is the `store` flag for the outbox).
2. Subscribes to `topic` at QoS 2 (exactly-once delivery). In the production design, this will subscribe to the per-device command topic.

**`MQTT_EVENT_DISCONNECTED`**

Prints a message. The ESP-IDF MQTT client handles reconnection automatically — no manual reconnect code is needed here.

**`MQTT_EVENT_SUBSCRIBED`**

Prints confirmation with the message ID returned by the broker's SUBACK.

**`MQTT_EVENT_DATA`** — The main message routing logic:

```cpp
// 1. Extract topic string from the event (it is NOT null-terminated by default)
char topic[event->topic_len + 1];
memcpy(topic, event->topic, event->topic_len);
topic[event->topic_len] = '\0';

// 2. Tokenise the topic by '/' into segments[]
char *segment[MAX_SEGMENT];
char *token = strtok_r(topic, "/", &savePtr);
while(token != NULL && tokenCount < MAX_SEGMENT) {
    segment[tokenCount++] = token;
    token = strtok_r(NULL, "/", &savePtr);
}

// 3. Extract payload string (also NOT null-terminated by default)
char payload[event->data_len + 1];
memcpy(payload, event->data, event->data_len);
payload[event->data_len] = '\0';

// 4. Route to the correct handler based on topic structure
if(strcmp(segment[1], username.c_str()) == 0) {
    // if(strcmp(segment[0], "cmnd") == 0) cmnd(segment, tokenCount, payload);
    // if(strcmp(segment[0], "stat") == 0) stat(segment, tokenCount, payload);
    // if(strcmp(segment[0], "tele") == 0) tele(segment, tokenCount, payload);
} else {
    Serial.println("Client ID Mismatch");
}
```

**Why manual null-termination?** The ESP-IDF MQTT event data (`event->topic` and `event->data`) are raw byte pointers into an internal buffer. They are **not** null-terminated. The code manually copies them into local stack-allocated char arrays and adds `'\0'` so standard C string functions (`strtok_r`, `strcmp`) work correctly.

**Topic routing logic:**
- `segment[0]` — the root namespace: `cmnd`, `stat`, or `tele`.
- `segment[1]` — the device ID (compared against `username` to ensure this message is for this device).
- `segment[2+]` — sub-topic levels passed to the handler for further dispatch.

**`MQTT_EVENT_ERROR`**

Prints an error message. The commented-out `globalErrorCount++` suggests this was going to feed into the reboot-on-N-errors mechanism in `loop()`.

---

#### `mqttPublish()`

```cpp
void mqttPublish(const char* pubTopic, const char *message, const int QoS, const int retain, const bool store) {
    esp_mqtt_client_enqueue(client, pubTopic, message, 0, QoS, retain, store);
}
```

A thin wrapper around `esp_mqtt_client_enqueue()`. Uses `enqueue` rather than `publish` — `enqueue` adds the message to the client's internal **outbox** (a persistent queue), which means:
- If the connection drops mid-publish, the message is retried when the client reconnects.
- It is safe to call from any FreeRTOS task, not just the MQTT task.

The `0` for the `len` parameter tells the library to use `strlen(message)` automatically.

---

## 9. Topic Routing — `src/functions/`

These three subdirectories define the three root-level MQTT namespaces. The naming convention mirrors **Tasmota's topic design**, which is a widely adopted pattern for IoT device communication:

| Namespace | Direction   | Purpose |
|-----------|-------------|---------|
| `cmnd`    | Cloud → Device | Send commands to the device (turn on/off, configure, etc.) |
| `stat`    | Device → Cloud | Device publishes its current state in response to a `cmnd` |
| `tele`    | Device → Cloud | Device autonomously publishes telemetry (periodic sensor readings, health data) |

Each has a corresponding C function with the same signature:

```cpp
void cmnd(char *segment[], const size_t seg_len, const char *payload);
void stat (char *segment[], const size_t seg_len, const char *payload);
void tele (char *segment[], const size_t seg_len, const char *payload);
```

- `segment[]`: The full array of `/`-split topic segments (so the handler can inspect `segment[2]`, `segment[3]`, etc. for sub-topic routing).
- `seg_len`: How many segments were actually parsed (bounds check before accessing the array).
- `payload`: The null-terminated message payload string.

### `cmnd/cmnd.cpp`

Currently an **empty stub**:

```cpp
void cmnd(char *segment[], const size_t seg_len, const char *payload) {
    // TODO: implement
}
```

This is where incoming commands will be parsed and acted upon (e.g., `cmnd/<device_id>/power` with payload `ON` → turn on a relay).

### `stat/stat.h` and `tele/tele.h`

Both are **header-only declarations** — no `.cpp` implementation files exist yet. They are included in `mqttManager.cpp` to prepare the routing structure, but the corresponding function bodies still need to be written.

---

## 10. Debug Command Handler — `src/cmd.cpp`

```cpp
void cmd(char *payload) { ... }
```

This is an **earlier, simpler command handler** — it predates the structured `cmnd`/`stat`/`tele` routing. It accepts a raw payload string and compares it against a list of hardcoded command strings.

| Payload      | Action |
|-------------|--------|
| `"reboot"`   | `ESP.restart()` — hard reboot |
| `"red"`      | Set onboard RGB LED to red (uses GRB order: `rgbLedWrite(10, 0, 255, 0)`) |
| `"blue"`     | Set onboard RGB LED to blue |
| `"green"`    | Set onboard RGB LED to green |
| `"white"`    | Set onboard RGB LED to white (all channels max) |
| `"off"`      | Turn off the onboard RGB LED |
| `"rp"`       | Set `RED_PIN` (GPIO 20) HIGH |
| `"bp"`       | Set `BLUE_PIN` (GPIO 5) HIGH |
| `"gr"`       | Set both `RED_PIN` and `BLUE_PIN` HIGH |
| `"off_pin"`  | Set both `RED_PIN` and `BLUE_PIN` LOW |
| `"on_pin"`   | Set both `RED_PIN` and `BLUE_PIN` HIGH |

> **Current status:** The call `cmd(payload)` inside `MQTT_EVENT_DATA` is commented out (`// cmd(payload)`). This function was used for quick hardware testing and has been superseded by the structured topic routing. It may be repurposed or removed.

> **GRB quirk:** The comment in the code explicitly warns: *"for some reason it is G R B instead of RGB so be careful."* The `rgbLedWrite()` Arduino helper accepts `(pin, r, g, b)` parameters but the underlying WS2812 LED in the ESP32-C3-DevKitM-1 uses a GRB wire order. The calls swap R and G: `rgbLedWrite(LED_PIN, 0, 255, 0)` produces red (not green) because the LED sees `G=0, R=255, B=0`.

---

## 11. Architecture — How It All Fits Together

```
┌─────────────────────────────────────────────────────────┐
│                     ESP32-C3 Device                     │
│                                                         │
│  setup()                                                │
│    │                                                    │
│    ├─ getDeviceSpecificConfig()  ← NVS (flash)          │
│    │     (ssid, wifiPassword, username, password)       │
│    │                                                    │
│    ├─ initWiFiConnection()       ← 2.4 GHz WiFi         │
│    │     blocks until connected / reboots               │
│    │                                                    │
│    └─ initMqtt()                 ← TLS TCP to broker    │
│          registers mqtt_event_handler                   │
│          starts MQTT client task (FreeRTOS)             │
│                                                         │
│  loop()  (Arduino main task)                            │
│    │                                                    │
│    ├─ esp_task_wdt_reset()       (every iteration)      │
│    │                                                    │
│    └─ every 60 s:                                       │
│         ├─ print free heap                              │
│         └─ checkWiFiStatus()                            │
│                                                         │
│  mqtt_event_handler()  (MQTT client task)               │
│    │                                                    │
│    ├─ CONNECTED  → publish "Online" + subscribe         │
│    ├─ DISCONNECTED → log (client auto-reconnects)       │
│    └─ DATA                                              │
│         ├─ tokenise topic by '/'                        │
│         ├─ verify segment[1] == username                │
│         └─ route by segment[0]:                         │
│              ├─ "cmnd" → cmnd()  [STUB]                 │
│              ├─ "stat" → stat()  [NOT IMPL]             │
│              └─ "tele" → tele()  [NOT IMPL]             │
│                                                         │
└─────────────────────────────────────────────────────────┘
         ↕  TLS/MQTT (port 8883)
┌─────────────────────────────────────────────────────────┐
│           EMQX Broker (emqx.internal.grdflo.com)        │
│     TLS cert signed by GridFlow-RootCA                  │
│     Auth: username + password per device                │
└─────────────────────────────────────────────────────────┘
```

---

## 12. NVS Provisioning

NVS (Non-Volatile Storage) is an ESP32 key-value store in flash, separate from the firmware. Before a device can be deployed, its NVS partition must be written with device-specific credentials.

The typical workflow:

1. Create an `nvs.csv` file (gitignored) with rows for each key-value pair:
   ```
   key,type,encoding,value
   creds,namespace,,
   wifi_ssid,data,string,MyWiFiNetwork
   wifi_pass,data,string,MyWiFiPassword
   dev_id,data,string,device-abc123
   mqtt_pass,data,string,secret-device-password
   ```

2. Use the `nvs_partition_gen.py` tool (from ESP-IDF) to produce `nvs.bin`.

3. Flash `nvs.bin` to the NVS partition address using `esptool.py` or PlatformIO's `uploadfs` target.

Both `nvs.csv` and `nvs.bin` are in `.gitignore` because they contain real credentials and should never be committed.

---

## 13. MQTT Topic Structure

The intended topic pattern (based on the routing code in `mqtt_event_handler`) is:

```
<root>/<device_id>/<subtopic...>
```

Where:
- `<root>` is one of `cmnd`, `stat`, `tele`
- `<device_id>` is `username` (= `dev_id` from NVS) — e.g., `GF-KD1-001`
- `<subtopic...>` is one or more additional levels handled inside the `cmnd()`, `stat()`, `tele()` functions

**Example topics (once implemented):**

| Topic | Direction | Meaning |
|-------|-----------|---------|
| `cmnd/GF-KD1-001/power` | Cloud → Device | Command: control main power |
| `stat/GF-KD1-001/power` | Device → Cloud | State response: current power state |
| `tele/GF-KD1-001/sensor` | Device → Cloud | Telemetry: periodic sensor data |

The **segment array** passed to each handler maps as:

```
topic:    cmnd  /  GF-KD1-001  /  power  /  channel  /  1
index:      0          1            2          3          4
```

So `segment[0]` = root, `segment[1]` = device ID, `segment[2+]` = action-specific path.

---

## 14. What Is Not Yet Implemented

| Component | File | Status | Notes |
|-----------|------|--------|-------|
| `cmnd` handler | `src/functions/cmnd/cmnd.cpp` | Empty stub | Function signature exists, body is empty |
| `stat` handler | `src/functions/stat/` | Header only | No `.cpp` file; function declared but not defined |
| `tele` handler | `src/functions/tele/` | Header only | No `.cpp` file; function declared but not defined |
| Topic routing activation | `mqttManager.cpp:57-59` | Commented out | The three `if(strcmp(segment[0], ...))` dispatch calls are commented out |
| Last Will (LWT) | `mqttManager.cpp:89-93` | Commented out | Pending final topic structure |
| `lightPin[]` / `chargePin[]` arrays | `config.h` | Declared, unused | Planned hardware control — pins not yet assigned |
| Global error counter / reboot | `config.cpp`, `main.cpp` | Commented out | Was tracking MQTT errors; disabled during development |
| Dynamic subscription | `mqttManager.h` | Commented out | `mqttSubscribe()` declared but commented everywhere |

---

*End of documentation.*
