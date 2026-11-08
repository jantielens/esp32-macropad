#include "list_provider.h"

#if HAS_DISPLAY

#include "pad_config.h"
#include "config_psram.h"
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <stdio.h>
#include <string.h>

// Cache pad items at init time — provide() is called from the LVGL task
// which has a PSRAM stack, making LittleFS reads (SPI flash) unsafe.
struct PadListEntry {
    ListItem item;
    bool exists;
};
static PadListEntry* s_cached_items = nullptr;
static portMUX_TYPE s_cache_mux = portMUX_INITIALIZER_UNLOCKED;

static uint8_t pads_provide(ListItem* items, uint8_t max_items) {
    if (!items || !max_items) return 0;
    uint8_t n = 0;
    portENTER_CRITICAL(&s_cache_mux);
    if (s_cached_items) {
        for (uint8_t i = 0; i < MAX_PADS && n < max_items && n < LIST_MAX_ITEMS; i++) {
            if (s_cached_items[i].exists) items[n++] = s_cached_items[i].item;
        }
    }
    portEXIT_CRITICAL(&s_cache_mux);
    return n;
}

static void pads_cache_rebuild() {
    if (!s_cached_items) {
        PadListEntry* entries = static_cast<PadListEntry*>(config_psram_alloc(
            sizeof(PadListEntry) * MAX_PADS, "pads_list"));
        if (!entries) return;
        portENTER_CRITICAL(&s_cache_mux);
        s_cached_items = entries;
        portEXIT_CRITICAL(&s_cache_mux);
    }
    for (uint8_t i = 0; i < MAX_PADS; i++) {
        if (!pad_config_exists(i)) continue;

        PadListEntry entry = {};
        entry.exists = true;
        snprintf(entry.item.id, LIST_ITEM_ID_MAX, "pad_%u", i);

        size_t len = 0;
        char* raw = pad_config_read_raw(i, &len);
        if (raw) {
            JsonDocument filter;
            filter["name"] = true;
            JsonDocument doc;
            if (deserializeJson(doc, raw, len, DeserializationOption::Filter(filter)) == DeserializationError::Ok
                && doc["name"].is<const char*>() && strlen(doc["name"].as<const char*>()) > 0) {
                snprintf(entry.item.label, LIST_ITEM_LABEL_MAX, "Pad %u: %s", i, doc["name"].as<const char*>());
            } else {
                snprintf(entry.item.label, LIST_ITEM_LABEL_MAX, "Pad %u", i);
            }
            free(raw);
        } else {
            snprintf(entry.item.label, LIST_ITEM_LABEL_MAX, "Pad %u", i);
        }
        portENTER_CRITICAL(&s_cache_mux);
        s_cached_items[i] = entry;
        portEXIT_CRITICAL(&s_cache_mux);
    }
}

static const ListProvider pads_provider = { "pads", "Select Pad", pads_provide };

void list_provider_pads_update(uint8_t page, bool exists) {
    if (page >= MAX_PADS) return;
    if (!s_cached_items) {
        pads_cache_rebuild();
        return;
    }

    PadListEntry entry = {};
    if (exists) {
        entry.exists = true;
        snprintf(entry.item.id, LIST_ITEM_ID_MAX, "pad_%u", page);
        const PadConfig* config = pad_config_acquire(page);
        if (config && config->name && config->name[0]) {
            snprintf(entry.item.label, LIST_ITEM_LABEL_MAX, "Pad %u: %s", page, config->name);
        } else {
            snprintf(entry.item.label, LIST_ITEM_LABEL_MAX, "Pad %u", page);
        }
        pad_config_release(config);
    }
    portENTER_CRITICAL(&s_cache_mux);
    s_cached_items[page] = entry;
    portEXIT_CRITICAL(&s_cache_mux);
}

void list_provider_pads_init() {
    pads_cache_rebuild();
    list_provider_register(&pads_provider);
}

#endif // HAS_DISPLAY
