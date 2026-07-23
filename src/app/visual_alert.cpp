#include "visual_alert.h"

#if HAS_DISPLAY

#include "display_manager.h"
#include "log_manager.h"

#include <lvgl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <string.h>
#include <math.h>

static const char* TAG = "VisAlert";

static const uint32_t FADE_MS = 200;

// ---------------------------------------------------------------------------
// Cross-task signaling (API callers → loop in main task)
// ---------------------------------------------------------------------------
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_pending_show = false;
static bool s_pending_stop = false;
static VisualAlertParams s_pending_params;

// ---------------------------------------------------------------------------
// LVGL state (only touched from main loop under LVGL mutex)
// ---------------------------------------------------------------------------
static lv_obj_t*   s_overlay = nullptr;
static lv_timer_t* s_dismiss_timer = nullptr;  // one-shot auto-stop (duration)
static lv_timer_t* s_pulse_timer = nullptr;    // drives blink toggle or breathe steps
static uint8_t     s_pattern = VA_PATTERN_SOLID;
static bool        s_blink_on = false;
static uint8_t     s_max_opa = 0;              // resolved peak opacity (0-255)
static uint8_t     s_floor_opa = 0;            // breathe trough opacity (0-255)
static uint32_t    s_period_ms = VA_DEFAULT_PERIOD_MS;
static uint32_t    s_breathe_phase_ms = 0;     // 0..period cycle position
static int16_t     s_cur_opa = -1;             // last applied bg_opa (-1 = unset)

// Breathe update cadence. A deliberate step interval keeps the full-screen
// overlay from re-rendering every LVGL frame while an alert persists; the
// raised-cosine curve still reads as a smooth pulse.
static const uint32_t BREATHE_TICK_MS = 60;

// ---------------------------------------------------------------------------
// Shared pattern string parser
// ---------------------------------------------------------------------------
uint8_t visual_alert_pattern_from_str(const char* s) {
    if (s && strcmp(s, "blink") == 0) return VA_PATTERN_BLINK;
    if (s && strcmp(s, "solid") == 0) return VA_PATTERN_SOLID;
    return VA_PATTERN_BREATHE;
}

// ---------------------------------------------------------------------------
// Internal helpers (must be called under LVGL mutex)
// ---------------------------------------------------------------------------

static void anim_bg_opa_cb(void* obj, int32_t v) {
    lv_obj_set_style_bg_opa((lv_obj_t*)obj, (lv_opa_t)v, 0);
}

// Apply a background opacity, skipping the style write (and its full-screen
// invalidation) when the value is unchanged.
static void set_overlay_opa(uint8_t opa) {
    if (!s_overlay || (int16_t)opa == s_cur_opa) return;
    s_cur_opa = (int16_t)opa;
    lv_obj_set_style_bg_opa(s_overlay, opa, 0);
}

static void destroy_overlay() {
    if (s_dismiss_timer) { lv_timer_delete(s_dismiss_timer); s_dismiss_timer = nullptr; }
    if (s_pulse_timer)   { lv_timer_delete(s_pulse_timer);   s_pulse_timer = nullptr; }
    if (s_overlay) {
        lv_anim_delete(s_overlay, nullptr);  // cancel any in-flight fade anim
        lv_obj_delete(s_overlay);
        s_overlay = nullptr;
    }
    s_cur_opa = -1;
}

// Cancel the pulse, fade the tint to transparent, then destroy.
static void fade_out_and_destroy() {
    if (!s_overlay) return;
    // Stop pulsing sources so the fade is monotonic.
    lv_anim_delete(s_overlay, nullptr);
    if (s_pulse_timer)   { lv_timer_delete(s_pulse_timer);   s_pulse_timer = nullptr; }
    if (s_dismiss_timer) { lv_timer_delete(s_dismiss_timer); s_dismiss_timer = nullptr; }

    lv_opa_t cur = lv_obj_get_style_bg_opa(s_overlay, LV_PART_MAIN);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_overlay);
    lv_anim_set_values(&a, cur, LV_OPA_TRANSP);
    lv_anim_set_duration(&a, FADE_MS);
    lv_anim_set_exec_cb(&a, anim_bg_opa_cb);
    lv_anim_set_completed_cb(&a, [](lv_anim_t* anim) {
        (void)anim;
        destroy_overlay();
    });
    lv_anim_start(&a);
}

static void on_dismiss_timer(lv_timer_t* timer) {
    (void)timer;
    s_dismiss_timer = nullptr;  // one-shot auto-deletes; don't delete inside its own cb
    fade_out_and_destroy();
}

static void on_pulse_timer(lv_timer_t* timer) {
    (void)timer;
    if (!s_overlay) return;
    if (s_pattern == VA_PATTERN_BLINK) {
        s_blink_on = !s_blink_on;
        set_overlay_opa(s_blink_on ? s_max_opa : LV_OPA_TRANSP);
    } else {
        // Breathe: advance the phase and map it onto a raised-cosine 0-max-0
        // curve so the pulse eases in and out.
        s_breathe_phase_ms += BREATHE_TICK_MS;
        if (s_breathe_phase_ms >= s_period_ms) s_breathe_phase_ms -= s_period_ms;
        float t01 = (float)s_breathe_phase_ms / (float)s_period_ms;
        float e = (1.0f - cosf(6.2831853f * t01)) * 0.5f;  // 0..1..0
        uint8_t opa = (uint8_t)(s_floor_opa + (float)(s_max_opa - s_floor_opa) * e);
        set_overlay_opa(opa);
    }
}

static void on_overlay_clicked(lv_event_t* e) {
    (void)e;
    fade_out_and_destroy();
    LOGI(TAG, "Dismissed by tap");
}

static void create_overlay(const VisualAlertParams* p) {
    // Replace any existing alert first (last-write-wins).
    destroy_overlay();

    if (!displayManager) return;
    lv_obj_t* layer = lv_layer_top();
    if (!layer) return;

    uint8_t intensity = (p->intensity >= 1 && p->intensity <= 100) ? p->intensity : VA_DEFAULT_INTENSITY;
    s_max_opa = (uint8_t)(intensity * 255 / 100);

    // Full-screen tint object
    s_overlay = lv_obj_create(layer);
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_size(s_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_overlay, lv_color_hex(p->color), 0);
    lv_obj_set_style_border_width(s_overlay, 0, 0);
    lv_obj_set_style_radius(s_overlay, 0, 0);

    // Tap anywhere to dismiss
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_overlay, on_overlay_clicked, LV_EVENT_CLICKED, nullptr);

    s_pattern   = p->pattern;
    s_period_ms = p->period_ms > 0 ? p->period_ms : VA_DEFAULT_PERIOD_MS;
    s_cur_opa   = -1;  // force the first opacity write on the fresh overlay

    switch (p->pattern) {
        case VA_PATTERN_SOLID:
            set_overlay_opa(s_max_opa);
            break;
        case VA_PATTERN_BLINK: {
            uint32_t half = s_period_ms / 2;
            if (half < 50) half = 50;  // floor guards absurd cadences
            s_blink_on = true;
            set_overlay_opa(s_max_opa);
            s_pulse_timer = lv_timer_create(on_pulse_timer, half, nullptr);
            break;
        }
        case VA_PATTERN_BREATHE:
        default:
            s_floor_opa = (uint8_t)(s_max_opa * 20 / 100);
            s_breathe_phase_ms = 0;
            set_overlay_opa(s_floor_opa);
            s_pulse_timer = lv_timer_create(on_pulse_timer, BREATHE_TICK_MS, nullptr);
            break;
    }

    // Auto-dismiss timer (0 = persist until stop/tap)
    if (p->duration_ms > 0) {
        s_dismiss_timer = lv_timer_create(on_dismiss_timer, p->duration_ms, nullptr);
        lv_timer_set_repeat_count(s_dismiss_timer, 1);
    }

    LOGI(TAG, "Show: color=#%06X pat=%u per=%u int=%u dur=%u",
         (unsigned)(p->color & 0xFFFFFF), p->pattern,
         (unsigned)s_period_ms, intensity, p->duration_ms);
}

// ---------------------------------------------------------------------------
// Public API (thread-safe via portMUX)
// ---------------------------------------------------------------------------

void visual_alert_show(const VisualAlertParams* params) {
    portENTER_CRITICAL(&s_mux);
    memcpy(&s_pending_params, params, sizeof(s_pending_params));
    s_pending_show = true;
    s_pending_stop = false;
    portEXIT_CRITICAL(&s_mux);
}

void visual_alert_stop() {
    portENTER_CRITICAL(&s_mux);
    s_pending_stop = true;
    s_pending_show = false;
    portEXIT_CRITICAL(&s_mux);
}

void visual_alert_loop() {
    bool do_show = false;
    bool do_stop = false;
    VisualAlertParams params;

    portENTER_CRITICAL(&s_mux);
    if (s_pending_show) {
        memcpy(&params, &s_pending_params, sizeof(params));
        s_pending_show = false;
        do_show = true;
    }
    if (s_pending_stop) {
        s_pending_stop = false;
        do_stop = true;
    }
    portEXIT_CRITICAL(&s_mux);

    if (!do_show && !do_stop) return;
    if (!displayManager) return;

    displayManager->lock();
    if (do_show) {
        create_overlay(&params);
    } else if (do_stop) {
        fade_out_and_destroy();
    }
    displayManager->unlock();
}

#endif // HAS_DISPLAY
