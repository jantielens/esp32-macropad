#include "net_binding.h"
#include "board_config.h"

#if HAS_DISPLAY

#include "binding_template.h"
#include "net_activity.h"
#include "log_manager.h"

#include <string.h>
#include <stdio.h>

#define TAG "NetBind"

// Largest value the "age" sub-key ever reports. Also stands in for "never".
// ~16.6 minutes — well past any window a visual cue would care about.
#define NET_BINDING_AGE_MAX  999999u

struct NetBindingKeyDef {
    const char* name;
    int channel;
};

static const NetBindingKeyDef kNetBindingKeys[] = {
    {"portal", NET_CH_PORTAL}, {"mcp", NET_CH_MCP}, {"mqtt_rx", NET_CH_MQTT_RX},
    {"mqtt_tx", NET_CH_MQTT_TX}, {"mqtt", NET_CH_AGG_MQTT}, {"http", NET_CH_HTTP},
    {"ble", NET_CH_BLE}, {"ota", NET_CH_OTA}, {"any", NET_CH_AGG_ANY},
};

static const NetBindingKeyDef* find_net_binding_key(const char* name) {
    if (!name) return nullptr;
    for (const NetBindingKeyDef& key : kNetBindingKeys) {
        if (strcmp(name, key.name) == 0) return &key;
    }
    return nullptr;
}

// ============================================================================
// Parse params: "channel" or "channel;sub"
// ============================================================================

static void parse_net_params(const char* params,
                             char* chan, size_t chan_len,
                             char* sub, size_t sub_len) {
    chan[0] = '\0';
    sub[0] = '\0';
    if (!params || !params[0]) return;

    const char* sep = strchr(params, ';');
    if (!sep) {
        strlcpy(chan, params, chan_len);
        return;
    }
    size_t clen = (size_t)(sep - params);
    if (clen >= chan_len) clen = chan_len - 1;
    memcpy(chan, params, clen);
    chan[clen] = '\0';
    strlcpy(sub, sep + 1, sub_len);
}

// ============================================================================
// Scheme resolver — called by binding_template_resolve()
// ============================================================================

static BindingResolverStatus net_binding_resolve(const char* params, char* out, size_t out_len) {
    char chan[16];
    char sub[16];
    parse_net_params(params, chan, sizeof(chan), sub, sizeof(sub));

    if (!chan[0]) {
        strlcpy(out, "ERR:no channel", out_len);
        return BINDING_RESOLVER_UNKNOWN;
    }

    const NetBindingKeyDef* key = find_net_binding_key(chan);
    if (!key) {
        strlcpy(out, "ERR:bad channel", out_len);
        return BINDING_RESOLVER_UNKNOWN;
    }

    uint32_t age;
    if (key->channel == NET_CH_AGG_ANY) {
        age = net_activity_age_any_ms();
    } else if (key->channel == NET_CH_AGG_MQTT) {
        age = net_activity_age_mqtt_ms();
    } else {
        age = net_activity_age_ms((net_channel_t)key->channel);
    }

    if (strcmp(sub, "age") == 0) {
        uint32_t v = (age == NET_ACTIVITY_NEVER) ? NET_BINDING_AGE_MAX : age;
        if (v > NET_BINDING_AGE_MAX) v = NET_BINDING_AGE_MAX;
        snprintf(out, out_len, "%u", (unsigned)v);
        return BINDING_RESOLVER_RESOLVED;
    }

    // Default sub-key: "1" when active within the default window, else "0".
    bool active = (age != NET_ACTIVITY_NEVER && age <= NET_ACTIVITY_DEFAULT_WINDOW_MS);
    strlcpy(out, active ? "1" : "0", out_len);
    return BINDING_RESOLVER_RESOLVED;
}

// ============================================================================
// Topic collector — net activity is local, no MQTT subscriptions needed
// ============================================================================

static void net_binding_collect(const char* params, void* user_data) {
    (void)params;
    (void)user_data;
}

// ============================================================================
// Binding metadata
// ============================================================================

static uint8_t net_binding_key_count() {
    return sizeof(kNetBindingKeys) / sizeof(kNetBindingKeys[0]);
}

static const char* net_binding_key_at(uint8_t index) {
    return index < net_binding_key_count() ? kNetBindingKeys[index].name : nullptr;
}

// ============================================================================
// Init — register the "net" scheme
// ============================================================================

void net_binding_init() {
    if (!binding_template_register("net", net_binding_resolve, net_binding_collect,
                                   {1, 2, 1, -1, BINDING_VALIDATION_STANDARD, false,
                                    net_binding_key_count, net_binding_key_at})) {
        LOGE(TAG, "Failed to register net binding scheme");
    }
}

#else // !HAS_DISPLAY

void net_binding_init() {}

#endif
