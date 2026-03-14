#pragma once

#include "board_config.h"

#if HAS_DISPLAY && HAS_MQTT

#include <stdint.h>
#include <stddef.h>
#include <math.h>

// ============================================================================
// Data Stream Registry
// ============================================================================
// Background data collection for history-based widgets (sparkline, etc.).
// Keeps per-widget ring buffers that are populated every poll cycle
// regardless of which screen is active.
//
// Thread safety: All functions must be called from the LVGL task only
// (same context as binding_template_resolve()).
//
// Usage:
//   1. data_stream_init()                   — once at startup
//   2. data_stream_rebuild()                — after pad config changes
//   3. data_stream_poll()                   — every LVGL cycle
//   4. data_stream_get() in widget create/update/tick

// Opaque handle to a data stream (-1 = invalid)
typedef int8_t data_stream_handle_t;
#define DATA_STREAM_INVALID ((data_stream_handle_t)-1)

// DATA_STREAM_MAX_STREAMS is defined in board_config.h (overridable per board, default 64)

// Read-only snapshot of a stream's ring buffer state.
// Returned by data_stream_get(). Pointers are valid until next
// data_stream_poll() or data_stream_rebuild() call.
struct DataStreamSnapshot {
    const float* samples;    // Ring buffer (slot_count entries)
    uint8_t  slot_count;     // Total slots in ring buffer
    uint8_t  head;           // Next write position
    uint8_t  count;          // Valid sample count (0..slot_count)
    float    auto_min;       // Tracked minimum across buffer
    float    auto_max;       // Tracked maximum across buffer
    float    last_value;     // Most recent numeric value (NAN if none)
};

// Initialize the registry. Call once during startup.
void data_stream_init();

// Rebuild streams from current pad configuration.
// Scans all pads for sparkline widgets and creates/removes streams
// to match. Preserves existing stream data when binding+config unchanged.
// Call when pad_config_get_generation() changes.
void data_stream_rebuild();

// Poll all registered streams: resolve bindings and feed ring buffers.
// Call once per LVGL cycle from the display manager task.
void data_stream_poll();

// Look up a stream handle by binding string + config.
// Returns DATA_STREAM_INVALID if not found.
data_stream_handle_t data_stream_find(const char* binding,
                                      uint16_t window_secs,
                                      uint8_t slot_count);

// Get a read-only snapshot of a stream's state.
// Returns false if handle is invalid or stream has no data.
bool data_stream_get(data_stream_handle_t handle, DataStreamSnapshot* out);

#endif // HAS_DISPLAY && HAS_MQTT
