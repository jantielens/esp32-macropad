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
//
// Bucketing: until the system clock is NTP-valid a stream advances on a
// boot-relative millis() grid. On the first valid clock the ring is reset and
// the stream switches to wall-clock buckets (floor(unix_ms / slot_ms)), which
// is what makes Home Assistant history merging possible.

// Opaque handle to a data stream (-1 = invalid)
typedef int8_t data_stream_handle_t;
#define DATA_STREAM_INVALID ((data_stream_handle_t)-1)

// DATA_STREAM_MAX_STREAMS is defined in board_config.h (overridable per board, default 64)

#if HAS_HA_HISTORY
// Maximum length of a Home Assistant entity ID used as a history source.
#define DATA_STREAM_HA_ENTITY_MAX_LEN 64

// Recorder statistic selector (measurement and total sensors differ in semantics)
#define HA_STAT_MEAN   0
#define HA_STAT_STATE  1
#define HA_STAT_SUM    2
#define HA_STAT_COUNT  3
#endif

// Read-only snapshot of a stream's ring buffer state.
// Returned by data_stream_get(). Pointers are valid until next
// data_stream_poll() or data_stream_rebuild() call.
struct DataStreamSnapshot {
    const float* samples;    // Ring buffer (slot_count entries)
    uint16_t slot_count;     // Total slots in ring buffer
    uint16_t head;           // Next write position
    uint16_t count;          // Valid sample count (0..slot_count)
    uint32_t rev;            // Bumped on every mutation (change detection)
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
// `ha_entity` / `ha_stat` are part of the identity so that two lines sharing a
// live binding but hydrating from different Home Assistant entities stay
// separate. Pass nullptr / 0 when the caller has no history source.
// Returns DATA_STREAM_INVALID if not found.
data_stream_handle_t data_stream_find(const char* binding,
                                      uint32_t window_secs,
                                      uint16_t slot_count,
                                      const char* ha_entity,
                                      uint8_t ha_stat);

// Get a read-only snapshot of a stream's state.
// Returns false if handle is invalid or stream has no data.
bool data_stream_get(data_stream_handle_t handle, DataStreamSnapshot* out);

#if HAS_HA_HISTORY
// Monotonic identity for a stream. Handles are array indices and are reused
// after a rebuild, so an in-flight hydration request must carry the uid and
// have it re-checked before its result is applied. Returns 0 for an invalid
// handle (0 is never a valid uid).
uint32_t data_stream_uid(data_stream_handle_t handle);

// Merge Recorder history into a stream's ring buffer.
//
// values[] holds `count` consecutive buckets, oldest first, where the last
// entry corresponds to `end_bucket`. Non-finite entries mean "no statistic for
// that period" and are left absent.
//
// Live data always wins: only ring slots not yet covered by locally collected
// samples are filled. Returns false (and changes nothing) when the handle, uid,
// or slot configuration no longer matches the request.
bool data_stream_apply_history(data_stream_handle_t handle, uint32_t uid,
                               uint64_t end_bucket, const float* values,
                               uint16_t count);

// Mark a stream's hydration complete without merging values. Used when
// Recorder successfully reports no usable statistics for the requested source.
bool data_stream_finish_history(data_stream_handle_t handle, uint32_t uid);
#endif // HAS_HA_HISTORY

#endif // HAS_DISPLAY && HAS_MQTT
