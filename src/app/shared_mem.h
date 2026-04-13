#pragma once

// ============================================================================
// Shared Memory — RAM-only key-value store with binding scheme
// ============================================================================
// Compile-time gated by IS_DARKROOM_TIMER.
//
// Simple float store for passing values between metering phases:
//   Phase 1a writes Lref → Phase 2 reads it automatically.
//
// Binding scheme "mem":
//   [mem:key]          — formatted float value or "---" if not set
//   [mem:key;format]   — with printf format override (e.g. "%.0f")
//
// Action dispatch commands (via shared_mem_dispatch):
//   "set_<key>:<value>"  — e.g. "set_lref:1847.3"
//
// Thread safety: uses portMUX spinlock for cross-task access.

#include <stddef.h>

// Initialize shared memory and register the "mem" binding scheme.
void shared_mem_init();

// Dispatch a command string (e.g. "set_lref:1847.3").
void shared_mem_dispatch(const char* cmd);

// Read a value by key. Returns 0.0f if key not found.
// is_set is set to true if the key exists, false otherwise.
float shared_mem_get(const char* key, bool* is_set = nullptr);

// Write a value by key. Creates entry if it doesn't exist.
void shared_mem_set(const char* key, float value);
