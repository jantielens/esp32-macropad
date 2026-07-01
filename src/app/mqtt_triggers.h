#pragma once

#include <stdint.h>
#include <stddef.h>
#include "board_config.h"

// ============================================================================
// MQTT-Triggered Actions
// ============================================================================
// A standalone, pad-independent action surface: when a matching MQTT message
// arrives, the trigger's action list is dispatched via action_list_dispatch().
// Users configure triggers (topic + optional exact-value filter + action chain)
// from the web portal. Config persists to LittleFS at /config/mqtt_triggers.json.
//
// Feature gating
// --------------
// The action system (action_list.h, action_dispatch.h) is compiled only when
// HAS_DISPLAY || HAS_BUTTON. e-paper boards set HAS_MQTT=true but have neither,
// so this feature must require BOTH MQTT and the action system. Call sites in
// mqtt_manager.cpp and app.ino guard each call with #if MQTT_TRIGGERS_ENABLED.
#define MQTT_TRIGGERS_ENABLED (HAS_MQTT && (HAS_DISPLAY || HAS_BUTTON))

#if MQTT_TRIGGERS_ENABLED

#include "pad_config.h"  // ButtonAction, MAX_BUTTON_ACTIONS

// Shared JSON document capacity for the trigger config (parse + serialize), used
// by both the storage layer and the portal component. Each trigger needs a
// topic, value, and up to MAX_BUTTON_ACTIONS actions; ~1 KB per trigger plus a
// fixed 2 KB envelope is comfortable headroom.
static constexpr size_t MQTT_TRIGGERS_JSON_CAP = (size_t)MAX_MQTT_TRIGGERS * 1024 + 2048;

// One configurable MQTT trigger. `topic` empty = unused slot. `value` empty =
// match any payload on the topic; otherwise exact string match on the raw
// payload. `actions` is dispatched via action_list_dispatch() on match.
struct MqttTriggerConfig {
    char topic[128];
    char value[64];
    ButtonAction actions[MAX_BUTTON_ACTIONS];
    uint8_t action_count;
};

// Allocate the trigger RAM cache (PSRAM preferred, SRAM fallback) and load the
// persisted config from LittleFS. Safe to call once at boot after storage mount.
void mqtt_triggers_init();

// Subscribe to every configured (non-empty) trigger topic. Called from
// MqttManager::onConnected() on every (re)connect so triggers survive reconnects.
void mqtt_triggers_on_connected();

// MQTT message callback (network task context). Keep fast: matches topic/value
// and sets a pending bit under a spinlock. No action dispatch here.
void mqtt_triggers_on_message(const char* topic, const uint8_t* payload, unsigned int length);

// Main-loop drain: snapshots/clears the pending bitmask under spinlock, then
// dispatches matched triggers' actions from a stack copy. Call from loop().
void mqtt_triggers_loop();

// Save raw JSON config to LittleFS, reload the RAM cache, publish storage usage.
// Returns true on success. Used by the portal component POST handler.
bool mqtt_triggers_save_raw(const uint8_t* json, size_t len);

// Read-only access to the cached trigger at `index` (bounds-checked). Returns
// nullptr if index >= MAX_MQTT_TRIGGERS or the cache failed to allocate. Used by
// the portal component GET handler.
const MqttTriggerConfig* mqtt_triggers_get(uint8_t index);

#endif  // MQTT_TRIGGERS_ENABLED
