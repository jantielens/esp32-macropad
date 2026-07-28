#include "data_stream.h"

#if HAS_DISPLAY && HAS_MQTT

#include "binding_template.h"
#include "ha_stats_resample.h"
#include "log_manager.h"
#include "pad_binding.h"
#include "pad_config.h"
#include "widgets/widget.h"
#if HAS_HA_HISTORY
#include "ha_stats.h"
#endif
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>
#include <sys/time.h>
#include <time.h>

#define TAG "DataStream"

// System clock is considered NTP-valid once it is past 2024-01-01.
#define CLOCK_VALID_EPOCH 1704067200L

// ============================================================================
// Internal stream entry
// ============================================================================

struct DataStream {
    bool     in_use;
    char     binding[CONFIG_LABEL_MAX_LEN];   // Full binding template string
    uint32_t window_secs;                      // Time window
    uint16_t slot_count;                       // Ring buffer size
    uint32_t uid;            // Monotonic identity (survives handle reuse, never 0)

    float*   samples;        // PSRAM ring buffer [slot_count]
    uint16_t head;           // Next write position
    uint16_t count;          // Valid entries (0..slot_count)
    uint32_t rev;            // Bumped on every ring mutation. History merges
                             // write behind the head without moving it, so
                             // head alone is not a sufficient change signal.
    float    auto_min;       // Min across buffer
    float    auto_max;       // Max across buffer
    float    last_value;     // Last ingested numeric value
    uint32_t last_slot_ms;   // millis() of last slot boundary (pre-NTP mode)
    bool     wallclock;      // true once bucketing is wall-clock aligned
    uint64_t newest_bucket;  // Bucket id of the newest slot (wall-clock mode)

#if HAS_HA_HISTORY
    char     ha_entity[DATA_STREAM_HA_ENTITY_MAX_LEN];  // "" = no history source
    uint8_t  ha_stat;                                    // HA_STAT_*
    bool     hydrate_done;   // History already merged (or nothing left to fill)
#endif
};

static DataStream g_streams[DATA_STREAM_MAX_STREAMS];
static bool g_initialized = false;
static uint32_t g_next_uid = 1;
#if HAS_HA_HISTORY
// Round-robin start index so one permanently failing stream cannot starve
// the others out of their turn.
static uint8_t g_hydrate_cursor = 0;
#endif

// ============================================================================
// Helpers
// ============================================================================

static uint32_t slot_duration_ms(const DataStream* s) {
    uint32_t d = (uint32_t)(s->window_secs * 1000ULL / (uint64_t)s->slot_count);
    return (d < 100) ? 100 : d;
}

static bool clock_is_valid() {
    return time(nullptr) >= CLOCK_VALID_EPOCH;
}

static uint64_t now_unix_ms() {
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000);
}

static uint64_t current_bucket(const DataStream* s) {
    return now_unix_ms() / (uint64_t)slot_duration_ms(s);
}

// Physical index of the newest written slot. Only valid when count > 0.
static uint16_t newest_index(const DataStream* s) {
    return (s->head == 0) ? (uint16_t)(s->slot_count - 1) : (uint16_t)(s->head - 1);
}

// Physical index of logical position `i`, where 0 is the oldest valid sample.
// History merging fills slots *behind* the live region, so the ring can wrap
// even while count < slot_count — the general form below handles both cases.
static uint16_t ring_index(const DataStream* s, uint16_t i) {
    return (uint16_t)((s->head + s->slot_count - s->count + i) % s->slot_count);
}

// Allocate a float ring buffer in PSRAM (fallback to regular heap)
static float* alloc_ring(uint16_t slot_count) {
    size_t sz = sizeof(float) * slot_count;
    float* buf = (float*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) buf = (float*)malloc(sz);
    return buf;
}

static void free_ring(float* buf) {
    if (buf) free(buf);
}

// Reset a stream's ring buffer to empty state
static void reset_ring(DataStream* s) {
    s->head = 0;
    s->count = 0;
    s->rev++;
    s->auto_min = INFINITY;
    s->auto_max = -INFINITY;
    s->last_value = NAN;
    s->last_slot_ms = millis();
    s->newest_bucket = 0;
}

// Recompute auto min/max from buffer contents so that expired outliers no
// longer stretch the range. Non-finite entries (history gaps) are ignored.
// O(slot_count) per call, but slot_count is small (typically 60-120).
static void recompute_auto_range(DataStream* s) {
    float a_min = INFINITY, a_max = -INFINITY;
    for (uint16_t i = 0; i < s->count; i++) {
        float v = s->samples[ring_index(s, i)];
        if (!isfinite(v)) continue;
        if (v < a_min) a_min = v;
        if (v > a_max) a_max = v;
    }
    s->auto_min = a_min;
    s->auto_max = a_max;
}

// Write `slots` ring positions using LOCF (Last Observation Carried Forward)
static void fill_locf(DataStream* s, uint32_t slots) {
    if (slots == 0) return;
    if (slots > s->slot_count) slots = s->slot_count;
    float fill = s->last_value;
    for (uint32_t i = 0; i < slots; i++) {
        s->samples[s->head] = fill;
        s->head = (uint16_t)((s->head + 1) % s->slot_count);
        if (s->count < s->slot_count) s->count++;
    }
    s->rev++;
}

// Advance the ring to the current time, filling skipped slots with LOCF.
static void advance_time(DataStream* s) {
    // Skip LOCF when no real sample has been ingested yet — avoid
    // injecting fake zeros that would corrupt auto-scale and display.
    if (isnan(s->last_value)) return;

    if (s->wallclock) {
        uint64_t bucket = current_bucket(s);
        if (bucket <= s->newest_bucket) return;
        uint64_t slots = bucket - s->newest_bucket;
        fill_locf(s, (slots > s->slot_count) ? s->slot_count : (uint32_t)slots);
        s->newest_bucket = bucket;
        return;
    }

    uint32_t slot_ms = slot_duration_ms(s);
    uint32_t elapsed = millis() - s->last_slot_ms;
    if (elapsed < slot_ms) return;

    uint32_t slots = elapsed / slot_ms;
    fill_locf(s, slots);
    s->last_slot_ms += slots * slot_ms;  // Grid-aligned
}

// Ingest a new numeric value into the stream
static void ingest_value(DataStream* s, float value) {
    if (s->count == 0) {
        // First ever sample — anchor the grid here.
        s->samples[s->head] = value;
        s->head = (uint16_t)((s->head + 1) % s->slot_count);
        s->count = 1;
        s->last_slot_ms = millis();
        if (s->wallclock) s->newest_bucket = current_bucket(s);
        s->rev++;
        s->last_value = value;
        recompute_auto_range(s);
        return;
    }

    // Carry the previous value across any skipped slots (this bumps rev itself
    // when it writes), then overwrite the newest slot with the value that
    // actually arrived.
    advance_time(s);
    s->last_value = value;

    // Polling resolves the same binding every LVGL cycle, so most calls land on
    // the slot that already holds this value. Only signal a change when the
    // stored sample really moves, otherwise every consumer redraws every frame.
    const uint16_t idx = newest_index(s);
    if (s->samples[idx] == value) return;

    s->samples[idx] = value;
    s->rev++;
    recompute_auto_range(s);
}

// ============================================================================
// Public API
// ============================================================================

#if HAS_HA_HISTORY
// Emit at most one deferral reason every HYDRATE_DEFER_LOG_MS. hydrate_streams()
// runs on every LVGL cycle, so an unthrottled log would flood the console.
#define HYDRATE_DEFER_LOG_MS 10000

static void log_hydrate_deferred(uint8_t stream, const char* reason) {
    static uint32_t last_ms = 0;
    const uint32_t now = millis();
    if (last_ms != 0 && (now - last_ms) < HYDRATE_DEFER_LOG_MS) return;
    last_ms = now;
    LOGD(TAG, "Stream[%d] hydration waiting: %s", stream, reason);
}

// Deliver any finished fetch, then queue the next stream that still has room
// for history. One request is in flight at a time, so streams take turns.
static void hydrate_streams() {
    if (ha_stats_deliver()) return;   // Give the merge its own poll cycle
    if (ha_stats_busy()) return;

    int8_t clock_pending = -1;   // First stream held back by an unsynced clock

    for (uint8_t n = 0; n < DATA_STREAM_MAX_STREAMS; n++) {
        const uint8_t i = (uint8_t)((g_hydrate_cursor + n) % DATA_STREAM_MAX_STREAMS);
        DataStream* s = &g_streams[i];
        if (!s->in_use || !s->samples) continue;
        if (s->hydrate_done || !s->ha_entity[0]) continue;
        if (!s->wallclock) {                            // Grid not aligned yet
            if (clock_pending < 0) clock_pending = (int8_t)i;
            continue;
        }
        if (s->count >= s->slot_count) {                // Live data covers it all
            s->hydrate_done = true;
            continue;
        }

        // Recorder only publishes on 5-minute boundaries, so a finer grid would
        // leave most slots empty — not worth a request.
        const uint32_t slot_ms = slot_duration_ms(s);
        if (slot_ms < HA_HISTORY_MIN_SLOT_SECS * 1000UL) {
            LOGD(TAG, "Stream[%d] slot %lums < recorder period — no hydration",
                 i, (unsigned long)slot_ms);
            s->hydrate_done = true;
            continue;
        }

        if (ha_stats_request((data_stream_handle_t)i, s->uid, s->ha_entity,
                             s->ha_stat, slot_ms, s->slot_count, current_bucket(s))) {
            g_hydrate_cursor = (uint8_t)((i + 1) % DATA_STREAM_MAX_STREAMS);
        } else {
            const uint32_t backoff = ha_stats_backoff_remaining_ms();
            if (backoff > 0) {
                char reason[48];
                snprintf(reason, sizeof(reason), "retry backoff, %lus left",
                         (unsigned long)(backoff / 1000));
                log_hydrate_deferred(i, reason);
            } else {
                log_hydrate_deferred(i, "WiFi or HA config not ready");
            }
        }
        return;
    }

    if (clock_pending >= 0) log_hydrate_deferred((uint8_t)clock_pending, "clock not synced");
}
#endif // HAS_HA_HISTORY

void data_stream_init() {
    memset(g_streams, 0, sizeof(g_streams));
    for (int i = 0; i < DATA_STREAM_MAX_STREAMS; i++) {
        g_streams[i].in_use = false;
        g_streams[i].samples = nullptr;
    }
    g_initialized = true;
#if HAS_HA_HISTORY
    ha_stats_init();
#endif
    LOGI(TAG, "Initialized (max %d streams)", DATA_STREAM_MAX_STREAMS);
}

void data_stream_rebuild() {
    if (!g_initialized) return;

    // Mark all streams for potential removal
    bool keep[DATA_STREAM_MAX_STREAMS] = {};

    // Temp config buffer — PSRAM preferred
    PadConfig* cfg = (PadConfig*)heap_caps_malloc(
        sizeof(PadConfig), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!cfg) cfg = (PadConfig*)malloc(sizeof(PadConfig));
    if (!cfg) {
        LOGE(TAG, "OOM for pad config in rebuild");
        return;
    }

    // Scan all pads for widgets that need data streams
    for (uint8_t page = 0; page < MAX_PADS; page++) {
        if (!pad_config_load(page, cfg)) continue;
        for (uint8_t b = 0; b < cfg->button_count; b++) {
            const ScreenButtonConfig& btn = cfg->buttons[b];
            if (!btn.widget.type[0]) continue;

            // Only widgets with getStreamParams need data streams
            const WidgetType* wt = widget_find(btn.widget.type);
            if (!wt || !wt->getStreamParams) continue;

            // Iterate over stream indices (0 = primary, 1/2 = extra lines)
            for (uint8_t si = 0; si < 3; si++) {
                uint32_t window_secs = 300;
                uint16_t slot_count = 60;
                const char* binding = nullptr;
                const char* ha_entity = nullptr;
                uint8_t ha_stat = 0;
                if (!wt->getStreamParams(&btn.widget, si,
                                         &window_secs, &slot_count, &binding,
                                         &ha_entity, &ha_stat))
                    break;  // No more streams for this widget
                if (!binding || !binding[0]) continue;

                // Expand [pad:] tokens so streams store the underlying template
                char expanded[BINDING_TEMPLATE_MAX_LEN];
                if (pad_binding_expand(cfg, binding, expanded, sizeof(expanded))) {
                    binding = expanded;
                }

                // Check if an existing stream matches
                data_stream_handle_t existing =
                    data_stream_find(binding, window_secs, slot_count, ha_entity, ha_stat);
                if (existing != DATA_STREAM_INVALID) {
                    keep[existing] = true;
                    continue;
                }

                // Allocate a new stream
                int slot = -1;
                for (int i = 0; i < DATA_STREAM_MAX_STREAMS; i++) {
                    if (!g_streams[i].in_use) { slot = i; break; }
                }
                if (slot < 0) {
                    LOGW(TAG, "Max streams reached, skipping: %s", binding);
                    continue;
                }

                DataStream* s = &g_streams[slot];
                s->in_use = true;
                s->uid = g_next_uid++;
                strlcpy(s->binding, binding, sizeof(s->binding));
                s->window_secs = window_secs;
                s->slot_count = slot_count;
                s->wallclock = clock_is_valid();
#if HAS_HA_HISTORY
                strlcpy(s->ha_entity, ha_entity ? ha_entity : "", sizeof(s->ha_entity));
                s->ha_stat = ha_stat;
                s->hydrate_done = false;
#endif

                // Allocate ring buffer
                if (s->samples) free_ring(s->samples);
                s->samples = alloc_ring(slot_count);
                if (!s->samples) {
                    LOGE(TAG, "OOM for ring buffer (%d slots)", slot_count);
                    s->in_use = false;
                    continue;
                }
                reset_ring(s);
                keep[slot] = true;

#if HAS_HA_HISTORY
                LOGI(TAG, "Stream[%d]: %s (window=%lus slots=%d ha=%s clock=%s)", slot,
                     binding, (unsigned long)window_secs, slot_count,
                     s->ha_entity[0] ? s->ha_entity : "-",
                     s->wallclock ? "synced" : "pending");
#else
                LOGI(TAG, "Stream[%d]: %s (window=%lus slots=%d)", slot, binding,
                     (unsigned long)window_secs, slot_count);
#endif
            }
        }
    }

    free(cfg);

    // Free streams that are no longer needed
    for (int i = 0; i < DATA_STREAM_MAX_STREAMS; i++) {
        if (g_streams[i].in_use && !keep[i]) {
            LOGI(TAG, "Stream[%d] removed: %s", i, g_streams[i].binding);
            free_ring(g_streams[i].samples);
            g_streams[i].samples = nullptr;
            g_streams[i].in_use = false;
        }
    }
}

void data_stream_poll() {
    if (!g_initialized) return;

    char resolved[BINDING_TEMPLATE_MAX_LEN];

    for (int i = 0; i < DATA_STREAM_MAX_STREAMS; i++) {
        DataStream* s = &g_streams[i];
        if (!s->in_use || !s->samples) continue;

        // First valid system clock: boot-relative samples cannot be aligned to
        // wall-clock buckets retroactively, so drop them and restart on the
        // wall-clock grid. Bumping the uid invalidates any in-flight hydration.
        if (!s->wallclock && clock_is_valid()) {
            s->wallclock = true;
            s->uid = g_next_uid++;
            reset_ring(s);
#if HAS_HA_HISTORY
            s->hydrate_done = false;  // Only now can history be aligned
#endif
            LOGD(TAG, "Stream[%d] switched to wall-clock buckets", i);
        }

        // Always advance time (LOCF for gaps)
        if (s->count > 0) {
            advance_time(s);
        }

        // Resolve binding
        bool ok = binding_template_resolve(s->binding, resolved, sizeof(resolved));
        if (!ok) continue;

        // Skip error/placeholder outputs from binding resolution
        if (resolved[0] == '\0') continue;
        if (strncmp(resolved, "ERR:", 4) == 0) continue;
        if (strcmp(resolved, "---") == 0) continue;

        // Parse numeric value
        char* end = nullptr;
        float value = strtof(resolved, &end);
        if (end == resolved) continue;  // Not a number — skip silently

        ingest_value(s, value);
    }

#if HAS_HA_HISTORY
    hydrate_streams();
#endif
}

data_stream_handle_t data_stream_find(const char* binding,
                                      uint32_t window_secs,
                                      uint16_t slot_count,
                                      const char* ha_entity,
                                      uint8_t ha_stat) {
    if (!binding || !binding[0]) return DATA_STREAM_INVALID;
#if !HAS_HA_HISTORY
    (void)ha_entity;
    (void)ha_stat;
#endif
    for (int i = 0; i < DATA_STREAM_MAX_STREAMS; i++) {
        if (!g_streams[i].in_use) continue;
        if (strcmp(g_streams[i].binding, binding) != 0) continue;
        if (g_streams[i].window_secs != window_secs) continue;
        if (g_streams[i].slot_count != slot_count) continue;
#if HAS_HA_HISTORY
        // Two lines can share a live binding but hydrate from different HA
        // entities, so the history source is part of the stream identity.
        if (strcmp(g_streams[i].ha_entity, ha_entity ? ha_entity : "") != 0) continue;
        if (g_streams[i].ha_stat != ha_stat) continue;
#endif
        return (data_stream_handle_t)i;
    }
    return DATA_STREAM_INVALID;
}

bool data_stream_get(data_stream_handle_t handle, DataStreamSnapshot* out) {
    if (handle < 0 || handle >= DATA_STREAM_MAX_STREAMS) return false;
    const DataStream* s = &g_streams[handle];
    if (!s->in_use || !s->samples) return false;

    out->samples = s->samples;
    out->slot_count = s->slot_count;
    out->head = s->head;
    out->count = s->count;
    out->rev = s->rev;
    out->auto_min = s->auto_min;
    out->auto_max = s->auto_max;
    out->last_value = s->last_value;
    return true;
}

#if HAS_HA_HISTORY

uint32_t data_stream_uid(data_stream_handle_t handle) {
    if (handle < 0 || handle >= DATA_STREAM_MAX_STREAMS) return 0;
    const DataStream* s = &g_streams[handle];
    return s->in_use ? s->uid : 0;
}

bool data_stream_apply_history(data_stream_handle_t handle, uint32_t uid,
                               uint64_t end_bucket, const float* values,
                               uint16_t count) {
    if (handle < 0 || handle >= DATA_STREAM_MAX_STREAMS) return false;
    DataStream* s = &g_streams[handle];
    if (!s->in_use || !s->samples) return false;
    if (s->uid != uid) return false;          // Stream was rebuilt or re-anchored
    if (!s->wallclock) return false;          // Boot-relative ring cannot be aligned
    if (!values || count == 0) return false;

    // Bring the ring up to "now" first so the newest slot maps to the current
    // bucket — history is positioned relative to that anchor.
    if (s->count > 0) advance_time(s);
    else s->newest_bucket = current_bucket(s);

    const uint16_t before = s->count;
    s->count = (uint16_t)ha_stats_merge(s->samples, s->slot_count, s->head,
                                        s->count, s->newest_bucket,
                                        values, count, end_bucket);
    s->hydrate_done = true;                   // Response consumed, don't re-ask
    if (s->count == before) return true;      // Nothing usable in the response

    s->rev++;                                 // Slots behind head changed
    recompute_auto_range(s);
    LOGD(TAG, "Stream[%d] hydrated %u slots from history", handle,
         (unsigned)(s->count - before));
    return true;
}

#endif // HAS_HA_HISTORY

#endif // HAS_DISPLAY && HAS_MQTT
