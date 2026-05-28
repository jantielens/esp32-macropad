#include "action_registry.h"

#if HAS_DISPLAY

#include <string.h>

// Small fixed-size registry — device-class action types are few (shutter is
// the only one today). Bump MAX_ACTION_TYPES if more device classes register
// their own action types in the future.
static constexpr int MAX_ACTION_TYPES = 4;
static const ActionTypeDef* s_types[MAX_ACTION_TYPES] = {};
static int s_count = 0;

void action_type_register(const ActionTypeDef* type) {
    if (!type || s_count >= MAX_ACTION_TYPES) return;
    s_types[s_count++] = type;
}

const ActionTypeDef* action_type_find(const char* type_name) {
    if (!type_name || !type_name[0]) return nullptr;
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_types[i]->type_name, type_name) == 0) return s_types[i];
    }
    return nullptr;
}

#endif // HAS_DISPLAY
