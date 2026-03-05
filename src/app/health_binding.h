#pragma once

// Register the "health" binding scheme with the binding template engine.
// Provides [health:key] and [health:key;format] tokens for local device
// telemetry (CPU, memory, RSSI, uptime, IP, hostname).  Call once during setup().
void health_binding_init();
