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

#endif // HAS_DISPLAY
