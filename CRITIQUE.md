# grdflo-microcontroller — Production Readiness Critique

> Reviewer: Claude (Opus 4.7) — 2026-06-05
> Scope: full firmware tree under `src/`, the NVS provisioning data
> (`nvs.csv`), and `platformio.ini`. The reviewer did **not** modify any
> production source; reproductions live in `test/host/test_critique.cpp`
> and were run with the host `g++` toolchain. The build itself compiles
> cleanly on `esp32-c3-devkitm-1` with no warnings (verified by running
> `pio run` from this tree).
>
> Each finding cites file and line. Severity is the reviewer's estimate
> of operational impact for fleet deployment on ESP32-C3 hardware.

---

## Severity legend

| Severity | Meaning |
| --- | --- |
| **BLOCKER** | Device will malfunction or self-destruct in normal operation. Do not flash to production until fixed. |
| **HIGH** | Real bug that will bite a non-trivial fraction of devices, or a major operational gap (no rollback). |
| **MEDIUM** | Edge-case bug, footgun, hardening gap. Will eventually cause incidents. |
| **LOW** | Code smell, marginal correctness issue, future-proofing concern. |
| **INFO** | Observation, not a defect. |

---

## Table of findings

| # | Severity | Area | Title |
|---|---|---|---|
| [1](#1) | **BLOCKER** | `main.cpp` + `checkWiFiStatus.cpp` | `WiFiDiconnectSince` reboots a healthy device every 3 minutes |
| [2](#2) | **HIGH** | `mqttManager.cpp` | Variable-length-array on MQTT-task stack (DoS / stack overflow vector) |
| [3](#3) | **HIGH** | `functions/cmnd/cmnd.cpp` | `cycle()` overwrites global state — second cycle strands the first charger OFF |
| [4](#4) | **HIGH** | `functions/cmnd/cmnd.cpp` | `cycle` has no cancel; manual `charger` commands during a cycle are overridden |
| [5](#5) | **HIGH** | Operations | No OTA path — bug fixes require physical access to every deployed unit |
| [6](#6) | **HIGH** | `config.cpp` + `cmnd.cpp` | Relay state is not persisted; reboot returns all chargers ON and all lights OFF |
| [7](#7) | **HIGH** | `config.cpp` | Light pins set to `OUTPUT` but never `digitalWrite`n at boot — undefined initial relay state until first command |
| [8](#8) | **MEDIUM** | `functions/cmnd/cmnd.cpp` | `cycle` with invalid payload silently swallows the command (no ack, no log of rejection) |
| [9](#9) | **MEDIUM** | `mqttManager.cpp` | `cmnd/all/#` is documented as "lights only" but the code happily routes `cmnd/all/charger/...` to every device |
| [10](#10) | **MEDIUM** | `functions/conf/conf.cpp` | `pref.putString()` return value is ignored — failed writes ack as success |
| [11](#11) | **MEDIUM** | `functions/conf/conf.cpp` | NVS edit takes effect only after manual reboot; no automatic restart even when changing wifi credentials |
| [12](#12) | **MEDIUM** | `functions/stat/stat.cpp` | JSON injection through topic segments in `statAck` |
| [13](#13) | **MEDIUM** | `config.cpp` | CA cert expires `2036-05-26`; embedded fleet will brick simultaneously |
| [14](#14) | **MEDIUM** | `mqttManager.cpp` | `keepalive = 20s` is aggressive for cellular / lossy networks |
| [15](#15) | **MEDIUM** | `main.cpp` | Watchdog is attached only to the loop task; the MQTT task can hang unmonitored |
| [16](#16) | **MEDIUM** | `config.cpp` | NVS read failure cannot be distinguished from a legitimate sentinel value `255` |
| [17](#17) | **MEDIUM** | `functions/cmnd/cmnd.cpp` | `atoi(payload)` makes `"on"`, `"true"`, `""`, `"OFF"` all silently mean OFF |
| [18](#18) | **MEDIUM** | `mqttManager.cpp` | `mqttPublish` ignores the `int` returned by `esp_mqtt_client_enqueue` — silent message loss |
| [19](#19) | **MEDIUM** | Operations | No firmware version anywhere — `stat/health` payload offers no traceability |
| [20](#20) | **LOW** | `mqttManager.cpp` | Topics > 6 segments are silently truncated (`MAX_SEGMENT == 6`) |
| [21](#21) | **LOW** | `mqttManager.cpp` | Leading `/` and `//` collapse via `strtok_r` — silently accepted topics |
| [22](#22) | **LOW** | `functions/stat/stat.cpp` | Retained `stat/<dev>/ack` and `stat/<dev>/health` are misleading — stale to late subscribers |
| [23](#23) | **LOW** | `config.cpp` | No duplicate-GPIO check across `chargerPin` / `lightPin` |
| [24](#24) | **LOW** | `config.cpp` | No GPIO validity check — provisioning `pin = 50` would silently fail at runtime |
| [25](#25) | **LOW** | `main.cpp` | `Serial.begin()` is called *after* the first `Serial.printf("BOOT TIME...")` race — first line may be dropped on USB-CDC |
| [26](#26) | **LOW** | `config.cpp` / `cmd.h` | Bricked-pin debt: `RED_PIN`/`BLUE_PIN == 21` and the entire `cmd.*` pair are dead code commented out |
| [27](#27) | **LOW** | `config.cpp` | `String username` / `password` `c_str()` pointers handed to `esp_mqtt_client_config_t` are correct *today* but brittle |
| [28](#28) | **LOW** | `statusManager.cpp` | `LED_PIN = 10` — ESP32-C3-DevKitM-1 onboard WS2812 is on GPIO 8; double-check |
| [29](#29) | **LOW** | `mqttManager.cpp` | `online` and `last_will` use QoS 2; QoS 1 retained is the more common idiom |
| [30](#30) | **LOW** | `config.cpp` | `globalErrorCounter` is read by loop / incremented by MQTT event task — not atomic |
| [31](#31) | **INFO** | All | No firmware-side log shipping; remote diagnostics impossible once deployed |
| [32](#32) | **INFO** | `functions/tele/tele.h` | Declared function `tele(...)` has no implementation — link risk if ever called |
| [33](#33) | **INFO** | `nvs.csv` | Plaintext credentials live in the repo |

---

## Detailed findings

### <a id="1"></a>1. **BLOCKER** — Healthy device hard-reboots every 3 minutes

> Fixed

**Files:** `src/main.cpp:54-57`, `src/wifiUtils/checkWiFiStatus.cpp:3-11`

```cpp
// main.cpp
unsigned long prevMillis = 0;
...
void loop() {
  ...
  if((currMillis - WiFiDiconnectSince) >= 180000) {
    statusHandler(STATE_ERROR);
    ESP.restart();
  }
```

```cpp
// checkWiFiStatus.cpp
unsigned long WiFiDiconnectSince = 0;

void checkWiFiStatus(const char *ssid, const char *password) {
    if (WiFi.status() != WL_CONNECTED) {
      ...
      WiFiDiconnectSince = millis();
    }
}
```

`WiFiDiconnectSince` is initialised to `0` and is **only** written when the
WiFi-watchdog detects a disconnect. On a perfectly healthy device:

1. Boot at `t=0`, WiFi up, `WiFiDiconnectSince` stays `0`.
2. Loop runs unconditional check `(currMillis - 0) >= 180000`.
3. At `t ≈ 180_000` ms the condition is true → `ESP.restart()`.

The condition has *no* `WiFi.status() != WL_CONNECTED` guard. The intent
of "if WiFi has been down for 3 minutes, give up and reboot" is wrong: it
fires regardless of WiFi state.

Worse: even after a successful reconnection there is no code path that
clears `WiFiDiconnectSince` back to "no disconnect pending". A single
disconnect at t=10s arms the reboot for t=190s — the device reboots even
though WiFi recovered at t=15s.

Reproduced in `test/host/test_critique.cpp::test_wifi_disconnect_timer_reboot_loop`:

```
[RUN ] test_wifi_disconnect_timer_reboot_loop
  first spurious reboot triggers at ms=180000
[PASS]
```

And `test_wifi_disconnect_timer_not_cleared_on_reconnect`:

```
  reboot fires at ms=190000 despite reconnect at 15000
```

> **Recommended fix shape:** guard the check with `WiFi.status() !=
> WL_CONNECTED`, and clear `WiFiDiconnectSince` (e.g. set to `currMillis`)
> on every iteration that observes the link up.

---

### <a id="2"></a>2. **HIGH** — Unbounded VLA on the MQTT event-task stack

> Fixed

**File:** `src/mqttManager/mqttManager.cpp:51-71`

```cpp
char topic[event->topic_len + 1];
memcpy(topic, event->topic, event->topic_len);
topic[event->topic_len] = '\0';
...
char payload[event->data_len + 1];
```

`event->topic_len` and `event->data_len` are attacker-controlled (anyone
who can publish to the broker). Both are stack-allocated VLAs on the
**MQTT event loop task**, whose default stack on ESP-IDF is roughly
6 KB. A topic of even a few kilobytes will overflow the task stack —
in the best case the WDT panics, in the worst case a controlled write
beyond the stack guards corrupts adjacent memory.

Even ignoring the malicious case: the firmware accepts a topic of any
length the broker delivers; an upstream misconfiguration or buggy
publisher could cause silent failure.

> **Recommended fix shape:** validate `event->topic_len <
> SOME_REASONABLE_CAP` (say 256) and drop oversize messages with a log.

---

### <a id="3"></a>3. **HIGH** — `cycle()` overwrites global state; a second cycle strands the first charger OFF

**File:** `src/functions/cmnd/cmnd.cpp:90-101`

```cpp
int cycle(unsigned long timeInSeconds, char *segment[]) {
    int chargerID = atoi(segment[3]);
    int s = charger(chargerID, false);
    ...
    cycleFlag = true;
    cycleID = chargerID;            // single global slot
    cycleInterval = timeInSeconds*1000;
    cycleStart = millis();
    ...
}
```

Only **one** cycle is tracked. A second `cycle` command issued before
the first expires overwrites `cycleID`, `cycleInterval`, `cycleStart`.
The first charger was turned OFF by `charger(chargerID, false)` but is
never re-enabled — the deferred dispatcher in `main.cpp:46-52` only
re-enables the *current* `cycleID`.

This is not a theoretical concern: the bus topology is one device
controlling multiple chargers. Two operators (or two consecutive
events from a single operator UI) overlap in time → one charger is
stuck OFF until a human notices and sends a manual `charger/<i>/1`.

Reproduced in `test_cycle_overwrite_loses_first_charger`:

```
  charger 0 re-enables=0, charger 1 re-enables=1
[PASS]
```

> **Recommended fix shape:** per-charger cycle state (array of
> `{flag,start,interval}` indexed by chargerID), or queue, or reject
> the second cycle with NACK.

---

### <a id="4"></a>4. **HIGH** — `cycle` cannot be cancelled and ignores manual commands

> send cycle command again with payload 1 --> effectively cancels the cycle and keeps the device in high.

**File:** `src/functions/cmnd/cmnd.cpp:50-68` and `:90-101`

`charger()` does NOT clear `cycleFlag`. If an operator sends
`cmnd/<dev>/charger/<X>/1` during a cycle (because they realised the
cycle was wrong), the manual command lands; the relay turns on; the
cycle timer later fires and *again* sets the same charger ON — which
is a no-op here, but in the inverse case (operator sends OFF, cycle
expires and forces ON) the cycle silently overrides the operator.

There is also no MQTT verb to cancel an in-flight cycle.

> **Recommended fix shape:** clear `cycleFlag` whenever `charger()` is
> called with the matching `cycleID`, and add a `cmnd/<dev>/charger/<i>/cancel`.

---

### <a id="5"></a>5. **HIGH** — No OTA path

**File:** project-wide; nothing under `src/` references `esp_https_ota`,
`ArduinoOTA`, or any equivalent.

The unit has WiFi + TLS already; the broker is already trusted. There
is no way to ship a fix for any bug below without a truck-roll. For a
fleet that is meant to be field-deployed "to stations" this is a
significant operational risk and arguably should be addressed *before*
the first production flash.

`min_spiffs.csv` is selected as the partition table — which has dual
OTA app slots — yet OTA itself is not implemented.

> **Recommended fix shape:** a `conf/<dev>/ota` topic that triggers an
> `esp_https_ota` pull from a signed HTTPS URL. Reuse `ca_cert`.

---

### <a id="6"></a>6. **HIGH** — Relay state not persisted; reboot resets everything

**File:** `src/config.cpp:80-115` and `src/main.cpp:46-57`

Every reboot path (the BLOCKER above, the 5-error MQTT reboot, the
WiFi-init failure reboot, the watchdog reboot, the manual `conf/reboot`)
ends up back in `getDeviceSpecificConfig()` which unconditionally:

* sets every charger pin to `HIGH` (i.e. relayState=1, "on") at
  `config.cpp:92-94`;
* leaves every light pin in its default state — `pinMode(OUTPUT)` only.

This means a power flicker can turn every EV charger *on* simultaneously
across a station (assuming the relay wiring convention documented in
`DOCUMENTATION.md`), and lights stay in an undefined initial state.
In a production deployment that's a real fault — there should be a
documented "fail-safe" state, and ideally a last-known-good state read
from NVS at boot.

> **Recommended fix shape:** define and document the boot-time default
> per channel (NVS-configurable); optionally persist `relayState[]` on
> every change with rate-limited NVS commits.

---

### <a id="7"></a>7. **HIGH** — Light pins are `OUTPUT` but never written at boot

**File:** `src/config.cpp:100-115`

```cpp
for(counter; counter < (65 + totalPins); counter++) {
    ...
    lightPin[i] = prefs.getUChar(tmp, 255);
    ...
    pinMode(lightPin[i], OUTPUT);
    // no digitalWrite() — line missing intentionally? unintentionally?
    i++;
}
```

Compare to the charger loop at `config.cpp:80-97`, which always
follows `pinMode(...,OUTPUT)` with `digitalWrite(...,HIGH)` and
`relayState[i]=1`. Lights get neither. The pin's initial output level
on the ESP32-C3 after `pinMode(OUTPUT)` is `LOW` on cold boot, but
**not** guaranteed across all reset reasons (brown-out, deep sleep
wake, etc.). Even on cold boot, with the documented active-low relay
wiring, GPIO LOW = relay energised = NO contact connected — i.e.
lights come on at boot, even though `relayState[]` is 0 ("off").

This is an internal inconsistency: the in-memory state says one thing
and the physical relay does another. Any first `cmnd/.../light/<i>/0`
command issued by the operator becomes a no-op (relay already at LOW)
that the operator sees as "successful" — until they realise the lights
were on the whole time.

> **Recommended fix shape:** explicitly `digitalWrite(lightPin[i], LOW
> or HIGH)` at boot, and document which.

---

### <a id="8"></a>8. **MEDIUM** — Invalid `cycle` payload silently swallowed

**File:** `src/functions/cmnd/cmnd.cpp:19-24`

```cpp
if(seg_len >= 5 && strcmp(segment[4], "cycle") == 0) {
    if(atoi(payload) < 1) {
        status = -1;
        return;          // <-- returns BEFORE statAck
    }
    status = cycle(atoi(payload), segment);
}
```

The `return;` bypasses `statAck(segment, seg_len, status == 0)` at
the end of `cmnd()`. From the broker's point of view the command
simply vanished. Operators get no NACK, no log, no clue. They retry
with the same bad payload and the device "ignores" them again.

> **Recommended fix shape:** replace the bare `return;` with a
> `statAck(..., false)` and then return.

---

### <a id="9"></a>9. **MEDIUM** — `cmnd/all/charger/...` works even though docs say "lights only"

**File:** `src/mqttManager/mqttManager.cpp:73` and `src/functions/cmnd/cmnd.cpp:18-29`

`mqttManager` accepts any topic where `segment[1]` is `username` *or*
`"all"`. It then forwards to `cmnd()`, which has no per-verb gate on
`"all"`. So a publisher sending `cmnd/all/charger/0/1` will be executed
by every device on the bus, turning charger 0 on across the fleet —
even though `DOCUMENTATION.md` claims `cmnd/all/#` is for "the
`light/all` flood pattern".

Either the documentation is wrong or the code is wrong — pick one. As
written, a misrouted broadcast could affect every site simultaneously.

> **Recommended fix shape:** in `cmnd()` reject `"all"` for the
> `"charger"` verb explicitly, *or* update the docs to make broadcast
> chargers an intentional feature.

---

### <a id="10"></a>10. **MEDIUM** — `pref.putString()` return value ignored

**File:** `src/functions/conf/conf.cpp:38-47`

```cpp
if(pref.isKey(segment[4])) {
    pref.putString(segment[4], payload);
    ok = true;
}
```

`Preferences::putString` returns `size_t` (bytes written, 0 on failure).
A flash wear-leveling failure, full NVS partition, or oversize payload
would all silently report `ok = true` in the ack. Operators have no
visibility.

> **Recommended fix shape:** `ok = (pref.putString(...) > 0);`

---

### <a id="11"></a>11. **MEDIUM** — NVS edits don't take effect until manual reboot

**File:** `src/functions/conf/conf.cpp:33-49`

Editing `wifi_pass`, `wifi_ssid`, `dev_id`, `mqtt_pass` writes to NVS
but never re-loads the in-memory `ssid` / `wifiPassword` / `username` /
`password` globals. A field engineer rotating credentials must
`conf/<dev>/edit/creds/wifi_pass <new>` *and* `conf/<dev>/reboot` —
and pray that the new credentials work before they walk away. There's
also no rollback if they were wrong.

> **Recommended fix shape:** after a successful credentials edit,
> trigger a delayed reboot (so the ack can be delivered first) and
> ideally store a "previous known good" copy for one-shot rollback.

---

### <a id="12"></a>12. **MEDIUM** — JSON injection via topic segments in `statAck`

**File:** `src/functions/stat/stat.cpp:18-36`

```cpp
snprintf(payload, sizeof(payload),
    "{\"topic\":\"%s\",\"ok\":%s,\"t\":%lu}",
    originTopic ? originTopic : "", ...);
```

MQTT topic spec permits most printable characters (excluding `+`, `#`,
`\0`) — including `"`, `\\`, `\n`. A publisher with broker credentials
can craft a topic that breaks out of the JSON string and injects keys.

Reproduced in `test_statAck_json_injection_via_topic`:

```
  produced: {"topic":"cmnd/devX/light/0","injected":"yes","ok":true,"t":12345}
```

The injected `"injected":"yes"` is treated as a top-level key by any
parser. If a downstream service trusts `stat/.../ack` payloads (e.g.
for audit logs or billing) this is a real vulnerability.

> **Recommended fix shape:** escape the topic string, or use
> `ArduinoJson` (which was previously a dependency and was removed per
> `DOCUMENTATION.md`).

---

### <a id="13"></a>13. **MEDIUM** — Embedded CA cert expires 2036-05-26

**File:** `src/config.cpp:6-25`

The `ca_cert` PEM is valid `2026-05-29 → 2036-05-26`. Ten years from
provisioning, every device in the fleet will simultaneously fail TLS
handshake and never reconnect. Without OTA (see finding #5), there is
no remote remediation.

> **Recommended fix shape:** plan for cert rotation as part of an OTA
> capability. Consider provisioning the CA via NVS so it can be
> updated without a firmware re-flash.

---

### <a id="14"></a>14. **MEDIUM** — Aggressive MQTT keepalive on lossy networks

**File:** `src/mqttManager/mqttManager.cpp:109`

```cpp
mqtt_cfg.session.keepalive = 20;
```

The broker will declare a 20-second-keepalive client dead at ~30s.
Cellular failover, WiFi roaming, or a brief AP reboot can all exceed
that. Each disconnect cycle takes the device through `STATE_MQTT_
DISCONNECTED` and re-publishes the retained `last_will` "offline" —
which then sticks until the broker delivers the next "online" — so
upstream consumers see a flicker.

> **Recommended fix shape:** raise to 60-120s unless tighter detection
> is needed; pair with a *reasonable* error budget before reboot.

---

### <a id="15"></a>15. **MEDIUM** — Watchdog only watches the loop task

**File:** `src/main.cpp:15-31`

```cpp
esp_task_wdt_add(NULL);    // == current task (loopTask)
```

The MQTT event loop is on a separate IDF task that handles ALL
incoming commands. If the dispatcher hangs (deadlock with TLS,
synchronous NVS write in the conf handler that misbehaves, etc.),
nothing detects it. `loopTask` keeps petting the WDT and the device
keeps "running" — silently ignoring MQTT.

> **Recommended fix shape:** also subscribe the MQTT event task to
> the WDT, e.g. on the first `MQTT_EVENT_CONNECTED` call
> `esp_task_wdt_add(xTaskGetCurrentTaskHandle())`. Or run the
> dispatcher work on a queue-fed worker task that pets WDT.

---

### <a id="16"></a>16. **MEDIUM** — Sentinel `255` is also a legal NVS value

**File:** `src/config.cpp:58-110`

```cpp
pinOffset = prefs.getUChar("offset", 255);
totalPins = prefs.getUChar("totalPin", 255);
...
chargerPin[i] = prefs.getUChar(tmp, 255);
if(chargerPin[i] == 255) { ESP.restart(); }
```

`255` is used as "missing key" sentinel. But `getUChar` cannot
distinguish missing vs. genuinely stored `255`. If a provisioning CSV
ever contained `255` as a real pin number it would trigger the error
path. Conversely, *any other* invalid value (a typo `99`) is happily
accepted by the read but fails silently later at `pinMode(99, OUTPUT)`.

> **Recommended fix shape:** `prefs.isKey(...)` to detect missing keys
> independently of values; range-check the read value against the
> known-good GPIOs for the C3.

---

### <a id="17"></a>17. **MEDIUM** — `atoi(payload)` makes typos turn relays OFF

**File:** `src/functions/cmnd/cmnd.cpp:26,34,38`

`atoi("on") == 0`, `atoi("true") == 0`, `atoi("") == 0`, `atoi("OFF") == 0`,
`atoi(" 1 ") == 1`, `atoi("17 bananas") == 17`.

For a production UI sitting between an operator and the broker, this
is a footgun. A new tech sends `cmnd/<dev>/light/3 on` expecting it
to turn the light on, and the relay de-energises instead.

Reproduced in `test_empty_payload_treated_as_off`.

> **Recommended fix shape:** strict parsing — accept only `"0"` and
> `"1"`. NACK anything else with `statAck(..., false)`.

---

### <a id="18"></a>18. **MEDIUM** — `mqttPublish` silently drops on a full queue

**File:** `src/mqttManager/mqttManager.cpp:139-141`

```cpp
void mqttPublish(...) {
    esp_mqtt_client_enqueue(client, pubTopic, message, 0, QoS, retain, store);
}
```

`esp_mqtt_client_enqueue` returns `int` (`-1` on full queue, message
id on success). If the queue is full because of broker congestion or
extended disconnect (the publishes are queued in RAM, not flash —
`store` flag notwithstanding), the message is silently dropped.

> **Recommended fix shape:** check the return value, log on `-1`,
> increment a "dropped" counter exposed via `stat/health`.

---

### <a id="19"></a>19. **MEDIUM** — No firmware version anywhere

**Files:** none — *that's the problem*.

`stat/<dev>/health` reports `{heap, rssi, uptime_ms, err}`. Nothing
identifies which firmware build the device is running. Fleet
operations is impossible without it: "which units still have the
broken cycle code?" cannot be answered from telemetry alone.

> **Recommended fix shape:** `#define FW_VERSION "..."` baked from a
> CI variable; include in `stat/health` and on `MQTT_EVENT_CONNECTED`
> announce; persist last-seen-version in NVS for boot-loop detection.

---

### <a id="20"></a>20. **LOW** — `MAX_SEGMENT = 6` silently truncates longer topics

**File:** `src/config.h:12`, used in `src/mqttManager/mqttManager.cpp:55-65`

A topic with 7+ slash-separated levels has its 7th segment silently
dropped. Today no command uses 6+ levels, so it doesn't matter — but
the truncation is silent, so a future "add a sub-verb" change will be
hard to debug.

Reproduced in `test_topic_parse_overflow_silent_truncation`.

> **Recommended fix shape:** check `token == NULL` after the loop; if
> any tokens remain, NACK / log.

---

### <a id="21"></a>21. **LOW** — `strtok_r` collapses leading & double slashes

**File:** `src/mqttManager/mqttManager.cpp:55-65`

`/cmnd/dev/light/0` and `cmnd//dev/light/0` are parsed identically to
`cmnd/dev/light/0`. The MQTT broker would route differently for the
leading-slash topic (which creates an empty topic level), but on the
device they look the same. Minor robustness issue.

Reproduced in `test_topic_parse_leading_slash_accepted` and
`test_topic_parse_double_slash_collapsed`.

---

### <a id="22"></a>22. **LOW** — `stat/.../ack` and `stat/.../health` are retained but ephemeral

**File:** `src/functions/stat/stat.cpp:15,27,58`

```cpp
mqttPublish(topic.c_str(), payload, 1, 1, true);  // QoS=1, retain=1
```

A retained `ack` means a late subscriber sees only the *last* ack —
useless. A retained `health` payload's `uptime_ms`/`heap`/`rssi` are
snapshots — a subscriber connecting hours later sees stale data
*labelled with what it was when published*. Retain semantics rarely
fit point-in-time payloads.

> **Recommended fix shape:** publish `ack` with `retain=0`. For
> `health`, either keep it retained (with the understanding that the
> data is "last-known") or move to non-retained per-event.

---

### <a id="23"></a>23. **LOW** — No duplicate GPIO check across pin map

**File:** `src/config.cpp:80-115`

Two NVS keys pointing at the same physical GPIO are accepted. Each
`pinMode` is harmless but `digitalWrite` from one channel will
clobber the other.

> **Recommended fix shape:** while building the arrays, populate a
> `bool used[40]` and fail (reboot loop, or LED error) on collision.

---

### <a id="24"></a>24. **LOW** — No GPIO validity check

**File:** `src/config.cpp:83-92`

`getUChar` returns 0..255. ESP32-C3 has valid GPIO 0..21, with several
strapping / flash pins that should not be used as outputs (GPIO 11-17
are typically flash; GPIO 8/9 are strapping). A typo'd `nvs.csv` could
provision GPIO 47 and the firmware would `pinMode(47, OUTPUT)` which
silently fails at the SDK layer.

> **Recommended fix shape:** allowlist of GPIOs known-good for relay
> drive on the C3, validated at boot.

---

### <a id="25"></a>25. **LOW** — `Serial.printf` before `Serial.begin` USB-CDC settle

**File:** `src/main.cpp:24-28`

```cpp
statusHandler(STATE_BOOT);
Serial.begin(115200);
Serial.printf("BOOT TIME FREE HEAP: %d\n", ESP.getFreeHeap());
```

With `-DARDUINO_USB_CDC_ON_BOOT=1`, the host PC needs to enumerate the
CDC endpoint before serial output is visible. The first line is often
lost. Not functional, but during field debugging this is annoying.

> **Recommended fix shape:** `while(!Serial && millis() < 2000);` or
> a 200-300ms `delay` after `Serial.begin`.

---

### <a id="26"></a>26. **LOW** — Dead-code debt

**Files:** `src/cmd.cpp`, `src/cmd.h`, `src/config.h:7-8`

`cmd.cpp` and `cmd.h` are entirely commented out. `config.h` defines
`RED_PIN` and `BLUE_PIN`, both set to `21`, with the comment
`// i know it is bricked`. None of those symbols are referenced by live
code. Carrying them slows future readers down and is a maintenance
liability.

> **Recommended fix shape:** delete `cmd.cpp`, `cmd.h`, `RED_PIN`,
> `BLUE_PIN`, and the `/* DEPRECIATED cmd(payload); */` comment block
> in `mqttManager.cpp:81-84`.

---

### <a id="27"></a>27. **LOW** — `c_str()` lifetimes handed to IDF MQTT config

**File:** `src/mqttManager/mqttManager.cpp:108-121`

```cpp
mqtt_cfg.credentials.client_id   = username.c_str();
mqtt_cfg.credentials.username    = username.c_str();
mqtt_cfg.credentials.authentication.password = password.c_str();
```

`username` / `password` are `String` globals set once at boot — today
the pointers are stable. But the code uses `static String lastWillTopic`
explicitly because the author knew this is fragile. Same treatment
should apply to anything else where a `String::c_str()` pointer is
stored long-term.

If a future change reassigns `username` (e.g. live credential rotation),
the IDF library will dereference a freed buffer. Better to convert
these to `static char[64]` copies up front.

---

### <a id="28"></a>28. **LOW** — `LED_PIN = 10` — verify against the actual board

**File:** `src/statusManager/statusManager.h:4`

The ESP32-C3-DevKitM-1 (per `platformio.ini` `board = esp32-c3-devkitm-1`)
has its onboard WS2812 RGB LED on **GPIO 8**, not 10. If the production
PCB does in fact route the LED to GPIO 10 this is correct — but worth
double-checking against the BOM/schematic.

---

### <a id="29"></a>29. **LOW** — QoS 2 for online status & last-will is overkill

**File:** `src/mqttManager/mqttManager.cpp:23, 120`

QoS 2 is exactly-once-delivery; it's the most expensive QoS level
(4-way handshake, broker-side storage). For "online/offline status"
QoS 1 retained is the conventional choice — duplicate delivery is
harmless.

---

### <a id="30"></a>30. **LOW** — `globalErrorCounter` is not atomic

**File:** `src/config.cpp:35`, written from MQTT event task, read &
reset from main loop.

`unsigned int` ++ from two threads is not atomic on ESP32-C3 (RISC-V).
Increments can be lost. The actual data-race window is small, but on
principle: declare `volatile std::atomic<unsigned int>` (or use
`Atomic` types) when touching from multiple FreeRTOS tasks.

---

### <a id="31"></a>31. **INFO** — No log shipping

Once flashed to a station, the only diagnostic surface is
`stat/.../health`. There is no way to retrieve `Serial.println(...)`
output. For a "remote-managed" device this is operationally weak.

> **Recommended fix shape:** a circular in-RAM log buffer publishable
> on demand via `conf/<dev>/log` or pushed on error.

---

### <a id="32"></a>32. **INFO** — `tele(...)` declared, not implemented

**File:** `src/functions/tele/tele.h:8`

`void tele(char *segment[], const size_t seg_len, const char *payload);`

No `.cpp`. The function is unreferenced today (so the link succeeds),
but a future caller would hit a link error. Either delete the header
or stub the function.

---

### <a id="33"></a>33. **INFO** — Plaintext credentials in `nvs.csv`

**File:** `nvs.csv`

```
wifi_pass,data,string,StayOnline@MSIPL
mqtt_pass,data,string,Passon!23
```

The repo carries production-looking credentials. Even though this is
a provisioning-input file (not embedded in firmware), checking it in
to git is a credential-disclosure risk. Use `.gitignore` for the
device-specific CSV; commit only a template.

---

## Reproductions

All reproductions live in `test/host/test_critique.cpp`. Run from the
project root:

```bash
cd test/host
g++ -std=c++17 -Wall -O0 test_critique.cpp -o test_critique
./test_critique
```

Result on this tree (2026-06-05):

```
=== 17 PASS  0 FAIL ===
```

Tests demonstrate (in order):

| Test name | Finding |
|---|---|
| `test_wifi_disconnect_timer_reboot_loop` | #1 |
| `test_wifi_disconnect_timer_not_cleared_on_reconnect` | #1 |
| `test_cycle_overwrite_loses_first_charger` | #3 |
| `test_cycle_overrides_manual_off` | #4 |
| `test_topic_parse_overflow_silent_truncation` | #20 |
| `test_topic_parse_leading_slash_accepted` | #21 |
| `test_topic_parse_double_slash_collapsed` | #21 |
| `test_topic_parse_empty_topic` | (defensive) |
| `test_empty_payload_treated_as_off` | #17 |
| `test_charger_bounds` | (positive — bounds are correct) |
| `test_statState_buffer_worst_case` | (positive — buffer is large enough) |
| `test_statAck_json_injection_via_topic` | #12 |
| `test_pin_offset_equals_total_allows_zero_lights` | #7 (boundary) |
| `test_pin_offset_zero_means_no_chargers_at_boot` | #7 (boundary) |
| `test_cycle_interval_overflow` | (informational) |
| `test_nvs_key_alphabet` | (positive — key generation is bounded) |
| `test_vla_topic_stack_risk_documented` | #2 |

---

## Summary recommendation

Hold production flashing until at minimum these three are fixed:

1. **#1** — the 3-minute reboot loop will be visible from the first
   device on the first day.
2. **#2** — the unbounded VLA is a remote-DoS / stack-corruption
   primitive. Easy to mitigate (one `if` statement).
3. **#5** — without OTA you cannot ship fixes for *anything* in this
   list once devices are in the field.

The cycle-handling bugs (#3, #4), the relay-state inconsistencies
(#6, #7), and the JSON-injection in `statAck` (#12) should be
addressed in the same release that ships OTA, since they will be
visible as operational defects within hours of deployment.

Everything else can be batched into a follow-up release.
