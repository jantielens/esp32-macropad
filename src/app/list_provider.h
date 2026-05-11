#pragma once

#include "board_config.h"

#if HAS_DISPLAY

#include <stddef.h>
#include <stdint.h>

#define LIST_ITEM_ID_MAX     17
#define LIST_ITEM_LABEL_MAX  64
#define LIST_MAX_ITEMS       16
#define LIST_MAX_PROVIDERS    8

struct ListItem {
    char id[LIST_ITEM_ID_MAX];
    char label[LIST_ITEM_LABEL_MAX];
};

struct ListProvider {
    const char* id;       // e.g. "shutter_tests"
    const char* title;    // e.g. "Select Test" — used as header/fallback label
    uint8_t (*provide)(ListItem* items, uint8_t max_items);
};

bool list_provider_register(const ListProvider* provider);
const ListProvider* list_provider_find(const char* id);

#endif // HAS_DISPLAY
