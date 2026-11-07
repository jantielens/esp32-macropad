#include "option_selector_widget.h"

#if HAS_DISPLAY

#include "option_selector_config.h"
#include "widget_value.h"
#include "../action_list.h"
#include "../action_registry.h"
#include "../action_validate.h"
#include "../device_dialog.h"
#include "../icon_store.h"
#include "../log_manager.h"
#include "../pad_overlay.h"
#include "../swipe_actions.h"
#include <new>
#include <stdio.h>
#include <string.h>

namespace option_selector_widget_impl {

constexpr uint8_t NO_SLOT = UINT8_MAX;

struct SlotState {
    lv_obj_t* hit;
    lv_obj_t* badge;
    lv_obj_t* content;
    uint32_t foreground;
    uint32_t background;
    uint8_t state;
    bool icon_available;
    bool shown;
    bool background_visible;
};

struct State {
    const OptionSelectorConfig* config;
    const ButtonAction* actions;
    lv_obj_t* parent;
    SlotState slots[OPTION_SELECTOR_SLOTS];
    uint32_t group_color;
    uint32_t pending_since;
    uint8_t action_count;
    uint8_t selected;
    uint8_t pending;
    uint8_t pressed;
    bool overlay;
    bool available;
    bool layout_dirty;
};

static_assert(sizeof(State) <= WIDGET_STATE_MAX_BYTES, "Option selector state exceeds widget storage");

const char* validate(JsonObjectConst button) {
    const char* error = option_selector_validate_config(button);
    if (error) return error;
    if (button.containsKey("lp_actions") &&
        (!button["lp_actions"].is<JsonArrayConst>() || button["lp_actions"].size()))
        return "option selector does not support long-press actions";
    return nullptr;
}

const char* validate_actions(JsonObjectConst button, JsonArrayConst actions) {
    bool any_option = false;
    for (unsigned index = 0; index < OPTION_SELECTOR_SLOTS; ++index) {
        char field[40];
        if (index == 0) snprintf(field, sizeof(field), "widget_option_label");
        else snprintf(field, sizeof(field), "widget_option_label_%u", index + 1);
        const char* label = button[field] | "";
        if (index == 0) snprintf(field, sizeof(field), "widget_option_icon");
        else snprintf(field, sizeof(field), "widget_option_icon_%u", index + 1);
        if (!label[0] && button[field].isNull()) continue;
        any_option = true;
        if (index == 0) snprintf(field, sizeof(field), "widget_option_value");
        else snprintf(field, sizeof(field), "widget_option_value_%u", index + 1);
        const char* value = button[field] | "";
        for (JsonObjectConst action : actions) {
            const char* error = action_validate_json(action, true, "{value}", value);
            if (error) return error;
        }
    }
    if (!any_option) {
        for (JsonObjectConst action : actions) {
            const char* error = action_validate_json(action, true, "{value}", "value");
            if (error) return error;
        }
    }
    return nullptr;
}

lv_color_t color(uint32_t rgb) {
    return lv_color_make((rgb >> 16) & 0xff, (rgb >> 8) & 0xff, rgb & 0xff);
}

void mark_layout(lv_event_t* event) {
    auto* state = static_cast<State*>(lv_event_get_user_data(event));
    if (state) state->layout_dirty = true;
}

void layout(State* state) {
    if (!state->parent) return;
    lv_obj_update_layout(state->parent);
    const int width = lv_obj_get_content_width(state->parent);
    const int height = lv_obj_get_content_height(state->parent);
    unsigned count = 0;
    for (const auto& slot : state->config->slots)
        if (slot.kind != OptionSelectorContent::None) ++count;
    unsigned position = 0;
    int group_radius = LV_RADIUS_CIRCLE;
    for (unsigned index = 0; index < OPTION_SELECTOR_SLOTS; ++index) {
        const auto& config = state->config->slots[index];
        SlotState& slot = state->slots[index];
        if (!slot.hit || config.kind == OptionSelectorContent::None) continue;
        const int start = count && width > 0 ? width * position / count : 0;
        const int end = count && width > 0 ? width * (position + 1) / count : 0;
        ++position;
        const int cell_width = end - start;
        if (cell_width <= 0 || height <= 0) {
            lv_obj_add_flag(slot.hit, LV_OBJ_FLAG_HIDDEN);
            slot.shown = false;
            continue;
        }
        lv_obj_set_pos(slot.hit, start, 0);
        lv_obj_set_size(slot.hit, cell_width, height);
        const int inset = state->config->option_inset;
        const int inset_x = inset < cell_width / 2 ? inset : (cell_width - 1) / 2;
        const int inset_y = inset < height / 2 ? inset : (height - 1) / 2;
        const int inner_width = cell_width - 2 * inset_x;
        const int inner_height = height - 2 * inset_y;
        const int badge_size = inner_width < inner_height ? inner_width : inner_height;
        if (badge_size / 2 < group_radius) group_radius = badge_size / 2;
        const bool icon = config.kind == OptionSelectorContent::Icon;
        lv_obj_set_size(slot.badge, inner_width, inner_height);
        lv_obj_center(slot.badge);
        if (icon && slot.icon_available && lv_obj_check_type(slot.content, &lv_image_class)) {
            const auto* image = static_cast<const lv_image_dsc_t*>(lv_image_get_src(slot.content));
            const int pad = state->config->icon_badge_padding;
            int usable_size = badge_size - 2 * pad;
            if (usable_size < 1) usable_size = 1;
            if (state->config->icon_size_pct)
                usable_size = usable_size * state->config->icon_size_pct / 100;
            if (image && image->header.w && image->header.h) {
                uint32_t sx = static_cast<uint32_t>(usable_size) * LV_SCALE_NONE / image->header.w;
                uint32_t sy = static_cast<uint32_t>(usable_size) * LV_SCALE_NONE / image->header.h;
                uint32_t scale = sx < sy ? sx : sy;
                lv_image_set_scale(slot.content, scale ? scale : 1);
            }
        } else if (slot.content) {
            lv_obj_set_width(slot.content, inner_width);
        }
        if (slot.content) lv_obj_center(slot.content);
    }
    lv_obj_set_style_radius(state->parent, group_radius, 0);
    state->layout_dirty = false;
}

void render(State* state) {
    if (!state->parent) return;
    const auto& config = *state->config;
    const uint32_t track = widget_default_rgb(DEFAULT_WIDGET_TRACK_COLOR);
    const uint32_t default_fg = lv_color_to_u32(lv_obj_get_style_text_color(state->parent, LV_PART_MAIN)) & 0xFFFFFFu;
    const uint32_t selected_fg = resolve_color(config.selected_foreground, default_fg);
    const uint32_t selected_bg = resolve_color(config.selected_background, track);
    const uint32_t group = resolve_color(config.group_color, track);
    const uint32_t option_bg = resolve_color(config.option_background, track);
    if (state->group_color != group) {
        state->group_color = group;
        lv_obj_set_style_bg_color(state->parent, color(group), 0);
    }
    for (unsigned index = 0; index < OPTION_SELECTOR_SLOTS; ++index) {
        const auto& option = config.slots[index];
        SlotState& slot = state->slots[index];
        if (!slot.hit) continue;
        char resolved[CONFIG_LABEL_MAX_LEN];
        bool valid = true;
        if (option.state[0])
            valid = binding_template_resolve_checked(option.state, resolved, sizeof(resolved));
        else resolved[0] = '\0';
        const BtnState button_state = pad_overlay_resolve_state(resolved, valid, state->overlay, option.state[0]);
        const bool shown = button_state != BTN_STATE_HIDDEN &&
            lv_obj_get_width(slot.hit) > 0 && lv_obj_get_height(slot.hit) > 0;
        if (slot.shown != shown) {
            slot.shown = shown;
            if (shown) lv_obj_clear_flag(slot.hit, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(slot.hit, LV_OBJ_FLAG_HIDDEN);
        }
        const uint8_t status = button_state == BTN_STATE_ENABLED && slot.icon_available && state->available &&
                               state->pending == NO_SLOT
            ? BTN_STATE_ENABLED : BTN_STATE_DISABLED;
        if (slot.state != status) {
            slot.state = status;
            lv_obj_set_style_opa(slot.hit, status == BTN_STATE_ENABLED ? LV_OPA_COVER : LV_OPA_50, 0);
            if (status != BTN_STATE_ENABLED && state->pressed == index) state->pressed = NO_SLOT;
        }
        const bool selected = state->selected == index;
        const uint32_t foreground = resolve_color(option.foreground, selected ? selected_fg : default_fg);
        const uint32_t background = resolve_color(option.background, selected ? selected_bg : option_bg);
        const bool background_visible = config.option_backgrounds || selected || state->pressed == index;
        if (slot.foreground != foreground) {
            slot.foreground = foreground;
            if (option.kind == OptionSelectorContent::Icon && slot.icon_available && option.content.icon.mono)
                lv_obj_set_style_image_recolor(slot.content, color(foreground), 0);
            else if (slot.content)
                lv_obj_set_style_text_color(slot.content, color(foreground), 0);
        }
        if (slot.background != background) {
            slot.background = background;
            lv_obj_set_style_bg_color(slot.badge, color(background), 0);
        }
        if (slot.background_visible != background_visible) {
            slot.background_visible = background_visible;
            lv_obj_set_style_bg_opa(slot.badge, background_visible ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        }
        if (option.kind == OptionSelectorContent::Label && slot.content) {
            binding_template_resolve(option.content.label, resolved, sizeof(resolved));
            if (strcmp(lv_label_get_text(slot.content), resolved)) lv_label_set_text(slot.content, resolved);
        }
    }
}

void cancel(State* state) {
    if (state->pressed == NO_SLOT) return;
    state->pressed = NO_SLOT;
    render(state);
}

void delete_guard(lv_event_t* event) {
    *static_cast<bool*>(lv_event_get_user_data(event)) = true;
}

void activate(State* state, uint8_t index) {
    if (index >= OPTION_SELECTOR_SLOTS || !state->available || state->pending != NO_SLOT ||
        state->slots[index].state != BTN_STATE_ENABLED || !state->slots[index].shown) return;
    const auto mode = state->config->mode;
    if (mode == OptionSelectorMode::Bound && (state->selected == index || !state->action_count)) return;
    ButtonAction actions[MAX_BUTTON_ACTIONS];
    for (uint8_t action = 0; action < state->action_count; ++action) {
        actions[action] = state->actions[action];
        if (!action_type_substitute_token(action_type_find(actions[action].type), actions[action],
                                          "{value}", state->config->slots[index].value)) {
            device_dialog_notify("Option action is too long", 3000);
            return;
        }
    }
    if (mode == OptionSelectorMode::Local) state->selected = index;
    if (mode == OptionSelectorMode::Bound) {
        state->pending = index;
        state->pending_since = lv_tick_get();
    }
    render(state);
    if (!state->action_count) return;
    lv_obj_t* parent = state->parent;
    bool deleted = false;
    lv_obj_add_event_cb(parent, delete_guard, LV_EVENT_DELETE, &deleted);
    const ActionResult result = action_list_dispatch_result(actions, state->action_count,
                                                           "OptionValue", ACTION_CONTINUATION_OWNER_LVGL);
    if (deleted) return;
    lv_obj_remove_event_cb_with_user_data(parent, delete_guard, &deleted);
    if (result == ACTION_FAILED) {
        state->pending = NO_SLOT;
        render(state);
        device_dialog_notify("Option action failed", 3000);
    }
}

void event(lv_event_t* event) {
#if HAS_TOUCH
    auto* state = static_cast<State*>(lv_event_get_user_data(event));
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(event));
    if (!state || !target) return;
    uint8_t index = NO_SLOT;
    for (uint8_t i = 0; i < OPTION_SELECTOR_SLOTS; ++i)
        if (state->slots[i].hit == target) index = i;
    if (index == NO_SLOT) return;
    const lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_PRESSED) {
        lv_indev_t* input = lv_event_get_indev(event);
        if (!input || lv_indev_get_type(input) != LV_INDEV_TYPE_POINTER || device_dialog_active() ||
            !state->available || state->pending != NO_SLOT ||
            state->slots[index].state != BTN_STATE_ENABLED || !state->slots[index].shown) return;
        state->pressed = index;
        render(state);
    } else if (code == LV_EVENT_RELEASED) {
        if (state->pressed != index) return;
        lv_indev_t* input = lv_event_get_indev(event);
        lv_point_t point{};
        lv_area_t bounds{};
        if (input) lv_indev_get_point(input, &point);
        lv_obj_get_coords(target, &bounds);
        const bool inside = input && point.x >= bounds.x1 && point.x <= bounds.x2 &&
                            point.y >= bounds.y1 && point.y <= bounds.y2;
        state->pressed = NO_SLOT;
        render(state);
        if (inside && !device_dialog_active() && lv_tick_get() - swipe_actions_last_swipe_time() >= 300)
            activate(state, index);
    } else if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_INDEV_RESET) {
        cancel(state);
    }
#else
    (void)event;
#endif
}

void create(lv_obj_t* parent, const WidgetConfig* widget, const ScreenButtonConfig* button,
            const PadRect*, const UIScaleInfo* scale, WidgetState* runtime) {
    auto* state = new (runtime->data) State{};
    state->config = option_selector_config(widget->data);
    state->parent = parent;
    state->selected = state->pending = state->pressed = NO_SLOT;
    state->available = state->config && state->config->mode != OptionSelectorMode::Bound;
    state->group_color = UINT32_MAX;
    if (!state->config) return;
    lv_obj_set_style_bg_opa(parent, state->config->group_background ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(parent, LV_RADIUS_CIRCLE, 0);
    LabelStyle label_style = button->style_center;
    if (state->config->font_size) label_style.font_size = state->config->font_size;
    const lv_font_t* font = pad_resolve_font(label_style, scale->font_large, runtime->font_owner);
    for (unsigned index = 0; index < OPTION_SELECTOR_SLOTS; ++index) {
        const auto& option = state->config->slots[index];
        if (option.kind == OptionSelectorContent::None) continue;
        SlotState& slot = state->slots[index];
        slot.hit = lv_obj_create(parent);
        slot.badge = lv_obj_create(slot.hit);
        slot.content = option.kind == OptionSelectorContent::Icon
            ? lv_image_create(slot.badge) : lv_label_create(slot.badge);
        slot.foreground = slot.background = UINT32_MAX;
        slot.state = UINT8_MAX;
        slot.icon_available = option.kind != OptionSelectorContent::Icon;
        for (lv_obj_t* object : {slot.hit, slot.badge}) {
            lv_obj_remove_style_all(object);
            lv_obj_set_style_pad_all(object, 0, 0);
            lv_obj_set_style_bg_opa(object, LV_OPA_TRANSP, 0);
            lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
        }
        lv_obj_set_style_radius(slot.badge, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_clip_corner(slot.hit, true, 0);
        lv_obj_clear_flag(slot.badge, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(slot.content, LV_OBJ_FLAG_CLICKABLE);
        if (option.kind == OptionSelectorContent::Label) {
            lv_label_set_long_mode(slot.content, LV_LABEL_LONG_CLIP);
            lv_obj_set_style_text_align(slot.content, LV_TEXT_ALIGN_CENTER, 0);
            lv_obj_set_style_text_font(slot.content, font, 0);
        }
#if HAS_TOUCH
        lv_obj_add_flag(slot.hit, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(slot.hit, event, LV_EVENT_ALL, state);
#else
        lv_obj_clear_flag(slot.hit, LV_OBJ_FLAG_CLICKABLE);
#endif
    }
    lv_obj_add_event_cb(parent, mark_layout, LV_EVENT_SIZE_CHANGED, state);
    state->layout_dirty = true;
    layout(state);
    render(state);
}

void update(lv_obj_t*, const WidgetConfig*, WidgetState*, const char*) {}

void tick(lv_obj_t*, const WidgetConfig* widget, WidgetState* runtime) {
    auto* state = reinterpret_cast<State*>(runtime->data);
    if (!state->config || !state->parent) return;
    if (state->config->mode == OptionSelectorMode::Bound) {
        char value[BINDING_TEMPLATE_MAX_LEN];
        state->available = widget->data_binding[0][0] &&
            binding_template_resolve_checked(widget->data_binding[0], value, sizeof(value));
        state->selected = NO_SLOT;
        if (state->available) {
            for (uint8_t index = 0; index < OPTION_SELECTOR_SLOTS; ++index) {
                if (state->config->slots[index].kind != OptionSelectorContent::None &&
                    widget_value_equal(value, state->config->slots[index].value)) {
                    state->selected = index;
                    break;
                }
            }
            if (state->pending == state->selected) state->pending = NO_SLOT;
        }
    }
    if (state->pending != NO_SLOT && uint32_t(lv_tick_get() - state->pending_since) >= 5000)
        state->pending = NO_SLOT;
    if (state->layout_dirty) layout(state);
    render(state);
}

void hide(WidgetState* runtime) {
    auto* state = reinterpret_cast<State*>(runtime->data);
    if (!state->config) return;
    state->pressed = state->pending = NO_SLOT;
    if (state->config->mode != OptionSelectorMode::None) state->selected = NO_SLOT;
    state->available = state->config->mode != OptionSelectorMode::Bound;
}

void show(WidgetState* runtime) {
    reinterpret_cast<State*>(runtime->data)->layout_dirty = true;
}

void destroy(WidgetState* runtime) {
    auto* state = reinterpret_cast<State*>(runtime->data);
    if (state->parent) lv_obj_remove_event_cb_with_user_data(state->parent, mark_layout, state);
    state->parent = nullptr;
}

void cancel_input(WidgetState* runtime) {
    cancel(reinterpret_cast<State*>(runtime->data));
}

#if HAS_MCP
void describe(JsonObject& out) {
    JsonArray fields = out.createNestedArray("config_fields");
    JsonObject mode = fields.createNestedObject();
    mode["name"] = "widget_option_mode"; mode["type"] = "string"; mode["default"] = "none";
    JsonArray modes = mode.createNestedArray("enum");
    for (const char* value : {"none", "bound", "local"}) modes.add(value);
    JsonObject source = fields.createNestedObject();
    source["name"] = "widget_data_binding"; source["type"] = "string"; source["max_length"] = 191;
    for (const char* name : {"widget_option_backgrounds", "widget_option_group_background"}) {
        JsonObject field = fields.createNestedObject();
        field["name"] = name; field["type"] = "boolean"; field["default"] = false;
    }
    for (const char* name : {"widget_option_group_color", "widget_option_background_color",
                             "widget_option_selected_fg_color",
                             "widget_option_selected_bg_color"}) {
        JsonObject field = fields.createNestedObject();
        field["name"] = name; field["type"] = "color";
    }
    const char* size_names[] = {"widget_option_font_size", "widget_option_icon_size_pct",
                                "widget_option_icon_badge_padding", "widget_option_inset"};
    const int size_maximums[] = {512, 100, MAX_ICON_BADGE_PADDING, OPTION_SELECTOR_MAX_INSET};
    for (unsigned index = 0; index < 4; ++index) {
        JsonObject field = fields.createNestedObject();
        field["name"] = size_names[index]; field["type"] = "number";
        field["minimum"] = 0; field["maximum"] = size_maximums[index];
        field["default"] = index == 3 ? OPTION_SELECTOR_DEFAULT_INSET : 0;
    }
    const char* suffixes[] = {"label", "value", "fg_color", "bg_color", "state"};
    for (unsigned index = 0; index < OPTION_SELECTOR_SLOTS; ++index) {
        for (const char* suffix : suffixes) {
            char field[48];
            if (index == 0) snprintf(field, sizeof(field), "widget_option_%s", suffix);
            else snprintf(field, sizeof(field), "widget_option_%s_%u", suffix, index + 1);
            JsonObject entry = fields.createNestedObject();
            entry["name"] = field;
            entry["type"] = "string";
            entry["max_length"] = !strcmp(suffix, "value") ? 63 : 191;
        }
        char icon[48];
        if (index == 0) snprintf(icon, sizeof(icon), "widget_option_icon");
        else snprintf(icon, sizeof(icon), "widget_option_icon_%u", index + 1);
        JsonObject entry = fields.createNestedObject();
        entry["name"] = icon; entry["type"] = "object";
    }
    out["note"] = "Five optional touch targets; use {value} in shared actions. Bound mode follows the reported selection.";
}
#endif

const WidgetPreview preview{"Option selector", "view_week"};

} // namespace option_selector_widget_impl

static const WidgetType option_selector_widget_type{
    "option_selector", nullptr, option_selector_widget_impl::create, option_selector_widget_impl::update,
    option_selector_widget_impl::destroy, option_selector_widget_impl::tick, nullptr, true,
#if HAS_MCP
    option_selector_widget_impl::describe,
#else
    nullptr,
#endif
    option_selector_widget_impl::show, option_selector_widget_impl::hide, option_selector_widget_impl::validate,
    &option_selector_widget_impl::preview, option_selector_widget_impl::cancel_input, option_selector_parse_config,
    option_selector_retain_config, option_selector_release_config, option_selector_visit_resources,
    option_selector_widget_impl::validate_actions
};
static struct OptionSelectorRegistration {
    OptionSelectorRegistration() { widget_register(&option_selector_widget_type); }
} option_selector_registration;

void option_selector_attach(WidgetState* runtime, const ButtonAction* actions, uint8_t count,
                            uint8_t page, bool overlay, uint8_t col, uint8_t row,
                            lv_event_cb_t input_guard, void* context) {
    using namespace option_selector_widget_impl;
    auto* state = reinterpret_cast<State*>(runtime->data);
    if (!state->config) return;
    state->actions = actions;
    state->action_count = count > MAX_BUTTON_ACTIONS ? MAX_BUTTON_ACTIONS : count;
    state->overlay = overlay;
    for (uint8_t index = 0; index < OPTION_SELECTOR_SLOTS; ++index) {
        SlotState& slot = state->slots[index];
        const auto& option = state->config->slots[index];
        if (!slot.hit) continue;
        if (input_guard) lv_obj_add_event_cb(slot.hit, input_guard,
            static_cast<lv_event_code_t>(LV_EVENT_ALL | LV_EVENT_PREPROCESS), context);
        if (option.kind != OptionSelectorContent::Icon) continue;
        char key[CONFIG_ICON_KEY_MAX_LEN];
        icon_store_build_option_key(page, overlay ? "overlay" : "main", col, row, index + 1, key, sizeof(key));
        IconRef ref{};
        slot.icon_available = icon_store_lookup(key, &ref);
        if (slot.icon_available) {
            lv_image_set_src(slot.content, ref.dsc);
            if (option.content.icon.mono) lv_obj_set_style_image_recolor_opa(slot.content, LV_OPA_COVER, 0);
        } else {
            lv_obj_delete(slot.content);
            slot.content = lv_label_create(slot.badge);
            lv_label_set_text(slot.content, "?");
            lv_obj_clear_flag(slot.content, LV_OBJ_FLAG_CLICKABLE);
        }
        slot.foreground = UINT32_MAX;
    }
    state->layout_dirty = true;
    layout(state);
    render(state);
}

#endif
