#pragma once

#include <ESPAsyncWebServer.h>
#include <stdint.h>

// Max components the registry can hold (static array, no heap allocation).
// 64 accommodates the current feature-rich device classes and leaves room for
// future portal fragments without silently omitting their navigation entries.
#define MAX_PORTAL_COMPONENTS 64

// Custom action definition — dispatched by (name, method) pair
struct ComponentAction {
    const char* name;                 // e.g. "tare", "brightness"
    WebRequestMethodComposite method; // HTTP_GET, HTTP_POST, HTTP_PUT, etc.
    ArRequestHandlerFunction handler; // for request-only handlers (no body)
    ArBodyHandlerFunction body_handler; // for handlers that receive a body (PUT/POST with JSON)
};

// Component definition — one per feature module
struct ComponentDef {
    const char* id;                // URL-safe identifier, e.g. "swipe-actions"
    const char* category;          // one of: device, display, camera, pads,
                                   //         actions, connectivity, audio, sensors, firmware
    const char* display_name;      // Human-readable, e.g. "Swipe Actions"
    int nav_order;                 // Sort order within category (lower = higher)

    // Config CRUD handlers (nullptr if component has no config)
    ArRequestHandlerFunction get_config;
    ArRequestHandlerFunction save_config;       // POST config request handler (no body)
    ArBodyHandlerFunction save_config_body;     // POST config body handler
    ArRequestHandlerFunction delete_config;

    // Custom actions beyond config CRUD
    const ComponentAction* custom_actions;
    uint8_t num_custom_actions;

    // UI fragment identifier (matches fragment filename without extension)
    // nullptr means this component contributes to a shared category fragment
    const char* fragment_id;

    // Optional feature-specific JavaScript asset required before this
    // component's fragment can initialize.
    const char* portal_script;

    // Optional feature-specific stylesheet required by this component's
    // fragment.
    const char* portal_style;
};

// Registry API
bool component_registry_add(ComponentDef* def);
uint8_t component_registry_count();
ComponentDef* component_registry_get(uint8_t index);
ComponentDef* component_registry_find(const char* id);
void component_registry_for_category(const char* category,
    void (*cb)(ComponentDef* def, void* ctx), void* ctx);

// Reset registry (for tests only)
void component_registry_reset();

// Save-body helper — shared boilerplate for save_config_body handlers.
// Handles chunk assembly, payload size check, and success/error response.
typedef bool (*ComponentSaveRawFn)(const uint8_t* data, size_t len);
void component_handle_save_body(AsyncWebServerRequest* request,
    uint8_t* data, size_t len, size_t index, size_t total,
    ComponentSaveRawFn save_fn, size_t max_size = 4096);

// Host-test allocation hooks. Passing nullptr restores the defaults.
typedef void* (*ComponentBodyAllocFn)(size_t size);
typedef void (*ComponentBodyFreeFn)(void* ptr);
void component_registry_set_body_allocator_for_test(ComponentBodyAllocFn alloc_fn,
                                                     ComponentBodyFreeFn free_fn);

// Auto-registration macro — mirrors REGISTER_WIDGET() pattern
// Usage: REGISTER_COMPONENT(my_feature);
// Requires a static ComponentDef named <name>_component in the same file
#define REGISTER_COMPONENT(name) \
    static struct _ComponentRegistrar_##name { \
        _ComponentRegistrar_##name() { \
            component_registry_add(&name##_component); \
        } \
    } _component_registrar_##name;

// Convenience macro for nav-only components (no config or custom action handlers).
// Collapses the 18-line boilerplate into a single line.
// Args: sym — C identifier, _id — JSON id string, _category — category string,
//       _display_name — human-readable name, _nav_order — int, _fragment — fragment id string.
#define REGISTER_NAV_COMPONENT(sym, _id, _category, _display_name, _nav_order, _fragment) \
    static ComponentDef sym##_component = { \
        .id               = _id, \
        .category         = _category, \
        .display_name     = _display_name, \
        .nav_order        = _nav_order, \
        .get_config       = nullptr, \
        .save_config      = nullptr, \
        .save_config_body = nullptr, \
        .delete_config    = nullptr, \
        .custom_actions   = nullptr, \
        .num_custom_actions = 0, \
        .fragment_id      = _fragment, \
        .portal_script    = nullptr, \
        .portal_style     = nullptr, \
    }; \
    REGISTER_COMPONENT(sym)
