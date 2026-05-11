#include "list_provider.h"

#if HAS_DISPLAY

#include "pad_config.h"
#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>

// Cache pad items at init time — provide() is called from the LVGL task
// which has a PSRAM stack, making LittleFS reads (SPI flash) unsafe.
static ListItem s_cached_items[LIST_MAX_ITEMS];
static uint8_t  s_cached_count = 0;

static uint8_t pads_provide(ListItem* items, uint8_t max_items) {
    uint8_t n = (s_cached_count < max_items) ? s_cached_count : max_items;
    memcpy(items, s_cached_items, n * sizeof(ListItem));
    return n;
}

static void pads_cache_rebuild() {
    s_cached_count = 0;
    for (uint8_t i = 0; i < MAX_PADS && s_cached_count < LIST_MAX_ITEMS; i++) {
        if (!pad_config_exists(i)) continue;

        snprintf(s_cached_items[s_cached_count].id, LIST_ITEM_ID_MAX, "pad_%u", i);

        size_t len = 0;
        char* raw = pad_config_read_raw(i, &len);
        if (raw) {
            JsonDocument filter;
            filter["name"] = true;
            JsonDocument doc;
            if (deserializeJson(doc, raw, len, DeserializationOption::Filter(filter)) == DeserializationError::Ok
                && doc["name"].is<const char*>() && strlen(doc["name"].as<const char*>()) > 0) {
                snprintf(s_cached_items[s_cached_count].label, LIST_ITEM_LABEL_MAX, "Pad %u: %s", i, doc["name"].as<const char*>());
            } else {
                snprintf(s_cached_items[s_cached_count].label, LIST_ITEM_LABEL_MAX, "Pad %u", i);
            }
            free(raw);
        } else {
            snprintf(s_cached_items[s_cached_count].label, LIST_ITEM_LABEL_MAX, "Pad %u", i);
        }
        s_cached_count++;
    }
}

static const ListProvider pads_provider = { "pads", "Select Pad", pads_provide };

// Rebuild the cached pads list. Safe to call from any task with a flash-safe
// stack (web server, setup) — NOT from the LVGL task (PSRAM stack).
void list_provider_pads_invalidate() {
    pads_cache_rebuild();
}

void list_provider_pads_init() {
    pads_cache_rebuild();
    list_provider_register(&pads_provider);
}

#endif // HAS_DISPLAY
