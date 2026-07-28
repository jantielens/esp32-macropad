#pragma once

#include "board_config.h"

#if HAS_HA_HISTORY

#include "data_stream.h"

#include <stdint.h>

// ============================================================================
// Home Assistant history hydration
// ============================================================================
// Backfills a sparkline data stream from Home Assistant's Recorder statistics
// so a configured window is populated after a reboot instead of starting empty.
//
// Only one request is in flight at a time: the HTTP round trip runs on a
// dedicated PSRAM-stacked task so it never blocks the LVGL task, and the result
// is handed back for the LVGL task to merge. data_stream.cpp drives both ends
// and queues its streams round-robin.

// Create the fetch task. Safe to call when the feature is unused — the task
// stays blocked until the first request arrives.
void ha_stats_init();

// Queue a hydration request. Returns false when a request is already in flight,
// when the caller's arguments are unusable, or when HA is not configured.
//
// `end_bucket` is the bucket id (floor(unix_ms / slot_ms)) of the newest slot
// the response should cover; `slot_count` buckets ending there are requested.
// The stream's `uid` is carried through so a stale response can be dropped.
bool ha_stats_request(data_stream_handle_t handle, uint32_t uid,
                      const char* entity_id, uint8_t statistic,
                      uint32_t slot_ms, uint16_t slot_count,
                      uint64_t end_bucket);

// True while a request is queued or being fetched.
bool ha_stats_busy();

// Milliseconds left on the global retry backoff that a failed fetch installs.
// 0 when requests are accepted right away. Diagnostics only.
uint32_t ha_stats_backoff_remaining_ms();

// Apply a completed fetch to its data stream. Must be called from the LVGL
// task. Returns true when a result was consumed (successfully applied or
// discarded as stale).
bool ha_stats_deliver();

#endif // HAS_HA_HISTORY
