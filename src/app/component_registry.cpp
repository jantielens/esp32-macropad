#include "component_registry.h"
#include "log_manager.h"

#include <string.h>

#define TAG "PORTAL"

static ComponentDef* _components[MAX_PORTAL_COMPONENTS];
static uint8_t _component_count = 0;

bool component_registry_add(ComponentDef* def) {
    if (_component_count >= MAX_PORTAL_COMPONENTS) {
        LOGE(TAG, "Registry full (%d max), cannot add '%s'",
            MAX_PORTAL_COMPONENTS, def->id);
        return false;
    }
    // Reject duplicate IDs
    for (uint8_t i = 0; i < _component_count; i++) {
        if (strcmp(_components[i]->id, def->id) == 0) {
            LOGW(TAG, "Duplicate component ID '%s' rejected", def->id);
            return false;
        }
    }
    _components[_component_count++] = def;
    return true;
}

uint8_t component_registry_count() {
    return _component_count;
}

ComponentDef* component_registry_get(uint8_t index) {
    if (index >= _component_count) return nullptr;
    return _components[index];
}

ComponentDef* component_registry_find(const char* id) {
    for (uint8_t i = 0; i < _component_count; i++) {
        if (strcmp(_components[i]->id, id) == 0) return _components[i];
    }
    return nullptr;
}

void component_registry_for_category(const char* category,
    void (*cb)(ComponentDef* def, void* ctx), void* ctx) {
    for (uint8_t i = 0; i < _component_count; i++) {
        if (strcmp(_components[i]->category, category) == 0) {
            cb(_components[i], ctx);
        }
    }
}

void component_registry_reset() {
    _component_count = 0;
}

void component_handle_save_body(AsyncWebServerRequest* request,
    uint8_t* data, size_t len, size_t index, size_t total,
    ComponentSaveRawFn save_fn, size_t max_size) {
    // Only handle final chunk (small payload, arrives in one piece)
    if (index + len < total) return;

    if (total > max_size) {
        request->send(413, "application/json", "{\"error\":\"payload too large\"}");
        return;
    }

    bool ok = save_fn(data, total);
    if (ok) {
        request->send(200, "application/json", "{\"success\":true}");
    } else {
        request->send(500, "application/json", "{\"success\":false,\"error\":\"save failed\"}");
    }
}
