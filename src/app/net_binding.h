#pragma once

// Register the "net" binding scheme with the binding template engine.
// Provides [net:channel] and [net:channel;age] tokens that expose live
// network / transport activity so labels, icon colors, and widget inputs can
// react to it. Call once during setup(), after binding_template init.
//
// Channels: portal, mcp, mqtt_rx, mqtt_tx, http, ble, ota, any
//
//   [net:portal]        -> "1" while a portal request was seen in the last
//                          ~400 ms, else "0"
//   [net:any]           -> "1" when any transport was active recently
//   [net:mqtt_rx;age]   -> milliseconds since the last inbound MQTT message
//                          (capped at 999999; 999999 also means "never")
//
// Thread safety: resolve runs on the LVGL task only, like every other scheme.
void net_binding_init();
