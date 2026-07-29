#include "component_registry.h"
#include "log_manager.h"

#include <stdlib.h>
#include <string.h>

#define TAG "PORTAL"

static ComponentDef* _components[MAX_PORTAL_COMPONENTS];
static uint8_t _component_count = 0;
static ComponentBodyAllocFn s_body_alloc = malloc;
static ComponentBodyFreeFn s_body_free = free;

struct ComponentBodyState {
    size_t total;
    size_t received;
    ComponentBodyFreeFn free_fn;
};

static uint8_t* component_body_data(ComponentBodyState* state) {
    return reinterpret_cast<uint8_t*>(state + 1);
}

static void component_body_cleanup(AsyncWebServerRequest* request) {
    if (!request || !request->_tempObject) return;
    ComponentBodyState* state =
        static_cast<ComponentBodyState*>(request->_tempObject);
    request->_tempObject = nullptr;
    state->free_fn(state);
}

void component_registry_set_body_allocator_for_test(ComponentBodyAllocFn alloc_fn,
                                                     ComponentBodyFreeFn free_fn) {
    s_body_alloc = alloc_fn ? alloc_fn : malloc;
    s_body_free = free_fn ? free_fn : free;
}

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
    if (total > max_size) {
        component_body_cleanup(request);
        request->send(413, "application/json", "{\"error\":\"payload too large\"}");
        return;
    }

    if (index == 0) {
        component_body_cleanup(request);
        if (total > SIZE_MAX - sizeof(ComponentBodyState)) {
            request->send(413, "application/json", "{\"error\":\"payload too large\"}");
            return;
        }
        ComponentBodyState* state = static_cast<ComponentBodyState*>(
            s_body_alloc(sizeof(ComponentBodyState) + total));
        if (!state) {
            request->send(500, "application/json", "{\"error\":\"out of memory\"}");
            return;
        }
        state->total = total;
        state->received = 0;
        state->free_fn = s_body_free;
        request->_tempObject = state;
    }

    ComponentBodyState* state =
        static_cast<ComponentBodyState*>(request->_tempObject);
        if (!state || state->total != total || index != state->received
            || index > total || len > total - index
            || state->received > total - len || (len > 0 && !data)) {
        component_body_cleanup(request);
        request->send(400, "application/json", "{\"error\":\"invalid body chunk\"}");
        return;
    }

    if (len > 0) memcpy(component_body_data(state) + index, data, len);
    state->received += len;
    if (index + len < total) return;
    if (state->received != total) {
        component_body_cleanup(request);
        request->send(400, "application/json", "{\"error\":\"incomplete body\"}");
        return;
    }

    bool ok = save_fn && save_fn(component_body_data(state), total);
    component_body_cleanup(request);
    if (ok) {
        request->send(200, "application/json", "{\"success\":true}");
    } else {
        request->send(500, "application/json", "{\"success\":false,\"error\":\"save failed\"}");
    }
}
