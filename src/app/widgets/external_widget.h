#pragma once

#include "../board_config.h"

#if HAS_NATIVE_EXTENSIONS

#include "../pad_config.h"

struct ExternalWidgetConfig {
    char extension_id[CONFIG_EXTENSION_ID_MAX_LEN];
    char config[CONFIG_EXTENSION_CONFIG_MAX_LEN];
    uint16_t tick_interval_ms;
};

static_assert(sizeof(ExternalWidgetConfig) <= WIDGET_CONFIG_MAX_BYTES,
              "ExternalWidgetConfig exceeds widget data capacity");

inline const ExternalWidgetConfig* external_widget_config(const WidgetConfig* config) {
    return reinterpret_cast<const ExternalWidgetConfig*>(config->data);
}

inline uint32_t external_widget_instance_id(uint8_t page, uint8_t col, uint8_t row) {
    return (static_cast<uint32_t>(page) << 16) |
           (static_cast<uint32_t>(col) << 8) | row;
}

#endif