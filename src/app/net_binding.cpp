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

static bool net_binding_resolve(const char* params, char* out, size_t out_len) {
    char chan[16];
    char sub[16];
    parse_net_params(params, chan, sizeof(chan), sub, sizeof(sub));

    if (!chan[0]) {
        strlcpy(out, "ERR:no channel", out_len);
        return false;
    }

    int id = net_activity_channel_from_name(chan);
    if (id < 0) {
        strlcpy(out, "ERR:bad channel", out_len);
        return false;
    }

    uint32_t age;
    if (id == NET_CH_AGG_ANY) {
        age = net_activity_age_any_ms();
    } else if (id == NET_CH_AGG_MQTT) {
        age = net_activity_age_mqtt_ms();
    } else {
        age = net_activity_age_ms((net_channel_t)id);
    }

    if (strcmp(sub, "age") == 0) {
        uint32_t v = (age == NET_ACTIVITY_NEVER) ? NET_BINDING_AGE_MAX : age;
        if (v > NET_BINDING_AGE_MAX) v = NET_BINDING_AGE_MAX;
        snprintf(out, out_len, "%u", (unsigned)v);
        return true;
    }

    // Default sub-key: "1" when active within the default window, else "0".
    bool active = (age != NET_ACTIVITY_NEVER && age <= NET_ACTIVITY_DEFAULT_WINDOW_MS);
    strlcpy(out, active ? "1" : "0", out_len);
    return true;
}

// ============================================================================
// Topic collector — net activity is local, no MQTT subscriptions needed
// ============================================================================

static void net_binding_collect(const char* params, void* user_data) {
    (void)params;
    (void)user_data;
}

// ============================================================================
// MCP capability manifest — self-description + authoring validation
// ============================================================================

#if HAS_MCP
#include <ArduinoJson.h>

static void net_scheme_describe(void* out) {
    JsonObject& o = *static_cast<JsonObject*>(out);
    o["syntax"]   = "[net:channel] or [net:channel;age]";
    o["example"]  = "[expr:[net:any]?\"#22c55e\":\"#334155\"]";
    o["channels"] = "portal, mcp, mqtt_rx, mqtt_tx, mqtt (rx+tx), http, ble, ota, any";
    o["note"]     = "Live network activity. Bare form -> \"1\" when the channel was active in the last ~400ms else \"0\"; ;age -> ms since last activity (capped 999999). Ideal for subtle activity cues, e.g. an icon color that flashes on traffic.";
}

// Validate a [net:CHANNEL] token's params (channel only; ;age / |fallback are
// stripped by the caller). Keeps the channel set as the single source of truth
// for both the manifest and authoring validation.
static char s_net_verr[96];
static const char* net_scheme_validate(const char* params) {
    if (!params || !params[0]) return nullptr;
    if (net_activity_channel_from_name(params) >= 0) return nullptr;
    snprintf(s_net_verr, sizeof(s_net_verr),
             "unknown net channel '%s' — use portal|mcp|mqtt_rx|mqtt_tx|mqtt|http|ble|ota|any", params);
    return s_net_verr;
}
#endif // HAS_MCP

// ============================================================================
// Init — register the "net" scheme
// ============================================================================

void net_binding_init() {
    if (!binding_template_register("net", net_binding_resolve, net_binding_collect)) {
        LOGE(TAG, "Failed to register net binding scheme");
    }
#if HAS_MCP
    binding_template_set_scheme_describe("net", net_scheme_describe);
    binding_template_set_scheme_validate("net", net_scheme_validate);
#endif
}

#else // !HAS_DISPLAY

void net_binding_init() {}

#endif
