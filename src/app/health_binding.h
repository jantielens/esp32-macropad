#pragma once

#include <stdint.h>

// Register the "health" binding scheme with the binding template engine.
// Provides [health:key] and [health:key;format] tokens for local device
// telemetry (CPU, memory, RSSI, uptime, IP, hostname).  Call once during setup().
void health_binding_init();

// Enumerate the supported [health:key] keys (for the MCP capability manifest).
// Returns 0 / nullptr on builds without the health resolver.
uint8_t health_binding_key_count();
const char* health_binding_key_at(uint8_t index);
const char* health_binding_key_desc_at(uint8_t index);
