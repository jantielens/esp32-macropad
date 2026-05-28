#include "shutter_capture.h"

#if IS_SHUTTER_TESTER

#include "shutter_adc.h"
#include "log_manager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

#define TAG "ShutterCap"

// ============================================================================
// Reference-counted lifecycle state
// ============================================================================
// s_refcount tracks the number of outstanding acquire() calls. On 0->1 we
// call shutter_adc_start(); on 1->0 we call shutter_adc_stop(). The tag
// table is for diagnostic logging only and tolerates unknown tags by logging
// rather than asserting.
#define SHUTTER_REFCOUNT_TAG_SLOTS 8
struct ShutterTagCount {
    const char* tag;
    int         count;
};
static ShutterTagCount  s_tag_counts[SHUTTER_REFCOUNT_TAG_SLOTS] = {};
static int              s_refcount     = 0;
static SemaphoreHandle_t s_refcount_mux = nullptr;

static int* find_tag_slot(const char* tag, bool create) {
    if (!tag) tag = "<null>";
    for (int i = 0; i < SHUTTER_REFCOUNT_TAG_SLOTS; i++) {
        if (s_tag_counts[i].tag && strcmp(s_tag_counts[i].tag, tag) == 0) {
            return &s_tag_counts[i].count;
        }
    }
    if (!create) return nullptr;
    for (int i = 0; i < SHUTTER_REFCOUNT_TAG_SLOTS; i++) {
        if (!s_tag_counts[i].tag) {
            s_tag_counts[i].tag   = tag;
            s_tag_counts[i].count = 0;
            return &s_tag_counts[i].count;
        }
    }
    LOGW(TAG, "Tag table full; dropping accounting for '%s'", tag);
    return nullptr;
}

// ============================================================================
// Preset slot descriptors
// ============================================================================

static const ShutterSensorSlot SLOTS_DIRECT_SINGLE[] = {
    { 0, SHUTTER_ADC_PIN_S1, -1, 0, 0, "S1" },
};

static const ShutterSensorSlot SLOTS_DIRECT_3LINE[] = {
    { 0, SHUTTER_ADC_PIN_S1, -1, 0, 0, "S1" },
    { 1, SHUTTER_ADC_PIN_S2, -1, 0, 1, "S2" },
    { 2, SHUTTER_ADC_PIN_S3, -1, 0, 2, "S3" },
};

static const ShutterSensorSlot SLOTS_DIRECT_4CORNER[] = {
    { 0, SHUTTER_ADC_PIN_S4, -1, 0, 0, "S1" },  // Slot 0 = TL — wired to GPIO S4
    { 1, SHUTTER_ADC_PIN_S3, -1, 0, 1, "S2" },  // Slot 1 = TR — wired to GPIO S3
    { 2, SHUTTER_ADC_PIN_S1, -1, 1, 0, "S3" },  // Slot 2 = BL — wired to GPIO S1
    { 3, SHUTTER_ADC_PIN_S2, -1, 1, 1, "S4" },  // Slot 3 = BR — wired to GPIO S2
};

// Reserved offload presets — slots have no local GPIO.
static const ShutterSensorSlot SLOTS_OFFLOAD_3LINE[] = {
    { 0, -1, 0, 0, 0, "S1" },
    { 1, -1, 1, 0, 1, "S2" },
    { 2, -1, 2, 0, 2, "S3" },
};

static const ShutterSensorSlot SLOTS_OFFLOAD_9MATRIX[] = {
    { 0, -1, 0, 0, 0, "S11" }, { 1, -1, 1, 0, 1, "S12" }, { 2, -1, 2, 0, 2, "S13" },
    { 3, -1, 3, 1, 0, "S21" }, { 4, -1, 4, 1, 1, "S22" }, { 5, -1, 5, 1, 2, "S23" },
    { 6, -1, 6, 2, 0, "S31" }, { 7, -1, 7, 2, 1, "S32" }, { 8, -1, 8, 2, 2, "S33" },
};

// ============================================================================
// Per-sensor position arrays (mm from mount geometric centre)
// ============================================================================

static const ShutterSensorPosition POS_DIRECT_SINGLE[] = {
    { 0.0f, 0.0f },
};

static const ShutterSensorPosition POS_DIRECT_3LINE[] = {
    { -11.2f, -7.4f },   // S1 (v1 mount defaults)
    {   0.0f,  0.0f },   // S2
    { +11.2f, +7.4f },   // S3
};

static const ShutterSensorPosition POS_DIRECT_4CORNER[] = {
    { -14.0f, +10.0f },  // S1 — top-left
    { +14.0f, +10.0f },  // S2 — top-right
    { -14.0f, -10.0f },  // S3 — bottom-left
    { +14.0f, -10.0f },  // S4 — bottom-right
};

// ============================================================================
// Preset table
// ============================================================================
// The two active presets ship now. The two reserved presets are visible as
// descriptors but cannot become active until an offload backend exists.

static const ShutterSensorPreset PRESET_TABLE[] = {
    {
        ShutterPresetId::DirectSingle, "direct_single", "Direct - Single",
        ShutterTopologyType::SingleSensor, 1, true, SHUTTER_SAMPLE_RATE_HZ,
        SLOTS_DIRECT_SINGLE, 1, true,
        POS_DIRECT_SINGLE, 1
    },
    {
        ShutterPresetId::Direct3Line, "direct_3_line", "Direct - 3-Line",
        ShutterTopologyType::ThreeLine, 3, true, SHUTTER_SAMPLE_RATE_HZ,
        SLOTS_DIRECT_3LINE, 3, true,
        POS_DIRECT_3LINE, 3
    },
    {
        ShutterPresetId::Offload3Line, "offload_3_line", "Indirect - 3-Line",
        ShutterTopologyType::ThreeLine, 3, false, SHUTTER_SAMPLE_RATE_HZ,
        SLOTS_OFFLOAD_3LINE, 3, false,   // active_now = false: no backend yet
        POS_DIRECT_3LINE, 3
    },
    {
        ShutterPresetId::Offload9Matrix, "offload_9_matrix", "Indirect - 3x3 Matrix",
        ShutterTopologyType::Matrix3x3, 9, false, SHUTTER_SAMPLE_RATE_HZ,
        SLOTS_OFFLOAD_9MATRIX, 9, false, // active_now = false: no backend yet
        nullptr, 0
    },
    {
        ShutterPresetId::Direct4Corner, "direct_4_corner", "Direct - 4-Corner",
        ShutterTopologyType::FourSensor, 4, true, SHUTTER_SAMPLE_RATE_HZ,
        SLOTS_DIRECT_4CORNER, 4, true,
        POS_DIRECT_4CORNER, 4
    },
    {
        ShutterPresetId::Direct4LShapeH, "direct_4_lshape_h", "Direct - 4 L-Shape Horizontal",
        ShutterTopologyType::FourSensor, 4, true, SHUTTER_SAMPLE_RATE_HZ,
        SLOTS_DIRECT_4CORNER, 4, false,  // active_now = false: reserved
        nullptr, 0
    },
    {
        ShutterPresetId::Direct4LShapeV, "direct_4_lshape_v", "Direct - 4 L-Shape Vertical",
        ShutterTopologyType::FourSensor, 4, true, SHUTTER_SAMPLE_RATE_HZ,
        SLOTS_DIRECT_4CORNER, 4, false,  // active_now = false: reserved
        nullptr, 0
    },
};

static const uint8_t PRESET_TABLE_COUNT =
    (uint8_t)(sizeof(PRESET_TABLE) / sizeof(PRESET_TABLE[0]));

// ============================================================================
// Module state
// ============================================================================

static bool s_capture_available = false;
static ShutterCaptureCaps s_caps = {};
static ShutterTriggerConfig s_trigger_cfg = {};
static const ShutterSensorPreset* s_active_preset = nullptr;

// ============================================================================
// Internal helpers
// ============================================================================

static bool board_has_pin(int gpio) {
    return gpio >= 0;
}

// Returns true if the given preset can be activated on this board/firmware.
static bool can_activate(const ShutterSensorPreset* p) {
    if (!p->active_now || !p->local_capture) return false;
    if (p->id == ShutterPresetId::Direct3Line) {
        return board_has_pin(SHUTTER_ADC_PIN_S1) &&
               board_has_pin(SHUTTER_ADC_PIN_S2) &&
               board_has_pin(SHUTTER_ADC_PIN_S3);
    }
    if (p->id == ShutterPresetId::Direct4Corner ||
        p->id == ShutterPresetId::Direct4LShapeH ||
        p->id == ShutterPresetId::Direct4LShapeV) {
        return board_has_pin(SHUTTER_ADC_PIN_S1) &&
               board_has_pin(SHUTTER_ADC_PIN_S2) &&
               board_has_pin(SHUTTER_ADC_PIN_S3) &&
               board_has_pin(SHUTTER_ADC_PIN_S4);
    }
    if (p->id == ShutterPresetId::DirectSingle) {
        return board_has_pin(SHUTTER_ADC_PIN_S1);
    }
    return false;
}

// Find a preset by string ID. Returns nullptr if not found.
static const ShutterSensorPreset* find_preset(const char* id_str) {
    if (!id_str || !id_str[0]) return nullptr;
    for (uint8_t i = 0; i < PRESET_TABLE_COUNT; i++) {
        if (strcmp(PRESET_TABLE[i].preset_id_str, id_str) == 0) {
            return &PRESET_TABLE[i];
        }
    }
    return nullptr;
}

// Resolve the active preset following the fallback chain:
//   requested → direct_3_line → direct_single → unavailable
static const ShutterSensorPreset* resolve_preset(const char* requested_id_str) {
    // Try the requested preset first.
    const ShutterSensorPreset* p = find_preset(requested_id_str);
    if (p && can_activate(p)) {
        return p;
    }
    if (p) {
        if (!p->active_now) {
            LOGW(TAG, "Preset '%s' is reserved (no backend) — falling back", requested_id_str ? requested_id_str : "<null>");
        } else {
            LOGW(TAG, "Preset '%s' unavailable on this board — falling back", requested_id_str ? requested_id_str : "<null>");
        }
    } else if (requested_id_str && requested_id_str[0]) {
        LOGW(TAG, "Unknown preset '%s' — falling back", requested_id_str);
    }

    // Fall back to direct_3_line.
    p = find_preset("direct_3_line");
    if (p && can_activate(p)) {
        LOGI(TAG, "Using fallback preset: direct_3_line");
        return p;
    }

    // Fall back to direct_single.
    p = find_preset("direct_single");
    if (p && can_activate(p)) {
        LOGI(TAG, "Using fallback preset: direct_single");
        return p;
    }

    LOGE(TAG, "No usable preset found — shutter tester unavailable");
    return nullptr;
}

// ============================================================================
// Public API
// ============================================================================

void shutter_capture_init(const char* preset_id_str) {
    s_capture_available = false;
    memset(&s_caps, 0, sizeof(s_caps));

    // Set default trigger configuration from board macros.
    for (int i = 0; i < SHUTTER_SENSOR_MAX; i++) {
        s_trigger_cfg.thresholds[i] = SHUTTER_DEFAULT_THRESHOLD;
    }
    s_trigger_cfg.pre_trigger_samples  = SHUTTER_PRE_TRIGGER_SAMPLES;
    s_trigger_cfg.post_trigger_samples = SHUTTER_POST_TRIGGER_SAMPLES;
    s_trigger_cfg.post_capture_samples = SHUTTER_POST_CAPTURE_SAMPLES;

    // Resolve the active preset.
    const ShutterSensorPreset* preset = resolve_preset(preset_id_str);
    if (!preset) return;

    LOGI(TAG, "Activating preset '%s' (%s), %d sensor(s)",
         preset->preset_id_str, preset->display_name, preset->sensor_count);

    // Compute GPIO-to-logical-slot mapping from the preset's slot array.
    // The ADC layer reorders its scan list so DMA data arrives in slot order.
    ShutterSensorSlotMapping slot_mapping = {};
    slot_mapping.count = preset->sensor_count;
    for (int i = 0; i < preset->sensor_count && i < SHUTTER_SENSOR_MAX; i++) {
        slot_mapping.gpio_pins[i] = preset->slots[i].local_gpio;
    }
    shutter_adc_set_slot_mapping(&slot_mapping);

    // Initialize the backend with the active sensor count.
    shutter_adc_init(preset->sensor_count);
    if (!shutter_adc_is_available()) {
        LOGE(TAG, "ADC backend init failed");
        return;
    }

    // Populate capabilities struct.
    s_caps.sensor_count               = preset->sensor_count;
    s_caps.sample_rate_hz_per_sensor  = preset->expected_sample_rate_hz_per_sensor;
    s_caps.topology                   = preset->topology;
    s_caps.preset_id                  = preset->id;
    s_caps.preset_id_str              = preset->preset_id_str;
    s_caps.preset_name                = preset->display_name;
    s_caps.backend_name               = "p4_local_adc";
    s_caps.waveform_available         = true;
    s_caps.local_capture              = true;

    s_capture_available = true;
    s_active_preset = preset;

    // Create the refcount mutex on first init. Subsequent re-inits keep it.
    if (!s_refcount_mux) {
        s_refcount_mux = xSemaphoreCreateMutex();
        if (!s_refcount_mux) {
            LOGE(TAG, "Failed to create refcount mutex — lifecycle will not work");
            s_capture_available = false;
            return;
        }
    }

    LOGI(TAG, "Capture layer ready: %s (engine parked until first acquire)", preset->display_name);
}

// ============================================================================
// Reference-counted lifecycle
// ============================================================================

bool shutter_capture_acquire(const char* tag) {
    if (!s_capture_available) {
        LOGW(TAG, "acquire('%s'): not available", tag ? tag : "<null>");
        return false;
    }
    if (!s_refcount_mux) return false;

    xSemaphoreTake(s_refcount_mux, portMAX_DELAY);

    bool first = (s_refcount == 0);
    if (first) {
        // Drop the mutex while calling into the ADC layer — shutter_adc_stop()
        // can block for up to a second waiting for the worker task to park,
        // and we must not hold the refcount mutex across that wait if another
        // task tries to release concurrently. The lifecycle window is
        // protected by the ADC layer's own mutex.
        xSemaphoreGive(s_refcount_mux);
        bool ok = shutter_adc_start();
        if (!ok) {
            LOGE(TAG, "acquire('%s'): adc_start failed", tag ? tag : "<null>");
            return false;
        }
        xSemaphoreTake(s_refcount_mux, portMAX_DELAY);
        // Re-check: another acquire may have raced us. The ADC start is
        // idempotent so either way the engine is now running.
    }

    int* slot = find_tag_slot(tag, true);
    if (slot) (*slot)++;
    s_refcount++;
    int total = s_refcount;
    xSemaphoreGive(s_refcount_mux);

    LOGI(TAG, "acquire('%s'): refcount=%d%s", tag ? tag : "<null>", total, first ? " (started)" : "");
    return true;
}

void shutter_capture_release(const char* tag) {
    if (!s_capture_available || !s_refcount_mux) return;

    xSemaphoreTake(s_refcount_mux, portMAX_DELAY);

    int* slot = find_tag_slot(tag, false);
    if (!slot || *slot <= 0) {
        LOGW(TAG, "release('%s'): no outstanding acquire", tag ? tag : "<null>");
        xSemaphoreGive(s_refcount_mux);
        return;
    }
    (*slot)--;
    if (s_refcount > 0) s_refcount--;
    bool last = (s_refcount == 0);
    int total = s_refcount;
    xSemaphoreGive(s_refcount_mux);

    LOGI(TAG, "release('%s'): refcount=%d%s", tag ? tag : "<null>", total, last ? " (stopping)" : "");
    if (last) {
        shutter_adc_stop();
    }
}

bool shutter_capture_is_running() {
    if (!s_capture_available || !s_refcount_mux) return false;
    xSemaphoreTake(s_refcount_mux, portMAX_DELAY);
    bool running = (s_refcount > 0);
    xSemaphoreGive(s_refcount_mux);
    return running;
}

bool shutter_capture_is_available() {
    return s_capture_available;
}

void shutter_capture_get_caps(ShutterCaptureCaps* out) {
    if (!out) return;
    *out = s_caps;
    // Override with the live calibrated rate from the ADC backend so the value
    // reported to UI/CSV reflects what the hardware actually delivers, not the
    // preset's expected/configured rate. Falls back to the preset value if the
    // backend is unavailable or has not yet calibrated.
    if (shutter_adc_is_available()) {
        float live = shutter_adc_get_sample_rate_hz();
        if (live > 0.0f) {
            out->sample_rate_hz_per_sensor = (uint32_t)(live + 0.5f);
        }
    }
}

uint8_t shutter_capture_get_positions(ShutterSensorPosition* out, uint8_t max_count) {
    if (!s_active_preset || !s_active_preset->positions || s_active_preset->position_count == 0) {
        return 0;
    }
    uint8_t count = s_active_preset->position_count;
    if (count > max_count) count = max_count;
    if (out && count > 0) {
        memcpy(out, s_active_preset->positions, count * sizeof(ShutterSensorPosition));
    }
    return count;
}

void shutter_capture_set_trigger_config(const ShutterTriggerConfig* cfg) {
    if (!cfg) return;
    s_trigger_cfg = *cfg;
    // Push the first sensor's threshold to the ADC backend (single global threshold).
    shutter_adc_set_threshold(cfg->thresholds[0]);
}

void shutter_capture_arm() {
    // Local ADC backend is always-listening; arm is a no-op.
}

void shutter_capture_poll() {
    // Local ADC backend is driven by its own FreeRTOS task; poll is a no-op.
}

bool shutter_capture_get_latest(ShutterCaptureFrame* out) {
    if (!s_capture_available || !out) return false;

    ShutterCapture adc_cap;
    if (!shutter_adc_get_capture(&adc_cap)) return false;
    if (!adc_cap.valid) return false;

    out->capture_id   = adc_cap.capture_id;
    out->timestamp_ms = adc_cap.timestamp_ms;
    out->sensor_count = adc_cap.sensor_count;
    out->topology     = s_caps.topology;
    out->preset_id    = s_caps.preset_id;
    out->valid        = true;

    // Copy thresholds from the active trigger config.
    for (int i = 0; i < SHUTTER_SENSOR_MAX; i++) {
        out->thresholds[i] = s_trigger_cfg.thresholds[i];
    }

    // Populate waveform views for active sensors.
    for (int i = 0; i < SHUTTER_SENSOR_MAX; i++) {
        if (i < adc_cap.sensor_count) {
            out->waveforms[i].samples              = adc_cap.sensors[i].samples;
            out->waveforms[i].count                = adc_cap.sensors[i].count;
            out->waveforms[i].trigger_index        = adc_cap.sensors[i].trigger_index;
            out->waveforms[i].sample_rate_hz       = adc_cap.sensors[i].sample_rate_hz;
            out->waveforms[i].logical_sensor_index = (uint8_t)i;
        } else {
            out->waveforms[i] = {};
        }
    }

    return true;
}

const ShutterSensorPreset* shutter_capture_get_preset_table(uint8_t* count_out) {
    if (count_out) *count_out = PRESET_TABLE_COUNT;
    return PRESET_TABLE;
}

// ============================================================================
// Alignment Mode API
// ============================================================================

void shutter_capture_start_alignment() {
    if (!s_capture_available) { LOGW(TAG, "start_alignment: not available"); return; }
    // Hold the engine for the duration of alignment mode.
    if (!shutter_capture_acquire("align")) {
        LOGE(TAG, "start_alignment: failed to acquire engine");
        return;
    }
    LOGI(TAG, "start_alignment: calling adc_start, adc_active=%d", shutter_adc_is_alignment_active());
    shutter_adc_start_alignment();
}

void shutter_capture_stop_alignment() {
    if (!s_capture_available) { LOGW(TAG, "stop_alignment: not available"); return; }
    bool was_active = shutter_adc_is_alignment_active();
    LOGI(TAG, "stop_alignment: calling adc_stop, adc_active=%d", was_active);
    shutter_adc_stop_alignment();
    // Only release if we previously acquired for alignment. The adc-level
    // "alignment active" flag tracks the request the user actually made.
    if (was_active) {
        shutter_capture_release("align");
    }
}

bool shutter_capture_is_alignment_active() {
    if (!s_capture_available) return false;
    return shutter_adc_is_alignment_active();
}

void shutter_capture_recalibrate() {
    if (!s_capture_available) { LOGW(TAG, "recalibrate: not available"); return; }
    // Recalibrate is meaningful only while the engine is held by some other
    // consumer (e.g. the shutter pad screen). If nobody is holding it, the
    // calibration would happen during the next start() anyway, so this is a
    // no-op with a warning rather than an implicit acquire/release pair.
    if (!shutter_capture_is_running()) {
        LOGW(TAG, "recalibrate: engine not running — ignored");
        return;
    }
    shutter_adc_recalibrate();
}

bool shutter_capture_is_calibrating() {
    if (!s_capture_available) return false;
    return shutter_adc_is_calibrating();
}

bool shutter_capture_get_alignment(ShutterAlignmentReading* out) {
    if (!s_capture_available || !out) return false;
    return shutter_adc_get_alignment(out);
}

#endif // IS_SHUTTER_TESTER
