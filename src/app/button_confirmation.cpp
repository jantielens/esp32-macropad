#include "button_confirmation.h"

#if HAS_DISPLAY

#include "action_list.h"
#include "display_manager.h"
#include "log_manager.h"
#include "pad_layout.h"
#include "config_psram.h"

#include <lvgl.h>
#include <stdio.h>
#include <string.h>

static const char* TAG = "Confirm";
static const uint32_t CONFIRM_TIMEOUT_MS = 10000;

static lv_obj_t* s_overlay = nullptr;
static lv_timer_t* s_timeout_timer = nullptr;
static ButtonAction* s_actions = nullptr;
static uint8_t s_action_count = 0;
static char s_event_label[24];
static lv_obj_t* s_cancel_button = nullptr;
static lv_obj_t* s_confirm_button = nullptr;
static bool s_confirm_selected = false;

static void destroy_prompt() {
    s_cancel_button = nullptr;
    s_confirm_button = nullptr;
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
    action_list_dispatch(s_actions, count, s_event_label,
                         ACTION_CONTINUATION_OWNER_LVGL);
}

bool button_confirmation_navigate(const char* command) {
    if (!s_overlay) return false;
    lv_obj_set_style_outline_width(s_cancel_button, s_confirm_selected ? 0 : 3, 0);
    lv_obj_set_style_outline_width(s_confirm_button, s_confirm_selected ? 3 : 0, 0);
    if (strcmp(command, "tap") == 0) {
        if (s_confirm_selected) on_confirm(nullptr);
        else cancel_prompt();
    } else if (strcmp(command, "previous") == 0 || strcmp(command, "next") == 0) {
        s_confirm_selected = !s_confirm_selected;
        lv_obj_set_style_outline_width(s_cancel_button, s_confirm_selected ? 0 : 3, 0);
        lv_obj_set_style_outline_width(s_confirm_button, s_confirm_selected ? 3 : 0, 0);
    }
    return true;
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
    lv_obj_set_style_text_font(label, pad_typography_font(pad_get_scale_info().font_medium), 0);
    lv_obj_set_width(label, LV_PCT(100));
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    lv_obj_set_height(label, lv_font_get_line_height(lv_obj_get_style_text_font(label, LV_PART_MAIN)));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(label);
    return button;
}

bool button_confirmation_show(const ButtonAction* actions, uint8_t count,
                              const char* event_label, const char* confirm_text,
                              const char* button_label) {
    if (!actions || count == 0 || count > MAX_BUTTON_ACTIONS) return false;
    if (!displayManager || !lv_layer_top()) return false;

    if (!s_actions) {
        s_actions = static_cast<ButtonAction*>(config_psram_alloc(
            sizeof(ButtonAction) * MAX_BUTTON_ACTIONS, "button_confirmation"));
        if (!s_actions) return false;
    }
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
    const lv_font_t* title_font = pad_typography_font(pad_get_scale_info().font_large);
    const lv_font_t* body_font = pad_typography_font(pad_get_scale_info().font_medium);
    int title_height = lv_font_get_line_height(title_font);
    const int body_height = lv_font_get_line_height(body_font);
    int title_bottom = title_height + 28 > 62 ? title_height + 28 : 62;
    int button_height = screen_height < 300 ? 42 : 48;
    if (button_height < body_height + 16) button_height = body_height + 16;
    const int minimum_height = title_bottom + body_height * 2 + button_height + 36;
    if (dialog_height < minimum_height) dialog_height = minimum_height;
    if (dialog_height > screen_height * 85 / 100) dialog_height = screen_height * 85 / 100;
    if (button_height > dialog_height / 3) button_height = dialog_height / 3;
    const int title_limit = dialog_height / 3 - 28;
    if (title_height > title_limit) title_height = title_limit > 1 ? title_limit : 1;
    if (title_bottom > title_height + 28) title_bottom = title_height + 28;

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
    lv_obj_set_style_text_font(title, title_font, 0);
    lv_obj_set_width(title, dialog_width - 32);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_height(title, title_height);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 18);

    int button_width = (dialog_width - 48) / 2;

    lv_obj_t* message = lv_label_create(dialog);
    lv_label_set_text(message, prompt);
    lv_label_set_long_mode(message, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(message, dialog_width - 32);
    int message_height = dialog_height - title_bottom - button_height - 36;
    if (message_height < 1) message_height = 1;
    lv_obj_set_height(message, message_height);
    lv_obj_set_style_text_color(message, lv_color_hex(0xE8EAED), 0);
    lv_obj_set_style_text_font(message, body_font, 0);
    lv_obj_set_style_text_align(message, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(message, LV_ALIGN_TOP_MID, 0, title_bottom);

    lv_obj_t* cancel = create_button(dialog, "Cancel", lv_color_hex(0x5F6368), on_cancel);
    lv_obj_set_size(cancel, button_width, button_height);
    lv_obj_align(cancel, LV_ALIGN_BOTTOM_LEFT, 12, -12);

    lv_obj_t* confirm = create_button(dialog, "Confirm", lv_color_hex(0xB3261E), on_confirm);
    lv_obj_set_size(confirm, button_width, button_height);
    lv_obj_align(confirm, LV_ALIGN_BOTTOM_RIGHT, -12, -12);
    s_cancel_button = cancel;
    s_confirm_button = confirm;
    s_confirm_selected = false;
    lv_obj_set_style_outline_color(cancel, lv_color_white(), 0);
    lv_obj_set_style_outline_color(confirm, lv_color_white(), 0);

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