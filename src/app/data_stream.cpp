#include "data_stream.h"

#if HAS_DISPLAY && HAS_MQTT

#include "binding_template.h"
#include "log_manager.h"
#include "pad_binding.h"
#include "pad_config.h"
#include "widgets/widget.h"
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>

#define TAG "DataStream"

// ============================================================================
// Internal stream entry
// ============================================================================

struct DataStream {
    bool     in_use;
    char     binding[CONFIG_LABEL_MAX_LEN];   // Full binding template string
    uint16_t window_secs;                      // Time window
    uint8_t  slot_count;                       // Ring buffer size

    float*   samples;        // PSRAM ring buffer [slot_count]
    uint8_t  head;           // Next write position
    uint8_t  count;          // Valid entries (0..slot_count)
    float    auto_min;       // Min across buffer
    float    auto_max;       // Max across buffer
    float    last_value;     // Last ingested numeric value
    uint32_t last_slot_ms;   // millis() of last slot boundary
};

static DataStream g_streams[DATA_STREAM_MAX_STREAMS];
static bool g_initialized = false;

// ============================================================================
// Helpers
// ============================================================================

static uint32_t slot_duration_ms(const DataStream* s) {
    uint32_t d = (uint32_t)s->window_secs * 1000UL / (uint32_t)s->slot_count;
    return (d < 100) ? 100 : d;
}

// Allocate a float ring buffer in PSRAM (fallback to regular heap)
static float* alloc_ring(uint8_t slot_count) {
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
    s->auto_min = INFINITY;
    s->auto_max = -INFINITY;
    s->last_value = NAN;
    s->last_slot_ms = millis();
}

// Advance time slots using LOCF (Last Observation Carried Forward)
static void advance_time(DataStream* s) {
    uint32_t now = millis();
    uint32_t slot_ms = slot_duration_ms(s);
    uint32_t elapsed = now - s->last_slot_ms;
    if (elapsed < slot_ms) return;

    uint32_t slots = elapsed / slot_ms;
    if (slots > s->slot_count) slots = s->slot_count;

    // Skip LOCF when no real sample has been ingested yet — avoid
    // injecting fake zeros that would corrupt auto-scale and display.
    if (isnan(s->last_value)) return;

    float fill = s->last_value;
    for (uint32_t i = 0; i < slots; i++) {
        s->samples[s->head] = fill;
        s->head = (uint8_t)((s->head + 1) % s->slot_count);
        if (s->count < s->slot_count) s->count++;
    }
    s->last_slot_ms += slots * slot_ms;  // Grid-aligned
}

// Ingest a new numeric value into the stream
static void ingest_value(DataStream* s, float value) {
    uint32_t slot_ms = slot_duration_ms(s);
    uint32_t elapsed = millis() - s->last_slot_ms;

    if (elapsed >= slot_ms) {
        // Advance past slots with LOCF, then overwrite newest with actual value
        float fill = isnan(s->last_value) ? value : s->last_value;
        advance_time(s);  // This advances and fills with LOCF
        // Overwrite the most-recently-written slot with actual new value
        uint8_t newest = (s->head == 0) ? (uint8_t)(s->slot_count - 1) : (uint8_t)(s->head - 1);
        s->samples[newest] = value;
    } else {
        // Still within the same slot — overwrite with latest
        if (s->count > 0) {
            uint8_t cur = (s->head == 0) ? (uint8_t)(s->slot_count - 1) : (uint8_t)(s->head - 1);
            s->samples[cur] = value;
        } else {
            // First ever sample
            s->samples[s->head] = value;
            s->head = (uint8_t)((s->head + 1) % s->slot_count);
            s->count = 1;
            s->last_slot_ms = millis();
        }
    }

    s->last_value = value;

    // Recompute auto min/max from buffer contents so that expired
    // outliers no longer stretch the range.  O(slot_count) per call
    // but slot_count is small (typically 60-120) so cost is negligible.
    float a_min = INFINITY, a_max = -INFINITY;
    for (uint8_t i = 0; i < s->count; i++) {
        uint8_t idx = (s->count < s->slot_count)
            ? i
            : (uint8_t)((s->head + i) % s->slot_count);
        float v = s->samples[idx];
        if (v < a_min) a_min = v;
        if (v > a_max) a_max = v;
    }
    s->auto_min = a_min;
    s->auto_max = a_max;
}

// ============================================================================
// Public API
// ============================================================================

void data_stream_init() {
    memset(g_streams, 0, sizeof(g_streams));
    for (int i = 0; i < DATA_STREAM_MAX_STREAMS; i++) {
        g_streams[i].in_use = false;
        g_streams[i].samples = nullptr;
    }
    g_initialized = true;
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
                uint16_t window_secs = 300;
                uint8_t slot_count = 60;
                const char* binding = nullptr;
                if (!wt->getStreamParams(&btn.widget, si,
                                         &window_secs, &slot_count, &binding))
                    break;  // No more streams for this widget
                if (!binding || !binding[0]) continue;

                // Expand [pad:] tokens so streams store the underlying template
                char expanded[BINDING_TEMPLATE_MAX_LEN];
                if (pad_binding_expand(cfg, binding, expanded, sizeof(expanded))) {
                    binding = expanded;
                }

                // Check if an existing stream matches
                data_stream_handle_t existing = data_stream_find(binding, window_secs, slot_count);
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
                strlcpy(s->binding, binding, sizeof(s->binding));
                s->window_secs = window_secs;
                s->slot_count = slot_count;

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

                LOGI(TAG, "Stream[%d]: %s (window=%ds slots=%d)", slot, binding, window_secs, slot_count);
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
}

data_stream_handle_t data_stream_find(const char* binding,
                                      uint16_t window_secs,
                                      uint8_t slot_count) {
    if (!binding || !binding[0]) return DATA_STREAM_INVALID;
    for (int i = 0; i < DATA_STREAM_MAX_STREAMS; i++) {
        if (!g_streams[i].in_use) continue;
        if (strcmp(g_streams[i].binding, binding) != 0) continue;
        if (g_streams[i].window_secs != window_secs) continue;
        if (g_streams[i].slot_count != slot_count) continue;
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
    out->auto_min = s->auto_min;
    out->auto_max = s->auto_max;
    out->last_value = s->last_value;
    return true;
}

#endif // HAS_DISPLAY && HAS_MQTT
