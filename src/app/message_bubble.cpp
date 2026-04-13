#include "message_bubble.h"

#if HAS_DISPLAY

#include "display_manager.h"
#include "pad_layout.h"
#include "log_manager.h"

#include <lvgl.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <string.h>

static const char* TAG = "Bubble";

// ---------------------------------------------------------------------------
// Shared location string parser
// ---------------------------------------------------------------------------
uint8_t notify_location_from_str(const char* loc) {
    if (loc && strcmp(loc, "top") == 0) return NOTIFY_LOC_TOP;
    if (loc && strcmp(loc, "center") == 0) return NOTIFY_LOC_CENTER;
    return NOTIFY_LOC_BOTTOM;
}

// ---------------------------------------------------------------------------
// Cross-task signaling (API callers → loop in main task)
// ---------------------------------------------------------------------------
static portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
static bool s_pending_show = false;
static bool s_pending_dismiss = false;
static MessageBubbleParams s_pending_params;

// ---------------------------------------------------------------------------
// LVGL state (only touched from main loop under LVGL mutex)
// ---------------------------------------------------------------------------
static lv_obj_t* s_container = nullptr;
static lv_obj_t* s_label = nullptr;
static lv_timer_t* s_dismiss_timer = nullptr;

// Layout constants
static const lv_coord_t BUBBLE_RADIUS = 12;
static const lv_coord_t BUBBLE_SHADOW = 8;
static const uint32_t   FADE_MS = 200;

// ---------------------------------------------------------------------------
// Internal helpers (must be called under LVGL mutex)
// ---------------------------------------------------------------------------

static void destroy_bubble() {
    if (s_dismiss_timer) {
        lv_timer_delete(s_dismiss_timer);
        s_dismiss_timer = nullptr;
    }
    if (s_container) {
        lv_obj_delete(s_container);
        s_container = nullptr;
        s_label = nullptr;
    }
}

static void on_dismiss_timer(lv_timer_t* timer) {
    (void)timer;
    // Fade out then destroy
    if (s_container) {
        lv_anim_t a;
        lv_anim_init(&a);
        lv_anim_set_var(&a, s_container);
        lv_anim_set_values(&a, lv_obj_get_style_opa(s_container, LV_PART_MAIN), LV_OPA_TRANSP);
        lv_anim_set_duration(&a, FADE_MS);
        lv_anim_set_exec_cb(&a, [](void* obj, int32_t v) {
            lv_obj_set_style_opa((lv_obj_t*)obj, (lv_opa_t)v, 0);
        });
        lv_anim_set_completed_cb(&a, [](lv_anim_t* anim) {
            (void)anim;
            destroy_bubble();
        });
        lv_anim_start(&a);
    }
    s_dismiss_timer = nullptr;
}

static void on_bubble_clicked(lv_event_t* e) {
    (void)e;
    destroy_bubble();
    LOGI(TAG, "Dismissed by tap");
}

static void create_bubble(const MessageBubbleParams* p) {
    // Destroy any existing bubble first
    destroy_bubble();

    if (!displayManager) return;
    lv_obj_t* layer = lv_layer_top();
    if (!layer) return;

    int scr_w = displayManager->getActiveWidth();
    int scr_h = displayManager->getActiveHeight();

    // Container
    s_container = lv_obj_create(layer);
    lv_obj_remove_style_all(s_container);

    // Dynamic padding scaled to screen size (floor: 12h / 8v)
    int pad_h = scr_w * 4 / 100;
    if (pad_h < 12) pad_h = 12;
    int pad_v = pad_h * 2 / 3;
    if (pad_v < 8) pad_v = 8;

    // Size: auto-width based on content, min 40% / max 85% of screen
    lv_obj_set_width(s_container, LV_SIZE_CONTENT);
    lv_obj_set_style_min_width(s_container, scr_w * 40 / 100, 0);
    lv_obj_set_style_max_width(s_container, scr_w * 85 / 100, 0);
    lv_obj_set_height(s_container, LV_SIZE_CONTENT);

    // Padding
    lv_obj_set_style_pad_left(s_container, pad_h, 0);
    lv_obj_set_style_pad_right(s_container, pad_h, 0);
    lv_obj_set_style_pad_top(s_container, pad_v, 0);
    lv_obj_set_style_pad_bottom(s_container, pad_v, 0);

    // Background
    uint8_t opa = (p->opacity > 0) ? p->opacity : 85;
    lv_obj_set_style_bg_color(s_container, lv_color_hex(p->bg_color), 0);
    lv_obj_set_style_bg_opa(s_container, (lv_opa_t)(opa * 255 / 100), 0);
    lv_obj_set_style_radius(s_container, BUBBLE_RADIUS, 0);

    // Shadow
    lv_obj_set_style_shadow_width(s_container, BUBBLE_SHADOW, 0);
    lv_obj_set_style_shadow_opa(s_container, LV_OPA_30, 0);
    lv_obj_set_style_shadow_color(s_container, lv_color_black(), 0);

    // Border
    if (p->has_border) {
        lv_obj_set_style_border_color(s_container, lv_color_hex(p->border_color), 0);
        lv_obj_set_style_border_width(s_container, 1, 0);
        lv_obj_set_style_border_opa(s_container, LV_OPA_COVER, 0);
    }

    // Position: horizontally centered, vertical per location
    lv_obj_set_align(s_container, LV_ALIGN_TOP_MID);
    switch (p->location) {
        case NOTIFY_LOC_TOP:
            lv_obj_set_y(s_container, scr_h / 10);
            break;
        case NOTIFY_LOC_CENTER:
            lv_obj_set_align(s_container, LV_ALIGN_CENTER);
            break;
        case NOTIFY_LOC_BOTTOM:
        default:
            lv_obj_set_align(s_container, LV_ALIGN_BOTTOM_MID);
            lv_obj_set_y(s_container, -(scr_h * 15 / 100));
            break;
    }

    // Label
    s_label = lv_label_create(s_container);
    lv_label_set_text(s_label, p->text);
    lv_obj_set_style_text_color(s_label, lv_color_hex(p->text_color), 0);
    lv_label_set_long_mode(s_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(s_label, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(s_label, scr_w * 85 / 100 - 2 * pad_h, 0);
    lv_obj_set_style_text_align(s_label, LV_TEXT_ALIGN_CENTER, 0);

    // Font: explicit override or scale-tier default (matches center label on buttons)
    if (p->font_size > 0) {
        const lv_font_t* font = pad_font_by_size(p->font_size);
        if (font) lv_obj_set_style_text_font(s_label, font, 0);
    } else {
        lv_obj_set_style_text_font(s_label, pad_get_scale_info().font_large, 0);
    }

    // Tap to dismiss
    lv_obj_add_flag(s_container, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_container, on_bubble_clicked, LV_EVENT_CLICKED, nullptr);

    // Start invisible, fade in
    lv_obj_set_style_opa(s_container, LV_OPA_TRANSP, 0);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_container);
    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_duration(&a, FADE_MS);
    lv_anim_set_exec_cb(&a, [](void* obj, int32_t v) {
        lv_obj_set_style_opa((lv_obj_t*)obj, (lv_opa_t)v, 0);
    });
    lv_anim_start(&a);

    // Auto-dismiss timer
    if (p->duration_ms > 0) {
        s_dismiss_timer = lv_timer_create(on_dismiss_timer, p->duration_ms, nullptr);
        lv_timer_set_repeat_count(s_dismiss_timer, 1);
    }

    LOGI(TAG, "Show: '%s' dur=%u loc=%u", p->text, p->duration_ms, p->location);
}

// ---------------------------------------------------------------------------
// Public API (thread-safe via portMUX)
// ---------------------------------------------------------------------------

void message_bubble_show(const MessageBubbleParams* params) {
    portENTER_CRITICAL(&s_mux);
    memcpy(&s_pending_params, params, sizeof(s_pending_params));
    s_pending_show = true;
    s_pending_dismiss = false;
    portEXIT_CRITICAL(&s_mux);
}

void message_bubble_dismiss() {
    portENTER_CRITICAL(&s_mux);
    s_pending_dismiss = true;
    s_pending_show = false;
    portEXIT_CRITICAL(&s_mux);
}

void message_bubble_loop() {
    // Check pending flags under spinlock, copy params if needed
    bool do_show = false;
    bool do_dismiss = false;
    MessageBubbleParams params;

    portENTER_CRITICAL(&s_mux);
    if (s_pending_show) {
        memcpy(&params, &s_pending_params, sizeof(params));
        s_pending_show = false;
        do_show = true;
    }
    if (s_pending_dismiss) {
        s_pending_dismiss = false;
        do_dismiss = true;
    }
    portEXIT_CRITICAL(&s_mux);

    if (!do_show && !do_dismiss) return;
    if (!displayManager) return;

    displayManager->lock();
    if (do_show) {
        if (!params.text[0]) {
            // Empty text = dismiss
            destroy_bubble();
            LOGI(TAG, "Dismiss (empty text)");
        } else {
            create_bubble(&params);
        }
    } else if (do_dismiss) {
        destroy_bubble();
        LOGI(TAG, "Dismiss (explicit)");
    }
    displayManager->unlock();
}

#endif // HAS_DISPLAY
