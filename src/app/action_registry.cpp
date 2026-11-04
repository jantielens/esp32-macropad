#include "action_registry.h"

#if HAS_DISPLAY || HAS_BUTTON

#include <stdio.h>
#include <string.h>

#include "binding_template.h"

// Built-ins and device classes share this fixed-size registry. Reserve room
// for device classes even when every built-in action is present.
#ifndef ACTION_TYPE_REGISTRY_MAX_TYPES
#define ACTION_TYPE_REGISTRY_MAX_TYPES 27
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

static bool action_token_fits(const char* field, size_t field_size,
                              const char* token, const char* replacement) {
    if (!field || !field_size || !token || !token[0] || !replacement ||
        strstr(replacement, token)) return false;
    const size_t field_length = strnlen(field, field_size);
    if (field_length == field_size) return false;
    const size_t token_length = strlen(token);
    const size_t replacement_length = strlen(replacement);
    size_t occurrences = 0;
    for (const char* cursor = field; (cursor = strstr(cursor, token)) != nullptr;
         cursor += token_length) ++occurrences;
    if (replacement_length > token_length) {
        const size_t growth = replacement_length - token_length;
        if (occurrences > (field_size - 1 - field_length) / growth) return false;
    }
    return true;
}

bool action_substitute_token_field(char* field, size_t field_size,
                                   const char* token, const char* replacement) {
    if (!action_token_fits(field, field_size, token, replacement)) return false;
    const size_t token_length = strlen(token);
    const size_t replacement_length = strlen(replacement);
    char* cursor = field;
    char* position = nullptr;
    while ((position = strstr(cursor, token)) != nullptr) {
        const size_t tail_length = strlen(position + token_length);
        memmove(position + replacement_length, position + token_length, tail_length + 1);
        memcpy(position, replacement, replacement_length);
        cursor = position + replacement_length;
    }
    return true;
}

bool action_type_substitute_token(const ActionTypeDef* def, ButtonAction& act,
                                  const char* token, const char* value) {
    if (!def || !token || !value || (strcmp(token, "{value}") && strcmp(token, "{step}")) ||
        strstr(value, "{value}") || strstr(value, "{step}")) return false;
    struct Context { const char* token; const char* value; bool apply; } context = {token, value, false};
    auto visit = [](const char*, char* field, size_t capacity, uint8_t flags, void* opaque) {
        auto* context = static_cast<Context*>(opaque);
        const size_t length = strnlen(field, capacity);
        if (length == capacity) return false;
        const char* other = strcmp(context->token, "{value}") == 0 ? "{step}" : "{value}";
        if (strstr(field, other)) return false;
        if (!strstr(field, context->token)) return true;
        if (!(flags & ACTION_FIELD_EVENT)) return false;
        if (context->apply)
            return action_substitute_token_field(field, capacity, context->token, context->value);
        return action_token_fits(field, capacity, context->token, context->value);
    };
    if (!action_type_visit_fields(def, act, visit, &context)) return false;
    context.apply = true;
    return action_type_visit_fields(def, act, visit, &context);
}

bool action_type_substitute_step(const ActionTypeDef* def, ButtonAction& act, float step) {
    char formatted[24];
    snprintf(formatted, sizeof(formatted), "%g", step);
    return action_type_substitute_token(def, act, "{step}", formatted);
}

bool action_type_visit_fields(const ActionTypeDef* def, ButtonAction& act,
                              ActionFieldVisitor visitor, void* context) {
    return !def || !def->fields || def->fields(act, visitor, context);
}

bool action_type_visit_bindable_fields(const ActionTypeDef* def, ButtonAction& act,
                                       ActionBindableFieldVisitor visitor, void* context) {
    if (!def || !visitor) return true;
    struct Context { ActionBindableFieldVisitor visitor; void* user; } adapter = {visitor, context};
    auto visit = [](const char*, char* field, size_t capacity, uint8_t flags, void* opaque) {
        auto* context = static_cast<Context*>(opaque);
        return !(flags & ACTION_FIELD_BINDING) ||
            context->visitor(field, capacity, flags & ACTION_FIELD_REJECT_OVERFLOW, context->user);
    };
    return action_type_visit_fields(def, act, visit, &adapter);
}

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
        if (!binding_template_resolve_checked(field, tmp, sizeof(tmp))) return false;
        if (reject_overflow && strlen(tmp) >= size) return false;
        strlcpy(field, tmp, size);
        return true;
    };
    return action_type_visit_bindable_fields(def, act, resolve, nullptr);
}

#if HAS_MQTT || HAS_HA_LIVE
void action_type_collect_topics(const ActionTypeDef* def, const ButtonAction& act, void* user_data) {
    auto collect = [](char* field, size_t, bool, void* context) {
        if (field[0]) binding_template_collect_topics(field, context);
        return true;
    };
    action_type_visit_bindable_fields(def, const_cast<ButtonAction&>(act), collect, user_data);
}
#endif

#endif // HAS_DISPLAY || HAS_BUTTON
