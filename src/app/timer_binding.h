#pragma once

// Register the "timer" binding scheme with the binding template engine.
// Provides [timer:id] tokens for displaying timer values in seconds.
//
// Keys:
//   [timer:1]           — timer 1 value in seconds with decisecond precision
//   [timer:1_state]     — "running", "paused", or "stopped"
//   [timer:1_expired]   — "ON" or "OFF" (countdown only)
//   [timer:1_mode]      — "up" or "down"
//
// Call once during setup().
void timer_binding_init();
