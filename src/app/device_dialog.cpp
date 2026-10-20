#include "device_dialog.h"

#if HAS_DISPLAY
#include "dialog_appearance.h"
#include "action_continuation.h"
#include "display_manager.h"
#include "fill_draw.h"
#include "log_manager.h"
#include "ota_activity.h"
#include "pad_layout.h"
#if HAS_TOUCH
#include "touch_manager.h"
#endif
#if HAS_BUTTON
#include "hw_button_config.h"
#endif
#include <algorithm>
#include <atomic>
#include <new>
#include <lvgl.h>

namespace {
struct DialogBackground {
    uint32_t color = 0;
    FillPattern pattern = FillPattern::None;
    uint8_t spacing = 8;
    uint8_t stroke = 1;
};

enum class DialogSelection : uint8_t { None, Cancel, Approve };

struct Dialog {
    lv_obj_t* overlay = nullptr;
    lv_obj_t* cancel = nullptr;
    lv_obj_t* approve = nullptr;
    PadFontCache fonts{1};
    DialogBackground background;
    uint32_t token = 0;
    uint32_t generation = 0;
    uint32_t deadline = 0;
    bool timed = false;
    DialogSelection selection = DialogSelection::None;
    ~Dialog() {
        if (overlay) lv_obj_delete(overlay);
        // fonts are released after all LVGL objects that reference them.
    }
};
Dialog* current = nullptr;
DialogTemplates templates;
std::atomic<uint32_t> input_generation{0};
uint32_t next_dialog_generation = 0;

bool confirmation_input_available() {
#if HAS_TOUCH
    if (touch_manager_contact_capacity() > 0) return true;
#endif
#if HAS_BUTTON
    bool select = false;
    bool tap = false;
    for (uint8_t i = 0; i < NUM_HW_BUTTONS; ++i) {
        const HwButtonConfig* config = hw_button_config_get(i);
        if (!config) continue;
        for (bool hold : {false, true}) {
            const ButtonAction* actions = hold ? config->hold_actions : config->tap_actions;
            const uint8_t count = hold ? config->hold_count : config->tap_count;
            for (uint8_t index = 0; index < count; ++index) {
                if (strcmp(actions[index].type, ACTION_TYPE_BUTTON_NAVIGATION)) continue;
                const char* command = actions[index].payload.button_navigation.command;
                select |= !strcmp(command, "next") || !strcmp(command, "previous");
                tap |= !strcmp(command, "tap");
                break;
            }
        }
    }
    return select && tap;
#else
    return false;
#endif
}

void reset_input() {
#if HAS_TOUCH
    touch_manager_cancel_physical_input();
#endif
    for (lv_indev_t* input = lv_indev_get_next(nullptr); input; input = lv_indev_get_next(input)) {
        lv_indev_reset(input, nullptr);
        lv_indev_wait_release(input);
    }
    ++input_generation;
}

void finish(bool approved, const char* reason) {
    Dialog* previous = current;
    if (!previous) return;
    current = nullptr;
    reset_input();
    const uint32_t token = previous->token;
    delete previous;
    if (token) {
        if (approved) action_continuation_complete(token, true);
        else action_continuation_release(token);
    }
    LOGI("Dialog", "%s", reason);
}

void click(lv_event_t* event) {
    const uint32_t generation = static_cast<uint32_t>(
        reinterpret_cast<uintptr_t>(lv_event_get_user_data(event)));
    if (!current || current->generation != generation) return;
    if (current->timed && static_cast<int32_t>(millis() - current->deadline) >= 0) {
        finish(false, "Dialog timed out; waiting confirmation cancelled");
        return;
    }
    lv_obj_t* target = lv_event_get_current_target_obj(event);
    if (!current->token) finish(false, "Notification dismissed");
    else if (target == current->approve) finish(true, "Confirmation approved");
    else if (target == current->cancel) finish(false, "Confirmation cancelled");
}

lv_obj_t* object(lv_obj_t* parent) {
    lv_obj_t* result = lv_obj_create(parent);
    if (result) {
        lv_obj_remove_style_all(result);
        lv_obj_remove_flag(result, LV_OBJ_FLAG_SCROLLABLE);
    }
    return result;
}

lv_point_t text_size(const char* text, const lv_font_t* font, int width) {
    lv_point_t size{};
    lv_text_get_size(&size, text, font, 0, 0, width, LV_TEXT_FLAG_NONE);
    return size;
}

void draw_background(lv_event_t* event) {
    const auto* background = static_cast<const DialogBackground*>(lv_event_get_user_data(event));
    lv_obj_t* surface = lv_event_get_current_target_obj(event);
    lv_area_t bounds;
    lv_obj_get_coords(surface, &bounds);
    const lv_point_t origin{bounds.x1, bounds.y1};
    fill_draw_pattern(lv_event_get_layer(event), bounds, lv_obj_get_style_radius(surface, LV_PART_MAIN),
        origin, background->pattern, lv_color_hex(background->color), background->spacing, background->stroke);
}

lv_obj_t* button(Dialog& dialog, lv_obj_t* parent, const char* text,
                 const DialogAppearance& style, const lv_font_t* font,
                 int width, int height, bool approve) {
    lv_obj_t* result = object(parent);
    if (!result) return nullptr;
    lv_obj_set_size(result, width, height);
    lv_obj_set_style_radius(result, style.corner_radius, 0);
    lv_obj_set_style_bg_color(result, lv_color_hex(approve ? style.approve_background : style.cancel_background), 0);
    lv_obj_set_style_bg_opa(result, LV_OPA_COVER, 0);
    lv_obj_set_style_outline_color(result, lv_color_hex(style.text_color), 0);
    lv_obj_set_style_outline_width(result, 0, 0);
    lv_obj_add_flag(result, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(result, click, LV_EVENT_CLICKED,
        reinterpret_cast<void*>(static_cast<uintptr_t>(dialog.generation)));
    lv_obj_t* label = lv_label_create(result);
    if (!label) return nullptr;
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(approve ? style.approve_color : style.cancel_color), 0);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(label, width - 16);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_label_set_text(label, text);
    const char* caption = lv_label_get_text(label);
    if (!caption || strcmp(caption, text)) return nullptr;
    lv_obj_center(label);
    return result;
}

bool create(Dialog& dialog, const char* message, const ConfirmPayload* confirmation,
            const DialogAppearance& style) {
    if (!displayManager || !lv_layer_top() || ota_activity_is_active() ||
        !message || !message[0] || strlen(message) >= CONFIG_DIALOG_MESSAGE_MAX_LEN) return false;
    const bool confirm = confirmation != nullptr;
    const int screen_width = displayManager->getActiveWidth();
    const int screen_height = displayManager->getActiveHeight();
    const DisplayContentRect safe = display_safe_content_rect(screen_width, screen_height);
    const int available_width = safe.w - 2 * style.margin;
    const int available_height = safe.h - 2 * style.margin;
    if (available_width <= 0 || available_height <= 0) return false;
    const int width = std::min(available_width, screen_width * (style.width ? style.width : 80) / 100);
    const int inset = style.padding + style.border_width;
    const int text_width = width - 2 * inset;
    if (text_width < 1) return false;

    LabelStyle font_style{};
    font_style.font_size = style.font_size;
    // Explicitly copy the device default into the dialog-owned font cache too.
    strlcpy(font_style.font_name, style.font[0] ? style.font : device_typography_get().font_name,
            sizeof(font_style.font_name));
    if (!font_style.font_name[0]) strlcpy(font_style.font_name, "@builtin", sizeof(font_style.font_name));
    const lv_font_t* font = pad_resolve_font(font_style, pad_get_scale_info().font_large, &dialog.fonts);
    if (!font) return false;
    const int line_height = lv_font_get_line_height(font);
    int button_width = 0;
    int button_height = 0;
    int buttons_space = 0;
    if (confirm) {
        if (!confirmation->approve_text[0] || !confirmation->cancel_text[0]) return false;
        if (text_width < 96) return false; // two usable 44px controls and the 8px gap
        const int row_width = text_width - 8;
        button_width = style.button_width ? row_width * style.button_width / 100 : row_width / 2;
        button_width = std::max(44, std::min(row_width / 2, button_width));
        const int captions_height = std::max(
            text_size(confirmation->approve_text, font, button_width - 16).y,
            text_size(confirmation->cancel_text, font, button_width - 16).y);
        button_height = style.button_height ? style.button_height : std::max(44, captions_height + 16);
        if (captions_height + 16 > button_height) return false;
        buttons_space = button_height + 8;
    }
    const int minimum_height = 2 * inset + buttons_space + line_height;
    if (minimum_height > available_height) return false;
    const int content_height = text_size(message, font, text_width).y;
    int height = style.height ? screen_height * style.height / 100
                             : 2 * inset + buttons_space + content_height;
    height = std::min(available_height, std::max(minimum_height, height));
    const int message_height = height - 2 * inset - buttons_space;

    dialog.overlay = object(lv_layer_top());
    if (!dialog.overlay) return false;
    lv_obj_add_flag(dialog.overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(dialog.overlay, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(dialog.overlay, screen_width, screen_height);
    lv_obj_set_style_bg_color(dialog.overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(dialog.overlay, style.backdrop_opacity * 255 / 100, 0);
    if (!confirm) lv_obj_add_event_cb(dialog.overlay, click, LV_EVENT_CLICKED,
        reinterpret_cast<void*>(static_cast<uintptr_t>(dialog.generation)));
    lv_obj_t* box = object(dialog.overlay);
    if (!box) return false;
    lv_obj_add_flag(box, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_size(box, width, height);
    lv_obj_set_x(box, safe.x + (safe.w - width) / 2);
    lv_obj_set_y(box, safe.y + (style.position == 0 ? style.margin
        : style.position == 2 ? safe.h - style.margin - height : (safe.h - height) / 2));
    lv_obj_set_style_bg_color(box, lv_color_hex(style.background_color), 0);
    lv_obj_set_style_bg_opa(box, style.background_opacity * 255 / 100, 0);
    lv_obj_set_style_border_color(box, lv_color_hex(style.border_color), 0);
    lv_obj_set_style_border_width(box, style.border_width, 0);
    lv_obj_set_style_border_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, style.corner_radius, 0);

    if (style.background_pattern != FillPattern::None && style.background_opacity) {
        lv_obj_t* background = object(box);
        if (!background) return false;
        lv_obj_remove_flag(background, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_set_pos(background, 0, 0);
        lv_obj_set_size(background, width - 2 * style.border_width, height - 2 * style.border_width);
        lv_obj_set_style_radius(background, std::max(0, static_cast<int>(style.corner_radius) - style.border_width), 0);
        lv_obj_set_style_bg_color(background, lv_color_hex(style.background_color), 0);
        lv_obj_set_style_bg_opa(background, LV_OPA_COVER, 0);
        // Blend the base and pattern together, without fading the border, text, or buttons.
        lv_obj_set_style_opa_layered(background, style.background_opacity * 255 / 100, 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
        dialog.background.color = style.background_pattern_color;
        dialog.background.pattern = style.background_pattern;
        dialog.background.spacing = style.pattern_spacing;
        dialog.background.stroke = style.pattern_stroke_width;
        lv_obj_add_event_cb(background, draw_background, LV_EVENT_DRAW_MAIN_END, &dialog.background);
    }

    lv_obj_t* scroll = object(box);
    if (!scroll) return false;
    lv_obj_set_pos(scroll, style.padding, style.padding);
    lv_obj_set_size(scroll, text_width, message_height);
    lv_obj_add_flag(scroll, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(scroll, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_scroll_dir(scroll, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(scroll, LV_SCROLLBAR_MODE_AUTO);
    lv_obj_t* text = lv_label_create(scroll);
    if (!text) return false;
    lv_obj_add_flag(text, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_set_style_text_font(text, font, 0);
    lv_obj_set_style_text_color(text, lv_color_hex(style.text_color), 0);
    lv_obj_set_style_text_align(text, style.horizontal_align == 0 ? LV_TEXT_ALIGN_LEFT :
        style.horizontal_align == 2 ? LV_TEXT_ALIGN_RIGHT : LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(text, text_width);
    lv_label_set_long_mode(text, LV_LABEL_LONG_WRAP);
    lv_label_set_text(text, message);
    const char* displayed = lv_label_get_text(text);
    if (!displayed || strcmp(displayed, message)) return false;
    const int slack = std::max(0, message_height - content_height);
    lv_obj_set_y(text, style.vertical_align == 0 ? 0 : style.vertical_align == 2 ? slack : slack / 2);
    if (confirm) {
        dialog.cancel = button(dialog, box, confirmation->cancel_text, style, font, button_width, button_height, false);
        dialog.approve = button(dialog, box, confirmation->approve_text, style, font, button_width, button_height, true);
        if (!dialog.cancel || !dialog.approve) return false;
        const int x = (text_width - 2 * button_width - 8) / 2 + style.padding;
        const int y = height - 2 * style.border_width - style.padding - button_height;
        lv_obj_set_pos(dialog.cancel, x, y);
        lv_obj_set_pos(dialog.approve, x + button_width + 8, y);
    }
    return true;
}

bool show(const char* message, uint32_t duration, const ConfirmPayload* confirmation, uint32_t token,
          const DialogAppearance* test_appearance = nullptr) {
    if (!displayManager) return false;
    bool locked = false;
    display_manager_lock_if_needed(&locked);
    Dialog* candidate = new (std::nothrow) Dialog;
    bool success = false;
    if (candidate) {
        if (++next_dialog_generation == 0) ++next_dialog_generation;
        candidate->generation = next_dialog_generation;
        const DialogAppearance appearance = test_appearance ? *test_appearance :
            confirmation ? templates.confirmation : templates.notification;
        success = create(*candidate, message, confirmation, appearance);
        if (success) {
            finish(false, "Dialog replaced; waiting confirmation cancelled");
            reset_input();
            candidate->token = token;
            candidate->timed = duration != 0;
            candidate->deadline = millis() + duration;
            current = candidate;
            lv_obj_remove_flag(candidate->overlay, LV_OBJ_FLAG_HIDDEN);
        } else {
            delete candidate;
        }
    }
    display_manager_unlock_if_needed(locked);
    if (!success) LOGW("Dialog", "Display/layout/resource unavailable; existing dialog preserved");
    return success;
}
}

bool device_dialog_notify(const char* message, uint32_t duration_ms) {
    if (duration_ms && (duration_ms < 1000 || duration_ms > 300000)) {
        LOGW("Dialog", "Invalid notification duration");
        return false;
    }
    return show(message, duration_ms, nullptr, 0);
}

bool device_dialog_confirm(const ConfirmPayload& content, uint32_t token) {
    if (!token || !confirmation_input_available()) {
        LOGW("Dialog", "Confirmation has no usable input; sequence stopped");
        return false;
    }
    return show(content.message, content.timeout_ms, &content, token);
}

bool device_dialog_test(bool confirmation, const DialogAppearance& appearance) {
    if (!confirmation)
        return show("Scene activated.\nYour room is ready.", 10000, nullptr, 0, &appearance);
    if (!confirmation_input_available()) {
        LOGW("Dialog", "Test confirmation has no usable input");
        return false;
    }
    uint32_t token = 0;
    if (!action_continuation_begin(nullptr, 0, "Dialog appearance test", &token)) {
        LOGW("Dialog", "No continuation slot for test confirmation");
        return false;
    }
    ConfirmPayload content{};
    strlcpy(content.message, "Restart the device?\nUnsaved work may be lost.", sizeof(content.message));
    strlcpy(content.cancel_text, "Cancel", sizeof(content.cancel_text));
    strlcpy(content.approve_text, "Continue", sizeof(content.approve_text));
    content.timeout_ms = 10000;
    if (!action_continuation_set_timeout(token, 0) ||
        !show(content.message, content.timeout_ms, &content, token, &appearance)) {
        action_continuation_release(token);
        return false;
    }
    action_continuation_mark_pending(token);
    return true;
}

void device_dialog_close() {
    if (!displayManager) return;
    bool locked = false;
    display_manager_lock_if_needed(&locked);
    finish(false, "Dialog closed; waiting confirmation cancelled");
    display_manager_unlock_if_needed(locked);
}

void device_dialog_dismiss_notification() {
    if (!displayManager) return;
    bool locked = false;
    display_manager_lock_if_needed(&locked);
    if (current && !current->token) finish(false, "Notification dismissed");
    display_manager_unlock_if_needed(locked);
}

uint32_t device_dialog_input_generation() { return input_generation.load(); }

bool device_dialog_active() {
    if (!displayManager) return false;
    bool locked = false;
    display_manager_lock_if_needed(&locked);
    const bool active = current != nullptr;
    display_manager_unlock_if_needed(locked);
    return active;
}

bool device_dialog_navigate(const char* command) {
    if (!current) return false;
    if (current->timed && static_cast<int32_t>(millis() - current->deadline) >= 0) {
        finish(false, "Dialog timed out; waiting confirmation cancelled");
        return true;
    }
    if (!current->token) {
        if (!strcmp(command, "tap") || !strcmp(command, "tap_hold"))
            finish(false, "Notification dismissed");
    } else if (!strcmp(command, "tap") && current->selection != DialogSelection::None) {
        const bool approved = current->selection == DialogSelection::Approve;
        finish(approved, approved ? "Confirmation approved" : "Confirmation cancelled");
    } else if (!strcmp(command, "next") || !strcmp(command, "previous") || !strcmp(command, "tap")) {
        if (current->selection == DialogSelection::None)
            current->selection = !strcmp(command, "previous") ? DialogSelection::Approve : DialogSelection::Cancel;
        else
            current->selection = current->selection == DialogSelection::Cancel ? DialogSelection::Approve : DialogSelection::Cancel;
        lv_obj_set_style_outline_width(current->cancel, current->selection == DialogSelection::Cancel ? 3 : 0, 0);
        lv_obj_set_style_outline_width(current->approve, current->selection == DialogSelection::Approve ? 3 : 0, 0);
    }
    return true;
}

void device_dialog_loop() {
    if (current && current->timed && static_cast<int32_t>(millis() - current->deadline) >= 0)
        finish(false, "Dialog timed out; waiting confirmation cancelled");
}

void device_dialog_set_templates(const DialogTemplates& value) {
    bool locked = false;
    display_manager_lock_if_needed(&locked);
    templates = value;
    display_manager_unlock_if_needed(locked);
}
#endif
