#include "toggle_widget.h"

#if HAS_DISPLAY
#include "../action_list.h"
#include <new>
#include <string.h>

static_assert(sizeof(ToggleConfig) <= WIDGET_CONFIG_MAX_BYTES, "Toggle config too large");
static_assert(sizeof(ToggleState) <= WIDGET_STATE_MAX_BYTES, "Toggle state too large");

static bool toggle_space(char value) {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n' || value == '\f' || value == '\v';
}

static bool toggle_equal(const char* left, const char* right) {
    while (toggle_space(*left)) ++left;
    while (toggle_space(*right)) ++right;
    size_t left_len = strlen(left), right_len = strlen(right);
    while (left_len && toggle_space(left[left_len - 1])) --left_len;
    while (right_len && toggle_space(right[right_len - 1])) --right_len;
    if (left_len != right_len) return false;
    for (size_t index = 0; index < left_len; ++index) {
        char first = left[index], second = right[index];
        if (first >= 'A' && first <= 'Z') first += 'a' - 'A';
        if (second >= 'A' && second <= 'Z') second += 'a' - 'A';
        if (first != second) return false;
    }
    return true;
}

static const char* toggle_validate(JsonObjectConst button) {
    const char* values[] = {"widget_toggle_on_value", "widget_toggle_off_value"};
    for (const char* field : values) {
        if (!button.containsKey(field)) continue;
        if (!button[field].is<const char*>() || strlen(button[field].as<const char*>()) >= 64 ||
            toggle_equal(button[field].as<const char*>(), "")) return "toggle values must be nonempty strings of at most 63 bytes";
    }
    if (toggle_equal(button[values[0]] | "on", button[values[1]] | "off"))
        return "toggle on and off values must differ";
    for (const char* field : {"widget_toggle_on_color", "widget_toggle_off_color", "widget_toggle_knob_color"}) {
        if (!button.containsKey(field)) continue;
        uint32_t color;
        const char* text = button[field].is<const char*>() ? button[field].as<const char*>() : "";
        if (strlen(text) != 7 || text[0] != '#' || !parse_hex_color(text, &color))
            return "toggle colors must be #RRGGBB literals";
    }
    if (button.containsKey("widget_toggle_read_only") && !button["widget_toggle_read_only"].is<bool>())
        return "toggle read-only must be boolean";
    if (button.containsKey("widget_toggle_width_pct") &&
        (!button["widget_toggle_width_pct"].is<int>() || button["widget_toggle_width_pct"].as<int>() < 20 ||
         button["widget_toggle_width_pct"].as<int>() > 100)) return "toggle width must be an integer from 20 to 100";
    if (button.containsKey("widget_toggle_icon_position")) {
        const char* placement = button["widget_toggle_icon_position"] | "";
        if (strcmp(placement, "left") && strcmp(placement, "above") && strcmp(placement, "below"))
            return "toggle icon position must be left, above, or below";
    }
    if (button.containsKey("widget_data_binding") && !button["widget_data_binding"].is<const char*>())
        return "toggle state source must be a string";
    return nullptr;
}

static void toggle_parse(const JsonObject& button, uint8_t* data) {
    auto* config = new (data) ToggleConfig{};
    strlcpy(config->on_value, button["widget_toggle_on_value"] | "on", sizeof(config->on_value));
    strlcpy(config->off_value, button["widget_toggle_off_value"] | "off", sizeof(config->off_value));
    strlcpy(config->on_color, button["widget_toggle_on_color"] | DEFAULT_WIDGET_SERIES_COLOR, sizeof(config->on_color));
    strlcpy(config->off_color, button["widget_toggle_off_color"] | DEFAULT_WIDGET_TRACK_COLOR, sizeof(config->off_color));
    strlcpy(config->knob_color, button["widget_toggle_knob_color"] | DEFAULT_WIDGET_INDICATOR_COLOR, sizeof(config->knob_color));
    config->width_pct = clamp_val(button["widget_toggle_width_pct"] | 100, 20, 100);
    const char* position = button["widget_toggle_icon_position"] | "left";
    config->icon_position = strcmp(position, "above") == 0 ? 1 : strcmp(position, "below") == 0 ? 2 : 0;
    config->read_only = button["widget_toggle_read_only"] | false;
}

static void toggle_render(ToggleState* state, bool animate) {
    lv_obj_set_style_anim_duration(state->control, animate && !DISPLAY_DISABLE_ANIMATIONS ? 120 : 0, 0);
    if (state->value) lv_obj_add_state(state->control, LV_STATE_CHECKED);
    else lv_obj_remove_state(state->control, LV_STATE_CHECKED);
    lv_obj_set_style_opa(state->control, !state->available || state->pending ? LV_OPA_50 : LV_OPA_COVER, 0);
    lv_obj_set_style_opa(state->control, state->known ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_KNOB);
}

static lv_area_t toggle_content_bounds(lv_obj_t* object) {
    if (!object) return {0, 0, -1, -1};
    const int width = lv_obj_get_width(object), height = lv_obj_get_height(object);
    lv_point_t corners[] = {{0, 0}, {width, 0}, {0, height}, {width, height}};
    if (lv_obj_check_type(object, &lv_image_class)) {
        lv_point_t pivot;
        lv_image_get_pivot(object, &pivot);
        lv_point_array_transform(corners, 4, lv_image_get_rotation(object),
                                 lv_image_get_scale_x(object), lv_image_get_scale_y(object), &pivot, true);
    }
    lv_area_t coordinates;
    lv_obj_get_coords(object, &coordinates);
    for (auto& corner : corners) {
        corner.x += coordinates.x1;
        corner.y += coordinates.y1;
    }
    lv_obj_transform_point_array(object, corners, 4, LV_OBJ_POINT_TRANSFORM_FLAG_NONE);
    lv_area_t bounds = {corners[0].x, corners[0].y, corners[0].x, corners[0].y};
    for (const auto& corner : corners) {
        if (corner.x < bounds.x1) bounds.x1 = corner.x;
        if (corner.y < bounds.y1) bounds.y1 = corner.y;
        if (corner.x > bounds.x2) bounds.x2 = corner.x;
        if (corner.y > bounds.y2) bounds.y2 = corner.y;
    }
    bounds.x1 -= coordinates.x1;
    bounds.y1 -= coordinates.y1;
    bounds.x2 -= coordinates.x1 + 1;
    bounds.y2 -= coordinates.y1 + 1;
    return bounds;
}

static void toggle_layout(const WidgetConfig* config, ToggleState* state) {
    const auto* settings = reinterpret_cast<const ToggleConfig*>(config->data);
    lv_obj_t* button = state->button;
    lv_obj_update_layout(button);
    const int available_width = lv_obj_get_content_width(button);
    int top = 0, bottom = lv_obj_get_content_height(button);
    if (state->top_label) top = lv_obj_get_y(state->top_label) + toggle_content_bounds(state->top_label).y2 + 5;
    if (state->bottom_label) bottom = lv_obj_get_y(state->bottom_label) + toggle_content_bounds(state->bottom_label).y1 - 4;
    const int height = bottom > top ? bottom - top : 0;
    const lv_area_t icon_bounds = toggle_content_bounds(state->icon);
    const lv_area_t label_bounds = toggle_content_bounds(state->center_label);
    const int icon_width = lv_area_get_width(&icon_bounds), icon_height = lv_area_get_height(&icon_bounds);
    const int label_width = lv_area_get_width(&label_bounds), label_height = lv_area_get_height(&label_bounds);
    const bool side_icon = state->icon && settings->icon_position == 0;
    const int row_prefix = (side_icon ? icon_width + 4 : 0) + (state->center_label ? label_width + 4 : 0);
    const int row_height_limit = height - (state->icon && !side_icon ? icon_height + 4 : 0);
    const int width_limit = available_width - row_prefix;
    int switch_width = width_limit < row_height_limit * 2 ? width_limit : row_height_limit * 2;
    switch_width = switch_width > 0 ? switch_width * settings->width_pct / 100 : 0;
    switch_width -= switch_width % 2;
    if (switch_width < 4) lv_obj_add_flag(state->control, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(state->control, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(state->control, switch_width, switch_width / 2);
    int row_height = switch_width / 2;
    if (label_height > row_height) row_height = label_height;
    if (side_icon && icon_height > row_height) row_height = icon_height;
    const int group_height = row_height + (state->icon && !side_icon ? icon_height + 4 : 0);
    const int group_top = top + (height - group_height) / 2 + state->offset_y;
    const int row_top = group_top + (state->icon && settings->icon_position == 1 ? icon_height + 4 : 0);
    int cursor = (available_width - row_prefix - switch_width) / 2 + state->offset_x;
    if (state->icon) {
        const int icon_left = side_icon ? cursor : (available_width - icon_width) / 2 + state->offset_x;
        const int icon_top = side_icon ? row_top + (row_height - icon_height) / 2
            : settings->icon_position == 1 ? group_top : row_top + row_height + 4;
        lv_obj_set_pos(state->icon, icon_left - icon_bounds.x1, icon_top - icon_bounds.y1);
        if (side_icon) cursor += icon_width + 4;
    }
    if (state->center_label) {
        lv_obj_set_pos(state->center_label, cursor + state->label_offset_x - label_bounds.x1,
                   row_top + (row_height - label_height) / 2 + state->label_offset_y - label_bounds.y1);
        cursor += label_width + 4;
    }
    lv_obj_set_pos(state->control, cursor, row_top + (row_height - switch_width / 2) / 2);
}

static void toggle_create(lv_obj_t* button, const WidgetConfig* config,
                          const ScreenButtonConfig* source, const PadRect*, const UIScaleInfo*,
                          lv_obj_t* icon, lv_obj_t* center, WidgetState* runtime) {
    const auto* settings = reinterpret_cast<const ToggleConfig*>(config->data);
    auto* state = new (runtime->data) ToggleState{};
    state->button = button;
    state->read_only = settings->read_only;
    state->bound = !toggle_equal(config->data_binding[0], "");
    state->known = state->available = !state->bound;
    state->control = lv_switch_create(button);
    lv_obj_remove_flag(state->control, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(state->control, LV_OBJ_FLAG_SCROLLABLE);
    state->icon = icon;
    state->center_label = center;
    state->offset_x = source->ui_offset_x;
    state->offset_y = source->ui_offset_y;
    state->label_offset_x = source->style_center.x_offset;
    state->label_offset_y = source->style_center.y_offset;
    for (uint32_t index = 0; index < lv_obj_get_child_count(button); ++index) {
        lv_obj_t* child = lv_obj_get_child(button, index);
        if (child == center || !lv_obj_check_type(child, &lv_label_class)) continue;
        const lv_align_t align = lv_obj_get_style_align(child, LV_PART_MAIN);
        if (align == LV_ALIGN_TOP_MID) state->top_label = child;
        if (align == LV_ALIGN_BOTTOM_MID) state->bottom_label = child;
    }
    if (icon) lv_obj_set_align(icon, LV_ALIGN_TOP_LEFT);
    if (center) {
        lv_obj_update_layout(button);
        lv_obj_set_align(center, LV_ALIGN_TOP_LEFT);
        lv_obj_set_width(center, LV_SIZE_CONTENT);
        lv_obj_set_style_max_width(center, lv_obj_get_content_width(button), 0);
    }
    toggle_layout(config, state);
    lv_obj_set_style_bg_color(state->control, resolve_lv_color(settings->off_color, 0x1a1a1a), LV_PART_MAIN);
    lv_obj_set_style_bg_color(state->control, resolve_lv_color(settings->on_color, 0x4caf50), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(state->control, resolve_lv_color(settings->knob_color, 0xffffff), LV_PART_KNOB);
    toggle_render(state, false);
}

static void toggle_report(const WidgetConfig* config, WidgetState* runtime, const char* value, bool valid) {
    auto* state = reinterpret_cast<ToggleState*>(runtime->data);
    const auto* settings = reinterpret_cast<const ToggleConfig*>(config->data);
    const bool on = valid && toggle_equal(value, settings->on_value);
    const bool off = valid && toggle_equal(value, settings->off_value);
    const bool available = on || off;
    const bool known = state->known || available;
    const bool reported = available ? on : state->value;
    const bool pending = state->pending && available && state->target != reported;
    if (state->available == available && state->known == known &&
        state->value == reported && state->pending == pending) return;
    const bool animate = state->available;
    state->available = available;
    state->known = known;
    state->value = reported;
    state->pending = pending;
    toggle_render(state, animate);
}

static void toggle_update(lv_obj_t*, const WidgetConfig* config, WidgetState* runtime, const char* value) {
    toggle_report(config, runtime, value, true);
}

static void toggle_tick(lv_obj_t*, const WidgetConfig* config, WidgetState* runtime) {
    auto* state = reinterpret_cast<ToggleState*>(runtime->data);
    toggle_layout(config, state);
    if (state->bound) {
        char value[BINDING_TEMPLATE_MAX_LEN];
        const bool valid = binding_template_resolve_checked(config->data_binding[0], value, sizeof(value));
        toggle_report(config, runtime, value, valid);
    }
    if (state->pending && uint32_t(lv_tick_get() - state->pending_since) >= 5000) {
        state->pending = false;
        toggle_render(state, false);
    }
}

bool toggle_interactive(const WidgetState* runtime) {
    const auto* state = reinterpret_cast<const ToggleState*>(runtime->data);
    return !state->read_only && state->available && !state->pending;
}

void toggle_tap(const WidgetConfig*, WidgetState* runtime,
                const ButtonAction* on_actions, uint8_t on_count,
                const ButtonAction* off_actions, uint8_t off_count) {
    if (!toggle_interactive(runtime)) return;
    auto* state = reinterpret_cast<ToggleState*>(runtime->data);
    const bool target = !state->value;
    const uint8_t count = target ? on_count : off_count;
    if (state->bound && !count) return;
    ButtonAction actions[MAX_BUTTON_ACTIONS];
    memcpy(actions, target ? on_actions : off_actions, count * sizeof(ButtonAction));
    state->target = target;
    state->pending = state->bound;
    state->pending_since = lv_tick_get();
    if (!state->bound) state->value = target;
    toggle_render(state, true);
    lv_obj_t* button = state->button;
    bool deleted = false;
    const auto delete_callback = [](lv_event_t* event) {
        *static_cast<bool*>(lv_event_get_user_data(event)) = true;
    };
    lv_obj_add_event_cb(button, delete_callback, LV_EVENT_DELETE, &deleted);
    const ActionResult result = action_list_dispatch_result(actions, count, target ? "ToggleOn" : "ToggleOff", ACTION_CONTINUATION_OWNER_LVGL);
    if (deleted) return;
    lv_obj_remove_event_cb_with_user_data(button, delete_callback, &deleted);
    if (result == ACTION_FAILED) {
        state->pending = false;
        toggle_render(state, false);
    }
}

static void toggle_hide(WidgetState* runtime) {
    auto* state = reinterpret_cast<ToggleState*>(runtime->data);
    state->pending = false;
    if (!state->bound) state->value = false;
    else state->available = false;
    toggle_render(state, false);
}
static void toggle_show(WidgetState*) {}
static void toggle_destroy(WidgetState*) {}

#if HAS_MCP
static void toggle_describe(JsonObject& out) {
    JsonArray fields = out.createNestedArray("config_fields");
    const char* names[] = {"widget_toggle_on_value", "widget_toggle_off_value", "widget_toggle_on_color", "widget_toggle_off_color", "widget_toggle_knob_color", "widget_toggle_icon_position"};
    const char* defaults[] = {"on", "off", DEFAULT_WIDGET_SERIES_COLOR, DEFAULT_WIDGET_TRACK_COLOR, DEFAULT_WIDGET_INDICATOR_COLOR, "left"};
    for (unsigned index = 0; index < 6; ++index) {
        JsonObject item = fields.createNestedObject();
        item["name"] = names[index];
        item["type"] = index >= 2 && index <= 4 ? "color" : "string";
        item["max"] = index >= 2 && index <= 4 ? 7 : 63;
        item["default"] = defaults[index];
        if (index == 5) {
            JsonArray choices = item.createNestedArray("enum");
            for (const char* choice : {"left", "above", "below"}) choices.add(choice);
        }
    }
    JsonObject width = fields.createNestedObject();
    width["name"] = "widget_toggle_width_pct";
    width["type"] = "number";
    width["minimum"] = 20;
    width["maximum"] = 100;
    width["default"] = 100;
    JsonObject read_only = fields.createNestedObject();
    read_only["name"] = "widget_toggle_read_only";
    read_only["type"] = "boolean";
    read_only["default"] = false;
    out["note"] = "State source: widget_data_binding (blank = local, resets off on pad hide). actions[] turns on; lp_actions[] turns off. No ordinary long press. Bound reports are authoritative; confirmation matches on/off category, timeout 5s. Static sources are supported. Use no off fallback for unavailable data.";
}
#endif
static const WidgetPreview toggle_preview = {"Toggle switch", "toggle_on"};
REGISTER_WIDGET_SCHEMA_VALIDATED_LIFECYCLE(toggle, nullptr, true, toggle_validate);
#endif