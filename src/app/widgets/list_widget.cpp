#include "list_widget.h"

#if HAS_DISPLAY && HAS_PSRAM

#include "list_config.h"
#include "widget.h"
#include "../action_list.h"
#include "../item_binding.h"
#include "../log_manager.h"
#include "../pad_binding.h"
#include "../screens/pad_screen.h"
#if HAS_TOUCH
#include "../touch_manager.h"
#endif
#include <esp_heap_caps.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

namespace list_widget_impl {

constexpr uint8_t none = UINT8_MAX;
constexpr int32_t auto_min_row_height = 44;
constexpr int32_t auto_vertical_padding = 8;

// Activations execute on the LVGL task, including navigation dispatched
// synchronously from an action. This guard spans all List instances.
bool activating = false;

struct ActivationGuard {
    ActivationGuard() { activating = true; }
    ~ActivationGuard() { activating = false; }
};

struct Row {
    lv_obj_t* object;
    lv_obj_t* top;
    lv_obj_t* bottom;
    char top_text[BINDING_TEMPLATE_MAX_LEN];
    char bottom_text[BINDING_TEMPLATE_MAX_LEN];
    uint32_t background;
    uint32_t border_color;
    uint32_t top_color;
    uint32_t bottom_color;
    int32_t text_y;
    int32_t width;
    int32_t height;
    int32_t position;
    int32_t border_width;
    int32_t label_x;
    int32_t label_width;
    bool bottom_shown;
    bool shown;
};

struct State {
    const ListConfig* config;
    ButtonTile* tile;
    lv_obj_t* parent;
    lv_obj_t* container;
    lv_obj_t* message;
    lv_obj_t* indicator;
    ListSourceHandle* source;
    ListSourceSnapshot* snapshot;
    Row* rows;
    const lv_font_t* fonts[2];
    const lv_font_t* navigation_font;
    LabelStyle styles[2];
    char color_binding[2][CONFIG_LABEL_STYLE_MAX_LEN];
    char error_message[128];
    lv_event_cb_t guard;
    void* guard_context;
    uint32_t revision;
    uint32_t press_revision;
    uint32_t track_color;
    uint32_t indicator_color;
    int32_t width;
    int32_t height;
    int32_t spacing;
    uint8_t count;
    uint8_t row_count;
    uint8_t focus;
    uint8_t page;
    uint8_t capacity;
    uint8_t slots;
    uint8_t pressed;
    bool held;
    bool press_feedback;
    bool visible;
    bool have_revision;
    bool layout_dirty;
    bool layout_ok;
    bool navigation_shown;
    lv_point_t press_point;
};

State* get(WidgetState* runtime) {
    State* state = nullptr;
    if (runtime) memcpy(&state, runtime->data, sizeof(state));
    return state;
}

void set(WidgetState* runtime, State* state) {
    memcpy(runtime->data, &state, sizeof(state));
}

void feedback(State* state, uint8_t index, lv_event_code_t event) {
    WidgetPressFeedback message{event, state->rows[index].object, state->rows[index].background, true,
                                index >= state->count};
    lv_obj_send_event(state->tile->obj, LV_EVENT_VALUE_CHANGED, &message);
}

void end_feedback(State* state, lv_event_code_t event) {
    if (!state->press_feedback) return;
    state->press_feedback = false;
    feedback(state, state->pressed, event);
}

void cancel(State* state) {
    if (!state) return;
    end_feedback(state, LV_EVENT_PRESS_LOST);
    state->pressed = none;
    state->held = false;
}

void clear_rows(State* state) {
#if HAS_TOUCH
    if (state->rows && state->container) touch_manager_cancel_object_input(state->container);
#endif
    cancel(state);
    if (state->rows) {
        for (uint8_t i = 0; i < state->row_count; ++i)
            if (state->rows[i].object) lv_obj_delete(state->rows[i].object);
        heap_caps_free(state->rows);
        state->rows = nullptr;
    }
    state->count = 0;
    state->row_count = 0;
    state->navigation_shown = false;
    state->layout_dirty = true;
    if (state->indicator) {
        lv_obj_add_flag(state->indicator, LV_OBJ_FLAG_HIDDEN);
    }
    if (state->snapshot) {
        state->snapshot->release();
        state->snapshot = nullptr;
    }
}

void message(State* state, const char* text) {
    if (!state->message) return;
    if (text) {
        // State messages are static and never take ownership of source errors.
        lv_label_set_text_static(state->message, text);
        lv_obj_clear_flag(state->message, LV_OBJ_FLAG_HIDDEN);
        lv_obj_center(state->message);
    } else {
        lv_obj_add_flag(state->message, LV_OBJ_FLAG_HIDDEN);
    }
}

void mark_layout(lv_event_t* event) {
    auto* state = static_cast<State*>(lv_event_get_user_data(event));
    if (state) state->layout_dirty = true;
}

void scroll_started(lv_event_t* event) {
    cancel(static_cast<State*>(lv_event_get_user_data(event)));
}

void update_indicator(State* state) {
    if (!state->indicator || lv_obj_has_flag(state->indicator, LV_OBJ_FLAG_HIDDEN) ||
        !state->capacity || state->count <= state->capacity) return;
    int32_t total;
    int32_t start;
    int32_t end;
    if (state->config->viewport == ListViewport::Paged) {
        total = state->count;
        start = state->page * state->capacity;
        end = start + state->capacity;
        if (end > total) end = total;
    } else {
        start = lv_obj_get_scroll_y(state->container);
        const int32_t remaining = lv_obj_get_scroll_bottom(state->container);
        if (start < 0) start = 0;
        total = start + state->height + (remaining > 0 ? remaining : 0);
        end = start + state->height;
    }
    widget_update_position_indicator(state->indicator, start, end, total, state->height);
}

void scroll_changed(lv_event_t* event) {
    auto* state = static_cast<State*>(lv_event_get_user_data(event));
    if (state) update_indicator(state);
}

// Keep the LVGL label on PSRAM-owned static text: its DOT mode mutates
// dynamically allocated text and does not support static text.
void ellipsize(char* text, const lv_font_t* font, int32_t width) {
    for (char* p = text; *p; ++p) if (*p == '\n' || *p == '\r') *p = ' ';
    lv_point_t size;
    lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
    if (size.x <= width) return;
    size_t length = strlen(text);
    if (length > BINDING_TEMPLATE_MAX_LEN - 4) length = BINDING_TEMPLATE_MAX_LEN - 4;
    while (length && (static_cast<unsigned char>(text[length]) & 0xC0) == 0x80) --length;
    for (;;) {
        memcpy(text + length, "...", 4);
        lv_text_get_size(&size, text, font, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);
        if (size.x <= width || !length) break;
        --length;
        while (length && (static_cast<unsigned char>(text[length]) & 0xC0) == 0x80) --length;
    }
}

void text(lv_obj_t* label, char* cache, const char* templ,
          const lv_font_t* font, int32_t width) {
    char resolved[BINDING_TEMPLATE_MAX_LEN];
    binding_template_resolve(templ, resolved, sizeof(resolved));
    ellipsize(resolved, font, width);
    if (strcmp(cache, resolved)) {
        strlcpy(cache, resolved, BINDING_TEMPLATE_MAX_LEN);
        lv_label_set_text_static(label, cache);
    }
}

void refresh_rows(State* state) {
    if (!state->snapshot || !state->rows || !state->layout_ok) return;
    const PadBinding* previous = nullptr;
    uint8_t previous_count = 0;
    pad_binding_get_bindings(&previous, &previous_count);
    pad_binding_set_bindings(state->tile->pad_bindings, state->tile->pad_binding_count);
    const auto& cfg = *state->config;
    const uint32_t track = resolve_color(cfg.indicator_track_color, widget_default_rgb(DEFAULT_BUTTON_BG_COLOR));
    const uint32_t indicator = resolve_color(cfg.indicator_color, widget_default_rgb(DEFAULT_BUTTON_FG_COLOR));
    if (state->track_color != track) {
        state->track_color = track;
        lv_obj_set_style_bg_color(state->indicator, lv_color_hex(track), LV_PART_MAIN);
    }
    if (state->indicator_color != indicator) {
        state->indicator_color = indicator;
        lv_obj_set_style_bg_color(state->indicator, lv_color_hex(indicator), LV_PART_INDICATOR);
    }
    for (uint8_t i = 0; i < state->row_count; ++i) {
        Row& row = state->rows[i];
        const bool navigation = i >= state->count;
        if (navigation && !state->navigation_shown) continue;
        ItemBindingScope item(navigation ? nullptr : state->snapshot, i);
        const bool selected = !navigation && cfg.allow_selection && i == state->focus;
        const lv_font_t* top_font = navigation ? state->navigation_font : state->fonts[0];
        const float default_value = resolve_number(cfg.default_border_width, 0);
        const float width_value = navigation ? lv_obj_get_style_border_width(state->parent, LV_PART_MAIN) :
            resolve_number(cfg.border_width, 0);
        const int default_width = !isfinite(default_value) || default_value < 0 ? 0 :
            (default_value > 32 ? 32 : static_cast<int>(default_value));
        const int width = !isfinite(width_value) || width_value < 0 ? 0 :
            (width_value > 32 ? 32 : static_cast<int>(width_value));
        int selected_width = navigation || !cfg.allow_selection ? width : cfg.selected_border_width >= 0 ? cfg.selected_border_width :
            (default_width > 1 ? default_width * 2 : 2);
        if (selected_width > 64) selected_width = 64;
        const int applied_width = selected ? selected_width : width;
        const int inset = cfg.padding + (width > selected_width ? width : selected_width);
        const int32_t text_width = state->width > 2 * inset ? state->width - 2 * inset : 1;
        if (!navigation) {
            text(row.top, row.top_text, cfg.label_top, top_font, text_width);
            text(row.bottom, row.bottom_text, cfg.label_bottom, state->fonts[1], text_width);
        }
        uint32_t bg = navigation ? lv_color_to_u32(lv_obj_get_style_bg_color(state->parent, LV_PART_MAIN)) & 0xffffff :
            resolve_color(cfg.background, widget_default_rgb(DEFAULT_BUTTON_BG_COLOR));
        if (selected) bg = resolve_color(cfg.selected_background, bg);
        if (row.background != bg) {
            row.background = bg;
            lv_obj_set_style_bg_color(row.object, lv_color_hex(bg), 0);
        }
        uint32_t border = navigation ? lv_color_to_u32(lv_obj_get_style_border_color(state->parent, LV_PART_MAIN)) & 0xffffff :
            resolve_color(cfg.border_color, widget_default_rgb(DEFAULT_BUTTON_BORDER_COLOR));
        if (selected) {
            const uint32_t default_color = resolve_color(default_width > 0 ? cfg.default_border_color :
                cfg.default_foreground, widget_default_rgb(default_width > 0 ? DEFAULT_BUTTON_BORDER_COLOR :
                                                            DEFAULT_BUTTON_FG_COLOR));
            border = resolve_color(cfg.selected_border_color, default_color);
        }
        if (row.border_color != border) {
            row.border_color = border;
            lv_obj_set_style_border_color(row.object, lv_color_hex(border), 0);
        }
        if (row.border_width != applied_width) {
            row.border_width = applied_width;
            lv_obj_set_style_border_width(row.object, applied_width, 0);
        }
        for (int j = 0; j < 2; ++j) {
            const lv_color_t foreground = lv_obj_get_style_text_color(state->parent, LV_PART_MAIN);
            const uint32_t fallback = lv_color_to_u32(navigation ? foreground :
                pad_resolve_label_color(state->styles[j], foreground)) & 0xffffff;
            uint32_t color = navigation ? fallback : resolve_color(state->color_binding[j], fallback);
            if (selected) color = resolve_color(j ? cfg.selected_bottom_color : cfg.selected_top_color, color);
            uint32_t& last = j ? row.bottom_color : row.top_color;
            if (last != color) {
                last = color;
                lv_obj_set_style_text_color(j ? row.bottom : row.top, lv_color_hex(color), 0);
            }
        }
        const int32_t top_height = lv_font_get_line_height(top_font);
        const int32_t bottom_height = lv_font_get_line_height(state->fonts[1]);
        const bool bottom = row.bottom_text[0] != '\0';
        const int32_t group = top_height + (bottom ? bottom_height : 0);
        const int32_t y = (row.height > group ? (row.height - group) / 2 : 0) - applied_width;
        if (row.text_y != y) {
            row.text_y = y;
            lv_obj_set_y(row.top, y);
            lv_obj_set_y(row.bottom, y + top_height);
        }
        if (row.label_x != inset - applied_width || row.label_width != text_width) {
            row.label_x = inset - applied_width;
            row.label_width = text_width;
            for (lv_obj_t* label : {row.top, row.bottom}) {
                lv_obj_set_x(label, row.label_x);
                lv_obj_set_width(label, text_width);
            }
        }
        if (row.bottom_shown != bottom) {
            row.bottom_shown = bottom;
            if (bottom) lv_obj_clear_flag(row.bottom, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(row.bottom, LV_OBJ_FLAG_HIDDEN);
        }
    }
    pad_binding_set_bindings(previous, previous_count);
}

void position_rows(State* state) {
    if (!state->rows || !state->capacity) return;
    const auto& cfg = *state->config;
    const uint8_t pages = (state->count + state->capacity - 1) / state->capacity;
    if (pages && state->page >= pages) state->page = pages - 1;
    bool changed = false;
    for (uint8_t i = 0; i < state->row_count; ++i) {
        Row& row = state->rows[i];
        const bool navigation = i >= state->count;
        const bool shown = navigation ? state->navigation_shown :
            cfg.viewport == ListViewport::Scroll || i / state->capacity == state->page;
        if (shown != row.shown) {
            changed = true;
            row.shown = shown;
            if (shown) lv_obj_clear_flag(row.object, LV_OBJ_FLAG_HIDDEN);
            else lv_obj_add_flag(row.object, LV_OBJ_FLAG_HIDDEN);
        }
        if (navigation) {
            const bool disabled = i == state->count ? state->page == 0 : state->page + 1 >= pages;
            if (disabled != lv_obj_has_state(row.object, LV_STATE_DISABLED)) {
                if (disabled) lv_obj_add_state(row.object, LV_STATE_DISABLED);
                else lv_obj_remove_state(row.object, LV_STATE_DISABLED);
            }
        }
        const uint8_t slot = navigation ? (i == state->count ? 0 : state->slots - 1) :
            cfg.viewport == ListViewport::Paged ? i % state->capacity + (state->navigation_shown ? 1 : 0) : i;
        const int32_t position = (slot / state->slots) * (state->height + state->spacing) +
            ((slot % state->slots) * (state->height + state->spacing)) / state->slots;
        const int32_t next = ((slot % state->slots + 1) * (state->height + state->spacing)) / state->slots;
        const int32_t row_height = next - ((slot % state->slots) * (state->height + state->spacing)) / state->slots
                                   - state->spacing;
        if (row.position != position) {
            changed = true;
            row.position = position;
            lv_obj_set_pos(row.object, 0, position);
        }
        if (row.width != state->width || row.height != row_height) {
            changed = true;
            row.width = state->width;
            row.height = row_height;
            lv_obj_set_size(row.object, state->width, row_height);
        }
    }
    if (changed && cfg.viewport == ListViewport::Scroll) lv_obj_update_layout(state->container);
    update_indicator(state);
}

void layout(State* state) {
    if (!state->layout_dirty) return;
    state->layout_dirty = false;
    cancel(state);
    lv_obj_update_layout(state->parent);
    state->width = lv_obj_get_content_width(state->parent);
    state->height = lv_obj_get_content_height(state->parent);
    const auto& cfg = *state->config;
    if (state->width < 1) state->width = 1;
    if (state->height < 1) state->height = 1;
    state->layout_ok = true;
    int32_t capacity = cfg.visible_items;
    if (!capacity) {
        int32_t row_height = lv_font_get_line_height(state->fonts[0]) + 2 * auto_vertical_padding;
        if (cfg.label_bottom[0]) row_height += lv_font_get_line_height(state->fonts[1]);
        if (row_height < auto_min_row_height) row_height = auto_min_row_height;
        capacity = (state->height + cfg.spacing) / (row_height + cfg.spacing);
        if (capacity < 1) capacity = 1;
        if (capacity > static_cast<int32_t>(LIST_SOURCE_MAX_ITEMS)) capacity = LIST_SOURCE_MAX_ITEMS;
    }
    const bool navigation = cfg.show_navigation && cfg.viewport == ListViewport::Paged;
    if (navigation && state->count > capacity && capacity < 3) capacity = 3;
    state->slots = capacity < state->height ? capacity : state->height;
    state->navigation_shown = navigation && state->count > state->slots;
    if (state->navigation_shown && state->slots < 3) {
        cancel(state);
        state->layout_ok = false;
        lv_obj_add_flag(state->container, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(state->indicator, LV_OBJ_FLAG_HIDDEN);
        LOGW("List", "List is too short for navigation");
        message(state, "List needs room for navigation");
        return;
    }
    state->capacity = state->slots - (state->navigation_shown ? 2 : 0);
    state->spacing = cfg.spacing;
    if (state->slots > 1 && state->spacing > (state->height - state->slots) / (state->slots - 1))
        state->spacing = (state->height - state->slots) / (state->slots - 1);
    if (state->count > state->capacity && state->width > 1) {
        const int32_t gutter = state->width - 1 < widget_indicator_width + widget_indicator_gap
            ? state->width - 1 : widget_indicator_width + widget_indicator_gap;
        const int32_t bar_width = gutter < widget_indicator_width ? gutter : widget_indicator_width;
        state->width -= gutter;
        lv_obj_set_pos(state->indicator, state->width + gutter - bar_width, 0);
        lv_obj_set_size(state->indicator, bar_width, state->height);
        lv_obj_clear_flag(state->indicator, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(state->indicator, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_set_size(state->container, state->width > 0 ? state->width : 1,
                    state->height > 0 ? state->height : 1);
    lv_obj_set_width(state->message, state->width > 0 ? state->width : 1);
    lv_obj_center(state->message);
    lv_obj_clear_flag(state->container, LV_OBJ_FLAG_HIDDEN);
    if (state->snapshot && state->count) message(state, nullptr);
    position_rows(state);
    refresh_rows(state);
}

bool activate(State* state, uint8_t index, bool hold) {
    if (!state->visible || !state->layout_ok || !state->snapshot || index >= state->count) return false;
    if (activating) {
        LOGW("List", "Reentrant List activation rejected");
        return false;
    }
    ButtonTile* tile = state->tile;
    const uint8_t count = hold ? tile->lp_action_count : tile->action_count;
    if (!count) {
        if (state->config->allow_selection)
            feedback(state, index, hold ? LV_EVENT_LONG_PRESSED : LV_EVENT_CLICKED);
        return true;
    }
    auto* actions = static_cast<ButtonAction*>(heap_caps_malloc(sizeof(ButtonAction) * count,
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!actions) { LOGE("List", "Cannot allocate activation actions in PSRAM"); return false; }
    memcpy(actions, hold ? tile->lp_actions : tile->actions, sizeof(ButtonAction) * count);
    ListSourceSnapshot* snapshot = state->snapshot;
    const PadBinding* previous = nullptr;
    uint8_t previous_count = 0;
    pad_binding_get_bindings(&previous, &previous_count);
    pad_binding_set_bindings(tile->pad_bindings, tile->pad_binding_count);
    ActionResult result;
    {
        ActivationGuard guard;
        ItemBindingScope item(snapshot, index);
        feedback(state, index, hold ? LV_EVENT_LONG_PRESSED : LV_EVENT_CLICKED);
        result = action_list_dispatch_result(actions, count, hold ? "ListHold" : "ListSelect",
                                            ACTION_CONTINUATION_OWNER_LVGL, true);
    }
    pad_binding_set_bindings(previous, previous_count);
    heap_caps_free(actions);
    return result != ACTION_FAILED;
}

bool current(State* state) {
    if (!state || !state->source || !state->snapshot) return false;
    ListSourceSnapshot* snapshot = nullptr;
    uint32_t revision = 0;
    const ListSourceState status = list_source_read(state->source, &snapshot, &revision, nullptr);
    const bool ready = status == ListSourceState::Ready && snapshot == state->snapshot &&
                       revision == state->revision;
    if (snapshot) snapshot->release();
    return ready;
}

void event(lv_event_t* e) {
    auto* state = static_cast<State*>(lv_event_get_user_data(e));
    if (!state || !state->rows) return;
    lv_obj_t* target = static_cast<lv_obj_t*>(lv_event_get_target(e));
    uint8_t index = none;
    for (uint8_t i = 0; i < state->row_count; ++i)
        if (state->rows[i].object == target) { index = i; break; }
    if (index == none) return;
    const lv_event_code_t code = lv_event_get_code(e);
    lv_event_stop_bubbling(e);
    const bool navigation = index >= state->count;
    if (navigation && (!state->navigation_shown || lv_obj_has_state(target, LV_STATE_DISABLED))) return;
    if (code == LV_EVENT_PRESSED) {
        lv_indev_t* input = lv_event_get_indev(e);
        if (!input || !state->visible || !state->layout_ok) return;
        state->pressed = index;
        state->press_revision = state->revision;
        state->held = false;
        lv_indev_get_point(input, &state->press_point);
        if (navigation || state->config->allow_selection || state->tile->action_count || state->tile->lp_action_count) {
            state->press_feedback = true;
            feedback(state, index, LV_EVENT_PRESSED);
        }
    } else if (code == LV_EVENT_PRESSING) {
        lv_indev_t* input = lv_event_get_indev(e);
        if (input && state->pressed == index) {
            lv_point_t point;
            lv_indev_get_point(input, &point);
            if (abs(point.x - state->press_point.x) > 10 || abs(point.y - state->press_point.y) > 10)
                cancel(state);
        }
    } else if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_INDEV_RESET ||
               code == LV_EVENT_SCROLL_BEGIN) {
        cancel(state);
    } else if ((code == LV_EVENT_LONG_PRESSED || code == LV_EVENT_RELEASED) &&
               state->pressed == index && state->press_revision == state->revision && !state->held) {
        lv_indev_t* input = lv_event_get_indev(e);
        lv_point_t point{};
        lv_area_t bounds{};
        if (!input || lv_indev_get_scroll_obj(input) || !current(state)) { cancel(state); return; }
        lv_indev_get_point(input, &point);
        lv_obj_get_coords(target, &bounds);
        if (point.x < bounds.x1 || point.x > bounds.x2 || point.y < bounds.y1 || point.y > bounds.y2) {
            cancel(state);
            return;
        }
        if (navigation) {
            if (code == LV_EVENT_LONG_PRESSED) return;
            end_feedback(state, LV_EVENT_RELEASED);
            state->pressed = none;
            feedback(state, index, LV_EVENT_CLICKED);
            list_widget_command(&state->tile->widget_state,
                                index == state->count ? "previous_page" : "next_page");
            return;
        }
        if (state->config->allow_selection) state->focus = index;
        position_rows(state);
        refresh_rows(state);
        if (code == LV_EVENT_LONG_PRESSED) state->held = true;
        else {
            end_feedback(state, LV_EVENT_RELEASED);
            state->pressed = none;
        }
        activate(state, index, code == LV_EVENT_LONG_PRESSED);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_CLICKED) {
        if (code == LV_EVENT_RELEASED) end_feedback(state, LV_EVENT_RELEASED);
        cancel(state);
    }
}

bool build_rows(State* state, ListSourceSnapshot* snapshot) {
    const size_t count = snapshot->items().size();
    if (!count) { snapshot->release(); message(state, "No items"); return true; }
    const size_t row_count = count +
        (state->config->show_navigation && state->config->viewport == ListViewport::Paged ? 2 : 0);
    Row* rows = static_cast<Row*>(heap_caps_calloc(row_count, sizeof(Row), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!rows) {
        snapshot->release();
        LOGE("List", "Cannot allocate rows in PSRAM");
        message(state, "List allocation error");
        return false;
    }
    state->snapshot = snapshot;
    state->rows = rows;
    state->count = static_cast<uint8_t>(count);
    state->row_count = static_cast<uint8_t>(row_count);
    for (uint8_t i = 0; i < state->row_count; ++i) {
        Row& row = rows[i];
        const bool navigation = i >= state->count;
        row.background = row.border_color = row.top_color = row.bottom_color = UINT32_MAX;
        row.text_y = row.width = row.height = row.position = row.border_width = INT32_MIN;
        row.label_x = row.label_width = INT32_MIN;
        row.shown = row.bottom_shown = true;
        row.object = lv_obj_create(state->container);
        if (row.object) row.top = lv_label_create(row.object);
        if (row.top) row.bottom = lv_label_create(row.object);
        if (!row.object || !row.top || !row.bottom) {
            clear_rows(state);
            LOGE("List", "Cannot allocate List visuals in PSRAM");
            message(state, "List allocation error");
            return false;
        }
        lv_obj_remove_style_all(row.object);
        lv_obj_set_style_bg_opa(row.object, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(row.object, state->config->radius, 0);
        lv_obj_set_style_border_opa(row.object, LV_OPA_COVER, 0);
        if (navigation) lv_obj_set_style_opa(row.object, LV_OPA_50, LV_STATE_DISABLED);
        lv_obj_clear_flag(row.object, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE));
        lv_obj_add_flag(row.object, LV_OBJ_FLAG_CLICKABLE);
        if (state->guard)
            lv_obj_add_event_cb(row.object, state->guard,
                static_cast<lv_event_code_t>(LV_EVENT_ALL | LV_EVENT_PREPROCESS), state->guard_context);
        lv_obj_add_event_cb(row.object, event, LV_EVENT_ALL, state);
        for (int j = 0; j < 2; ++j) {
            lv_obj_t* label = j ? row.bottom : row.top;
            lv_obj_remove_style_all(label);
            lv_obj_clear_flag(label, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_GESTURE_BUBBLE));
            const lv_font_t* font = navigation ? state->navigation_font : state->fonts[j];
            lv_obj_set_style_text_font(label, font, 0);
            lv_obj_set_style_text_align(label, navigation ? LV_TEXT_ALIGN_CENTER : pad_resolve_align(state->styles[j]), 0);
            lv_obj_set_height(label, lv_font_get_line_height(font));
            lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
            lv_label_set_text_static(label, j ? row.bottom_text : row.top_text);
        }
        if (navigation) {
            strlcpy(row.top_text, i == state->count ? LV_SYMBOL_UP : LV_SYMBOL_DOWN, sizeof(row.top_text));
            lv_label_set_text_static(row.top, row.top_text);
        }
    }
    message(state, nullptr);
    return true;
}

void poll(State* state) {
    if (!state->visible || !state->source) return;
    ListSourceSnapshot* next = nullptr;
    uint32_t revision = 0;
    const char* error = nullptr;
    const ListSourceState status = list_source_read(state->source, &next, &revision, &error);
    if (!state->have_revision || revision != state->revision) {
        bool same_rows = status == ListSourceState::Ready && next && state->snapshot &&
                         next->items().size() == state->count;
        for (uint8_t i = 0; same_rows && i < state->count; ++i)
            same_rows = !strcmp(next->items()[i]["id"], state->snapshot->items()[i]["id"]);
        if (same_rows) {
            if (next->items() != state->snapshot->items()) cancel(state);
            else if (state->pressed != none) state->press_revision = revision;
            state->snapshot->release();
            state->snapshot = next;
            state->revision = revision;
            refresh_rows(state);
            return;
        }
        char focused_id[LIST_SOURCE_MAX_ID_BYTES + 1] = {};
        const uint8_t old_focus = state->focus;
        const int32_t scroll = lv_obj_get_scroll_y(state->container);
        if (state->snapshot && old_focus < state->count)
            strlcpy(focused_id, state->snapshot->items()[old_focus]["id"] | "", sizeof(focused_id));
        clear_rows(state);
        state->revision = revision;
        state->have_revision = true;
        state->focus = none;
        if (status == ListSourceState::Ready && next) {
            if (build_rows(state, next) && state->count) {
                for (uint8_t i = 0; focused_id[0] && i < state->count; ++i)
                    if (!strcmp(focused_id, state->snapshot->items()[i]["id"] | "")) { state->focus = i; break; }
                layout(state);
                if (state->config->viewport == ListViewport::Scroll) {
                    lv_obj_update_layout(state->container);
                    lv_obj_scroll_to_y(state->container, scroll, LV_ANIM_OFF);
                }
            }
        } else {
            if (next) next->release();
            if (status == ListSourceState::Error) {
                LOGE("List", "Source error: %s", error ? error : "unknown");
                snprintf(state->error_message, sizeof(state->error_message), "List: %.110s",
                         error ? error : "source error");
                message(state, state->error_message);
            } else message(state, "Loading");
        }
        layout(state);
    } else if (next) next->release();
    refresh_rows(state);
}

void create(lv_obj_t* parent, const WidgetConfig* widget, const ScreenButtonConfig*,
            const PadRect*, const UIScaleInfo* scale, WidgetState* runtime) {
    set(runtime, nullptr);
    auto* state = static_cast<State*>(heap_caps_calloc(1, sizeof(State), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!state) { LOGE("List", "Cannot allocate List state in PSRAM"); return; }
    set(runtime, state);
    state->config = list_config(widget->data);
    state->tile = reinterpret_cast<ButtonTile*>(reinterpret_cast<uint8_t*>(runtime) - offsetof(ButtonTile, widget_state));
    state->parent = parent;
    state->focus = state->pressed = none;
    state->track_color = state->indicator_color = UINT32_MAX;
    if (!state->config) return;
    label_style_parse(state->config->style_top, &state->styles[0], state->color_binding[0], sizeof(state->color_binding[0]));
    label_style_parse(state->config->style_bottom, &state->styles[1], state->color_binding[1], sizeof(state->color_binding[1]));
    state->fonts[0] = pad_resolve_font(state->styles[0], scale->font_small, runtime->font_owner);
    state->fonts[1] = pad_resolve_font(state->styles[1], scale->font_small, runtime->font_owner);
    state->navigation_font = scale->font_small;
    state->container = lv_obj_create(parent);
    if (!state->container) { LOGE("List", "Cannot allocate List container"); return; }
    lv_obj_remove_style_all(state->container);
    lv_obj_set_style_pad_all(state->container, 0, 0);
    lv_obj_clear_flag(state->container, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLL_ELASTIC | LV_OBJ_FLAG_SCROLL_CHAIN |
                                       LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_CLICKABLE));
    if (state->config->viewport == ListViewport::Paged)
        lv_obj_clear_flag(state->container, LV_OBJ_FLAG_SCROLLABLE);
    else {
        lv_obj_add_flag(state->container, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_scroll_dir(state->container, LV_DIR_VER);
        lv_obj_add_event_cb(state->container, scroll_started, LV_EVENT_SCROLL_BEGIN, state);
        lv_obj_add_event_cb(state->container, scroll_changed, LV_EVENT_SCROLL, state);
    }
    state->message = lv_label_create(parent);
    if (!state->message) { LOGE("List", "Cannot allocate List state label"); return; }
    lv_obj_clear_flag(state->message, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_text_font(state->message, pad_typography_font(scale->font_small), 0);
    lv_obj_set_style_text_align(state->message, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(state->message, LV_LABEL_LONG_CLIP);
    state->indicator = widget_create_position_indicator(parent);
    if (!state->indicator) {
        LOGE("List", "Cannot allocate List position indicator");
        message(state, "List allocation error");
        return;
    }
    lv_obj_add_event_cb(parent, mark_layout, LV_EVENT_SIZE_CHANGED, state);
    state->layout_dirty = true;
    message(state, "Loading");
}

void show(WidgetState* runtime) {
    State* state = get(runtime);
    if (!state || state->visible || !state->config || !state->container || !state->message || !state->indicator) return;
    state->visible = true;
    state->source = list_source_acquire(state->config->source);
    state->have_revision = false;
    state->layout_dirty = true;
    if (!state->source) message(state, "List allocation error");
}

void hide(WidgetState* runtime) {
    State* state = get(runtime);
    if (!state) return;
    state->visible = false;
    clear_rows(state);
    if (state->source) list_source_release(state->source);
    state->source = nullptr;
    state->focus = none;
    state->page = 0;
    state->have_revision = false;
    if (state->container) {
        lv_obj_scroll_to_y(state->container, 0, LV_ANIM_OFF);
    }
}

void destroy(WidgetState* runtime) {
    State* state = get(runtime);
    if (!state) return;
    hide(runtime);
    if (state->parent) {
        lv_obj_remove_event_cb_with_user_data(state->parent, mark_layout, state);
        if (state->container) lv_obj_delete(state->container);
        if (state->message) lv_obj_delete(state->message);
        if (state->indicator) lv_obj_delete(state->indicator);
    }
    heap_caps_free(state);
    set(runtime, nullptr);
}

void tick(lv_obj_t*, const WidgetConfig*, WidgetState* runtime) {
    State* state = get(runtime);
    if (!state || !state->config || !state->container || !state->message || !state->visible) return;
    layout(state);
    poll(state);
}

void cancel_input(WidgetState* runtime) { cancel(get(runtime)); }
const char* validate(JsonObjectConst button) { return list_validate_config(button); }
const WidgetPreview preview{"List", "list"};

#if HAS_MCP
void describe(JsonObject& out) {
    JsonArray fields = out.createNestedArray("config_fields");
    JsonObject source = fields.createNestedObject();
    source["name"] = "widget_data_binding";
    source["type"] = "string";
    source["required"] = true;
    source["max_length"] = CONFIG_LABEL_MAX_LEN - 1;
    source["description"] = "Literal collection token from list_sources; not a scalar binding or filter.";

    JsonObject id = fields.createNestedObject();
    id["name"] = "widget_list_id";
    id["type"] = "string";
    id["required"] = true;
    id["max_length"] = 32;
    id["pattern"] = "^[A-Za-z0-9_-]{1,32}$";
    id["description"] = "Identifier unique within the composed pad, used by list_nav.";

    for (const char* name : {"widget_list_label_top", "widget_list_label_bottom"}) {
        JsonObject field = fields.createNestedObject();
        field["name"] = name;
        field["type"] = "string";
        field["max_length"] = CONFIG_LABEL_MAX_LEN - 1;
        field["bindable"] = true;
        field["default"] = !strcmp(name, "widget_list_label_top") ? "[item:title|Untitled]" : "";
        field["description"] = "One-line row text; item fields and scalar bindings resolve in the row context.";
    }
    for (const char* name : {"widget_list_style_top", "widget_list_style_bottom"}) {
        JsonObject field = fields.createNestedObject();
        field["name"] = name;
        field["type"] = "string";
        field["max_length"] = CONFIG_LABEL_STYLE_MAX_LEN - 1;
        field["default"] = "";
        field["description"] = "Label style DSL; color may use bindings, other properties are static; only dot long mode is supported.";
    }
    for (const char* name : {"widget_list_bg_color", "widget_list_border_color",
                             "widget_list_selected_bg_color", "widget_list_selected_border_color",
                             "widget_list_selected_top_color", "widget_list_selected_bottom_color"}) {
        JsonObject color = fields.createNestedObject();
        color["name"] = name;
        color["type"] = "color";
        color["max_length"] = CONFIG_COLOR_MAX_LEN - 1;
        color["bindable"] = true;
        color["description"] = "Optional row color or item-context binding.";
    }
    for (const char* name : {"widget_list_indicator_track_color", "widget_list_indicator_color"}) {
        JsonObject color = fields.createNestedObject();
        color["name"] = name;
        color["type"] = "color";
        color["max_length"] = CONFIG_COLOR_MAX_LEN - 1;
        color["bindable"] = true;
        color["description"] = "Optional position indicator color or scalar binding; item bindings are not supported.";
    }

    struct Geometry { const char* name; unsigned minimum; unsigned maximum; unsigned value; };
    const Geometry geometry[] = {
        {"widget_list_visible_items", 0, LIST_SOURCE_MAX_ITEMS, 0},
        {"widget_list_padding", 0, 32, 6},
        {"widget_list_spacing", 0, 32, 4},
        {"widget_list_radius", 0, 64, 8},
    };
    for (const auto& spec : geometry) {
        JsonObject field = fields.createNestedObject();
        field["name"] = spec.name;
        field["type"] = "number";
        field["minimum"] = spec.minimum;
        field["maximum"] = spec.maximum;
        field["default"] = spec.value;
        field["description"] = !strcmp(spec.name, "widget_list_visible_items")
            ? "Row slots; paged navigation reserves two when items overflow. Use 0 for Auto or at least 3 with navigation enabled. Auto fits label fonts with a 44 px target minimum."
            : "Static whole pixels.";
    }
    for (const char* name : {"widget_list_border_width", "widget_list_selected_border_width"}) {
        JsonObject field = fields.createNestedObject();
        field["name"] = name;
        field["type"] = "number";
        field["minimum"] = 0;
        field["maximum"] = 32;
        field["description"] = "Optional border width in pixels; omitted uses button defaults.";
    }
    JsonObject selection = fields.createNestedObject();
    selection["name"] = "widget_list_allow_selection";
    selection["type"] = "boolean";
    selection["default"] = true;
    selection["description"] = "Keep the activated item selected; disabling this preserves item actions and transient feedback.";
    JsonObject navigation = fields.createNestedObject();
    navigation["name"] = "widget_list_show_navigation";
    navigation["type"] = "boolean";
    navigation["default"] = true;
    navigation["description"] = "Paged only: reserve top/bottom row slots for previous/next-page chevrons when items overflow. Controls use button appearance and default tap audio, never item actions.";
    JsonObject viewport = fields.createNestedObject();
    viewport["name"] = "widget_list_viewport";
    viewport["type"] = "string";
#if HAS_LVGL_EPAPER
    viewport["default"] = "paged";
#else
    viewport["default"] = "scroll";
#endif
    JsonArray choices = viewport.createNestedArray("enum");
    choices.add("scroll");
    choices.add("paged");
    viewport["description"] = "Vertical touch scrolling or non-animated fixed-row paging.";
    out["note"] = "Up to 20 items per source. Tap or hold runs item actions and optionally selects the item; item bindings cannot cross a pause or navigation. Page navigation preserves selection.";
}
#endif

}

static const WidgetType list_widget_type{
    "list", nullptr, list_widget_impl::create, nullptr, list_widget_impl::destroy, list_widget_impl::tick, nullptr, true,
#if HAS_MCP
    list_widget_impl::describe,
#else
    nullptr,
#endif
    list_widget_impl::show, list_widget_impl::hide, list_widget_impl::validate,
    &list_widget_impl::preview, list_widget_impl::cancel_input,
    list_parse_config, list_retain_config, list_release_config, list_visit_resources,
    nullptr, list_validate_pad_context, true
};
static struct ListWidgetRegistration {
    ListWidgetRegistration() { widget_register(&list_widget_type); }
} list_widget_registration;

bool list_widget_command(WidgetState* runtime, const char* command) {
    using namespace list_widget_impl;
    State* state = get(runtime);
    if (!state || !command || !state->visible || !current(state) ||
        !state->layout_ok || !state->count) return false;
    if (strcmp(command, "previous_page") && strcmp(command, "next_page")) return false;
    cancel(state);
    if (state->config->viewport == ListViewport::Paged) {
        const uint8_t pages = (state->count + state->capacity - 1) / state->capacity;
        uint8_t page = state->page;
        if (!strcmp(command, "previous_page")) {
            if (page) --page;
        } else if (page + 1 < pages) ++page;
        state->page = page;
        position_rows(state);
    } else {
        lv_obj_update_layout(state->container);
        const int32_t scroll = lv_obj_get_scroll_y(state->container);
        const int32_t target = scroll + (!strcmp(command, "previous_page") ? -state->height : state->height);
        lv_obj_scroll_to_y(state->container, target > 0 ? target : 0, LV_ANIM_OFF);
    }
    return true;
}

void list_widget_attach(WidgetState* runtime, lv_event_cb_t guard, void* context) {
    using namespace list_widget_impl;
    State* state = get(runtime);
    if (!state) return;
    state->guard = guard;
    state->guard_context = context;
    for (uint8_t i = 0; guard && i < state->row_count; ++i)
        lv_obj_add_event_cb(state->rows[i].object, guard,
            static_cast<lv_event_code_t>(LV_EVENT_ALL | LV_EVENT_PREPROCESS), context);
}

#endif
