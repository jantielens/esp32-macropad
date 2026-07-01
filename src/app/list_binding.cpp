#include "list_binding.h"
#include "board_config.h"

#if HAS_DISPLAY

#include "binding_template.h"
#include "list_provider.h"
#include "log_manager.h"
#include <string.h>

#define TAG "ListBind"

// ============================================================================
// Per-provider state — set by widget click handler, read by resolver
// ============================================================================

struct ListBindingEntry {
    char provider_id[LIST_ITEM_ID_MAX];
    char selected_id[LIST_ITEM_ID_MAX];
};

static ListBindingEntry g_entries[LIST_MAX_PROVIDERS];
static uint8_t g_entry_count = 0;

void list_binding_set_selected(const char* provider_id, const char* item_id) {
    if (!provider_id || !provider_id[0]) return;

    // Find existing entry
    for (uint8_t i = 0; i < g_entry_count; i++) {
        if (strcmp(g_entries[i].provider_id, provider_id) == 0) {
            if (item_id) {
                strlcpy(g_entries[i].selected_id, item_id, LIST_ITEM_ID_MAX);
            } else {
                g_entries[i].selected_id[0] = '\0';
            }
            return;
        }
    }

    // Create new entry (lazy init)
    if (g_entry_count >= LIST_MAX_PROVIDERS) {
        LOGW(TAG, "Binding map full, cannot add '%s'", provider_id);
        return;
    }
    strlcpy(g_entries[g_entry_count].provider_id, provider_id, LIST_ITEM_ID_MAX);
    if (item_id) {
        strlcpy(g_entries[g_entry_count].selected_id, item_id, LIST_ITEM_ID_MAX);
    } else {
        g_entries[g_entry_count].selected_id[0] = '\0';
    }
    g_entry_count++;
}

// ============================================================================
// Resolver — params = "provider_id.selected" (dot-delimited)
// ============================================================================

static bool list_binding_resolve(const char* params, char* out, size_t out_len) {
    // Split on '.' — left = provider_id, right = key
    const char* dot = strchr(params, '.');
    if (!dot) return false;

    size_t id_len = (size_t)(dot - params);
    if (id_len == 0 || id_len >= LIST_ITEM_ID_MAX) return false;

    char provider_id[LIST_ITEM_ID_MAX];
    memcpy(provider_id, params, id_len);
    provider_id[id_len] = '\0';

    const char* key = dot + 1;
    if (strcmp(key, "selected") != 0) return false;

    // Look up entry
    for (uint8_t i = 0; i < g_entry_count; i++) {
        if (strcmp(g_entries[i].provider_id, provider_id) == 0) {
            if (g_entries[i].selected_id[0] == '\0') return false;
            strlcpy(out, g_entries[i].selected_id, out_len);
            return true;
        }
    }
    return false;
}

// ============================================================================
// Collector (no MQTT topics to collect)
// ============================================================================

static void list_binding_collect(const char* params, void* user_data) {
    (void)params;
    (void)user_data;
}

// ============================================================================
// Init
// ============================================================================

#if HAS_MCP
#include <ArduinoJson.h>
static void list_scheme_describe(void* out) {
    JsonObject& o = *static_cast<JsonObject*>(out);
    o["syntax"]    = "[list:provider.selected]";
    o["example"]   = "[list:pads.selected]";
    o["providers"] = "see list_providers[]";
}

// Validate a [list:PROVIDER.field] token: the provider must be registered.
static char s_list_verr[80];
static const char* list_scheme_validate(const char* params) {
    if (!params || !params[0]) return nullptr;
    char provider[LIST_ITEM_ID_MAX];
    size_t n = 0;
    while (params[n] && params[n] != '.' && n < sizeof(provider) - 1) { provider[n] = params[n]; n++; }
    provider[n] = '\0';
    if (!provider[0] || list_provider_find(provider)) return nullptr;
    snprintf(s_list_verr, sizeof(s_list_verr),
             "unknown list provider '%s' — use one from capabilities.list_providers", provider);
    return s_list_verr;
}
#endif

void list_binding_init() {
    if (!binding_template_register("list", list_binding_resolve, list_binding_collect)) {
        LOGE(TAG, "Failed to register list binding scheme");
    } else {
        LOGI(TAG, "List binding scheme registered");
    }
#if HAS_MCP
    binding_template_set_scheme_describe("list", list_scheme_describe);
    binding_template_set_scheme_validate("list", list_scheme_validate);
#endif
}

#else // !HAS_DISPLAY

void list_binding_init() {}

#endif // HAS_DISPLAY
