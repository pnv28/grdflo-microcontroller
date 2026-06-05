// Host-side critique tests for grdflo-microcontroller firmware.
//
// These tests do NOT modify or include any production source. They re-create
// the *logic* of small pieces of the firmware (topic parsing, payload sizing,
// timer math) so we can drive edge-case inputs through them and demonstrate
// whether the production firmware would behave correctly.
//
// Build & run (host, no MCU needed):
//   g++ -std=c++17 -Wall -O0 test_critique.cpp -o test_critique && ./test_critique
//
// Each TEST prints PASS / FAIL. Exit code == 0 iff all PASS.

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <climits>

// ---------------------------------------------------------------------------
// Test harness
// ---------------------------------------------------------------------------
static int g_pass = 0, g_fail = 0;
#define TEST(name) static void name();                                       \
                   static void run_##name() {                                \
                       printf("\n[RUN ] %s\n", #name);                       \
                       int before = g_fail; name();                         \
                       if (g_fail == before) { ++g_pass; printf("[PASS] %s\n", #name); } \
                       else                   { printf("[FAIL] %s\n", #name); } \
                   }                                                          \
                   static void name()
#define EXPECT(cond) do { if (!(cond)) { ++g_fail; printf("  EXPECT failed: %s @ %d\n", #cond, __LINE__); } } while(0)
#define EXPECT_EQ(a,b) do { auto _a=(a); auto _b=(b); if (!(_a==_b)) { ++g_fail; printf("  EXPECT_EQ failed @ %d: lhs=%lld rhs=%lld\n", __LINE__, (long long)_a, (long long)_b); } } while(0)
#define EXPECT_STREQ(a,b) do { const char* _a=(a); const char* _b=(b); if (strcmp(_a,_b)!=0) { ++g_fail; printf("  EXPECT_STREQ failed @ %d: lhs=%s rhs=%s\n", __LINE__, _a, _b); } } while(0)

// ===========================================================================
// 1) WiFiDiconnectSince reboot loop
//
// Reproduces main.cpp's check:
//   if((currMillis - WiFiDiconnectSince) >= 180000) { ESP.restart(); }
//
// WiFiDiconnectSince is initialised to 0 and ONLY updated in checkWiFiStatus()
// when WiFi.status() != WL_CONNECTED. If WiFi stays connected, the value
// remains 0 forever -- and once millis() reaches 180000 the boolean is true
// every iteration, so the device will hard-reboot at ~3 minutes of uptime.
// ===========================================================================
TEST(test_wifi_disconnect_timer_reboot_loop) {
    unsigned long WiFiDiconnectSince = 0;  // exactly as in checkWiFiStatus.cpp
    bool wifi_connected_throughout = true;  // imagine WiFi never drops

    // Simulate 4 minutes of uptime, sampling every 100ms. Track first
    // moment the reboot condition becomes true.
    unsigned long first_reboot_ms = ULONG_MAX;
    for (unsigned long ms = 0; ms < 240000; ms += 100) {
        // checkWiFiStatus() only fires inside the 60-second heartbeat block.
        // Even if it fired every iteration, with WiFi connected it would NOT
        // touch WiFiDiconnectSince -- so the value stays 0.
        (void)wifi_connected_throughout;
        if ((ms - WiFiDiconnectSince) >= 180000UL) {
            first_reboot_ms = ms;
            break;
        }
    }
    printf("  first spurious reboot triggers at ms=%lu\n", first_reboot_ms);
    EXPECT(first_reboot_ms <= 180000UL);  // bug demonstrated
}

// ===========================================================================
// 2) WiFiDiconnectSince is also wrong on RECONNECT:
//    once it has been set to t0 by a disconnect, even after WiFi recovers
//    nothing in the code resets it -- so 180s later we reboot anyway.
// ===========================================================================
TEST(test_wifi_disconnect_timer_not_cleared_on_reconnect) {
    unsigned long WiFiDiconnectSince = 0;
    // t=10s WiFi drops, checkWiFiStatus runs:
    WiFiDiconnectSince = 10000;
    // t=15s WiFi is back. In production code nothing clears WiFiDiconnectSince
    // on reconnect. We watch the same condition.
    unsigned long ms_when_reboots = 0;
    for (unsigned long ms = 15000; ms < 600000; ms += 100) {
        if ((ms - WiFiDiconnectSince) >= 180000UL) { ms_when_reboots = ms; break; }
    }
    printf("  reboot fires at ms=%lu despite reconnect at 15000\n", ms_when_reboots);
    EXPECT(ms_when_reboots > 0 && ms_when_reboots <= 190100UL);
}

// ===========================================================================
// 3) Cycle command overwrite race -- the cmnd/cmnd.cpp `cycle()` function
//    overwrites the single shared (cycleID, cycleStart, cycleInterval, cycleFlag)
//    triplet. A second cycle command leaves the first charger stuck OFF
//    because the deferred re-enable in loop() only fires for cycleID.
// ===========================================================================
namespace cycle_sim {
    // Mirrors the production globals.
    int cycleID = 0;
    unsigned long cycleStart = 0;
    unsigned long cycleInterval = 0;
    bool cycleFlag = false;

    // Mirrors charger() -- just records calls.
    std::vector<std::pair<int,bool>> charger_calls;
    int charger(int id, bool on) {
        charger_calls.push_back({id, on});
        return 0;
    }

    // Mirrors cycle() with the same logic as production.
    int cycle(unsigned long secs, int chargerID, unsigned long now_ms) {
        int s = charger(chargerID, false);
        if (s != 0) return s;
        cycleFlag = true;
        cycleID = chargerID;
        cycleInterval = secs * 1000;
        cycleStart = now_ms;
        return 0;
    }

    // Mirrors the dispatch in loop().
    void tick(unsigned long now_ms) {
        if (cycleFlag && (now_ms - cycleStart) >= cycleInterval) {
            charger(cycleID, true);
            cycleFlag = false;
        }
    }
}

TEST(test_cycle_overwrite_loses_first_charger) {
    using namespace cycle_sim;
    charger_calls.clear();
    cycleFlag = false;

    // t=0: cycle charger 0 for 60s
    cycle(60, 0, 0);
    // t=10s: cycle charger 1 for 60s -- this overwrites cycleID
    cycle(60, 1, 10000);
    // simulate 5 minutes
    for (unsigned long t = 10000; t < 300000; t += 1000) tick(t);

    // Production behaviour: only ONE re-enable, and it is for charger 1.
    // Charger 0 was turned OFF and is never turned back on.
    int reenables_for_0 = 0, reenables_for_1 = 0;
    for (auto &c : charger_calls) if (c.second) {
        if (c.first == 0) ++reenables_for_0;
        if (c.first == 1) ++reenables_for_1;
    }
    printf("  charger 0 re-enables=%d, charger 1 re-enables=%d\n",
           reenables_for_0, reenables_for_1);
    EXPECT_EQ(reenables_for_0, 0);   // BUG: charger 0 is stranded OFF
    EXPECT_EQ(reenables_for_1, 1);
}

// ===========================================================================
// 4) Cycle ignores manual override -- if the operator sends a manual ON
//    while a cycle is pending, the cycle timer will later force the same
//    charger OFF... actually it forces it ON. But if the operator sends
//    a manual OFF after a cycle was started, the cycle will fight back
//    and turn it ON at expiry. cycleFlag is never cleared on manual cmd.
// ===========================================================================
TEST(test_cycle_overrides_manual_off) {
    using namespace cycle_sim;
    charger_calls.clear();
    cycleFlag = false;

    cycle(30, 0, 0);              // turn off, schedule re-enable in 30s
    // operator sends a manual OFF after 5s (cycleFlag is NOT cleared)
    charger(0, false);
    // operator sends a manual ON after 10s -- they want it ON now
    charger(0, true);
    // cycle still fires at t=30s and "re-enables" charger 0 (no-op here,
    // but the firmware also clears cycleFlag only after the timer fires)
    for (unsigned long t = 0; t < 60000; t += 1000) tick(t);

    // We expect manual control to be authoritative. Production code keeps
    // cycleFlag true through manual commands.
    EXPECT(cycleFlag == false);   // cleared *after* timer expiry, not by manual cmd
    // The point is: there is no API surface to *cancel* an in-flight cycle.
}

// ===========================================================================
// 5) MQTT topic parsing -- reproduces the strtok_r flow in mqttManager.cpp.
//    Issues we want to demonstrate:
//      a) topic with > MAX_SEGMENT segments is silently truncated
//      b) leading '/' is silently accepted
//      c) consecutive '//' is silently collapsed
//      d) empty topic returns tokenCount=0 -> early return OK
// ===========================================================================
namespace topic_parse {
    constexpr size_t MAX_SEGMENT = 6;
    static size_t parse(char *topic, char *out_segments[MAX_SEGMENT]) {
        size_t n = 0;
        char *save = nullptr;
        char *tok = strtok_r(topic, "/", &save);
        while (tok && n < MAX_SEGMENT) {
            out_segments[n++] = tok;
            tok = strtok_r(nullptr, "/", &save);
        }
        return n;
    }
}

TEST(test_topic_parse_overflow_silent_truncation) {
    using namespace topic_parse;
    char t[] = "cmnd/devX/light/0/extra/junk/morejunk";  // 7 segments
    char *segs[MAX_SEGMENT] = {};
    size_t n = parse(t, segs);
    EXPECT_EQ(n, MAX_SEGMENT);                              // we cap at 6
    EXPECT_STREQ(segs[0], "cmnd");
    EXPECT_STREQ(segs[5], "junk");                          // "morejunk" silently dropped
}

TEST(test_topic_parse_leading_slash_accepted) {
    using namespace topic_parse;
    char t[] = "/cmnd/devX/light/0";
    char *segs[MAX_SEGMENT] = {};
    size_t n = parse(t, segs);
    // strtok_r skips leading delimiters, so the leading '/' is invisible.
    // MQTT spec says leading '/' creates an empty topic level -- so a
    // publisher could send "/cmnd/devX/light/0" and the device would
    // accept it as if it were "cmnd/devX/light/0".
    EXPECT_EQ(n, 4);
    EXPECT_STREQ(segs[0], "cmnd");
}

TEST(test_topic_parse_double_slash_collapsed) {
    using namespace topic_parse;
    char t[] = "cmnd//devX//light/0";
    char *segs[MAX_SEGMENT] = {};
    size_t n = parse(t, segs);
    EXPECT_EQ(n, 4);   // empty segments silently elided
    EXPECT_STREQ(segs[1], "devX");
}

TEST(test_topic_parse_empty_topic) {
    using namespace topic_parse;
    char t[] = "";
    char *segs[MAX_SEGMENT] = {};
    size_t n = parse(t, segs);
    EXPECT_EQ(n, 0);   // early return path
}

// ===========================================================================
// 6) Empty payload turns chargers/lights OFF.  atoi("") returns 0.
//    A publisher who forgets to set a payload accidentally de-energises.
// ===========================================================================
TEST(test_empty_payload_treated_as_off) {
    EXPECT_EQ(atoi(""), 0);
    EXPECT_EQ(atoi("on"), 0);       // non-numeric → 0 → "off"
    EXPECT_EQ(atoi("true"), 0);     // non-numeric → 0 → "off"
    EXPECT_EQ(atoi("0"), 0);
    EXPECT_EQ(atoi("1"), 1);
    EXPECT_EQ(atoi("  1 "), 1);     // leading space tolerated
    EXPECT_EQ(atoi("17 bananas"), 17);  // garbage suffix ignored
    // A typo "ON" in the operator's UI silently turns the relay OFF.
    // That's a sharp edge.
}

// ===========================================================================
// 7) Charger bounds check is correct (off-by-one safe).
//    chargerID >= pinOffset OR chargerID < 0 is rejected.
//    Reproduces logic from cmnd.cpp.
// ===========================================================================
TEST(test_charger_bounds) {
    int pinOffset = 4;
    auto allowed = [&](int id) { return !(id >= pinOffset || id < 0); };
    EXPECT(allowed(0));
    EXPECT(allowed(3));
    EXPECT(!allowed(4));       // off-by-one boundary -- correctly rejected
    EXPECT(!allowed(-1));
    EXPECT(!allowed(INT_MIN));
}

// ===========================================================================
// 8) statState payload sizing.  Worst-case 16-pin device, all relays "on",
//    must fit in payload[256].
// ===========================================================================
TEST(test_statState_buffer_worst_case) {
    char payload[256];
    int n = 0;
    int pinOffset = 8, totalPins = 16;
    int relayState[16];
    for (int i = 0; i < 16; ++i) relayState[i] = 1;  // worst-case width

    n += snprintf(payload + n, sizeof(payload) - n, "{\"chargers\":[");
    for (int i = 0; i < pinOffset; ++i) {
        n += snprintf(payload + n, sizeof(payload) - n, "%s%d", i ? "," : "", relayState[i]);
    }
    n += snprintf(payload + n, sizeof(payload) - n, "],\"lights\":[");
    for (int i = pinOffset; i < totalPins; ++i) {
        n += snprintf(payload + n, sizeof(payload) - n, "%s%d", i == pinOffset ? "" : ",", relayState[i]);
    }
    n += snprintf(payload + n, sizeof(payload) - n, "]}");
    printf("  produced JSON (%d bytes): %s\n", n, payload);
    EXPECT(n < (int)sizeof(payload));
    EXPECT(payload[n-1] == '}');
}

// ===========================================================================
// 9) JSON injection through topic segment used in statAck.
//
// statAck builds:  {"topic":"<original>","ok":...,"t":...}
// originTopic is built by concatenating segments[] joined with '/'.  MQTT
// topics technically allow many characters including '"'. If a topic segment
// contains '"' the JSON becomes invalid / injectable.
// ===========================================================================
TEST(test_statAck_json_injection_via_topic) {
    const char *segments[] = {"cmnd", "devX", "light", "0\",\"injected\":\"yes"};
    std::string original;
    for (size_t i = 0; i < 4; ++i) {
        if (i) original += "/";
        original += segments[i];
    }
    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"topic\":\"%s\",\"ok\":%s,\"t\":%lu}",
             original.c_str(), "true", 12345UL);
    printf("  produced: %s\n", payload);
    // Production firmware does no escaping.  We assert the *bug* -- the
    // payload now contains an injected key.
    EXPECT(strstr(payload, "\"injected\":\"yes\"") != nullptr);
}

// ===========================================================================
// 10) Pin offset boundary -- pinOffset == totalPins is allowed by the
//     validation `pinOffset > totalPins`. That makes lightPin = new int[0].
//     The light loop then never executes -- no lights configurable.
// ===========================================================================
TEST(test_pin_offset_equals_total_allows_zero_lights) {
    uint8_t pinOffset = 4, totalPins = 4;
    bool valid = !(totalPins > 16 || pinOffset > totalPins);
    EXPECT(valid);                              // passes validation
    int n_light_pins = (int)totalPins - (int)pinOffset;
    EXPECT_EQ(n_light_pins, 0);                 // zero-length allocation
}

// ===========================================================================
// 11) Pin offset == 0 boundary -- means zero chargers, all lights.
//     The "all chargers off at boot" loop never runs.
// ===========================================================================
TEST(test_pin_offset_zero_means_no_chargers_at_boot) {
    uint8_t pinOffset = 0, totalPins = 8;
    bool valid = !(totalPins > 16 || pinOffset > totalPins);
    EXPECT(valid);
    int n_charger_pins = pinOffset;
    EXPECT_EQ(n_charger_pins, 0);
    // The "set every charger HIGH at boot" loop is skipped.
    // Lights are pinMode'd OUTPUT but never digitalWrite'n,
    // so their initial GPIO level is the chip default (LOW).
}

// ===========================================================================
// 12) Cycle interval overflow -- atoi() into int, multiplied by 1000 into
//     unsigned long. For very large requested seconds the math wraps.
// ===========================================================================
TEST(test_cycle_interval_overflow) {
    int payload_seconds = atoi("9999999999");   // int saturates / overflows
    printf("  atoi of 9999999999 = %d\n", payload_seconds);
    // implementation-defined, but with 32-bit int atoi returns INT_MAX on overflow on glibc
    // We just demonstrate the cast.
    unsigned long ms = (unsigned long)payload_seconds * 1000UL;
    printf("  cycleInterval ms = %lu\n", ms);
    // No assertion -- this test is illustrative.
}

// ===========================================================================
// 13) NVS-style key generator: tmp[2] = { 'A'+i, '\0' }.
//     Verify the alphabet only goes up to 'P' when totalPins == 16.
//     Anything beyond 16 would step past 'P' but is rejected by validation.
// ===========================================================================
TEST(test_nvs_key_alphabet) {
    char tmp[2] = {0};
    int last_char = 0;
    for (int counter = 65; counter < (65 + 16); ++counter) {
        tmp[0] = (char)counter; tmp[1] = '\0';
        last_char = counter;
    }
    EXPECT_EQ(last_char, 65 + 15);     // 'P'
    EXPECT_EQ((char)last_char, 'P');
}

// ===========================================================================
// 14) VLA stack-allocation risk -- if MQTT broker sends a 10KB topic name
//     mqttManager declares `char topic[event->topic_len + 1]`. With the
//     MQTT task default stack ~6KB-8KB the firmware would corrupt the
//     stack. Here we only print the risk threshold -- no assertion.
// ===========================================================================
TEST(test_vla_topic_stack_risk_documented) {
    size_t mqtt_task_stack = 6 * 1024;  // typical IDF default for MQTT task
    size_t risky_topic_len = mqtt_task_stack - 1024;  // leave headroom
    printf("  topic_len > %zu bytes would risk MQTT-task stack overflow\n",
           risky_topic_len);
    // Spec-level: there is no length cap before the VLA declaration.
    EXPECT(risky_topic_len > 0);
}

// ===========================================================================
// MAIN -- declared after all TEST() bodies but referenced via run_* shims.
// ===========================================================================
int main() {
    printf("=== grdflo-microcontroller host critique tests ===\n");

    #define RUN(name) run_##name()
    RUN(test_wifi_disconnect_timer_reboot_loop);
    RUN(test_wifi_disconnect_timer_not_cleared_on_reconnect);
    RUN(test_cycle_overwrite_loses_first_charger);
    RUN(test_cycle_overrides_manual_off);
    RUN(test_topic_parse_overflow_silent_truncation);
    RUN(test_topic_parse_leading_slash_accepted);
    RUN(test_topic_parse_double_slash_collapsed);
    RUN(test_topic_parse_empty_topic);
    RUN(test_empty_payload_treated_as_off);
    RUN(test_charger_bounds);
    RUN(test_statState_buffer_worst_case);
    RUN(test_statAck_json_injection_via_topic);
    RUN(test_pin_offset_equals_total_allows_zero_lights);
    RUN(test_pin_offset_zero_means_no_chargers_at_boot);
    RUN(test_cycle_interval_overflow);
    RUN(test_nvs_key_alphabet);
    RUN(test_vla_topic_stack_risk_documented);

    printf("\n=== %d PASS  %d FAIL ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
