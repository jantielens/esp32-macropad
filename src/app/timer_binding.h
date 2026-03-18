#pragma once

// Register the "timer" binding scheme with the binding template engine.
// Provides [timer:id] and [timer:id;format] tokens for displaying timer values.
//
// Keys:
//   [timer:1]           — timer 1 value in default mm:ss format
//   [timer:2;hh:mm:ss]  — timer 2 value in hh:mm:ss format
//   [timer:1;mm:ss.d]   — timer 1 with decisecond precision
//   [timer:1;ss]        — timer 1 as total seconds
//   [timer:1_state]     — "running", "paused", or "stopped"
//   [timer:1_expired]   — "ON" or "OFF" (countdown only)
//   [timer:1_mode]      — "up" or "down"
//
// Call once during setup().
void timer_binding_init();
