#include "button_confirmation.h"

#if HAS_DISPLAY

#include "action_list.h"
#include "display_manager.h"
#include "log_manager.h"
#include "pad_layout.h"

#include <lvgl.h>
#include <stdio.h>
#include <string.h>

static const char* TAG = "Confirm";
static const uint32_t CONFIRM_TIMEOUT_MS = 10000;

static lv_obj_t* s_overlay = nullptr;
static lv_timer_t* s_timeout_timer = nullptr;
static ButtonAction s_actions[MAX_BUTTON_ACTIONS];
static uint8_t s_action_count = 0;
static char s_event_label[24];

static void destroy_prompt() {
    if (s_timeout_timer) {
        lv_timer_delete(s_timeout_timer);
        s_timeout_timer = nullptr;
    }
    if (s_overlay) {
        lv_obj_delete(s_overlay);
        s_overlay = nullptr;
    }
}

static void cancel_prompt() {
    if (!s_overlay && s_action_count == 0) return;
    destroy_prompt();
    s_action_count = 0;
    LOGI(TAG, "Cancelled");
}

void button_confirmation_cancel() {
    cancel_prompt();
}

static void on_timeout(lv_timer_t* timer) {
    (void)timer;
    s_timeout_timer = nullptr;
    cancel_prompt();
}

static void on_cancel(lv_event_t* event) {
    (void)event;
    cancel_prompt();
}

static void on_confirm(lv_event_t* event) {
    (void)event;
    uint8_t count = s_action_count;
    s_action_count = 0;
    destroy_prompt();
    LOGI(TAG, "Confirmed; dispatching %u action(s)", count);
    action_list_dispatch(s_actions, count, s_event_label);
}

static lv_obj_t* create_button(lv_obj_t* parent, const char* text,
                               lv_color_t bg_color, lv_event_cb_t callback) {
    lv_obj_t* button = lv_obj_create(parent);
    lv_obj_remove_style_all(button);
    lv_obj_set_style_bg_color(button, bg_color, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(button, 8, 0);
    lv_obj_add_flag(button, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(button, callback, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* label = lv_label_create(button);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, pad_get_scale_info().font_medium, 0);
    lv_obj_center(label);
    return button;
}

bool button_confirmation_show(const ButtonAction* actions, uint8_t count,
                              const char* event_label, const char* confirm_text,
                              const char* button_label) {
    if (!actions || count == 0 || count > MAX_BUTTON_ACTIONS) return false;
    if (!displayManager || !lv_layer_top()) return false;

    destroy_prompt();
    memcpy(s_actions, actions, count * sizeof(ButtonAction));
    s_action_count = count;
    strlcpy(s_event_label, event_label ? event_label : "Button", sizeof(s_event_label));

    char prompt[CONFIG_CONFIRM_TEXT_MAX_LEN];
    if (confirm_text && confirm_text[0]) {
        strlcpy(prompt, confirm_text, sizeof(prompt));
    } else if (button_label && button_label[0]) {
        snprintf(prompt, sizeof(prompt), "Run \"%s\"?", button_label);
    } else {
        strlcpy(prompt, "Execute this button action?", sizeof(prompt));
    }

    int screen_width = displayManager->getActiveWidth();
    int screen_height = displayManager->getActiveHeight();
    int dialog_width = screen_width * 86 / 100;
    if (dialog_width > 560) dialog_width = 560;
    int dialog_height = screen_height * 52 / 100;
    if (dialog_height < 180) dialog_height = 180;
    if (dialog_height > screen_height * 85 / 100) dialog_height = screen_height * 85 / 100;

    s_overlay = lv_obj_create(lv_layer_top());
    if (!s_overlay) {
        s_action_count = 0;
        return false;
    }
    lv_obj_remove_style_all(s_overlay);
    lv_obj_set_size(s_overlay, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_overlay, LV_OPA_60, 0);
    lv_obj_add_flag(s_overlay, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t* dialog = lv_obj_create(s_overlay);
    lv_obj_remove_style_all(dialog);
    lv_obj_set_size(dialog, dialog_width, dialog_height);
    lv_obj_center(dialog);
    lv_obj_set_style_bg_color(dialog, lv_color_hex(0x202124), 0);
    lv_obj_set_style_bg_opa(dialog, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(dialog, lv_color_hex(0x5F6368), 0);
    lv_obj_set_style_border_width(dialog, 1, 0);
    lv_obj_set_style_radius(dialog, 8, 0);

    lv_obj_t* title = lv_label_create(dialog);
    lv_label_set_text(title, "Confirm action");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, pad_get_scale_info().font_large, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    int button_width = (dialog_width - 48) / 2;
    int button_height = screen_height < 300 ? 42 : 48;

    lv_obj_t* message = lv_label_create(dialog);
    lv_label_set_text(message, prompt);
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(message, dialog_width - 32);
    int message_height = dialog_height - 62 - button_height - 36;
    if (message_height < 36) message_height = 36;
    lv_obj_set_height(message, message_height);
    lv_obj_set_style_text_color(message, lv_color_hex(0xE8EAED), 0);
    lv_obj_set_style_text_font(message, pad_get_scale_info().font_medium, 0);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(message, LV_ALIGN_TOP_MID, 0, 62);

    lv_obj_t* cancel = create_button(dialog, "Cancel", lv_color_hex(0x5F6368), on_cancel);
    lv_obj_set_size(cancel, button_width, button_height);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 12, -12);

    lv_obj_t* confirm = create_button(dialog, "Confirm", lv_color_hex(0xB3261E), on_confirm);
    lv_obj_set_size(confirm, button_width, button_height);
    lv_obj_align(confirm, LV_ALIGN_BOTTOM_RIGHT, -12, -12);

    s_timeout_timer = lv_timer_create(on_timeout, CONFIRM_TIMEOUT_MS, nullptr);
    if (!s_timeout_timer) {
        cancel_prompt();
        return false;
    }
    lv_timer_set_repeat_count(s_timeout_timer, 1);
    LOGI(TAG, "Prompt shown for %u action(s)", count);
    return true;
}

#endif // HAS_DISPLAY