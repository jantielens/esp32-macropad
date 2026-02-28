#pragma once

#include "board_config.h"

#if HAS_MQTT

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// MQTT Subscription Store
// ============================================================================
// Thread-safe store for MQTT subscription payloads. The MQTT callback (network
// thread) writes payloads via store_set(). The LVGL task reads via store_get().
// A per-entry dirty flag lets the reader detect changes.
//
// Lifecycle:
//   1. mqtt_sub_store_init()          — allocate store (call once at boot)
//   2. mqtt_sub_store_subscribe_all() — scan all pad configs, subscribe unique topics
//   3. mqtt_sub_store_set()           — called from MQTT message callback
//   4. mqtt_sub_store_get()           — called from PadScreen::update()
//   5. mqtt_sub_store_resubscribe()   — called on MQTT reconnect

#define MQTT_SUB_STORE_MAX_ENTRIES   64
#define MQTT_SUB_STORE_MAX_VALUE_LEN 256

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the store (allocates memory). Call once during boot.
void mqtt_sub_store_init();

// Scan all pad page configs and subscribe to every unique MQTT topic found
// in label bindings. Call after MQTT connects and after config saves.
void mqtt_sub_store_subscribe_all();

// Re-subscribe all tracked topics (call on MQTT reconnect).
void mqtt_sub_store_resubscribe();

// Store an incoming MQTT payload for a topic. Thread-safe.
// Called from the PubSubClient message callback.
void mqtt_sub_store_set(const char* topic, const uint8_t* payload, unsigned int len);

// Retrieve the latest payload for a topic. Thread-safe.
// Returns true if the topic exists in the store. Sets *changed to true if the
// value was updated since the last get() call for this topic.
// buf is filled with the null-terminated payload (truncated to buf_len-1).
bool mqtt_sub_store_get(const char* topic, char* buf, size_t buf_len, bool* changed);

// Extract a value from a JSON payload using a dot-separated path.
// path="." returns the raw value. path="temp" returns doc["temp"].
// path="a.b" returns doc["a"]["b"]. Result written to out (null-terminated).
// Returns true on success.
bool mqtt_sub_store_extract_json(const char* payload, const char* path, char* out, size_t out_len);

// Apply a printf format string to a raw value. Best-effort: if the format
// string is invalid or the value can't be parsed, the raw value is copied.
// Result written to out (null-terminated).
void mqtt_sub_store_format_value(const char* raw_value, const char* format, char* out, size_t out_len);

#ifdef __cplusplus
}
#endif

#endif // HAS_MQTT
