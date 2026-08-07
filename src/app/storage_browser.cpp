#include "storage_browser.h"

#include "board_config.h"

#if HAS_STORAGE_BROWSER

#include "fs_health.h"
#include "storage.h"

namespace {
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

String entry_name(const String& entry_path) {
    String normalized = entry_path;
    while (normalized.length() > 1 && normalized.endsWith("/")) {
        normalized.remove(normalized.length() - 1);
    }
    const int slash = normalized.lastIndexOf('/');
    return slash >= 0 ? normalized.substring(slash + 1) : normalized;
}
} // namespace

bool storage_browser_path_is_safe(const String& path) {
    return path.length() > 0 && path.length() <= STORAGE_BROWSER_PATH_MAX_LEN &&
           path[0] == '/' && path.indexOf("//") < 0 && path.indexOf("..") < 0;
}

const char* storage_browser_file_content_type(const String& path) {
    if (path.endsWith(".png")) return "image/png";
    if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
    if (path.endsWith(".gif")) return "image/gif";
    if (path.endsWith(".webp")) return "image/webp";
    if (path.endsWith(".mp3")) return "audio/mpeg";
    if (path.endsWith(".wav")) return "audio/wav";
    if (path.endsWith(".ogg")) return "audio/ogg";
    return "application/octet-stream";
}

void storage_browser_status_to_json(JsonObject result) {
    FSHealthStats stats;
    fs_health_get(&stats);
    result["backend"] = backend_name(stats.backend);
    result["mounted"] = stats.storage_mounted;
    result["card_type"] = card_type_name(stats.card_type);
    result["used_bytes"] = stats.storage_used_bytes;
    result["total_bytes"] = stats.storage_total_bytes;
    result["free_bytes"] = stats.storage_total_bytes >= stats.storage_used_bytes
        ? stats.storage_total_bytes - stats.storage_used_bytes : 0;
}

bool storage_browser_list(const String& path, JsonObject result, const char*& error) {
    if (!storage_browser_path_is_safe(path)) {
        error = "invalid storage path";
        return false;
    }

    File directory = Storage.open(path);
    if (!directory || !directory.isDirectory()) {
        error = "directory not found";
        return false;
    }

    result["path"] = path;
    JsonArray entries = result["entries"].to<JsonArray>();
    File entry = directory.openNextFile();
    while (entry && entries.size() < STORAGE_BROWSER_LIST_MAX_ENTRIES) {
        const String name = entry_name(entry.name());
        JsonObject item = entries.add<JsonObject>();
        item["name"] = name;
        item["path"] = path == "/" ? "/" + name : path + "/" + name;
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
    result["truncated"] = static_cast<bool>(entry);
    if (entry) entry.close();
    return true;
}
#endif // HAS_STORAGE_BROWSER