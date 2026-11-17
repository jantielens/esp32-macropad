#include "list_config.h"

#if HAS_DISPLAY && HAS_PSRAM

#include "../item_binding.h"
#include "../button_defaults.h"
#include "../pad_composition.h"
#include <esp_heap_caps.h>
#include <atomic>
#include <memory>
#include <new>
#include <stdio.h>
#include <string.h>

namespace list_config_impl {

#if HAS_LVGL_EPAPER
constexpr const char* default_viewport = "paged";
#else
constexpr const char* default_viewport = "scroll";
#endif

struct Storage {
    std::atomic<uint32_t> references;
    ListConfig config;
};
static_assert(sizeof(Storage*) <= WIDGET_CONFIG_MAX_BYTES, "List pointer exceeds widget storage");

Storage* storage(const uint8_t* data) {
    Storage* owner = nullptr;
    memcpy(&owner, data, sizeof(owner));
    return owner;
}

bool valid_string(JsonObjectConst button, const char* field, size_t capacity, bool required = false) {
    if (!button.containsKey(field)) return !required;
    if (!button[field].is<const char*>()) return false;
    JsonString value = button[field].as<JsonString>();
    return value.size() < capacity && strlen(value.c_str()) == value.size();
}

bool valid_style(const char* style) {
    char buf[CONFIG_LABEL_STYLE_MAX_LEN];
    strlcpy(buf, style, sizeof(buf));
    char* cursor = buf;
    while (cursor && *cursor) {
        char* token = cursor;
        bool quoted = false;
        unsigned depth = 0;
        for (; *cursor; ++cursor) {
            if (*cursor == '"') quoted = !quoted;
            else if (!quoted) {
                if (*cursor == '[') ++depth;
                else if (*cursor == ']' && depth) --depth;
                else if (*cursor == ';' && !depth) break;
            }
        }
        if (*cursor) *cursor++ = '\0';
        else cursor = nullptr;
        while (*token == ' ') ++token;
        char* colon = strchr(token, ':');
        if (!colon) continue;
        *colon++ = '\0';
        if (!strcmp(token, "mode") && strcmp(colon, "dot")) return false;
        if (strcmp(token, "color") && strchr(colon, '[')) return false;
    }
    return true;
}

void copy_field(JsonObjectConst button, const char* name, char* target, size_t size,
                const char* fallback = "") {
    strlcpy(target, button[name] | fallback, size);
}

const char* field_error(JsonObjectConst button, const char* field, const char* error) {
    static thread_local std::unique_ptr<char[]> message;
    constexpr size_t capacity = 640;
    if (!message) message.reset(new (std::nothrow) char[capacity]);
    if (!message) return "List validation failed; out of memory while formatting error details";
    snprintf(message.get(), capacity, "List '%.32s', field '%s': %s",
             button["widget_list_id"] | "", field, error);
    return message.get();
}
}

const char* list_validate_config(JsonObjectConst button, const PadBinding* bindings,
                                 uint8_t binding_count) {
    using namespace list_config_impl;
    if (!valid_string(button, "widget_data_binding", CONFIG_LABEL_MAX_LEN, true))
        return "List source must be a collection token of at most 191 bytes";
    const ListSourceDescriptor* source = list_source_find(button["widget_data_binding"].as<const char*>());
    if (!source) return "unknown List collection source";
    if (!valid_string(button, "widget_list_id", 33, true) ||
        !list_identifier_valid(button["widget_list_id"].as<const char*>()))
        return "List ID must contain 1-32 ASCII letters, digits, _ or -";
    for (const char* field : {"widget_data_binding_2", "widget_data_binding_3", "widget_data_binding_4"}) {
        if (button.containsKey(field) && !button[field].isNull() &&
            (!button[field].is<const char*>() || button[field].as<const char*>()[0]))
            return "obsolete List filter or extra source is not supported";
    }
    for (const char* field : {"widget_list_label_top", "widget_list_label_bottom", "widget_list_bg_color",
                              "widget_list_border_color", "widget_list_selected_bg_color",
                              "widget_list_selected_border_color", "widget_list_selected_top_color",
                              "widget_list_selected_bottom_color"}) {
        if (!valid_string(button, field, CONFIG_LABEL_MAX_LEN)) return "List row text or color is too long or not a string";
        if (button.containsKey(field)) {
            const char* error = item_binding_validate(button[field].as<const char*>(), source, bindings, binding_count);
            if (error) return field_error(button, field, error);
        }
    }
    for (const char* field : {"widget_list_bg_color", "widget_list_border_color",
                              "widget_list_selected_bg_color", "widget_list_selected_border_color",
                              "widget_list_selected_top_color", "widget_list_selected_bottom_color",
                              "widget_list_indicator_track_color", "widget_list_indicator_color"}) {
        if (!valid_string(button, field, CONFIG_COLOR_MAX_LEN)) return "List color is too long or not a string";
        const char* color = button[field] | "";
        uint32_t parsed_color;
        if (*color && !parse_hex_color(color, &parsed_color) && !strchr(color, '['))
            return "List color must be hex or a binding";
    }
    for (const char* field : {"widget_list_indicator_track_color", "widget_list_indicator_color"}) {
        const char* error = item_binding_validate(button[field] | "", nullptr, bindings, binding_count);
        if (error) return field_error(button, field,
            !strcmp(error, "item binding is unavailable in this context")
                ? "position indicator colors have no item context; use a scalar binding instead"
                : error);
    }
    for (const char* field : {"widget_list_style_top", "widget_list_style_bottom"}) {
        if (!valid_string(button, field, CONFIG_LABEL_STYLE_MAX_LEN)) return "List style is too long or not a string";
        const char* style = button[field] | "";
        if (!valid_style(style)) return field_error(button, field, "List styles allow only dot mode and static non-color properties");
        const char* error = item_binding_validate(style, source, bindings, binding_count);
        if (error) return field_error(button, field, error);
    }
    struct Range { const char* field; unsigned min; unsigned max; };
    const Range ranges[] = {{"widget_list_visible_items", 0, LIST_SOURCE_MAX_ITEMS},
                            {"widget_list_border_width", 0, 32}, {"widget_list_selected_border_width", 0, 32},
                            {"widget_list_padding", 0, 32}, {"widget_list_spacing", 0, 32},
                            {"widget_list_radius", 0, 64}};
    for (const auto& range : ranges) {
        if (button.containsKey(range.field) &&
            (!button[range.field].is<unsigned>() || button[range.field].as<unsigned>() < range.min ||
             button[range.field].as<unsigned>() > range.max))
            return "List geometry must use whole pixels within the supported range";
    }
    if (button.containsKey("widget_list_row_height")) return "List row height has been replaced by visible items";
    if (button.containsKey("widget_list_allow_selection") && !button["widget_list_allow_selection"].is<bool>())
        return "List allow selection must be a boolean";
    if (!valid_string(button, "widget_list_viewport", 7)) return "List viewport must be scroll or paged";
    const char* viewport = button["widget_list_viewport"] | default_viewport;
    if (strcmp(viewport, "scroll") && strcmp(viewport, "paged"))
        return "List viewport must be scroll or paged";
    if (button.containsKey("widget_list_show_navigation") && !button["widget_list_show_navigation"].is<bool>())
        return "List show navigation must be a boolean";
    const unsigned visible_items = button["widget_list_visible_items"] | 0U;
    if (!strcmp(viewport, "paged") && (button["widget_list_show_navigation"] | true) &&
        visible_items && visible_items < 3)
        return "Paged List navigation requires at least 3 visible row slots, or Auto";
    return nullptr;
}

const char* list_validate_actions(JsonObjectConst button, const PadBinding* bindings, uint8_t binding_count) {
    const ListSourceDescriptor* source = list_source_find(button["widget_data_binding"] | "");
    if (!source) return "unknown List collection source";
    for (const char* field : {"actions", "lp_actions"}) {
        if (!button.containsKey(field) || button[field].isNull()) continue;
        if (!button[field].is<JsonArrayConst>()) return "List actions must be an array";
        const char* error = item_binding_validate_actions(button[field].as<JsonArrayConst>(),
                                                          source, bindings, binding_count);
        if (error) return list_config_impl::field_error(button, field, error);
    }
    return nullptr;
}

namespace list_config_impl {

const char* validate_without_item(JsonVariantConst value, const PadBinding* bindings,
                                  uint8_t count, unsigned depth = 0) {
    if (depth > 16) return "pad validation nesting too deep";
    if (value.is<const char*>()) return item_binding_validate(value.as<const char*>(), nullptr, bindings, count);
    if (value.is<JsonObjectConst>()) {
        for (JsonPairConst pair : value.as<JsonObjectConst>()) {
            const char* error = validate_without_item(pair.value(), bindings, count, depth + 1);
            if (error) return error;
        }
    } else if (value.is<JsonArrayConst>()) {
        for (JsonVariantConst child : value.as<JsonArrayConst>()) {
            const char* error = validate_without_item(child, bindings, count, depth + 1);
            if (error) return error;
        }
    }
    return nullptr;
}

bool row_field(const char* name) {
    return !strcmp(name, "widget_list_label_top") || !strcmp(name, "widget_list_label_bottom") ||
           !strcmp(name, "widget_list_style_top") || !strcmp(name, "widget_list_style_bottom") ||
           !strcmp(name, "widget_list_bg_color") || !strcmp(name, "widget_list_border_color") ||
           !strcmp(name, "widget_list_selected_bg_color") || !strcmp(name, "widget_list_selected_border_color") ||
           !strcmp(name, "widget_list_selected_top_color") || !strcmp(name, "widget_list_selected_bottom_color");
}

const char* validate_context_button(JsonObjectConst button, const PadBinding* bindings, uint8_t count,
                                    const char** ids, uint8_t& id_count) {
    const bool is_list = !strcmp(button["widget_type"] | "", "list");
    if (is_list) {
        const char* error = list_validate_config(button, bindings, count);
        if (error) return error;
        error = list_validate_actions(button, bindings, count);
        if (error) return error;
        const char* id = button["widget_list_id"];
        for (uint8_t index = 0; index < id_count; ++index)
            if (!strcmp(ids[index], id)) return "List IDs must be unique within the composed pad";
        if (id_count >= MAX_PAD_BUTTONS) return "too many List widgets";
        ids[id_count++] = id;
    }
    for (JsonPairConst pair : button) {
        const char* name = pair.key().c_str();
        if (is_list && (row_field(name) || !strcmp(name, "widget_data_binding") ||
                        !strcmp(name, "actions") || !strcmp(name, "lp_actions"))) continue;
        const char* error = validate_without_item(pair.value(), bindings, count);
        if (error) {
            if (is_list) return field_error(button, name, error);
            return error;
        }
    }
    return nullptr;
}

const char* validate_targets(JsonVariantConst value, const char* const* ids, uint8_t count) {
    if (value.is<JsonArrayConst>()) {
        for (JsonVariantConst child : value.as<JsonArrayConst>()) {
            const char* error = validate_targets(child, ids, count);
            if (error) return error;
        }
    } else if (value.is<JsonObjectConst>()) {
        JsonObjectConst object = value.as<JsonObjectConst>();
        if (!strcmp(object["type"] | "", ACTION_TYPE_LIST_NAVIGATION)) {
            const char* target = object["target"] | "";
            for (uint8_t index = 0; index < count; ++index)
                if (!strcmp(ids[index], target)) return nullptr;
            return "list_nav target must identify a List on the composed pad";
        }
    }
    return nullptr;
}

const char* validate_button_targets(JsonObjectConst button, const char* const* ids, uint8_t count) {
    for (const char* field : {"actions", "lp_actions", "widget_numericrocker_action"}) {
        const char* error = validate_targets(button[field], ids, count);
        if (error) return error;
    }
    return nullptr;
}
}

const char* list_validate_pad_context(JsonObjectConst pad, const PadComposedContext& context) {
    using namespace list_config_impl;
    for (JsonPairConst pair : pad) {
        const char* name = pair.key().c_str();
        if (!strcmp(name, "bindings") || !strcmp(name, "buttons") || !strcmp(name, "overlay_buttons")) continue;
        const char* error = validate_without_item(pair.value(), context.bindings, context.binding_count);
        if (error) return error;
    }

    const char* ids[MAX_PAD_BUTTONS] = {};
    uint8_t id_count = 0;
    for (uint8_t index = 0; index < context.button_count; ++index) {
        const char* error = validate_context_button(context.buttons[index], context.bindings,
                                                    context.binding_count, ids, id_count);
        if (error) return error;
    }
    const char* error = validate_targets(pad["pad_actions"], ids, id_count);
    if (error) return error;
    for (uint8_t index = 0; index < context.button_count; ++index) {
        error = validate_button_targets(context.buttons[index], ids, id_count);
        if (error) return error;
    }
    return nullptr;
}

bool list_parse_config(const JsonObject& button, uint8_t* data) {
    using namespace list_config_impl;
    // Pad aliases require the composed pad context and are checked by the caller.
    if (list_validate_config(button)) return false;
    void* memory = heap_caps_malloc(sizeof(Storage), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) return false;
    auto* owner = new (memory) Storage{};
    owner->references.store(1, std::memory_order_relaxed);
    auto& config = owner->config;
    config.source = list_source_find(button["widget_data_binding"] | "");
    copy_field(button, "widget_list_id", config.id, sizeof(config.id));
    copy_field(button, "widget_list_label_top", config.label_top, sizeof(config.label_top), "[item:title|Untitled]");
    copy_field(button, "widget_list_label_bottom", config.label_bottom, sizeof(config.label_bottom));
    copy_field(button, "widget_list_style_top", config.style_top, sizeof(config.style_top));
    copy_field(button, "widget_list_style_bottom", config.style_bottom, sizeof(config.style_bottom));
    const ButtonDefaults* defaults = button_defaults_get();
    const char* bg = defaults->bg_color[0] ? defaults->bg_color : DEFAULT_BUTTON_BG_COLOR;
    const char* border = defaults->border_color[0] ? defaults->border_color : DEFAULT_BUTTON_BORDER_COLOR;
    const char* width = defaults->border_width[0] ? defaults->border_width : DEFAULT_BUTTON_BORDER_WIDTH;
    strlcpy(config.default_border_width, width, sizeof(config.default_border_width));
    strlcpy(config.default_border_color, border, sizeof(config.default_border_color));
    strlcpy(config.default_foreground, defaults->fg_color[0] ? defaults->fg_color : DEFAULT_BUTTON_FG_COLOR,
            sizeof(config.default_foreground));
    copy_field(button, "widget_list_bg_color", config.background, sizeof(config.background), bg);
    copy_field(button, "widget_list_border_color", config.border_color, sizeof(config.border_color), border);
    if (!config.background[0]) strlcpy(config.background, bg, sizeof(config.background));
    if (!config.border_color[0]) strlcpy(config.border_color, border, sizeof(config.border_color));
    if (button.containsKey("widget_list_border_width"))
        snprintf(config.border_width, sizeof(config.border_width), "%u", button["widget_list_border_width"].as<unsigned>());
    else strlcpy(config.border_width, width, sizeof(config.border_width));
    copy_field(button, "widget_list_selected_bg_color", config.selected_background, sizeof(config.selected_background));
    copy_field(button, "widget_list_selected_border_color", config.selected_border_color, sizeof(config.selected_border_color));
    copy_field(button, "widget_list_selected_top_color", config.selected_top_color, sizeof(config.selected_top_color));
    copy_field(button, "widget_list_selected_bottom_color", config.selected_bottom_color, sizeof(config.selected_bottom_color));
    copy_field(button, "widget_list_indicator_track_color", config.indicator_track_color, sizeof(config.indicator_track_color), bg);
    copy_field(button, "widget_list_indicator_color", config.indicator_color, sizeof(config.indicator_color), config.default_foreground);
    if (!config.indicator_track_color[0]) strlcpy(config.indicator_track_color, bg, sizeof(config.indicator_track_color));
    if (!config.indicator_color[0]) strlcpy(config.indicator_color, config.default_foreground, sizeof(config.indicator_color));
    config.selected_border_width = button.containsKey("widget_list_selected_border_width")
        ? button["widget_list_selected_border_width"].as<int>() : -1;
    config.visible_items = button["widget_list_visible_items"] | 0U;
    config.padding = button["widget_list_padding"] | 6U;
    config.spacing = button["widget_list_spacing"] | 4U;
    config.radius = button["widget_list_radius"] | 8U;
    config.viewport = !strcmp(button["widget_list_viewport"] | default_viewport, "paged")
        ? ListViewport::Paged : ListViewport::Scroll;
    config.allow_selection = button["widget_list_allow_selection"] | true;
    config.show_navigation = button["widget_list_show_navigation"] | true;
    memcpy(data, &owner, sizeof(owner));
    return true;
}

void list_retain_config(uint8_t* data) {
    auto* owner = list_config_impl::storage(data);
    if (owner) owner->references.fetch_add(1, std::memory_order_relaxed);
}

void list_release_config(uint8_t* data) {
    auto* owner = list_config_impl::storage(data);
    if (!owner) return;
    memset(data, 0, sizeof(owner));
    if (owner->references.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        owner->~Storage();
        heap_caps_free(owner);
    }
}

const ListConfig* list_config(const uint8_t* data) {
    const auto* owner = list_config_impl::storage(data);
    return owner ? &owner->config : nullptr;
}

void list_visit_resources(const uint8_t* data, WidgetResourceVisitor visitor, void* context) {
    const ListConfig* config = list_config(data);
    if (!config || !visitor) return;
    visitor(WidgetResourceKind::Binding, "widget_list_label_top", 0, config->label_top, context);
    visitor(WidgetResourceKind::Binding, "widget_list_label_bottom", 0, config->label_bottom, context);
    visitor(WidgetResourceKind::Binding, "widget_list_style_top", 0, config->style_top, context);
    visitor(WidgetResourceKind::Binding, "widget_list_style_bottom", 0, config->style_bottom, context);
    visitor(WidgetResourceKind::Binding, "widget_list_bg_color", 0, config->background, context);
    visitor(WidgetResourceKind::Binding, "widget_list_border_color", 0, config->border_color, context);
    visitor(WidgetResourceKind::Binding, "widget_list_border_width", 0, config->border_width, context);
    visitor(WidgetResourceKind::Binding, "button_default_border_width", 0, config->default_border_width, context);
    visitor(WidgetResourceKind::Binding, "button_default_border_color", 0, config->default_border_color, context);
    visitor(WidgetResourceKind::Binding, "button_default_fg_color", 0, config->default_foreground, context);
    visitor(WidgetResourceKind::Binding, "widget_list_selected_bg_color", 0, config->selected_background, context);
    visitor(WidgetResourceKind::Binding, "widget_list_selected_border_color", 0, config->selected_border_color, context);
    visitor(WidgetResourceKind::Binding, "widget_list_selected_top_color", 0, config->selected_top_color, context);
    visitor(WidgetResourceKind::Binding, "widget_list_selected_bottom_color", 0, config->selected_bottom_color, context);
    visitor(WidgetResourceKind::Binding, "widget_list_indicator_track_color", 0, config->indicator_track_color, context);
    visitor(WidgetResourceKind::Binding, "widget_list_indicator_color", 0, config->indicator_color, context);
}

#endif
