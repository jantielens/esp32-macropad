#include "web_portal_recipes.h"

#if HAS_DISPLAY

#include "log_manager.h"
#include "storage.h"
#include "web_portal_auth.h"
#include "web_portal_json.h"

#include <ArduinoJson.h>

#include <esp_heap_caps.h>

namespace {
constexpr char kCatalogPath[] = "/config/recipes.json";
constexpr char kCatalogTemporaryPath[] = "/config/recipes.json.tmp";
constexpr size_t kCatalogMaxBytes = 64 * 1024;
constexpr char kEmptyCatalog[] = "{\"schema\":1,\"catalog_version\":\"device\",\"recipes\":[]}";

struct CatalogUpload {
    uint8_t* buffer;
    size_t total;
    size_t received;
};

CatalogUpload* catalog_upload(AsyncWebServerRequest* request) {
    return request ? static_cast<CatalogUpload*>(request->_tempObject) : nullptr;
}

void catalog_upload_reset(AsyncWebServerRequest* request) {
    CatalogUpload* upload = catalog_upload(request);
    if (!upload) return;
    if (upload->buffer) heap_caps_free(upload->buffer);
    delete upload;
    request->_tempObject = nullptr;
}

bool catalog_save(const uint8_t* json, size_t length) {
    File file = Storage.open(kCatalogTemporaryPath, "w");
    if (!file) return false;
    const size_t written = file.write(json, length);
    file.close();
    if (written != length) {
        Storage.remove(kCatalogTemporaryPath);
        return false;
    }
    if (Storage.exists(kCatalogPath)) Storage.remove(kCatalogPath);
    if (Storage.rename(kCatalogTemporaryPath, kCatalogPath)) return true;
    Storage.remove(kCatalogTemporaryPath);
    return false;
}
}  // namespace

void handleGetRecipeCatalog(AsyncWebServerRequest* request) {
    if (!portal_auth_gate(request)) return;
    if (!Storage.exists(kCatalogPath)) {
        request->send(200, "application/json", kEmptyCatalog);
        return;
    }
    File file = Storage.open(kCatalogPath, "r");
    if (!file || file.size() == 0 || file.size() > kCatalogMaxBytes) {
        if (file) file.close();
        web_portal_send_json_error(request, 500, "Recipe catalog is unreadable");
        return;
    }
    file.close();
    request->send(Storage, kCatalogPath, "application/json");
}

void handlePostRecipeCatalog(AsyncWebServerRequest* request, uint8_t* data,
                             size_t length, size_t index, size_t total) {
    if (!portal_auth_gate(request)) return;
    if (index == 0) {
        catalog_upload_reset(request);
        if (total == 0 || total > kCatalogMaxBytes) {
            web_portal_send_json_error(request, 413, "Recipe catalog exceeds 64 KiB");
            return;
        }
        uint8_t* buffer = static_cast<uint8_t*>(heap_caps_malloc(total + 1,
            psramFound() ? MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT : MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
        if (!buffer) {
            web_portal_send_json_error(request, 503, "Recipe catalog is out of memory");
            return;
        }
        request->_tempObject = new CatalogUpload{buffer, total, 0};
        if (!request->_tempObject) {
            heap_caps_free(buffer);
            web_portal_send_json_error(request, 503, "Recipe catalog is out of memory");
            return;
        }
        request->onDisconnect([request]() { catalog_upload_reset(request); });
    }
    CatalogUpload* upload = catalog_upload(request);
    if (!upload || upload->total != total || index != upload->received ||
        length > upload->total - upload->received || (length && !data)) {
        catalog_upload_reset(request);
        web_portal_send_json_error(request, 400, "Invalid recipe catalog upload");
        return;
    }
    if (length) memcpy(upload->buffer + index, data, length);
    upload->received += length;
    if (upload->received != upload->total) return;

    upload->buffer[upload->total] = '\0';
    JsonDocument document;
    if (deserializeJson(document, upload->buffer, upload->total)) {
        catalog_upload_reset(request);
        web_portal_send_json_error(request, 400, "Recipe catalog is not valid JSON");
        return;
    }
    const bool saved = catalog_save(upload->buffer, upload->total);
    catalog_upload_reset(request);
    if (!saved) {
        web_portal_send_json_error(request, 500, "Failed to save recipe catalog");
        return;
    }
    request->send(200, "application/json", "{\"success\":true}");
}

#endif  // HAS_DISPLAY