#include "component_registry.h"

#include "storage.h"
#include "storage_browser.h"
#include "web_portal_json.h"
#include "web_portal_utils.h"

#include <ArduinoJson.h>

namespace {
void storage_status_get(AsyncWebServerRequest* request) {
    auto doc = make_psram_json_doc(384);
    storage_browser_status_to_json(doc->to<JsonObject>());
    web_portal_send_json_sized(request, doc);
}

void storage_list_get(AsyncWebServerRequest* request) {
    const String path = request->hasParam("path") ? request->getParam("path")->value() : "/";
    auto doc = make_psram_json_doc(16 * 1024);
    const char* error = nullptr;
    if (!storage_browser_list(path, doc->to<JsonObject>(), error)) {
        const int status = strcmp(error, "invalid storage path") == 0 ? 400 : 404;
        request->send(status, "application/json", String("{\"error\":\"") + error + "\"}");
        return;
    }
    web_portal_send_json_sized(request, doc);
}

void storage_file_get(AsyncWebServerRequest* request) {
    const String path = request->hasParam("path") ? request->getParam("path")->value() : "";
    if (!storage_browser_path_is_safe(path)) {
        web_portal_send_json_error(request, 400, "Invalid storage path");
        return;
    }

    File file = Storage.open(path, "r");
    if (!file || file.isDirectory()) {
        if (file) file.close();
        web_portal_send_json_error(request, 404, "File not found");
        return;
    }
    file.close();

    sendFileThrottled(request, path.c_str(), storage_browser_file_content_type(path));
}

const ComponentAction storage_actions[] = {
    {"status", HTTP_GET, storage_status_get, nullptr},
    {"list", HTTP_GET, storage_list_get, nullptr},
    {"file", HTTP_GET, storage_file_get, nullptr},
};
} // namespace

static ComponentDef storage_component = {
    .id = "storage",
    .category = "device",
    .display_name = "Storage",
    .nav_order = 30,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = storage_actions,
    .num_custom_actions = sizeof(storage_actions) / sizeof(storage_actions[0]),
    .fragment_id = "storage",
};
REGISTER_COMPONENT(storage);