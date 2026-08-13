#include "action_registry.h"

#if HAS_DISPLAY || HAS_BUTTON

#include <stdio.h>
#include <string.h>

#if HAS_MQTT
#include "binding_template.h"
#endif

// Built-ins and device classes share this fixed-size registry. Reserve room
// for device classes even when every built-in action is present.
#ifndef ACTION_TYPE_REGISTRY_MAX_TYPES
#define ACTION_TYPE_REGISTRY_MAX_TYPES 24
#endif
static constexpr int MAX_ACTION_TYPES = ACTION_TYPE_REGISTRY_MAX_TYPES;
static const ActionTypeDef* s_types[MAX_ACTION_TYPES] = {};
static int s_count = 0;

void action_type_register(const ActionTypeDef* type) {
    if (!type || !type->type_name || action_type_find(type->type_name)
            || s_count >= MAX_ACTION_TYPES) return;
    s_types[s_count++] = type;
}

const ActionTypeDef* action_type_find(const char* type_name) {
    if (!type_name || !type_name[0]) return nullptr;
    for (int i = 0; i < s_count; i++) {
        if (strcmp(s_types[i]->type_name, type_name) == 0) return s_types[i];
    }
    return nullptr;
}

uint8_t action_type_count() { return (uint8_t)s_count; }

const ActionTypeDef* action_type_at(uint8_t index) {
    return (index < s_count) ? s_types[index] : nullptr;
}

bool action_type_is_supported(const char* type_name) {
    const ActionTypeDef* type = action_type_find(type_name);
    return type && (!type->available || type->available());
}

const char* action_type_validate(const ActionTypeDef* type, JsonObjectConst action) {
    if (!type || !type->validate) return nullptr;
    return type->validate(action);
}

void action_substitute_step_field(char* field, size_t field_size, float step) {
    if (!field) return;
    const char* token = "{step}";
    const size_t token_len = 6;
    char tmp[24];
    snprintf(tmp, sizeof(tmp), "%g", step);
    size_t repl_len = strlen(tmp);

    char* pos = strstr(field, token);
    while (pos) {
        size_t tail_len = strlen(pos + token_len);
        if ((size_t)(pos - field) + repl_len + tail_len >= field_size) return;
        memmove(pos + repl_len, pos + token_len, tail_len + 1);
        memcpy(pos, tmp, repl_len);
        pos = strstr(pos + repl_len, token);
    }
}

void action_type_substitute_step(const ActionTypeDef* def, ButtonAction& act, float step) {
    if (!def || !def->value_field) return;
    size_t size = 0;
    char* field = def->value_field(act, &size);
    if (field && size) action_substitute_step_field(field, size, step);
}

bool action_type_visit_bindable_fields(const ActionTypeDef* def, ButtonAction& act,
                                       ActionBindableFieldVisitor visitor, void* context) {
    if (!def || !visitor) return true;
    if (def->binding_fields) return def->binding_fields(act, visitor, context);
    if (!def->value_field) return true;
    size_t size = 0;
    char* field = def->value_field(act, &size);
    return !field || !field[0] || visitor(field, size, true, context);
}

#if HAS_MQTT
bool action_type_has_binding(const ActionTypeDef* def, const ButtonAction& act) {
    bool has_binding = false;
    auto check = [](char* field, size_t, bool, void* context) {
        bool* result = static_cast<bool*>(context);
        *result = field[0] && memchr(field, '[', strlen(field)) != nullptr;
        return !*result;
    };
    action_type_visit_bindable_fields(def, const_cast<ButtonAction&>(act), check, &has_binding);
    return has_binding;
}

bool action_type_resolve_bindings(const ActionTypeDef* def, ButtonAction& act) {
    auto resolve = [](char* field, size_t size, bool reject_overflow, void*) {
        if (!field[0] || !size || !binding_template_has_bindings(field)) return true;
        char tmp[BINDING_TEMPLATE_MAX_LEN];
        binding_template_resolve(field, tmp, sizeof(tmp));
        if (reject_overflow && strlen(tmp) >= size) return false;
        strlcpy(field, tmp, size);
        return true;
    };
    return action_type_visit_bindable_fields(def, act, resolve, nullptr);
}

void action_type_collect_topics(const ActionTypeDef* def, const ButtonAction& act, void* user_data) {
    auto collect = [](char* field, size_t, bool, void* context) {
        if (field[0]) binding_template_collect_topics(field, context);
        return true;
    };
    action_type_visit_bindable_fields(def, const_cast<ButtonAction&>(act), collect, user_data);
}
#endif

#endif // HAS_DISPLAY || HAS_BUTTON
