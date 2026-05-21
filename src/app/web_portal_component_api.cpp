#include "web_portal_component_api.h"
#include "board_config.h"
#include "component_registry.h"
#include "web_portal_auth.h"
#include "web_portal_cors.h"
#include "web_portal_json.h"
#include "web_portal_state.h"
#include "log_manager.h"

#include <algorithm>
#include <string.h>

#define TAG "CompAPI"

// ============================================================================
// Category definitions — hardcoded order for consistent nav UX
// ============================================================================

struct NavCategory {
    const char* id;
    const char* display_name;
    const char* icon;
};

static const NavCategory kNavCategories[] = {
    {"device",       "Device",       "\xe2\x9a\x99\xef\xb8\x8f"},  // ⚙️
    {"display",      "Display",      "\xf0\x9f\x96\xa5\xef\xb8\x8f"}, // 🖥️
    {"pads",         "Pads",         "\xf0\x9f\x8e\x9b\xef\xb8\x8f"}, // 🎛️
    {"actions",      "Actions",      "\xe2\x9a\xa1"},               // ⚡
    {"connectivity", "Connectivity", "\xf0\x9f\x93\xa1"},           // 📡
    {"audio",        "Audio",        "\xf0\x9f\x94\x8a"},           // 🔊
    {"sensors",      "Sensors",      "\xf0\x9f\x93\x8a"},           // 📊
    {"firmware",     "Firmware",     "\xf0\x9f\x92\xbe"},           // 💾
};

static constexpr uint8_t kNumCategories = sizeof(kNavCategories) / sizeof(kNavCategories[0]);

// ============================================================================
// Helper: find action by (name, method) in a component
// ============================================================================

static const ComponentAction* find_action(const ComponentDef* comp,
    const char* name, WebRequestMethodComposite method) {
    for (uint8_t i = 0; i < comp->num_custom_actions; i++) {
        if (strcmp(comp->custom_actions[i].name, name) == 0 &&
            comp->custom_actions[i].method == method) {
            return &comp->custom_actions[i];
        }
    }
    return nullptr;
}

// ============================================================================
// Helper: check if a string matches any hardcoded category ID
// ============================================================================

static bool is_hardcoded_category(const char* id) {
    for (uint8_t c = 0; c < kNumCategories; c++) {
        if (strcmp(id, kNavCategories[c].id) == 0) return true;
    }
    return false;
}

// ============================================================================
// GET /api/portal/nav — build nav JSON from registered components
// ============================================================================

static void handlePortalNav(AsyncWebServerRequest* request) {
    if (!portal_auth_gate(request)) return;
    bool ap_mode = web_portal_is_ap_mode_active();

    auto doc = make_psram_json_doc(4096);
    if (!doc) {
        web_portal_send_json_error(request, 503, "Out of memory");
        return;
    }

    if (ap_mode) (*doc)["ap_mode"] = true;

    JsonArray categories = doc->createNestedArray("categories");

    // Temp buffer for sorting items within a category
    struct ItemEntry {
        const char* id;
        const char* display_name;
        int nav_order;
    };
    ItemEntry items[MAX_PORTAL_COMPONENTS];

    // --- Primary category support ---
    // Board variants can define PORTAL_PRIMARY_CATEGORY to promote a custom
    // category to first position in the nav.  Suppressed in AP mode (where
    // the first-boot setup wizard is the only landing target) and when the
    // primary category collides with a hardcoded category ID.
    const bool has_primary = !ap_mode
        && PORTAL_PRIMARY_CATEGORY[0] != '\0'
        && !is_hardcoded_category(PORTAL_PRIMARY_CATEGORY);

    bool primary_valid = false;  // set true once fragment validation passes

    if (has_primary) {
        // Collect components whose category matches the primary
        uint8_t item_count = 0;
        for (uint8_t i = 0; i < component_registry_count(); i++) {
            ComponentDef* comp = component_registry_get(i);
            if (strcmp(comp->category, PORTAL_PRIMARY_CATEGORY) == 0) {
                const char* nav_id = (comp->fragment_id && comp->fragment_id[0]) ? comp->fragment_id : comp->id;
                items[item_count++] = {nav_id, comp->display_name, comp->nav_order};
            }
        }

        // Validate: PORTAL_PRIMARY_FRAGMENT must be among the collected items
        if (item_count > 0 && PORTAL_PRIMARY_FRAGMENT[0] != '\0') {
            for (uint8_t i = 0; i < item_count; i++) {
                if (strcmp(items[i].id, PORTAL_PRIMARY_FRAGMENT) == 0) {
                    primary_valid = true;
                    break;
                }
            }
        }

        if (primary_valid) {
            std::sort(items, items + item_count,
                [](const ItemEntry& a, const ItemEntry& b) {
                    return a.nav_order < b.nav_order;
                });

            JsonObject cat_obj = categories.createNestedObject();
            cat_obj["id"] = PORTAL_PRIMARY_CATEGORY;
            cat_obj["display_name"] = PORTAL_PRIMARY_LABEL[0] != '\0'
                ? PORTAL_PRIMARY_LABEL : PORTAL_PRIMARY_CATEGORY;
            cat_obj["icon"] = PORTAL_PRIMARY_ICON;

            JsonArray items_arr = cat_obj.createNestedArray("items");
            for (uint8_t i = 0; i < item_count; i++) {
                JsonObject item = items_arr.createNestedObject();
                item["id"] = items[i].id;
                item["display_name"] = items[i].display_name;
            }
        }
    }

    // Emit top-level "primary" object when validation passed
    if (primary_valid) {
        JsonObject primary_obj = doc->createNestedObject("primary");
        primary_obj["fragment"]  = PORTAL_PRIMARY_FRAGMENT;
        primary_obj["category"]  = PORTAL_PRIMARY_CATEGORY;
        primary_obj["label"]     = PORTAL_PRIMARY_LABEL;
        primary_obj["icon"]      = PORTAL_PRIMARY_ICON;
    } else if (ap_mode) {
        // First-boot AP mode: route the SPA to the setup wizard on init.
        // Server-side redirects break Android/iOS captive-portal probes, so
        // landing-fragment selection happens client-side via this field.
        JsonObject primary_obj = doc->createNestedObject("primary");
        primary_obj["fragment"] = "setup";
        primary_obj["category"] = "device";
    }

    // --- Hardcoded categories ---
    for (uint8_t c = 0; c < kNumCategories; c++) {
        const NavCategory& cat = kNavCategories[c];

        // In AP mode, only show the Device category
        if (ap_mode && strcmp(cat.id, "device") != 0) continue;

        // Collect items for this category
        //
        // First-boot wizard ("setup" component): visible ONLY in AP mode, and
        // when visible it is the *only* item in the device category — the
        // wifi / device-name / network entries are suppressed so the wizard
        // is the single hand-off path. In STA mode the wizard is hidden and
        // the normal entries take over.
        uint8_t item_count = 0;
        for (uint8_t i = 0; i < component_registry_count(); i++) {
            ComponentDef* comp = component_registry_get(i);
            if (strcmp(comp->category, cat.id) != 0) continue;
            bool is_setup = (strcmp(comp->id, "setup") == 0);
            if (is_setup && !ap_mode) continue;  // hide wizard outside AP mode
            // In AP mode the outer loop has already restricted us to the
            // device category, so suppressing every non-setup item here
            // leaves the wizard as the single hand-off path.
            if (!is_setup && ap_mode) continue;
            const char* nav_id = (comp->fragment_id && comp->fragment_id[0]) ? comp->fragment_id : comp->id;
            items[item_count++] = {nav_id, comp->display_name, comp->nav_order};
        }

        if (item_count == 0) continue;  // skip empty categories

        // Sort by nav_order
        std::sort(items, items + item_count,
            [](const ItemEntry& a, const ItemEntry& b) {
                return a.nav_order < b.nav_order;
            });

        JsonObject cat_obj = categories.createNestedObject();
        cat_obj["id"] = cat.id;
        cat_obj["display_name"] = cat.display_name;
        cat_obj["icon"] = cat.icon;

        JsonArray items_arr = cat_obj.createNestedArray("items");
        for (uint8_t i = 0; i < item_count; i++) {
            JsonObject item = items_arr.createNestedObject();
            item["id"] = items[i].id;
            item["display_name"] = items[i].display_name;
        }
    }

    web_portal_send_json_chunked(request, doc);
}

// ============================================================================
// GET /api/component/{id}/config
// ============================================================================

static void handleComponentGetConfig(AsyncWebServerRequest* request) {
    if (!portal_auth_gate(request)) return;
    String compId = request->pathArg(0);
    ComponentDef* comp = component_registry_find(compId.c_str());
    if (!comp || !comp->get_config) {
        request->send(404, "application/json", "{\"error\":\"not found\"}");
        return;
    }
    comp->get_config(request);
}

// ============================================================================
// POST /api/component/{id}/config — request handler (fires after body)
// ============================================================================

static void handleComponentSaveConfigRequest(AsyncWebServerRequest* request) {
    String compId = request->pathArg(0);
    ComponentDef* comp = component_registry_find(compId.c_str());
    if (!comp) {
        request->send(404, "application/json", "{\"error\":\"not found\"}");
        return;
    }
    // If body handler exists, it already processed the request — do nothing.
    // If only save_config (no body) exists, dispatch.
    if (!comp->save_config_body && comp->save_config) {
        if (!portal_auth_gate(request)) return;
        comp->save_config(request);
    }
}

// ============================================================================
// POST /api/component/{id}/config — body handler
// ============================================================================

static void handleComponentSaveConfigBody(AsyncWebServerRequest* request,
    uint8_t* data, size_t len, size_t index, size_t total) {
    if (!portal_auth_gate(request)) return;
    String compId = request->pathArg(0);
    ComponentDef* comp = component_registry_find(compId.c_str());
    if (!comp || !comp->save_config_body) return;
    comp->save_config_body(request, data, len, index, total);
}

// ============================================================================
// DELETE /api/component/{id}/config
// ============================================================================

static void handleComponentDeleteConfig(AsyncWebServerRequest* request) {
    String compId = request->pathArg(0);
    ComponentDef* comp = component_registry_find(compId.c_str());
    if (!comp || !comp->delete_config) {
        request->send(404, "application/json", "{\"error\":\"not found\"}");
        return;
    }
    if (!portal_auth_gate(request)) return;
    comp->delete_config(request);
}

// ============================================================================
// Custom action — request handler (GET, POST without body, or POST/PUT after body)
// ============================================================================

static void handleComponentActionRequest(AsyncWebServerRequest* request) {
    String compId = request->pathArg(0);
    String actionName = request->pathArg(1);

    ComponentDef* comp = component_registry_find(compId.c_str());
    if (!comp) {
        request->send(404, "application/json", "{\"error\":\"unknown component\"}");
        return;
    }

    WebRequestMethodComposite method = request->method();
    const ComponentAction* action = find_action(comp, actionName.c_str(), method);
    if (!action) {
        request->send(404, "application/json", "{\"error\":\"unknown action\"}");
        return;
    }

    if (!portal_auth_gate(request)) return;

    // If action has a body handler, it already processed the request
    if (action->body_handler) return;

    if (action->handler) {
        action->handler(request);
    }
}

// ============================================================================
// Custom action — body handler (POST/PUT with body)
// ============================================================================

static void handleComponentActionBody(AsyncWebServerRequest* request,
    uint8_t* data, size_t len, size_t index, size_t total) {
    if (!portal_auth_gate(request)) return;
    String compId = request->pathArg(0);
    String actionName = request->pathArg(1);

    ComponentDef* comp = component_registry_find(compId.c_str());
    if (!comp) return;

    WebRequestMethodComposite method = request->method();
    const ComponentAction* action = find_action(comp, actionName.c_str(), method);
    if (!action || !action->body_handler) return;

    action->body_handler(request, data, len, index, total);
}

// ============================================================================
// CORS preflight for generic component routes
// ============================================================================

static void handleCorsPreflightGeneric(AsyncWebServerRequest* request) {
    web_portal_send_cors_preflight(request);
}

// ============================================================================
// Route registration
// ============================================================================

void web_portal_register_component_routes(AsyncWebServer* server) {
    // Nav endpoint
    server->on("/api/portal/nav", HTTP_GET, handlePortalNav);
    server->on("/api/portal/nav", HTTP_OPTIONS, handleCorsPreflightGeneric);

    // Config CRUD — regex routes (registered before action routes for first-match)
    server->on(
        AsyncURIMatcher::regex("^/api/component/([a-z0-9-]+)/config$"),
        HTTP_GET, handleComponentGetConfig);

    server->on(
        AsyncURIMatcher::regex("^/api/component/([a-z0-9-]+)/config$"),
        HTTP_POST, handleComponentSaveConfigRequest, nullptr, handleComponentSaveConfigBody);

    server->on(
        AsyncURIMatcher::regex("^/api/component/([a-z0-9-]+)/config$"),
        HTTP_DELETE, handleComponentDeleteConfig);

    // Custom action routes — regex with two capture groups
    server->on(
        AsyncURIMatcher::regex("^/api/component/([a-z0-9-]+)/([a-z0-9-]+)$"),
        HTTP_GET, handleComponentActionRequest);

    server->on(
        AsyncURIMatcher::regex("^/api/component/([a-z0-9-]+)/([a-z0-9-]+)$"),
        HTTP_POST, handleComponentActionRequest, nullptr, handleComponentActionBody);

    server->on(
        AsyncURIMatcher::regex("^/api/component/([a-z0-9-]+)/([a-z0-9-]+)$"),
        HTTP_PUT, handleComponentActionRequest, nullptr, handleComponentActionBody);

    // CORS preflight for all component and portal routes
    server->on("/api/component/*", HTTP_OPTIONS, handleCorsPreflightGeneric);

    LOGI(TAG, "Registered component API routes (%d components)", component_registry_count());
}
