#include "component_registry.h"

#include "fs_health.h"
#include "storage.h"
#include "web_portal_json.h"
#include "web_portal_utils.h"

#include <ArduinoJson.h>

namespace {
constexpr size_t STORAGE_LIST_MAX_ENTRIES = 128;
constexpr size_t STORAGE_PATH_MAX_LEN = 192;

const char* backend_name(FSBackend backend) {
    return backend == FS_BACKEND_SDMMC ? "sdmmc" : "littlefs";
}

const char* card_type_name(FSCardType card_type) {
    switch (card_type) {
        case FS_CARD_TYPE_SD: return "sd";
        case FS_CARD_TYPE_SDHC: return "sdhc";
        case FS_CARD_TYPE_NONE: return "none";
        case FS_CARD_TYPE_UNKNOWN: return "unknown";
        default: return "not_applicable";
    }
}

bool storage_path_is_safe(const String& path) {
    return path.length() > 0 && path.length() <= STORAGE_PATH_MAX_LEN &&
           path[0] == '/' && path.indexOf("//") < 0 &&
           path.indexOf("..") < 0;
}

String storage_entry_name(const String& entry_path) {
    String normalized = entry_path;
    while (normalized.length() > 1 && normalized.endsWith("/")) {
        normalized.remove(normalized.length() - 1);
    }
    const int slash = normalized.lastIndexOf('/');
    return slash >= 0 ? normalized.substring(slash + 1) : normalized;
}

const char* storage_file_content_type(const String& path) {
    if (path.endsWith(".png")) return "image/png";
    if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
    if (path.endsWith(".gif")) return "image/gif";
    if (path.endsWith(".webp")) return "image/webp";
    if (path.endsWith(".mp3")) return "audio/mpeg";
    if (path.endsWith(".wav")) return "audio/wav";
    if (path.endsWith(".ogg")) return "audio/ogg";
    return "application/octet-stream";
}

void storage_status_get(AsyncWebServerRequest* request) {
    FSHealthStats stats;
    fs_health_get(&stats);

    auto doc = make_psram_json_doc(384);
    (*doc)["backend"] = backend_name(stats.backend);
    (*doc)["mounted"] = stats.storage_mounted;
    (*doc)["card_type"] = card_type_name(stats.card_type);
    (*doc)["used_bytes"] = stats.storage_used_bytes;
    (*doc)["total_bytes"] = stats.storage_total_bytes;
    (*doc)["free_bytes"] = stats.storage_total_bytes >= stats.storage_used_bytes
        ? stats.storage_total_bytes - stats.storage_used_bytes : 0;
    web_portal_send_json_sized(request, doc);
}

void storage_list_get(AsyncWebServerRequest* request) {
    const String path = request->hasParam("path") ? request->getParam("path")->value() : "/";
    if (!storage_path_is_safe(path)) {
        request->send(400, "application/json", "{\"error\":\"invalid storage path\"}");
        return;
    }

    File directory = Storage.open(path);
    if (!directory || !directory.isDirectory()) {
        request->send(404, "application/json", "{\"error\":\"directory not found\"}");
        return;
    }

    auto doc = make_psram_json_doc(16 * 1024);
    (*doc)["path"] = path;
    JsonArray entries = (*doc)["entries"].to<JsonArray>();
    File entry = directory.openNextFile();
    while (entry && entries.size() < STORAGE_LIST_MAX_ENTRIES) {
        const String entry_path = entry.name();
        const String entry_name = storage_entry_name(entry_path);
        JsonObject item = entries.add<JsonObject>();
        item["name"] = entry_name;
        item["path"] = path == "/" ? "/" + entry_name : path + "/" + entry_name;
        item["type"] = entry.isDirectory() ? "directory" : "file";
        item["size"] = entry.isDirectory() ? 0 : entry.size();
        const time_t modified_at = entry.getLastWrite();
        if (modified_at > 0) {
            item["modified_at"] = modified_at;
        } else {
            item["modified_at"] = nullptr;
        }
        entry.close();
        entry = directory.openNextFile();
    }
    directory.close();
    (*doc)["truncated"] = static_cast<bool>(entry);
    if (entry) entry.close();
    web_portal_send_json_sized(request, doc);
}

void storage_file_get(AsyncWebServerRequest* request) {
    const String path = request->hasParam("path") ? request->getParam("path")->value() : "";
    if (!storage_path_is_safe(path)) {
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

    sendFileThrottled(request, path.c_str(), storage_file_content_type(path));
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