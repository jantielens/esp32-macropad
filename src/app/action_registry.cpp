#include "action_registry.h"

#if HAS_DISPLAY || HAS_BUTTON

#include <stdio.h>
#include <string.h>

#if HAS_MQTT
#include "binding_template.h"
#endif

// Small fixed-size registry. Built-ins and device classes share this table;
// 16 leaves room beyond the largest current device-class configuration.
static constexpr int MAX_ACTION_TYPES = 16;
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

#if HAS_MQTT
bool action_type_has_binding(const ActionTypeDef* def, const ButtonAction& act) {
    if (!def || !def->value_field) return false;
    size_t size = 0;
    // value_field only computes a pointer into the payload arm; reading it
    // through a const ButtonAction is safe, so the const_cast is benign.
    char* field = def->value_field(const_cast<ButtonAction&>(act), &size);
    return field && field[0] && memchr(field, '[', strlen(field)) != nullptr;
}

bool action_type_resolve_bindings(const ActionTypeDef* def, ButtonAction& act) {
    if (!def || !def->value_field) return true;
    size_t size = 0;
    char* field = def->value_field(act, &size);
    if (field && field[0] && size && binding_template_has_bindings(field)) {
        char tmp[BINDING_TEMPLATE_MAX_LEN];
        binding_template_resolve(field, tmp, sizeof(tmp));
        if (strlen(tmp) >= size) return false;
        strlcpy(field, tmp, size);
    }
    return true;
}

void action_type_collect_topics(const ActionTypeDef* def, const ButtonAction& act, void* user_data) {
    if (!def || !def->value_field) return;
    size_t size = 0;
    // Reading the field pointer through a const ButtonAction is safe (see above).
    char* field = def->value_field(const_cast<ButtonAction&>(act), &size);
    if (field && field[0]) binding_template_collect_topics(field, user_data);
}
#endif

#endif // HAS_DISPLAY || HAS_BUTTON
