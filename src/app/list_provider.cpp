#include "list_provider.h"

#if HAS_DISPLAY

#include <string.h>

// ============================================================================
// ListProvider registry — static array + linear scan
// ============================================================================

static const ListProvider* g_providers[LIST_MAX_PROVIDERS];
static uint8_t g_provider_count = 0;

bool list_provider_register(const ListProvider* provider) {
    if (!provider || g_provider_count >= LIST_MAX_PROVIDERS) return false;
    g_providers[g_provider_count++] = provider;
    return true;
}

const ListProvider* list_provider_find(const char* id) {
    if (!id) return nullptr;
    for (uint8_t i = 0; i < g_provider_count; i++) {
        if (strcmp(g_providers[i]->id, id) == 0) return g_providers[i];
    }
    return nullptr;
}

void list_substitute_id_in_field(char* field, size_t field_size, const char* id) {
    const char* token = "{id}";
    const size_t token_len = 4;
    size_t repl_len = strlen(id);
    char* pos = strstr(field, token);
    while (pos) {
        size_t tail_len = strlen(pos + token_len);
        if ((size_t)(pos - field) + repl_len + tail_len >= field_size) return;
        memmove(pos + repl_len, pos + token_len, tail_len + 1);
        memcpy(pos, id, repl_len);
        pos = strstr(pos + repl_len, token);
    }
}

#endif // HAS_DISPLAY
