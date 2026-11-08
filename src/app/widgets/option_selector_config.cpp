#include "option_selector_config.h"

#if HAS_DISPLAY

#include "../pad_validate.h"
#include "widget_value.h"
#include <esp_heap_caps.h>
#include <atomic>
#include <new>
#include <stdio.h>
#include <string.h>

namespace option_selector_config_impl {

struct SelectorStorage {
    std::atomic<uint32_t> references;
    OptionSelectorConfig config;
};

static_assert(sizeof(SelectorStorage*) <= WIDGET_CONFIG_MAX_BYTES, "Selector pointer exceeds widget storage");

SelectorStorage* storage(const uint8_t* data) {
    SelectorStorage* result = nullptr;
    memcpy(&result, data, sizeof(result));
    return result;
}

void slot_field(char* output, size_t size, const char* field, unsigned slot) {
    if (slot == 0) snprintf(output, size, "widget_option_%s", field);
    else snprintf(output, size, "widget_option_%s_%u", field, slot + 1);
}

bool valid_string(JsonObjectConst button, const char* field, size_t capacity) {
    if (!button.containsKey(field) || button[field].isNull()) return true;
    if (!button[field].is<const char*>()) return false;
    const JsonString value = button[field].as<JsonString>();
    return value.size() < capacity && strlen(value.c_str()) == value.size();
}

bool valid_id(const char* value) {
    if (!value || !*value) return false;
    for (const char* p = value; *p; ++p) {
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '_' || *p == '-')) return false;
    }
    return true;
}

bool literal_color(JsonObjectConst button, const char* name) {
    if (!button.containsKey(name)) return true;
    if (!button[name].is<const char*>()) return false;
    const char* value = button[name].as<const char*>();
    uint32_t color;
    return strlen(value) == 7 && value[0] == '#' && parse_hex_color(value, &color);
}

void copy_field(JsonObjectConst button, const char* name, char* target, size_t size, const char* fallback = "") {
    const char* value = button[name] | fallback;
    strlcpy(target, value, size);
}

} // namespace option_selector_config_impl

const char* option_selector_validate_config(JsonObjectConst button) {
    using namespace option_selector_config_impl;
    const char* mode = button["widget_option_mode"] | "none";
    if (strcmp(mode, "none") && strcmp(mode, "bound") && strcmp(mode, "local"))
        return "option mode must be none, bound, or local";
    if (!valid_string(button, "widget_option_group_color", CONFIG_COLOR_MAX_LEN))
        return "option group color must be a string of at most 191 bytes";
    if (!valid_string(button, "widget_option_background_color", CONFIG_COLOR_MAX_LEN))
        return "option background color must be a string of at most 191 bytes";
    if (!literal_color(button, "widget_option_selected_fg_color") ||
        !literal_color(button, "widget_option_selected_bg_color"))
        return "selected option colors must be #RRGGBB literals";
    for (const char* field : {"widget_option_backgrounds", "widget_option_group_background"}) {
        if (button.containsKey(field) && !button[field].is<bool>())
            return "option background controls must be boolean";
    }
    for (const char* field : {"widget_option_font_size", "widget_option_icon_size_pct",
                              "widget_option_icon_badge_padding", "widget_option_inset",
                              "widget_option_height_pct"}) {
        if (button.containsKey(field) && !button[field].is<unsigned>())
            return "option sizes must be nonnegative integers";
    }
    if ((button["widget_option_font_size"] | 0U) > 512 ||
        (button["widget_option_icon_size_pct"] | 0U) > 100 ||
        (button["widget_option_icon_badge_padding"] | 0U) > MAX_ICON_BADGE_PADDING ||
        (button["widget_option_inset"] | 0U) > OPTION_SELECTOR_MAX_INSET ||
        (button["widget_option_height_pct"] | 100U) < 1 ||
        (button["widget_option_height_pct"] | 100U) > 100)
        return "option size is out of range";
    if (!valid_string(button, "widget_data_binding", CONFIG_LABEL_MAX_LEN))
        return "option state source must be a string of at most 191 bytes";

    const char* values[OPTION_SELECTOR_SLOTS] = {};
    for (unsigned index = 0; index < OPTION_SELECTOR_SLOTS; ++index) {
        char field[48];
        slot_field(field, sizeof(field), "label", index);
        if (!valid_string(button, field, CONFIG_LABEL_MAX_LEN)) return "option label must be a string of at most 191 bytes";
        const char* label = button[field] | "";
        slot_field(field, sizeof(field), "icon", index);
        const bool has_icon = !button[field].isNull();
        if (has_icon && !button[field].is<JsonObjectConst>()) return "option icon must be an object";
        if (has_icon) {
            const char* error = pad_validate_icon_metadata(button[field].as<JsonObjectConst>());
            if (error) return error;
        }
        slot_field(field, sizeof(field), "value", index);
        if (!valid_string(button, field, CONFIG_BINDABLE_SHORT_LEN)) return "option value must be a string of at most 63 bytes";
        const char* value = button[field] | "";
        if ((label[0] != '\0') + has_icon > 1) return "an option cannot have both a label and an icon";
        const bool has_content = label[0] || has_icon;
        if (value[0] && (!has_content || !valid_id(value)))
            return "option values require content and may contain only ASCII letters, digits, _ or -";
        if (has_content && !value[0] && !strcmp(mode, "bound"))
            return "bound options require a value";
        for (const char* suffix : {"fg_color", "bg_color", "state"}) {
            slot_field(field, sizeof(field), suffix, index);
            if (!valid_string(button, field, CONFIG_LABEL_MAX_LEN))
                return "option color and state fields must be strings of at most 191 bytes";
        }
        if (value[0] && strcmp(mode, "none")) {
            for (unsigned previous = 0; previous < index; ++previous)
                if (values[previous] && widget_value_equal(value, values[previous]))
                    return "option values must be distinct in bound and local modes";
        }
        values[index] = value[0] ? value : nullptr;
    }
    return nullptr;
}

bool option_selector_parse_config(const JsonObject& button, uint8_t* data) {
    using namespace option_selector_config_impl;
    if (option_selector_validate_config(button)) return false;
    void* memory = heap_caps_malloc(sizeof(SelectorStorage), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) return false;
    auto* owner = new (memory) SelectorStorage{};
    owner->references.store(1, std::memory_order_relaxed);
    OptionSelectorConfig& config = owner->config;
    const char* mode = button["widget_option_mode"] | "none";
    config.mode = !strcmp(mode, "bound") ? OptionSelectorMode::Bound :
                  !strcmp(mode, "local") ? OptionSelectorMode::Local : OptionSelectorMode::None;
    config.option_backgrounds = button["widget_option_backgrounds"] | false;
    config.group_background = button["widget_option_group_background"] | false;
    copy_field(button, "widget_option_group_color", config.group_color, sizeof(config.group_color));
    copy_field(button, "widget_option_background_color", config.option_background, sizeof(config.option_background));
    copy_field(button, "widget_option_selected_fg_color", config.selected_foreground,
               sizeof(config.selected_foreground), DEFAULT_WIDGET_INDICATOR_COLOR);
    copy_field(button, "widget_option_selected_bg_color", config.selected_background,
               sizeof(config.selected_background), DEFAULT_WIDGET_SERIES_COLOR);
    config.font_size = button["widget_option_font_size"] | 0;
    config.icon_size_pct = button["widget_option_icon_size_pct"] | 0;
    config.icon_badge_padding = button["widget_option_icon_badge_padding"] | 0;
    config.option_inset = button["widget_option_inset"] | OPTION_SELECTOR_DEFAULT_INSET;
    config.height_pct = button["widget_option_height_pct"] | 100;
    for (unsigned index = 0; index < OPTION_SELECTOR_SLOTS; ++index) {
        OptionSelectorSlot& slot = config.slots[index];
        char field[48];
        slot_field(field, sizeof(field), "value", index);
        copy_field(button, field, slot.value, sizeof(slot.value));
        slot_field(field, sizeof(field), "icon", index);
        if (button[field].is<JsonObjectConst>()) {
            JsonObjectConst icon = button[field].as<JsonObjectConst>();
            slot.kind = OptionSelectorContent::Icon;
            copy_field(icon, "source_id", slot.content.icon.source_id, sizeof(slot.content.icon.source_id));
            slot.content.icon.mono = !strcmp(icon["kind"] | "", "mono");
        } else {
            slot_field(field, sizeof(field), "label", index);
            copy_field(button, field, slot.content.label, sizeof(slot.content.label));
            slot.kind = slot.content.label[0] ? OptionSelectorContent::Label : OptionSelectorContent::None;
        }
        slot_field(field, sizeof(field), "fg_color", index);
        copy_field(button, field, slot.foreground, sizeof(slot.foreground));
        slot_field(field, sizeof(field), "bg_color", index);
        copy_field(button, field, slot.background, sizeof(slot.background));
        slot_field(field, sizeof(field), "state", index);
        copy_field(button, field, slot.state, sizeof(slot.state));
    }
    memcpy(data, &owner, sizeof(owner));
    return true;
}

void option_selector_retain_config(uint8_t* data) {
    auto* owner = option_selector_config_impl::storage(data);
    if (owner) owner->references.fetch_add(1, std::memory_order_relaxed);
}

void option_selector_release_config(uint8_t* data) {
    auto* owner = option_selector_config_impl::storage(data);
    if (!owner) return;
    memset(data, 0, sizeof(owner));
    if (owner->references.fetch_sub(1, std::memory_order_acq_rel) == 1) {
        owner->~SelectorStorage();
        heap_caps_free(owner);
    }
}

const OptionSelectorConfig* option_selector_config(const uint8_t* data) {
    const auto* owner = option_selector_config_impl::storage(data);
    return owner ? &owner->config : nullptr;
}

void option_selector_visit_resources(const uint8_t* data, WidgetResourceVisitor visitor,
                                     void* context) {
    using option_selector_config_impl::slot_field;
    const OptionSelectorConfig* config = option_selector_config(data);
    if (!config || !visitor) return;
    visitor(WidgetResourceKind::Binding, "widget_option_group_color", 0,
            config->group_color, context);
    visitor(WidgetResourceKind::Binding, "widget_option_background_color", 0,
            config->option_background, context);
    for (unsigned index = 0; index < OPTION_SELECTOR_SLOTS; ++index) {
        const OptionSelectorSlot& slot = config->slots[index];
        char field[48];
        if (slot.kind == OptionSelectorContent::Icon) {
            slot_field(field, sizeof(field), "icon", index);
            visitor(slot.content.icon.mono ? WidgetResourceKind::MonoIcon : WidgetResourceKind::ColorIcon,
                    field, index + 1, slot.content.icon.source_id, context);
        } else if (slot.kind == OptionSelectorContent::Label) {
            slot_field(field, sizeof(field), "label", index);
            visitor(WidgetResourceKind::Binding, field, index + 1, slot.content.label, context);
        }
        const char* names[] = {"fg_color", "bg_color", "state"};
        const char* values[] = {slot.foreground, slot.background, slot.state};
        for (unsigned n = 0; n < 3; ++n) {
            slot_field(field, sizeof(field), names[n], index);
            visitor(WidgetResourceKind::Binding, field, index + 1, values[n], context);
        }
    }
}

#endif
