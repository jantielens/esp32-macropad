#include "widgets/widget.h"

#if HAS_DISPLAY && IS_SHUTTER_TESTER

#include "../shutter_capture.h"
#include "../shutter_measure.h"
#include "log_manager.h"
#include <string.h>
#include <math.h>

#define TAG "Waveform"

// ============================================================================
// Waveform Widget
// ============================================================================
// Renders the captured ADC waveform from the shutter tester as overlaid line
// traces (one per active sensor channel). Polls the capture seam via
// shutter_capture_get_latest() and decimates samples to screen width.
//
// Layout within the tile:
//   ┌────────────────────────────┐
//   │    label_top               │
//   │  ┌──────────────────────┐  │
//   │  │  ╱╲   ╲╱  ╱╲        │  │  ← waveform traces (3 channels)
//   │  │ ─┼─────────┼──── thr │  │  ← threshold reference line
//   │  │  │         │         │  │  ← trigger marker (vertical)
//   │  └──────────────────────┘  │
//   │    label_bottom            │
//   └────────────────────────────┘

// Max display points per channel (one per pixel column).
#define WAVEFORM_MAX_POINTS 480

// ---- Config struct ----

#define CONFIG_COLOR_SHORT_LEN 12

struct WaveformConfig {
    char     line_color_1[CONFIG_COLOR_SHORT_LEN];  // Sensor 1 color
    char     line_color_2[CONFIG_COLOR_SHORT_LEN];  // Sensor 2 color
    char     line_color_3[CONFIG_COLOR_SHORT_LEN];  // Sensor 3 color
    char     line_color_4[CONFIG_COLOR_SHORT_LEN];  // Sensor 4 color
    char     threshold_color[CONFIG_COLOR_SHORT_LEN]; // Threshold line color
    char     trigger_color[CONFIG_COLOR_SHORT_LEN];   // Trigger marker color
    uint8_t  line_width;           // Line thickness (default 2)
    uint8_t  sensor;               // 0 = all sensors, 1..N = specific sensor only (N <= SHUTTER_SENSOR_MAX)
    bool     show_threshold;       // Draw adaptive threshold line
    bool     show_trigger;         // Draw vertical trigger marker
    bool     show_target;          // Draw target speed reference band
    bool     invert_y;             // Invert Y axis (pulse goes up instead of down)
};

static_assert(sizeof(WaveformConfig) <= WIDGET_CONFIG_MAX_BYTES,
              "WaveformConfig exceeds WIDGET_CONFIG_MAX_BYTES");

// ---- Runtime state ----

struct WaveformState {
    lv_obj_t*           lines[SHUTTER_SENSOR_MAX];  // LVGL line objects
    lv_point_precise_t* points[SHUTTER_SENSOR_MAX]; // Point arrays
    lv_obj_t*           threshold_line;    // Horizontal threshold reference
    lv_point_precise_t  threshold_pts[2];  // Threshold line endpoints
    lv_obj_t*           trigger_line;      // Vertical trigger marker
    lv_point_precise_t  trigger_pts[2];    // Trigger line endpoints
    lv_obj_t*           target_rect;       // Target speed rectangle overlay
    int16_t  chart_w;             // Cached chart width in pixels
    int16_t  chart_h;             // Cached chart height in pixels
    int16_t  chart_x;             // Chart left offset in button
    int16_t  chart_y;             // Chart top offset in button
    uint8_t  sensor_count;        // Number of sensors active at create time
    uint8_t  effective_sensor;    // Resolved display sensor (0=all, else clamped to count)
    uint32_t last_capture_id;     // Change detection: last processed capture_id
    uint32_t last_meas_ts;        // Change detection: last processed measurement timestamp
    uint32_t last_meas_count;     // Change detection: last processed measure_count
    uint16_t point_count;         // Current number of rendered points per line
};

static_assert(sizeof(WaveformState) <= WIDGET_STATE_MAX_BYTES,
              "WaveformState exceeds WIDGET_STATE_MAX_BYTES");

// ---- Parse ----

static void waveform_parse(const JsonObject& btn, uint8_t* data) {
    auto* cfg = reinterpret_cast<WaveformConfig*>(data);
    memset(cfg, 0, sizeof(WaveformConfig));

    widget_parse_field(btn["widget_waveform_color_1"], cfg->line_color_1, sizeof(cfg->line_color_1), "#4CAF50");
    widget_parse_field(btn["widget_waveform_color_2"], cfg->line_color_2, sizeof(cfg->line_color_2), "#2196F3");
    widget_parse_field(btn["widget_waveform_color_3"], cfg->line_color_3, sizeof(cfg->line_color_3), "#9C27B0");
    widget_parse_field(btn["widget_waveform_color_4"], cfg->line_color_4, sizeof(cfg->line_color_4), "#F44336");
    widget_parse_field(btn["widget_waveform_threshold_color"], cfg->threshold_color, sizeof(cfg->threshold_color), "#FF9800");
    widget_parse_field(btn["widget_waveform_trigger_color"], cfg->trigger_color, sizeof(cfg->trigger_color), "#FFFFFF");

    cfg->line_width = btn["widget_waveform_line_width"] | (uint8_t)2;
    if (cfg->line_width < 1) cfg->line_width = 1;

    cfg->sensor = btn["widget_waveform_sensor"] | (uint8_t)0;
    // Clamping to caps.sensor_count happens at create time when the capture seam is available.

    cfg->show_threshold = btn["widget_waveform_show_threshold"] | true;
    cfg->show_trigger   = btn["widget_waveform_show_trigger"] | true;
    cfg->show_target    = btn["widget_waveform_show_target"] | true;
    cfg->invert_y       = btn["widget_waveform_invert_y"] | false;
}

// ---- Create ----

static void waveform_create(lv_obj_t* tile, const WidgetConfig* wcfg,
                            const ScreenButtonConfig* btn,
                            const PadRect* rect, const UIScaleInfo* scale,
                            lv_obj_t* icon_img, lv_obj_t* center_label,
                            WidgetState* state) {
    (void)icon_img;
    (void)center_label;

    auto* cfg = reinterpret_cast<const WaveformConfig*>(wcfg->data);
    auto* st = reinterpret_cast<WaveformState*>(state->data);
    memset(st, 0, sizeof(WaveformState));

    // Compute chart area layout
    bool has_top = btn->label_top[0];
    bool has_bot = btn->label_bottom[0];
    int16_t label_h = lv_font_get_line_height(scale->font_small) + 2;
    int16_t top_h = has_top ? label_h : 0;
    int16_t bot_h = has_bot ? label_h : 0;

    lv_obj_update_layout(tile);
    int16_t content_w = (int16_t)lv_obj_get_content_width(tile);
    int16_t content_h = (int16_t)lv_obj_get_content_height(tile);

    st->chart_x = 0;
    st->chart_y = top_h;
    st->chart_w = content_w;
    st->chart_h = content_h - top_h - bot_h;
    if (st->chart_h < 8) st->chart_h = 8;
    if (st->chart_w < 8) st->chart_w = 8;

    // Query capture caps to know how many sensors are active
    ShutterCaptureCaps caps;
    shutter_capture_get_caps(&caps);
    st->sensor_count = caps.sensor_count;
    // Clamp cfg->sensor to active sensor count; 0 means show all
    if (cfg->sensor > caps.sensor_count) {
        st->effective_sensor = 0;
    } else {
        st->effective_sensor = cfg->sensor;
    }

    // Clamp max points to chart width
    uint16_t max_pts = (st->chart_w > WAVEFORM_MAX_POINTS) ? WAVEFORM_MAX_POINTS : (uint16_t)st->chart_w;

    // Create a clip container for the chart area
    lv_obj_t* chart_area = lv_obj_create(tile);
    lv_obj_set_pos(chart_area, st->chart_x + btn->ui_offset_x, st->chart_y + btn->ui_offset_y);
    lv_obj_set_size(chart_area, st->chart_w, st->chart_h);
    lv_obj_set_style_pad_all(chart_area, 0, 0);
    lv_obj_set_style_border_width(chart_area, 0, 0);
    lv_obj_set_style_bg_opa(chart_area, LV_OPA_TRANSP, 0);
    lv_obj_set_style_clip_corner(chart_area, true, 0);
    lv_obj_set_scrollbar_mode(chart_area, LV_SCROLLBAR_MODE_OFF);
    lv_obj_clear_flag(chart_area, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));

    // Allocate point arrays and create line objects for each active sensor
    // Colors for sensors 1-4 come from config; 5+ use a deterministic palette.
    static const uint32_t extended_palette[] = {
        0xFF9800, 0x00BCD4, 0xE91E63, 0x8BC34A, 0xFF5722
    };
    for (int i = 0; i < st->sensor_count; i++) {
        // Skip sensors not selected
        if (st->effective_sensor != 0 && st->effective_sensor != (i + 1)) {
            st->points[i] = nullptr;
            st->lines[i]  = nullptr;
            continue;
        }
        st->points[i] = (lv_point_precise_t*)lv_malloc(sizeof(lv_point_precise_t) * max_pts);
        if (!st->points[i]) {
            LOGE(TAG, "Failed to allocate points[%d]", i);
            return;
        }

        lv_obj_t* line = lv_line_create(chart_area);
        uint32_t def_color;
        const char* color_str = nullptr;
        if (i == 0)       { color_str = cfg->line_color_1; def_color = 0x4CAF50; }
        else if (i == 1)  { color_str = cfg->line_color_2; def_color = 0x2196F3; }
        else if (i == 2)  { color_str = cfg->line_color_3; def_color = 0x9C27B0; }
        else if (i == 3)  { color_str = cfg->line_color_4; def_color = 0xF44336; }
        else              { color_str = nullptr; def_color = extended_palette[(i - 4) % 5]; }
        lv_obj_set_style_line_color(line, resolve_lv_color(color_str ? color_str : "", def_color), 0);
        lv_obj_set_style_line_width(line, cfg->line_width, 0);
        lv_obj_set_style_line_rounded(line, true, 0);
        lv_obj_clear_flag(line, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
        lv_line_set_points(line, st->points[i], 0);
        st->lines[i] = line;
    }

    // Create threshold reference line (horizontal)
    if (cfg->show_threshold) {
        lv_obj_t* tl = lv_line_create(chart_area);
        lv_obj_set_style_line_color(tl, resolve_lv_color(cfg->threshold_color, 0xFF9800), 0);
        lv_obj_set_style_line_width(tl, 1, 0);
        lv_obj_set_style_line_opa(tl, LV_OPA_60, 0);
        lv_obj_set_style_line_dash_width(tl, 4, 0);
        lv_obj_set_style_line_dash_gap(tl, 3, 0);
        lv_obj_clear_flag(tl, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
        lv_obj_move_to_index(tl, 0);  // Behind data lines
        st->threshold_line = tl;
    }

    // Create trigger marker line (vertical)
    if (cfg->show_trigger) {
        lv_obj_t* tr = lv_line_create(chart_area);
        lv_obj_set_style_line_color(tr, resolve_lv_color(cfg->trigger_color, 0xFFFFFF), 0);
        lv_obj_set_style_line_width(tr, 1, 0);
        lv_obj_set_style_line_opa(tr, LV_OPA_40, 0);
        lv_obj_set_style_line_dash_width(tr, 2, 0);
        lv_obj_set_style_line_dash_gap(tr, 3, 0);
        lv_obj_clear_flag(tr, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
        lv_obj_move_to_index(tr, 0);  // Behind data lines
        st->trigger_line = tr;
    }

    // Create target speed rectangle (single-sensor mode only, rendered behind waveform)
    if (cfg->show_target && cfg->sensor != 0) {
        lv_obj_t* rect = lv_obj_create(chart_area);
        lv_obj_set_style_bg_color(rect, lv_color_hex(0xCCCCCC), 0);
        lv_obj_set_style_bg_opa(rect, LV_OPA_30, 0);
        lv_obj_set_style_border_width(rect, 0, 0);
        lv_obj_set_style_radius(rect, 0, 0);
        lv_obj_set_style_pad_all(rect, 0, 0);
        lv_obj_set_scrollbar_mode(rect, LV_SCROLLBAR_MODE_OFF);
        lv_obj_clear_flag(rect, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
        lv_obj_add_flag(rect, LV_OBJ_FLAG_HIDDEN);  // Hidden until measurement arrives
        lv_obj_move_to_index(rect, 0);  // Behind everything
        st->target_rect = rect;
    }

    st->last_capture_id = 0;
    st->point_count = 0;
}

// ---- Decimation and Redraw ----

static void waveform_redraw(const WaveformConfig* cfg, WaveformState* st,
                            const ShutterCaptureFrame* cap) {
    if (!cap->valid) return;

    // Determine sample count (use waveform 0 as reference — all same count)
    uint32_t total_samples = cap->waveforms[0].count;
    if (total_samples < 2) return;

    uint16_t max_pts = (st->chart_w > WAVEFORM_MAX_POINTS) ? WAVEFORM_MAX_POINTS : (uint16_t)st->chart_w;

    // ---- Viewport computation ----
    // Default: show entire capture buffer.
    uint32_t view_start = 0;
    uint32_t view_end = total_samples;

    // If measurement is available AND matches this capture, zoom to the pulse
    // with proportional padding.  When the measurement is stale (from a previous
    // capture), fall back to full-buffer view to avoid mismatched viewport.
    // Padding = pulse_width / 2 on each side, so pulse occupies ~50% of the view.
    // Use ALL sensors (not just visible ones) so viewports are consistent across
    // widgets showing different sensor selections.
    ShutterMeasurement meas;
    bool meas_valid = shutter_measure_get_latest(&meas) && meas.valid;
    bool meas_matches_capture = meas_valid &&
        (meas.timestamp_ms >= cap->timestamp_ms) &&
        (meas.timestamp_ms - cap->timestamp_ms < 1000);  // Within 1s of capture
    if (meas_matches_capture) {
        // Find earliest start and latest end across ALL sensors.
        uint32_t pulse_start = UINT32_MAX;
        uint32_t pulse_end = 0;
        for (int ch = 0; ch < st->sensor_count; ch++) {
            if (!meas.sensors[ch].valid) continue;
            if (meas.sensors[ch].start_idx < pulse_start) pulse_start = meas.sensors[ch].start_idx;
            if (meas.sensors[ch].end_idx > pulse_end) pulse_end = meas.sensors[ch].end_idx;
        }
        if (pulse_start < pulse_end) {
            uint32_t pulse_width = pulse_end - pulse_start;
            uint32_t padding = pulse_width / 2;
            if (padding < 64) padding = 64;  // Minimum padding for visual context
            view_start = (pulse_start > padding) ? (pulse_start - padding) : 0;
            view_end = pulse_end + padding;
            if (view_end > total_samples) view_end = total_samples;
        }
    }

    uint32_t view_samples = view_end - view_start;
    if (view_samples < 2) view_samples = 2;
    // Always use full chart width — stretch viewport to fill all available pixels.
    uint16_t num_points = max_pts;

    // Y-axis: auto-scale from observed min/max across visible channels (viewport only)
    uint16_t y_min = 4095;
    uint16_t y_max = 0;
    for (int ch = 0; ch < st->sensor_count; ch++) {
        if (!st->lines[ch]) continue;  // Sensor not shown
        const uint16_t* samples = cap->waveforms[ch].samples;
        uint32_t count = cap->waveforms[ch].count;
        for (uint32_t i = view_start; i < view_end && i < count; i++) {
            if (samples[i] < y_min) y_min = samples[i];
            if (samples[i] > y_max) y_max = samples[i];
        }
    }
    // Add 2% padding
    uint16_t range = y_max - y_min;
    uint16_t pad = range / 50;
    if (pad < 10) pad = 10;
    int32_t y_lo = (int32_t)y_min - pad;
    int32_t y_hi = (int32_t)y_max + pad;
    if (y_lo < 0) y_lo = 0;
    if (y_hi > 4095) y_hi = 4095;
    float y_range = (float)(y_hi - y_lo);
    if (y_range < 1.0f) y_range = 1.0f;

    // Decimate and map each channel to pixel coordinates (viewport)
    float x_step = (float)(view_samples - 1) / (float)(num_points - 1);

    for (int ch = 0; ch < st->sensor_count; ch++) {
        if (!st->lines[ch]) continue;  // Sensor not shown
        const uint16_t* samples = cap->waveforms[ch].samples;
        uint32_t count = cap->waveforms[ch].count;

        for (uint16_t px = 0; px < num_points; px++) {
            // Point-sample decimation: pick the sample at this x position within viewport
            uint32_t src_idx = view_start + (uint32_t)(px * x_step + 0.5f);
            if (src_idx >= count) src_idx = count - 1;

            float val = (float)samples[src_idx];
            float ratio = (val - (float)y_lo) / y_range;
            if (ratio < 0.0f) ratio = 0.0f;
            if (ratio > 1.0f) ratio = 1.0f;

            // Default: high ADC = top (baseline), low ADC = bottom (pulse dips down)
            // invert_y: low ADC = top (pulse goes up)
            int16_t py;
            if (cfg->invert_y) {
                py = (int16_t)(ratio * (float)(st->chart_h - 1));
            } else {
                py = (int16_t)((1.0f - ratio) * (float)(st->chart_h - 1));
            }

            st->points[ch][px].x = (lv_value_precise_t)px;
            st->points[ch][px].y = (lv_value_precise_t)py;
        }

        lv_line_set_points(st->lines[ch], st->points[ch], num_points);
    }

    st->point_count = num_points;

    // Helper: convert a sample index to pixel X within the viewport
    auto sample_to_px = [&](uint32_t idx) -> int16_t {
        if (idx <= view_start) return 0;
        if (idx >= view_end) return (int16_t)(num_points - 1);
        float f = (float)(idx - view_start) / (float)(view_samples - 1) * (float)(num_points - 1);
        return (int16_t)f;
    };

    // Update threshold reference line
    if (st->threshold_line) {
        // Use the threshold from the capture frame (sensor 0)
        float thr_val = (float)cap->thresholds[0];
        float thr_ratio = (thr_val - (float)y_lo) / y_range;
        if (thr_ratio < 0.0f) thr_ratio = 0.0f;
        if (thr_ratio > 1.0f) thr_ratio = 1.0f;
        int16_t thr_py = cfg->invert_y
            ? (int16_t)(thr_ratio * (float)(st->chart_h - 1))
            : (int16_t)((1.0f - thr_ratio) * (float)(st->chart_h - 1));
        st->threshold_pts[0].x = 0;
        st->threshold_pts[0].y = (lv_value_precise_t)thr_py;
        st->threshold_pts[1].x = (lv_value_precise_t)(num_points - 1);
        st->threshold_pts[1].y = (lv_value_precise_t)thr_py;
        lv_line_set_points(st->threshold_line, st->threshold_pts, 2);
    }

    // Update trigger marker (vertical line at pre-trigger boundary)
    if (st->trigger_line) {
        uint32_t trigger_idx = cap->waveforms[0].trigger_index;
        int16_t trig_px = sample_to_px(trigger_idx);
        st->trigger_pts[0].x = (lv_value_precise_t)trig_px;
        st->trigger_pts[0].y = 0;
        st->trigger_pts[1].x = (lv_value_precise_t)trig_px;
        st->trigger_pts[1].y = (lv_value_precise_t)(st->chart_h - 1);
        lv_line_set_points(st->trigger_line, st->trigger_pts, 2);
    }

    // Update target speed rectangle from measurement result
    if (meas_valid) {
        // Always consume the count so the all-sensors path doesn't loop.
        st->last_meas_ts    = meas.timestamp_ms;
        st->last_meas_count = shutter_measure_get_count();
    }
    if (st->target_rect) {
        int ch = st->effective_sensor - 1;  // Single-sensor mode: sensor 1→index 0, etc.
        if (meas_matches_capture && meas.nearest_duration_ms > 0 && meas.sensors[ch].valid) {

            float sample_rate = cap->waveforms[0].sample_rate_hz;
            float target_samples = meas.nearest_duration_ms * sample_rate / 1000.0f;

            // X position: anchored at this sensor's start_idx
            int16_t px_x = sample_to_px(meas.sensors[ch].start_idx);
            int16_t px_x_end = sample_to_px(meas.sensors[ch].start_idx + (uint32_t)(target_samples + 0.5f));
            int16_t px_w = px_x_end - px_x;
            if (px_w < 4) px_w = 4;

            // Full chart height
            lv_obj_set_pos(st->target_rect, px_x, 0);
            lv_obj_set_size(st->target_rect, px_w, st->chart_h);
            lv_obj_clear_flag(st->target_rect, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(st->target_rect, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// ---- Tick ----

static void waveform_tick(lv_obj_t* tile, const WidgetConfig* wcfg, WidgetState* state) {
    (void)tile;
    auto* cfg = reinterpret_cast<const WaveformConfig*>(wcfg->data);
    auto* st = reinterpret_cast<WaveformState*>(state->data);

    // Check that at least one sensor line is allocated
    bool has_any = false;
    for (int i = 0; i < st->sensor_count; i++) {
        if (st->lines[i] && st->points[i]) { has_any = true; break; }
    }
    if (!has_any) return;

    // Poll capture seam for new frame
    ShutterCaptureFrame cap;
    if (!shutter_capture_get_latest(&cap)) return;
    if (!cap.valid) return;

    // Change detection: redraw if capture or measurement changed
    bool capture_changed = (cap.capture_id != st->last_capture_id || st->last_capture_id == 0);
    bool meas_changed = false;
    bool meas_matches = false;
    {
        ShutterMeasurement meas;
        if (shutter_measure_get_latest(&meas) && meas.valid) {
            meas_changed = (meas.timestamp_ms != st->last_meas_ts) ||
                           (shutter_measure_get_count() != st->last_meas_count);
            meas_matches = (meas.timestamp_ms >= cap.timestamp_ms) &&
                           (meas.timestamp_ms - cap.timestamp_ms < 1000);
        }
    }

    if (capture_changed) {
        st->last_capture_id = cap.capture_id;
        if (!meas_matches) return;  // Wait for measurement to arrive
    } else if (!meas_changed) {
        return;  // Nothing new
    }

    waveform_redraw(cfg, st, &cap);
}

// ---- Update (no-op, widget is self-driven) ----

static void waveform_update(lv_obj_t* tile, const WidgetConfig* wcfg,
                            WidgetState* state, const char* raw_value) {
    (void)tile; (void)wcfg; (void)state; (void)raw_value;
}

// ---- Destroy ----

static void waveform_destroy(WidgetState* state) {
    auto* st = reinterpret_cast<WaveformState*>(state->data);
    for (int i = 0; i < SHUTTER_SENSOR_MAX; i++) {
        if (st->points[i]) {
            lv_free(st->points[i]);
            st->points[i] = nullptr;
        }
    }
}

// ---- Registration ----

REGISTER_WIDGET(waveform, nullptr, true);

#endif // HAS_DISPLAY && IS_SHUTTER_TESTER
