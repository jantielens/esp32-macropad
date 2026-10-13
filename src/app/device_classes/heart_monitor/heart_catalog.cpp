#include "heart_catalog.h"
#include "heart_capture.h"
#include "fs_indexed_store.h"
#include "storage.h"
#include "ota_activity.h"
#include "log_manager.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>

namespace heart_monitor {
namespace {
const char* const catalog_fields[] = {
    "session_id", "started_unix_time_s", "duration_s", "sample_count", "complete", "stop_reason",
    "sufficient_reliable_data", "reliable_coverage", "reliable_duration_s",
    "reliable_bpm_mean", "reliable_bpm_min", "reliable_bpm_max"
};
const FsIndexedStoreRootField catalog_root[] = {
    {"import_version", FsIndexedStoreRootField::TYPE_UINT32, {.default_uint32 = 0}}
};
FsIndexedStore catalog_store("/storage/heart/catalog", catalog_fields,
    sizeof(catalog_fields) / sizeof(catalog_fields[0]), catalog_root, 1);
SemaphoreHandle_t catalog_mutex = nullptr;
bool catalog_ready = false, import_requested = false;
uint32_t import_scanned = 0, import_added = 0, import_failed = 0;
File import_directory;
char import_session[9] = {};

bool valid_session_id(const char* id) {
    if (!id || strlen(id) != 8) return false;
    for (size_t index = 0; index < 8; ++index)
        if (!((id[index] >= '0' && id[index] <= '9') || (id[index] >= 'a' && id[index] <= 'f'))) return false;
    return true;
}

void capture_path(const char* id, const char* extension, char* path, size_t size) {
    snprintf(path, size, "/storage/heart/capture-%s.%s", id, extension);
}

class CatalogReadStream {
public:
    explicit CatalogReadStream(File& file) : file_(file) {}
    static int read(void* context) {
        return static_cast<CatalogReadStream*>(context)->read_byte();
    }
private:
    int read_byte() {
        if (ota_activity_is_active()) return -1;
        if (++reads_ % 4096 == 0) vTaskDelay(1);
        return ota_activity_is_active() ? -1 : file_.read();
    }
    File& file_;
    uint32_t reads_ = 0;
};
}

bool catalog_init() {
    catalog_mutex = xSemaphoreCreateMutex();
    if (!catalog_mutex || !storage_mount()) return false;
    if (!Storage.exists("/storage") && !Storage.mkdir("/storage")) return false;
    if (!Storage.exists("/storage/heart") && !Storage.mkdir("/storage/heart")) return false;
    catalog_ready = catalog_store.begin();
    return catalog_ready;
}

bool catalog_prepare() {
    if (!catalog_ready || ota_activity_is_active()) return false;
    xSemaphoreTake(catalog_mutex, portMAX_DELAY);
    const bool dirty = !ota_activity_is_active() && catalog_store.set_root_uint32("import_version", 0);
    if (dirty) import_requested = true;
    xSemaphoreGive(catalog_mutex);
    return dirty;
}

bool catalog_add(const char* id) {
    if (!catalog_ready || !valid_session_id(id) || ota_activity_is_active()) return false;
    char report_path[96], csv_path[96];
    capture_path(id, "json", report_path, sizeof(report_path));
    capture_path(id, "csv", csv_path, sizeof(csv_path));
    File file = Storage.open(report_path, FILE_READ);
    if (!file) return false;
    JsonDocument metadata;
    uint64_t csv_bytes = 0;
    CatalogReadStream stream(file);
    const bool parsed = catalog_read_metadata(CatalogReadStream::read, &stream, id, metadata, csv_bytes);
    file.close();
    if (!parsed || ota_activity_is_active()) return false;
    xSemaphoreTake(catalog_mutex, portMAX_DELAY);
    if (ota_activity_is_active()) { xSemaphoreGive(catalog_mutex); return false; }
    File csv = Storage.open(csv_path, FILE_READ);
    const bool paired = Storage.exists(report_path) && csv && csv.size() == csv_bytes;
    csv.close();
    bool saved = false;
    if (paired && !ota_activity_is_active()) {
        if (catalog_store.exists(id)) saved = catalog_store.patch_entry(id, metadata.as<JsonObject>());
        else {
            String content;
            serializeJson(metadata, content);
            saved = catalog_store.add(id, content, metadata.as<JsonObject>());
        }
    }
    xSemaphoreGive(catalog_mutex);
    return saved;
}

String catalog_list() {
    if (!catalog_ready || ota_activity_is_active()) return "";
    JsonDocument document;
    if (deserializeJson(document, catalog_store.list())) return "";
    xSemaphoreTake(catalog_mutex, portMAX_DELAY);
    uint32_t version = 0;
    catalog_store.get_root_uint32("import_version", version);
    document["import"]["active"] = import_requested || version != 1;
    document["import"]["scanned"] = import_scanned;
    document["import"]["added"] = import_added;
    document["import"]["failed"] = import_failed;
    xSemaphoreGive(catalog_mutex);
    String result;
    serializeJson(document, result);
    return result;
}

bool catalog_remove(const char* id) {
    if (!catalog_ready || !valid_session_id(id) || ota_activity_is_active()) return false;
    xSemaphoreTake(catalog_mutex, portMAX_DELAY);
    if (ota_activity_is_active()) { xSemaphoreGive(catalog_mutex); return false; }
    char path[96];
    bool removed = true;
    for (const char* extension : {"json", "csv"}) {
        if (ota_activity_is_active()) { removed = false; break; }
        capture_path(id, extension, path, sizeof(path));
        if (Storage.exists(path) && !Storage.remove(path)) { removed = false; break; }
    }
    if (removed && ota_activity_is_active()) removed = false;
    if (removed) {
        const String metadata_path = catalog_store.data_path(id);
        if (catalog_store.exists(id)) removed = catalog_store.remove(id);
        else if (Storage.exists(metadata_path.c_str())) removed = Storage.remove(metadata_path.c_str());
    }
    xSemaphoreGive(catalog_mutex);
    return removed;
}

bool catalog_request_import() {
    if (!catalog_ready || ota_activity_is_active()) return false;
    xSemaphoreTake(catalog_mutex, portMAX_DELAY);
    bool accepted = !import_requested && !ota_activity_is_active();
    if (accepted) accepted = catalog_store.set_root_uint32("import_version", 0);
    if (accepted) {
        import_requested = true;
        import_scanned = import_added = import_failed = 0;
    }
    xSemaphoreGive(catalog_mutex);
    return accepted;
}

void catalog_import_tick() {
    if (!catalog_ready || ota_activity_is_active()) return;
    char id[9] = {};
    xSemaphoreTake(catalog_mutex, portMAX_DELAY);
    if (ota_activity_is_active()) { xSemaphoreGive(catalog_mutex); return; }
    if (!import_requested) {
        uint32_t version = 0;
        catalog_store.get_root_uint32("import_version", version);
        import_requested = version != 1;
    }
    if (!import_requested) { xSemaphoreGive(catalog_mutex); return; }
    if (!import_directory) import_directory = Storage.open("/storage/heart");
    if (!import_directory) { xSemaphoreGive(catalog_mutex); return; }
    if (!import_session[0]) {
        File file = import_directory.openNextFile();
        if (!file) {
            import_directory.close();
            if (!ota_activity_is_active() && catalog_store.set_root_uint32("import_version", 1)) import_requested = false;
            xSemaphoreGive(catalog_mutex);
            return;
        }
        const char* name = strrchr(file.name(), '/');
        name = name ? name + 1 : file.name();
        if (!file.isDirectory() && strlen(name) == 21 && strncmp(name, "capture-", 8) == 0 && strcmp(name + 16, ".json") == 0) {
            memcpy(import_session, name + 8, 8);
            import_session[8] = '\0';
        }
        file.close();
    }
    if (valid_session_id(import_session)) {
        snprintf(id, sizeof(id), "%s", import_session);
        if (catalog_store.exists(id)) { ++import_scanned; import_session[0] = '\0'; id[0] = '\0'; }
    } else import_session[0] = '\0';
    xSemaphoreGive(catalog_mutex);
    if (!id[0]) return;
    const bool added = catalog_add(id);
    xSemaphoreTake(catalog_mutex, portMAX_DELAY);
    if (!ota_activity_is_active()) {
        ++import_scanned;
        if (added) ++import_added; else ++import_failed;
        import_session[0] = '\0';
    }
    xSemaphoreGive(catalog_mutex);
}
}