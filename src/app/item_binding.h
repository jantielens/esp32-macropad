#pragma once

#include "board_config.h"

#if HAS_DISPLAY && HAS_PSRAM

#include "list_source.h"

struct PadBinding;

// Validate item tokens, including those reached through explicit pad aliases.
// Passing no source rejects item access. Returns a static error on failure.
const char* item_binding_validate(const char* templ, const ListSourceDescriptor* source,
                                  const PadBinding* bindings, uint8_t binding_count);

// Validate item access in an action sequence after ordinary action validation.
const char* item_binding_validate_actions(JsonArrayConst actions, const ListSourceDescriptor* source,
                                          const PadBinding* bindings, uint8_t binding_count);

// Context is confined to the binding/LVGL task. Nested scopes restore their
// predecessor and retain the snapshot for the full synchronous operation.
class ItemBindingScope {
public:
    ItemBindingScope(ListSourceSnapshot* snapshot, size_t index);
    ~ItemBindingScope();
    ItemBindingScope(const ItemBindingScope&) = delete;
    ItemBindingScope& operator=(const ItemBindingScope&) = delete;

    JsonObjectConst item() const;
    // End every nested item scope on this task before a context boundary.
    static void end_current();

private:
    ListSourceSnapshot* snapshot_;
    size_t index_;
    ItemBindingScope* previous_;
};

void item_binding_init();

#endif
