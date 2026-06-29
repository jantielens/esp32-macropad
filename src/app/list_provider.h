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

// Registry enumeration (for the MCP capability manifest): feature branches
// register providers, so the manifest can list available [list:id] sources
// generated, not hand-coded.
uint8_t list_provider_count();
const ListProvider* list_provider_at(uint8_t index);

// Apply a comma-separated filter expression to an item list, compacting in place.
// Returns the new count. Empty filter or NULL = pass-through (returns count).
// Rules: glob (case-insensitive */?, matched against id then label), index (#N or
// #N-M), or exclusion (! prefix). Item passes if any positive rule matches AND no
// exclusion rule matches. If only exclusion rules are given, items default to
// included. See docs/pad-editor-guide.md for full syntax.
uint8_t list_filter_items(ListItem* items, uint8_t count, const char* filter);

#endif // HAS_DISPLAY
