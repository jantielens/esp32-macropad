#include "list_provider.h"

#if HAS_DISPLAY

#include "board_config.h"

#if IS_SHUTTER_TESTER

#include "shutter_test_scripts.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <string.h>

static ListItem        s_cached_items[LIST_MAX_ITEMS];
static uint8_t         s_cached_count = 0;
static portMUX_TYPE    s_cache_mux = portMUX_INITIALIZER_UNLOCKED;

static uint8_t shutter_tests_provide(ListItem* items, uint8_t max_items) {
    portENTER_CRITICAL(&s_cache_mux);
    uint8_t n = (s_cached_count < max_items) ? s_cached_count : max_items;
    memcpy(items, s_cached_items, n * sizeof(ListItem));
    portEXIT_CRITICAL(&s_cache_mux);
    return n;
}

void list_provider_shutter_tests_refresh() {
    ShutterTestParseResult* result = (ShutterTestParseResult*)heap_caps_malloc(
        sizeof(ShutterTestParseResult), MALLOC_CAP_SPIRAM);
    if (!result) return;
    uint8_t count = shutter_test_scripts_parse(result);
    ListItem staging[LIST_MAX_ITEMS];
    uint8_t  staged = 0;
    for (uint8_t i = 0; i < count && staged < LIST_MAX_ITEMS; i++) {
        strlcpy(staging[staged].id, result->scripts[i].id, LIST_ITEM_ID_MAX);
        strlcpy(staging[staged].label, result->scripts[i].name, LIST_ITEM_LABEL_MAX);
        staged++;
    }
    heap_caps_free(result);
    portENTER_CRITICAL(&s_cache_mux);
    memcpy(s_cached_items, staging, staged * sizeof(ListItem));
    s_cached_count = staged;
    portEXIT_CRITICAL(&s_cache_mux);
}

static const ListProvider shutter_tests_provider = {
    "shutter_tests", "Select Test", shutter_tests_provide
};

void list_provider_shutter_tests_init() {
    list_provider_shutter_tests_refresh();
    list_provider_register(&shutter_tests_provider);
}

#endif // IS_SHUTTER_TESTER
#endif // HAS_DISPLAY
