#include "action_catalog.h"

#if HAS_DISPLAY || HAS_BUTTON

#include "action_continuation.h"
#include "action_registry.h"

// Built-ins and device-class actions are emitted from the live registry, so
// their metadata and availability share one definition.
void action_catalog_emit(JsonArray actions, bool include_field_docs) {
    for (uint8_t i = 0; i < action_type_count(); ++i) {
        const ActionTypeDef* type = action_type_at(i);
        if (!type || !type->type_name || !action_type_is_supported(type->type_name)) continue;
        JsonObject action = actions.createNestedObject();
        action["type"] = type->type_name;
        ButtonAction parsed{};
        auto emit = [](const char* name, char*, size_t capacity, uint8_t flags, void* opaque) {
            if (!(flags & ACTION_FIELD_EVENT)) return true;
            auto* action = static_cast<JsonObject*>(opaque);
            JsonObject field = (*action)["template_fields"][name].to<JsonObject>();
            field["max"] = capacity - 1;
            return true;
        };
        action_type_visit_fields(type, parsed, emit, &action);
        action["alarm_hook_allowed"] = type->execution == ACTION_EXECUTION_SYNC;
        action["pausable"] = type->execution == ACTION_EXECUTION_PAUSABLE;
        if (type->describe) {
            type->describe(action);
            if (!include_field_docs) action.remove("fields");
        } else {
            action["group"] = "Device";
            action["label"] = type->type_name;
        }
    }
}

#endif // HAS_DISPLAY || HAS_BUTTON
