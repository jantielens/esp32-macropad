#include "web_portal_brew_templates.h"

#if IS_COFFEE_SCALE

#include "../brew/brew_template_dsl.h"
#include "../brew/brew_template_loader.h"
#include "../brew/brew_templates.h"
#include "log_manager.h"
#include "storage.h"
#include "web_portal_auth.h"
#include "web_portal_cors.h"
#include "web_portal_routes.h"
#include "web_portal_json.h"

#include <ArduinoJson.h>
#define TAG "WebBTpl"

// ----------------------------------------------------------------------------
// GET /api/brew-templates — list all templates
// ----------------------------------------------------------------------------

void handleGetBrewTemplates(AsyncWebServerRequest* request) {
    uint8_t count = brew_template_count();

    auto doc = make_psram_json_doc(count * 256 + 128);
    if (!doc) {
        web_portal_send_json_error(request, 503, "Out of memory");
        return;
    }

    JsonArray arr = doc->to<JsonArray>();
    for (uint8_t i = 0; i < count; i++) {
        const BrewTemplate* t = brew_template_get(i);
        if (!t) continue;
        JsonObject obj = arr.createNestedObject();
        obj["name"]         = t->name;
        obj["display_name"] = t->display_name;
        obj["description"]  = t->description;
        obj["is_dynamic"]   = t->is_dynamic;
        obj["stage_count"]  = t->stage_count;
    }

    web_portal_send_json_chunked(request, doc);
}

// ----------------------------------------------------------------------------
// GET /api/brew-templates/get?name=xxx — download single template as JSON
// ----------------------------------------------------------------------------

void handleGetBrewTemplate(AsyncWebServerRequest* request) {
    if (!request->hasParam("name")) {
        web_portal_send_json_error(request, 400, "Missing name parameter");
        return;
    }

    String name = request->getParam("name")->value();
    if (name.length() == 0 || name.indexOf("..") >= 0 || name.indexOf('/') >= 0) {
        web_portal_send_json_error(request, 400, "Invalid name");
        return;
    }

    const BrewTemplate* t = brew_template_find(name.c_str());
    if (!t) {
        web_portal_send_json_error(request, 404, "Template not found");
        return;
    }

    // Serialize to JSON via brew_dsl_serialize
    char buf[4096];
    int len = brew_dsl_serialize(t, buf, sizeof(buf));
    if (len < 0) {
        web_portal_send_json_error(request, 500, "Serialization failed");
        return;
    }

    request->send(200, "application/json", buf);
}

// ----------------------------------------------------------------------------
// POST /api/brew-templates — upload template JSON body
// ----------------------------------------------------------------------------

void handlePostBrewTemplate(AsyncWebServerRequest* request, uint8_t* data,
                            size_t len, size_t index, size_t total) {
    if (!portal_auth_gate(request)) return;

    if (total > 8192) {
        web_portal_send_json_error(request, 413, "Template too large (max 8KB)");
        return;
    }

    // Allocate accumulation buffer on first chunk
    if (index == 0) {
        char* buf = new (std::nothrow) char[total + 1];
        if (!buf) {
            web_portal_send_json_error(request, 503, "Out of memory");
            return;
        }
        request->_tempObject = buf;
    }

    // Accumulate this chunk
    char* buf = (char*)request->_tempObject;
    if (!buf) return;  // allocation failed on first chunk
    memcpy(buf + index, data, len);

    // Not all data received yet
    if (index + len < total) return;

    // Final chunk — null-terminate and process
    buf[total] = '\0';
    char* json = buf;
    request->_tempObject = nullptr;  // we own the pointer now

    // Validate by parsing
    BrewTemplate* tmpl = nullptr;
    BrewStage* stages = nullptr;
    char err[80] = {};
    int rc = brew_dsl_parse(json, total, &tmpl, &stages, err, sizeof(err));
    if (rc != BREW_DSL_OK) {
        delete[] json;
        web_portal_send_json_error(request, 400, err[0] ? err : "Invalid template JSON");
        return;
    }

    // Sanitize name for filesystem safety
    const char* tname = tmpl->name;
    if (strchr(tname, '/') || strchr(tname, '\\') || strstr(tname, "..")) {
        delete[] stages;
        delete tmpl;
        delete[] json;
        web_portal_send_json_error(request, 400, "Invalid template name");
        return;
    }

    // Ensure directory exists
    if (!Storage.exists(BREW_TEMPLATE_DIR)) {
        Storage.mkdir(BREW_TEMPLATE_DIR);
    }

    // Write file
    char path[64];
    snprintf(path, sizeof(path), BREW_TEMPLATE_DIR "/%s.json", tname);

    // Free parsed template — we write the raw JSON, not re-serialized
    delete[] stages;
    delete tmpl;

    File f = Storage.open(path, "w");
    if (!f) {
        delete[] json;
        web_portal_send_json_error(request, 500, "Failed to write file");
        return;
    }
    f.write((const uint8_t*)json, total);
    f.close();
    delete[] json;

    // Reload dynamic templates
    brew_template_loader_reload();
    storage_publish_usage();

    LOGI(TAG, "Saved template to %s", path);
    request->send(200, "application/json", "{\"ok\":true}");
}

// ----------------------------------------------------------------------------
// DELETE /api/brew-templates?name=xxx — delete template from persistent storage
// ----------------------------------------------------------------------------

void handleDeleteBrewTemplate(AsyncWebServerRequest* request) {
    if (!portal_auth_gate(request)) return;

    if (!request->hasParam("name")) {
        web_portal_send_json_error(request, 400, "Missing name parameter");
        return;
    }

    String name = request->getParam("name")->value();
    if (name.length() == 0 || name.indexOf("..") >= 0 || name.indexOf('/') >= 0) {
        web_portal_send_json_error(request, 400, "Invalid name");
        return;
    }

    char path[64];
    snprintf(path, sizeof(path), BREW_TEMPLATE_DIR "/%s.json", name.c_str());

    if (!Storage.exists(path)) {
        // No FS file — check if it's a built-in
        const BrewTemplate* t = brew_template_find(name.c_str());
        if (t && !t->is_dynamic) {
            web_portal_send_json_error(request, 409, "Cannot delete built-in template");
            return;
        }
        web_portal_send_json_error(request, 404, "Template file not found");
        return;
    }

    Storage.remove(path);
    brew_template_loader_reload();
    storage_publish_usage();

    // Check if a built-in re-emerged with this name
    const BrewTemplate* t = brew_template_find(name.c_str());
    bool reset_to_default = (t && !t->is_dynamic && strcmp(t->name, name.c_str()) == 0);

    LOGI(TAG, "Deleted %s%s", path, reset_to_default ? " (reset to built-in)" : "");

    if (reset_to_default) {
        request->send(200, "application/json", "{\"ok\":true,\"status\":\"reset_to_default\"}");
    } else {
        request->send(200, "application/json", "{\"ok\":true,\"status\":\"deleted\"}");
    }
}

void web_portal_brew_templates_register_routes(AsyncWebServer* server) {
    server->on("/api/brew-templates/get", HTTP_OPTIONS, [](AsyncWebServerRequest *r){ web_portal_send_cors_preflight(r); });
    server->on("/api/brew-templates/get", HTTP_GET, handleGetBrewTemplate);

    server->on("/api/brew-templates", HTTP_OPTIONS, [](AsyncWebServerRequest *r){ web_portal_send_cors_preflight(r); });
    server->on("/api/brew-templates", HTTP_GET, handleGetBrewTemplates);
    server->on(
        "/api/brew-templates",
        HTTP_POST,
        [](AsyncWebServerRequest *request) {
            if (!portal_auth_gate(request)) return;
            if (request->_tempObject) {
                delete[] (char*)request->_tempObject;
                request->_tempObject = nullptr;
            }
        },
        NULL,
        handlePostBrewTemplate
    );
    server->on("/api/brew-templates", HTTP_DELETE, handleDeleteBrewTemplate);
}

REGISTER_ROUTES(web_portal_brew_templates_register_routes);

#endif // IS_COFFEE_SCALE

#undef TAG
