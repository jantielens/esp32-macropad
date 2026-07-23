#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ============================================================================
// Network Activity Tracker
// ============================================================================
// Lock-free record of the "last activity" timestamp per network transport
// channel. Producers call net_activity_mark() from any task (web / AsyncTCP,
// MQTT network task, BLE host, OTA writer). Consumers read age / active state
// — primarily the LVGL task via the [net:...] binding scheme, which lets users
// bind icon colors, labels, or widget inputs to live network activity.
//
// Thread safety: each channel is a single 32-bit aligned scalar plus a byte
// flag. Scalar reads and writes are atomic on ESP32 (Xtensa + RISC-V), so no
// mutex or spinlock is required. mark() is safe from any task context and
// costs a couple of stores — cheap enough for network hot paths.

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NET_CH_PORTAL = 0,   // Inbound web portal HTTP requests
    NET_CH_MCP,          // Inbound MCP (Model Context Protocol) JSON-RPC
    NET_CH_MQTT_RX,      // Inbound MQTT messages
    NET_CH_MQTT_TX,      // Outbound MQTT publishes
    NET_CH_HTTP,         // Outbound HTTP client (image fetch, Home Assistant REST)
    NET_CH_BLE,          // BLE HID reports
    NET_CH_OTA,          // Firmware OTA flash writes
    NET_CH_COUNT
} net_channel_t;

// Sentinel returned by age functions when a channel has never seen activity.
#define NET_ACTIVITY_NEVER  0xFFFFFFFFu

// Synthetic aggregate ids returned by net_activity_channel_from_name() for
// names that are not a single physical channel. They live just past the real
// channel range so callers can distinguish them from a NET_CH_* value.
#define NET_CH_AGG_ANY   (NET_CH_COUNT)       // "any"  — any channel
#define NET_CH_AGG_MQTT  (NET_CH_COUNT + 1)   // "mqtt" — mqtt_rx or mqtt_tx

// Default "active" window used by the plain [net:channel] binding (ms).
#define NET_ACTIVITY_DEFAULT_WINDOW_MS  400u

// Record activity on a channel. Safe from any task. Cheap (two stores).
void net_activity_mark(net_channel_t ch);

// Milliseconds since the last activity on a channel, or NET_ACTIVITY_NEVER.
uint32_t net_activity_age_ms(net_channel_t ch);

// Milliseconds since the most recent activity on ANY channel, or
// NET_ACTIVITY_NEVER when nothing has happened yet.
uint32_t net_activity_age_any_ms(void);

// Milliseconds since the most recent inbound OR outbound MQTT activity
// (min of mqtt_rx / mqtt_tx), or NET_ACTIVITY_NEVER when neither has fired.
uint32_t net_activity_age_mqtt_ms(void);

// True when the channel saw activity within the last window_ms.
bool net_activity_is_active(net_channel_t ch, uint32_t window_ms);

// Resolve a channel name to an id. Recognizes the channel names
// ("portal", "mcp", "mqtt_rx", "mqtt_tx", "http", "ble", "ota") and the
// aggregates "any" (NET_CH_AGG_ANY) and "mqtt" (NET_CH_AGG_MQTT, rx+tx).
// Returns the channel/aggregate id, or -1 for an unknown name. Exposed for the
// binding scheme and unit tests.
int net_activity_channel_from_name(const char* name);

#ifdef __cplusplus
}
#endif
