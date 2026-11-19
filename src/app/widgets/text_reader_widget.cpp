#include "text_reader_widget.h"

#if HAS_DISPLAY && HAS_PSRAM

#include "widget.h"
#include "../button_defaults.h"
#include "../log_manager.h"
#if HAS_TOUCH
#include "../touch_manager.h"
#endif
#include <esp_heap_caps.h>
#include <new>
#include <string.h>

namespace text_reader_impl {

#if HAS_LVGL_EPAPER
constexpr const char* default_viewport = "paged";
#else
constexpr const char* default_viewport = "scroll";
#endif

struct Config {
    char style[CONFIG_LABEL_STYLE_MAX_LEN];
    char indicator_track_color[CONFIG_COLOR_MAX_LEN];
    char indicator_color[CONFIG_COLOR_MAX_LEN];
    bool paged;
    bool navigation;
};
static_assert(sizeof(Config) <= WIDGET_CONFIG_MAX_BYTES, "Text reader config too large");

struct State {
    lv_obj_t* parent = nullptr;
    lv_obj_t* viewport = nullptr;
    lv_obj_t* label = nullptr;
    lv_obj_t* message = nullptr;
    lv_obj_t* indicator = nullptr;
    lv_obj_t* controls[2]{};
    BindingTextView text;
    char scalar[BINDING_TEMPLATE_MAX_LEN]{};
    char color_binding[CONFIG_LABEL_STYLE_MAX_LEN]{};
    const lv_font_t* font = nullptr;
    uint32_t color = UINT32_MAX;
    uint32_t indicator_track_color = UINT32_MAX;
    uint32_t indicator_color = UINT32_MAX;
    int32_t width = -1;
    int32_t height = -1;
    int32_t text_height = 0;
    int32_t page_height = 1;
    int32_t page = 0;
    int32_t pages = 1;
    int32_t control_height = 0;
    int8_t pressed = -1;
    lv_point_t press_point{};
    bool visible = false;
    bool dirty = true;
    bool navigation = false;
    bool paged = false;
    bool show_navigation = false;
    bool text_pressed = false;
    bool tap_cancelled = false;
};

State* get(WidgetState* runtime) {
    State* state = nullptr;
    memcpy(&state, runtime->data, sizeof(state));
    return state;
}

void cancel(State* state) {
    if (state) {
        state->pressed = -1;
        state->text_pressed = false;
        state->tap_cancelled = false;
    }
}

void cancel_input(State* state) {
    cancel(state);
#if HAS_TOUCH
    if (state && state->parent) touch_manager_cancel_object_input(state->parent);
#endif
}

void release_text(State* state) {
    // Detach LVGL's borrowed pointer before retiring the snapshot.
    if (state->label) lv_label_set_text_static(state->label, "");
    if (state->text.release) state->text.release(state->text.owner);
    state->text = {};
}

void update_indicator(State* state) {
    const int32_t start = state->paged ? state->page * state->page_height
                                      : lv_obj_get_scroll_y(state->viewport);
    const int32_t height = state->paged ? state->page_height : state->height;
    widget_update_position_indicator(state->indicator, start, start + height,
                                     state->text_height, state->height);
}

void position(State* state) {
    lv_obj_set_y(state->label, state->paged ? -state->page * state->page_height : 0);
    for (int i = 0; i < 2; ++i) {
        const bool disabled = i ? state->page + 1 >= state->pages : state->page == 0;
        if (disabled) lv_obj_add_state(state->controls[i], LV_STATE_DISABLED);
        else lv_obj_remove_state(state->controls[i], LV_STATE_DISABLED);
    }
    update_indicator(state);
}

void layout(State* state) {
    if (!state->dirty) return;
    lv_obj_update_layout(state->parent);
    state->dirty = false;
    cancel_input(state);
    int32_t width = state->width > 0 ? state->width : 1;
    const int32_t height = state->height > 0 ? state->height : 1;
    const int32_t line = lv_font_get_line_height(state->font);
    lv_point_t measured{};
    lv_text_get_size(&measured, state->text.text ? state->text.text : state->scalar,
                     state->font, 0, 0, width, LV_TEXT_FLAG_NONE);
    int32_t gutter = 0;
    if (measured.y > height && width > 1) {
        gutter = clamp_val<int32_t>(width - 1, 0, widget_indicator_width + widget_indicator_gap);
        width -= gutter;
        lv_text_get_size(&measured, state->text.text ? state->text.text : state->scalar,
                         state->font, 0, 0, width, LV_TEXT_FLAG_NONE);
    }
    state->text_height = measured.y;
    state->navigation = state->paged && state->show_navigation && measured.y > height;
    const int32_t available = height - (state->navigation ? 2 * state->control_height : 0);
    const bool usable = state->width > 0 && available >= line;
    state->page_height = available >= line ? available / line * line : line;
    state->pages = state->text_height > 0
        ? (state->text_height + state->page_height - 1) / state->page_height : 1;
    if (state->page >= state->pages) state->page = state->pages - 1;
    lv_obj_set_size(state->viewport, width, state->paged ? state->page_height : height);
    lv_obj_set_pos(state->viewport, 0, state->navigation ? state->control_height : 0);
    lv_obj_set_width(state->label, width);
    lv_obj_set_height(state->label, state->text_height > 0 ? state->text_height : line);
    lv_obj_set_size(state->message, width, height);
    if (usable) {
        lv_obj_remove_flag(state->viewport, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(state->message, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(state->viewport, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(state->message, LV_OBJ_FLAG_HIDDEN);
    }
    if (usable && gutter) {
        const int32_t bar_width = gutter < widget_indicator_width ? gutter : widget_indicator_width;
        lv_obj_set_pos(state->indicator, width + gutter - bar_width, 0);
        lv_obj_set_size(state->indicator, bar_width, height);
        lv_obj_remove_flag(state->indicator, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(state->indicator, LV_OBJ_FLAG_HIDDEN);
    }
    for (int i = 0; i < 2; ++i) {
        lv_obj_set_size(state->controls[i], width, state->control_height);
        lv_obj_set_pos(state->controls[i], 0, i ? height - state->control_height : 0);
        if (usable && state->navigation) lv_obj_remove_flag(state->controls[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(state->controls[i], LV_OBJ_FLAG_HIDDEN);
    }
    position(state);
}

void resize(lv_event_t* event) {
    auto* state = static_cast<State*>(lv_event_get_user_data(event));
    const int32_t width = lv_obj_get_content_width(state->parent);
    const int32_t height = lv_obj_get_content_height(state->parent);
    if (width == state->width && height == state->height) return;
    state->width = width;
    state->height = height;
    state->dirty = true;
}

void control_event(lv_event_t* event) {
    auto* state = static_cast<State*>(lv_event_get_user_data(event));
    auto* target = lv_event_get_target_obj(event);
    const int8_t index = target == state->controls[0] ? 0 : 1;
    const auto code = lv_event_get_code(event);
    lv_event_stop_bubbling(event);
    if (!state->visible || !state->navigation || lv_obj_has_state(target, LV_STATE_DISABLED)) return;
    lv_indev_t* input = lv_event_get_indev(event);
    if (code == LV_EVENT_PRESSED && input) {
        state->pressed = index;
        lv_indev_get_point(input, &state->press_point);
    } else if (code == LV_EVENT_PRESSING && input) {
        lv_point_t point;
        lv_indev_get_point(input, &point);
        if (LV_ABS(point.x - state->press_point.x) > 10 || LV_ABS(point.y - state->press_point.y) > 10)
            cancel(state);
    } else if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_INDEV_RESET) {
        cancel(state);
    } else if (code == LV_EVENT_RELEASED && state->pressed == index) {
        cancel(state);
        if (!input || lv_indev_get_scroll_obj(input)) return;
        lv_point_t point;
        lv_area_t bounds;
        lv_indev_get_point(input, &point);
        lv_obj_get_coords(target, &bounds);
        if (point.x < bounds.x1 || point.x > bounds.x2 || point.y < bounds.y1 || point.y > bounds.y2) return;
        WidgetPressFeedback feedback{LV_EVENT_CLICKED, target, 0, false, true};
        lv_obj_send_event(lv_obj_get_parent(state->parent), LV_EVENT_VALUE_CHANGED, &feedback);
        state->page += index ? 1 : -1;
        position(state);
    }
}

void viewport_event(lv_event_t* event) {
    auto* state = static_cast<State*>(lv_event_get_user_data(event));
    lv_event_stop_bubbling(event);
    const auto code = lv_event_get_code(event);
    if (code == LV_EVENT_SCROLL) update_indicator(state);
    if (!state->paged || !state->visible) return;
    lv_indev_t* input = lv_event_get_indev(event);
    if (code == LV_EVENT_PRESSED && input) {
        state->text_pressed = true;
        state->tap_cancelled = false;
        lv_indev_get_point(input, &state->press_point);
    } else if (code == LV_EVENT_PRESSING && input && state->text_pressed) {
        lv_point_t point;
        lv_indev_get_point(input, &point);
        if (LV_ABS(point.x - state->press_point.x) > 10 || LV_ABS(point.y - state->press_point.y) > 10)
            state->tap_cancelled = true;
    } else if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_INDEV_RESET) {
        cancel(state);
    } else if (code == LV_EVENT_RELEASED && input && state->text_pressed) {
        lv_point_t point;
        lv_indev_get_point(input, &point);
        const int32_t dx = point.x - state->press_point.x;
        const int32_t dy = point.y - state->press_point.y;
        const bool tapped = !state->tap_cancelled && LV_ABS(dx) <= 10 && LV_ABS(dy) <= 10;
        cancel(state);
        lv_area_t bounds;
        lv_obj_get_coords(state->viewport, &bounds);
        if (point.x < bounds.x1 || point.x > bounds.x2 || point.y < bounds.y1 || point.y > bounds.y2) return;
        int32_t direction;
        if (tapped) direction = point.y < bounds.y1 + lv_obj_get_height(state->viewport) / 2 ? -1 : 1;
        else if (LV_ABS(dy) >= 30 && LV_ABS(dy) > LV_ABS(dx)) direction = dy < 0 ? 1 : -1;
        else return;
        const int32_t next = state->page + direction;
        if (next >= 0 && next < state->pages) {
            if (tapped) {
                WidgetPressFeedback feedback{LV_EVENT_CLICKED, state->viewport, 0, false, true};
                lv_obj_send_event(lv_obj_get_parent(state->parent), LV_EVENT_VALUE_CHANGED, &feedback);
            }
            state->page = next;
            position(state);
        }
    }
}

void parse(const JsonObject& button, uint8_t* data) {
    auto* config = new (data) Config{};
    strlcpy(config->style, button["widget_text_reader_style"] | "", sizeof(config->style));
    widget_parse_field(button["widget_text_reader_indicator_track_color"],
                       config->indicator_track_color, sizeof(config->indicator_track_color), "");
    widget_parse_field(button["widget_text_reader_indicator_color"],
                       config->indicator_color, sizeof(config->indicator_color), "");
    const ButtonDefaults* defaults = button_defaults_get();
    if (!config->indicator_track_color[0])
        strlcpy(config->indicator_track_color, defaults->bg_color[0] ? defaults->bg_color : DEFAULT_BUTTON_BG_COLOR,
                sizeof(config->indicator_track_color));
    if (!config->indicator_color[0])
        strlcpy(config->indicator_color, defaults->fg_color[0] ? defaults->fg_color : DEFAULT_BUTTON_FG_COLOR,
                sizeof(config->indicator_color));
    config->paged = !strcmp(button["widget_text_reader_viewport"] | default_viewport, "paged");
    config->navigation = button["widget_text_reader_show_navigation"] | true;
}

const char* validate(JsonObjectConst button) {
    for (const char* field : {"widget_text_reader_indicator_track_color", "widget_text_reader_indicator_color"}) {
        if (!button.containsKey(field)) continue;
        if (!button[field].is<const char*>()) return "Text reader indicator colors must be strings";
        const JsonString value = button[field].as<JsonString>();
        if (value.size() >= CONFIG_COLOR_MAX_LEN || strlen(value.c_str()) != value.size())
            return "Text reader indicator color is too long or contains a null character";
        uint32_t color;
        if (value.size() && !parse_hex_color(value.c_str(), &color) && !strchr(value.c_str(), '['))
            return "Text reader indicator colors must be hex or a scalar binding";
    }
    for (const char* field : {"widget_data_binding", "widget_text_reader_style", "widget_text_reader_viewport"}) {
        if (!button.containsKey(field)) continue;
        const size_t limit = !strcmp(field, "widget_text_reader_style")
            ? CONFIG_LABEL_STYLE_MAX_LEN : CONFIG_LABEL_MAX_LEN;
        if (!button[field].is<const char*>()) return "Text reader fields must be strings";
        const JsonString value = button[field].as<JsonString>();
        if (value.size() >= limit || strlen(value.c_str()) != value.size())
            return "Text reader text or style is too long or contains a null character";
    }
    const char* viewport = button["widget_text_reader_viewport"] | default_viewport;
    if (strcmp(viewport, "scroll") && strcmp(viewport, "paged"))
        return "Text reader viewport must be scroll or paged";
    if (button.containsKey("widget_text_reader_show_navigation") &&
        !button["widget_text_reader_show_navigation"].is<bool>())
        return "Text reader show navigation must be a boolean";
    if ((button["actions"].is<JsonArrayConst>() && button["actions"].size()) ||
        (button["lp_actions"].is<JsonArrayConst>() && button["lp_actions"].size()))
        return "Text reader owns touch input; use a separate button for actions";
    for (const char* field : {"widget_data_binding_2", "widget_data_binding_3", "widget_data_binding_4"}) {
        if (button.containsKey(field) &&
            (!button[field].is<const char*>() || button[field].as<const char*>()[0]))
            return "Text reader accepts one text binding";
    }
    LabelStyle style{};
    label_style_parse(button["widget_text_reader_style"] | "", &style);
    if ((style.long_mode && style.long_mode != LABEL_MODE_WRAP) ||
        style.x_offset || style.y_offset || style.font_upscale)
        return "Text reader always wraps; text offsets and bitmap upscaling are not supported";
    return nullptr;
}

void poll(State* state, const WidgetConfig* config) {
    BindingTextView next;
    const bool full = binding_template_acquire_text(config->data_binding[0], &next) == BINDING_RESOLVER_RESOLVED;
    bool changed;
    char scalar[BINDING_TEMPLATE_MAX_LEN];
    if (full) {
        changed = next.owner != state->text.owner || next.text != state->text.text;
    } else {
        binding_template_resolve(config->data_binding[0], scalar, sizeof(scalar));
        changed = state->text.text || strcmp(scalar, state->scalar);
    }
    if (!changed) {
        if (next.release) next.release(next.owner);
        return;
    }
    cancel_input(state);
    release_text(state);
    state->text = next;
    if (!full) strlcpy(state->scalar, scalar, sizeof(state->scalar));
    lv_label_set_text_static(state->label, full ? state->text.text : state->scalar);
    lv_obj_scroll_to_y(state->viewport, 0, LV_ANIM_OFF);
    state->page = 0;
    state->dirty = true;
}

void create(lv_obj_t* parent, const WidgetConfig* config, const ScreenButtonConfig*,
            const PadRect*, const UIScaleInfo* scale, WidgetState* runtime) {
    void* memory = heap_caps_malloc(sizeof(State), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    State* state = memory ? new (memory) State{} : nullptr;
    memcpy(runtime->data, &state, sizeof(state));
    if (!state) { LOGE("TextReader", "Cannot allocate reader state in PSRAM"); return; }
    const auto* settings = reinterpret_cast<const Config*>(config->data);
    state->parent = parent;
    state->paged = settings->paged;
    state->show_navigation = settings->navigation;
    LabelStyle style{};
    label_style_parse(settings->style, &style, state->color_binding, sizeof(state->color_binding));
    state->font = pad_resolve_font(style, scale->font_small, runtime->font_owner);
    state->control_height = lv_font_get_line_height(scale->font_small) + 12;
    state->viewport = lv_obj_create(parent);
    state->message = lv_label_create(parent);
    if (!state->viewport || !state->message) {
        LOGE("TextReader", "Cannot allocate reader viewport");
        return;
    }
    lv_obj_remove_style_all(state->viewport);
    lv_obj_set_style_pad_all(state->viewport, 0, 0);
    lv_obj_clear_flag(state->viewport, static_cast<lv_obj_flag_t>(
        LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_GESTURE_BUBBLE));
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_GESTURE_BUBBLE);
    if (state->paged) lv_obj_clear_flag(state->viewport, LV_OBJ_FLAG_SCROLLABLE);
    else {
        lv_obj_set_scroll_dir(state->viewport, LV_DIR_VER);
        lv_obj_set_scrollbar_mode(state->viewport, LV_SCROLLBAR_MODE_OFF);
    }
    lv_obj_add_event_cb(state->viewport, viewport_event, LV_EVENT_ALL, state);
    lv_obj_set_style_text_font(state->message, pad_typography_font(scale->font_small), 0);
    lv_label_set_text_static(state->message, "Reader needs more height");
    lv_obj_clear_flag(state->message, LV_OBJ_FLAG_CLICKABLE);
    state->label = lv_label_create(state->viewport);
    if (!state->label) { LOGE("TextReader", "Cannot allocate reader label"); return; }
    lv_obj_clear_flag(state->label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_font(state->label, state->font, 0);
    lv_obj_set_style_text_align(state->label, style.align ? pad_resolve_align(style) : LV_TEXT_ALIGN_LEFT, 0);
    if (!state->color_binding[0] && (style.color & 0x01000000)) {
        state->color = style.color & 0xffffff;
        lv_obj_set_style_text_color(state->label, lv_color_hex(state->color), 0);
    }
    lv_label_set_long_mode(state->label, LV_LABEL_LONG_WRAP);
    lv_label_set_text_static(state->label, "");
    state->indicator = widget_create_position_indicator(parent);
    if (!state->indicator) { LOGE("TextReader", "Cannot allocate reader position indicator"); return; }
    for (int i = 0; i < 2; ++i) {
        state->controls[i] = lv_button_create(parent);
        if (!state->controls[i]) { LOGE("TextReader", "Cannot allocate reader navigation"); return; }
        lv_obj_remove_style_all(state->controls[i]);
        lv_obj_set_style_bg_opa(state->controls[i], LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(state->controls[i], lv_obj_get_style_bg_color(parent, LV_PART_MAIN), 0);
        lv_obj_set_style_opa(state->controls[i], LV_OPA_50, LV_STATE_DISABLED);
        lv_obj_set_style_bg_opa(state->controls[i], LV_OPA_50, LV_STATE_PRESSED);
        lv_obj_set_style_radius(state->controls[i], 4, 0);
        lv_obj_clear_flag(state->controls[i], static_cast<lv_obj_flag_t>(
            LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE));
        lv_obj_add_event_cb(state->controls[i], control_event, LV_EVENT_ALL, state);
        lv_obj_t* label = lv_label_create(state->controls[i]);
        if (!label) { LOGE("TextReader", "Cannot allocate navigation label"); return; }
        lv_obj_set_style_text_font(label, pad_typography_font(scale->font_small), 0);
        lv_label_set_text_static(label, i ? LV_SYMBOL_DOWN : LV_SYMBOL_UP);
        lv_obj_center(label);
        lv_obj_add_flag(state->controls[i], LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_add_flag(state->message, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(parent, resize, LV_EVENT_SIZE_CHANGED, state);
    lv_obj_update_layout(parent);
    state->width = lv_obj_get_content_width(parent);
    state->height = lv_obj_get_content_height(parent);
}

bool ready(State* state) {
    return state && state->viewport && state->label && state->message && state->indicator &&
        state->controls[0] && state->controls[1];
}

void show(WidgetState* runtime) {
    State* state = get(runtime);
    if (!ready(state)) return;
    state->visible = true;
    state->dirty = true;
}

void hide(WidgetState* runtime) {
    State* state = get(runtime);
    if (!state) return;
    cancel_input(state);
    state->visible = false;
    release_text(state);
    state->scalar[0] = '\0';
    state->page = 0;
}

void destroy(WidgetState* runtime) {
    State* state = get(runtime);
    if (!state) return;
    hide(runtime);
    lv_obj_remove_event_cb_with_user_data(state->parent, resize, state);
    if (state->viewport) lv_obj_delete(state->viewport);
    if (state->message) lv_obj_delete(state->message);
    if (state->indicator) lv_obj_delete(state->indicator);
    for (auto* control : state->controls) if (control) lv_obj_delete(control);
    state->~State();
    heap_caps_free(state);
    state = nullptr;
    memcpy(runtime->data, &state, sizeof(state));
}

void tick(lv_obj_t*, const WidgetConfig* config, WidgetState* runtime) {
    State* state = get(runtime);
    if (!ready(state) || !state->visible) return;
    poll(state, config);
    const auto* settings = reinterpret_cast<const Config*>(config->data);
    lv_color_t indicator_color;
    if (resolve_color_changed(settings->indicator_track_color, widget_default_rgb(DEFAULT_BUTTON_BG_COLOR),
                              &state->indicator_track_color, &indicator_color))
        lv_obj_set_style_bg_color(state->indicator, indicator_color, LV_PART_MAIN);
    if (resolve_color_changed(settings->indicator_color, widget_default_rgb(DEFAULT_BUTTON_FG_COLOR),
                              &state->indicator_color, &indicator_color))
        lv_obj_set_style_bg_color(state->indicator, indicator_color, LV_PART_INDICATOR);
    if (state->color_binding[0]) {
        const uint32_t fallback = lv_color_to_u32(lv_obj_get_style_text_color(state->parent, LV_PART_MAIN)) & 0xffffff;
        const uint32_t color = resolve_color(state->color_binding, fallback);
        if (color != state->color) {
            state->color = color;
            lv_obj_set_style_text_color(state->label, lv_color_hex(color), 0);
        }
    }
    layout(state);
}

void cancel_runtime(WidgetState* runtime) { cancel(get(runtime)); }

void resources(const uint8_t* data, WidgetResourceVisitor visitor, void* context) {
    const auto* config = reinterpret_cast<const Config*>(data);
    visitor(WidgetResourceKind::Binding, "widget_text_reader_style", 0, config->style, context);
    visitor(WidgetResourceKind::Binding, "widget_text_reader_indicator_track_color", 0,
            config->indicator_track_color, context);
    visitor(WidgetResourceKind::Binding, "widget_text_reader_indicator_color", 0,
            config->indicator_color, context);
}

static const WidgetPreview text_reader_preview = {"Text reader", "article"};

#if HAS_MCP
void describe(JsonObject& out) {
    JsonArray fields = out.createNestedArray("config_fields");
    JsonObject text = fields.createNestedObject();
    text["name"] = "widget_data_binding";
    text["type"] = "string";
    text["bindable"] = true;
    text["max_length"] = CONFIG_LABEL_MAX_LEN - 1;
    text["default"] = "";
    text["description"] = "Text or binding template. An exact supported full-text token reads the complete retained source; mixed templates use ordinary short scalar bindings.";
    JsonObject style = fields.createNestedObject();
    style["name"] = "widget_text_reader_style";
    style["type"] = "string";
    style["max_length"] = CONFIG_LABEL_STYLE_MAX_LEN - 1;
    style["default"] = "";
    style["description"] = "Label style: font, font_size, font_metrics, align, color. Always wraps; no offsets or bitmap upscaling. Only color accepts bindings.";
    JsonObject viewport = fields.createNestedObject();
    viewport["name"] = "widget_text_reader_viewport";
    viewport["type"] = "string";
    viewport["default"] = default_viewport;
    JsonArray choices = viewport.createNestedArray("enum");
    choices.add("scroll");
    choices.add("paged");
    viewport["description"] = "Vertical touch scrolling or non-animated line-aligned paging. In Paged mode, tap the upper/lower text half for previous/next page, or swipe vertically.";
    JsonObject navigation = fields.createNestedObject();
    navigation["name"] = "widget_text_reader_show_navigation";
    navigation["type"] = "boolean";
    navigation["default"] = true;
    navigation["description"] = "Paged only: show up/down controls when text overflows. Half-surface taps and vertical swipes work with or without controls; no external navigation actions.";
    for (const char* name : {"widget_text_reader_indicator_track_color", "widget_text_reader_indicator_color"}) {
        JsonObject color = fields.createNestedObject();
        color["name"] = name;
        color["type"] = "color";
        color["max_length"] = CONFIG_COLOR_MAX_LEN - 1;
        color["bindable"] = true;
        color["description"] = "Optional position indicator color or scalar binding. Empty uses the device button background/foreground default.";
    }
    out["note"] = "Requires display and PSRAM. Owns touch input; no button actions. New text resets position. Scrolling and paging are manual.";
}
#endif

}

static const WidgetType text_reader_widget_type{
    "text_reader", text_reader_impl::parse, text_reader_impl::create, nullptr,
    text_reader_impl::destroy, text_reader_impl::tick, nullptr, true,
#if HAS_MCP
    text_reader_impl::describe,
#else
    nullptr,
#endif
    text_reader_impl::show, text_reader_impl::hide, text_reader_impl::validate,
    &text_reader_impl::text_reader_preview, text_reader_impl::cancel_runtime, nullptr, nullptr, nullptr,
    text_reader_impl::resources, nullptr, nullptr, false
};
static struct TextReaderRegistration {
    TextReaderRegistration() { widget_register(&text_reader_widget_type); }
} text_reader_registration;

void text_reader_attach(WidgetState* runtime, lv_event_cb_t guard, void* context) {
    auto* state = text_reader_impl::get(runtime);
    if (!state) return;
    for (auto* object : {state->viewport, state->controls[0], state->controls[1]})
        if (object) lv_obj_add_event_cb(object, guard,
            static_cast<lv_event_code_t>(LV_EVENT_ALL | LV_EVENT_PREPROCESS), context);
}
#endif
