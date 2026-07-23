#include "net_activity.h"

#include <Arduino.h>  // millis()
#include <string.h>

// Per-channel last-activity timestamp (millis). Guarded by s_ever so that a
// genuine timestamp of 0 (first millisecond after boot) is not mistaken for
// "never seen". Both arrays are plain scalars: a single aligned store/load is
// atomic on ESP32, and readers gate on s_ever then tolerate a one-tick skew on
// the timestamp, so no lock is needed.
static volatile uint32_t s_last_ms[NET_CH_COUNT] = {0};
static volatile bool     s_ever[NET_CH_COUNT]    = {false};

void net_activity_mark(net_channel_t ch) {
    if ((unsigned)ch >= (unsigned)NET_CH_COUNT) return;
    s_last_ms[ch] = (uint32_t)millis();
    s_ever[ch] = true;
}

uint32_t net_activity_age_ms(net_channel_t ch) {
    if ((unsigned)ch >= (unsigned)NET_CH_COUNT) return NET_ACTIVITY_NEVER;
    if (!s_ever[ch]) return NET_ACTIVITY_NEVER;
    // Unsigned subtraction wraps correctly across the millis() rollover.
    return (uint32_t)millis() - s_last_ms[ch];
}

uint32_t net_activity_age_any_ms(void) {
    uint32_t best = NET_ACTIVITY_NEVER;
    uint32_t now = (uint32_t)millis();
    for (int i = 0; i < NET_CH_COUNT; i++) {
        if (!s_ever[i]) continue;
        uint32_t age = now - s_last_ms[i];
        if (age < best) best = age;
    }
    return best;
}

uint32_t net_activity_age_mqtt_ms(void) {
    uint32_t best = NET_ACTIVITY_NEVER;
    uint32_t now = (uint32_t)millis();
    if (s_ever[NET_CH_MQTT_RX]) {
        uint32_t age = now - s_last_ms[NET_CH_MQTT_RX];
        if (age < best) best = age;
    }
    if (s_ever[NET_CH_MQTT_TX]) {
        uint32_t age = now - s_last_ms[NET_CH_MQTT_TX];
        if (age < best) best = age;
    }
    return best;
}

bool net_activity_is_active(net_channel_t ch, uint32_t window_ms) {
    uint32_t age = net_activity_age_ms(ch);
    return age != NET_ACTIVITY_NEVER && age <= window_ms;
}

int net_activity_channel_from_name(const char* name) {
    if (!name || !name[0]) return -1;
    static const struct {
        const char* n;
        int         id;
    } kMap[] = {
        {"portal",  NET_CH_PORTAL},
        {"mcp",     NET_CH_MCP},
        {"mqtt_rx", NET_CH_MQTT_RX},
        {"mqtt_tx", NET_CH_MQTT_TX},
        {"http",    NET_CH_HTTP},
        {"ble",     NET_CH_BLE},
        {"ota",     NET_CH_OTA},
        {"mqtt",    NET_CH_AGG_MQTT},  // aggregate: mqtt_rx or mqtt_tx
        {"any",     NET_CH_AGG_ANY},   // aggregate: any channel
    };
    for (size_t i = 0; i < sizeof(kMap) / sizeof(kMap[0]); i++) {
        if (strcmp(name, kMap[i].n) == 0) return kMap[i].id;
    }
    return -1;
}
