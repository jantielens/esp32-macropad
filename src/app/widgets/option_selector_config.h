#pragma once

#include "../board_config.h"

#if HAS_DISPLAY

#include "../pad_config.h"
#include "widget_registry.h"
#include <ArduinoJson.h>

constexpr uint8_t OPTION_SELECTOR_SLOTS = 5;
constexpr uint8_t OPTION_SELECTOR_DEFAULT_INSET = 4;
constexpr uint8_t OPTION_SELECTOR_MAX_INSET = 64;

enum class OptionSelectorContent : uint8_t { None, Label, Icon };
enum class OptionSelectorMode : uint8_t { None, Bound, Local };

struct OptionSelectorSlot {
    char value[CONFIG_BINDABLE_SHORT_LEN];
    union {
        char label[CONFIG_LABEL_MAX_LEN];
        struct {
            char source_id[CONFIG_ICON_SOURCE_ID_MAX_LEN];
            bool mono;
        } icon;
    } content;
    char foreground[CONFIG_COLOR_MAX_LEN];
    char background[CONFIG_COLOR_MAX_LEN];
    char state[CONFIG_BTN_STATE_MAX_LEN];
    OptionSelectorContent kind;
};

struct OptionSelectorConfig {
    OptionSelectorSlot slots[OPTION_SELECTOR_SLOTS];
    char group_color[CONFIG_COLOR_MAX_LEN];
    char option_background[CONFIG_COLOR_MAX_LEN];
    char selected_foreground[8];
    char selected_background[8];
    uint16_t font_size;
    uint8_t icon_size_pct;
    uint8_t icon_badge_padding;
    uint8_t option_inset;
    uint8_t height_pct;
    OptionSelectorMode mode;
    bool option_backgrounds;
    bool group_background;
};

const char* option_selector_validate_config(JsonObjectConst button);
bool option_selector_parse_config(const JsonObject& button, uint8_t* data);
void option_selector_retain_config(uint8_t* data);
void option_selector_release_config(uint8_t* data);
const OptionSelectorConfig* option_selector_config(const uint8_t* data);
void option_selector_visit_resources(const uint8_t* data, WidgetResourceVisitor visitor,
                                     void* context);

#endif
