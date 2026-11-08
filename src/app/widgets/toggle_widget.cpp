#include "toggle_widget.h"

#if HAS_DISPLAY
#include "../action_list.h"
#include "widget_value.h"
#include <new>
#include <string.h>

static_assert(sizeof(ToggleConfig) <= WIDGET_CONFIG_MAX_BYTES, "Toggle config too large");
static_assert(sizeof(ToggleState) <= WIDGET_STATE_MAX_BYTES, "Toggle state too large");

static const char* toggle_validate(JsonObjectConst button) {
    const char* values[] = {"widget_toggle_on_value", "widget_toggle_off_value"};
    for (const char* field : values) {
        if (!button.containsKey(field)) continue;
        if (!button[field].is<const char*>() || strlen(button[field].as<const char*>()) >= 64 ||
            widget_value_equal(button[field].as<const char*>(), "")) return "toggle values must be nonempty strings of at most 63 bytes";
    }
    if (widget_value_equal(button[values[0]] | "on", button[values[1]] | "off"))
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
    config->width_pct = clamp_val(button["widget_toggle_width_pct"] | 50, 20, 100);
    config->read_only = button["widget_toggle_read_only"] | false;
}

static void toggle_render(ToggleState* state, bool animate) {
    lv_obj_set_style_anim_duration(state->control, animate && !DISPLAY_DISABLE_ANIMATIONS ? 120 : 0, 0);
    if (state->value) lv_obj_add_state(state->control, LV_STATE_CHECKED);
    else lv_obj_remove_state(state->control, LV_STATE_CHECKED);
    lv_obj_set_style_opa(state->control, !state->available || state->pending ? LV_OPA_50 : LV_OPA_COVER, 0);
    lv_obj_set_style_opa(state->control, state->known ? LV_OPA_COVER : LV_OPA_TRANSP, LV_PART_KNOB);
}

static void toggle_layout(const WidgetConfig* config, ToggleState* state) {
    const auto* settings = reinterpret_cast<const ToggleConfig*>(config->data);
    lv_obj_t* button = state->button;
    lv_obj_update_layout(button);
    const int width = lv_obj_get_content_width(button);
    const int height = lv_obj_get_content_height(button);
    if (state->layout_width == width && state->layout_height == height) return;
    state->layout_width = width;
    state->layout_height = height;
    const int width_limit = width * settings->width_pct / 100;
    const int height_limit = height * 2;
    int switch_width = width_limit < height_limit ? width_limit : height_limit;
    if (switch_width < 0) switch_width = 0;
    switch_width -= switch_width % 2;
    if (switch_width < 4) lv_obj_add_flag(state->control, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(state->control, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_size(state->control, switch_width, switch_width / 2);
    lv_obj_align(state->control, LV_ALIGN_CENTER, 0, 0);
}

static void toggle_create(lv_obj_t* button, const WidgetConfig* config,
                          const ScreenButtonConfig* source, const PadRect*, const UIScaleInfo*,
                          WidgetState* runtime) {
    (void)source;
    const auto* settings = reinterpret_cast<const ToggleConfig*>(config->data);
    auto* state = new (runtime->data) ToggleState{};
    state->button = button;
    state->read_only = settings->read_only;
    state->bound = !widget_value_equal(config->data_binding[0], "");
    state->known = state->available = !state->bound;
    state->control = lv_switch_create(button);
    lv_obj_remove_flag(state->control, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(state->control, LV_OBJ_FLAG_SCROLLABLE);
    toggle_layout(config, state);
    lv_obj_set_style_bg_color(state->control, resolve_lv_color(settings->off_color, 0x1a1a1a), LV_PART_MAIN);
    lv_obj_set_style_bg_color(state->control, resolve_lv_color(settings->on_color, 0x4caf50), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(state->control, resolve_lv_color(settings->knob_color, 0xffffff), LV_PART_KNOB);
    toggle_render(state, false);
}

static void toggle_report(const WidgetConfig* config, WidgetState* runtime, const char* value, bool valid) {
    auto* state = reinterpret_cast<ToggleState*>(runtime->data);
    const auto* settings = reinterpret_cast<const ToggleConfig*>(config->data);
    const bool on = valid && widget_value_equal(value, settings->on_value);
    const bool off = valid && widget_value_equal(value, settings->off_value);
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
    const char* names[] = {"widget_toggle_on_value", "widget_toggle_off_value", "widget_toggle_on_color", "widget_toggle_off_color", "widget_toggle_knob_color"};
    const char* defaults[] = {"on", "off", DEFAULT_WIDGET_SERIES_COLOR, DEFAULT_WIDGET_TRACK_COLOR, DEFAULT_WIDGET_INDICATOR_COLOR};
    for (unsigned index = 0; index < 5; ++index) {
        JsonObject item = fields.createNestedObject();
        item["name"] = names[index];
        item["type"] = index >= 2 && index <= 4 ? "color" : "string";
        item["max"] = index >= 2 && index <= 4 ? 7 : 63;
        item["default"] = defaults[index];
    }
    JsonObject width = fields.createNestedObject();
    width["name"] = "widget_toggle_width_pct";
    width["type"] = "number";
    width["minimum"] = 20;
    width["maximum"] = 100;
    width["default"] = 50;
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