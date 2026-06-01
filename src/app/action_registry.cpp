#include "action_registry.h"

#if HAS_DISPLAY

#include <stdio.h>
#include <string.h>

// Small fixed-size registry. Sized for the largest device class today
// (darkroom-timer registers 5: expose, strip, meter, print, shelly). Bump
// MAX_ACTION_TYPES if a future device class registers more.
static constexpr int MAX_ACTION_TYPES = 8;
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

#endif // HAS_DISPLAY
