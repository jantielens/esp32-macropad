#include "fs_indexed_store.h"

#include "log_manager.h"

#include <LittleFS.h>
#include <algorithm>
#include <string.h>
#include <vector>

#define TAG "FsStore"

// Initial JSON document capacity for the manifest.  The manifest holds
// metadata-only entries (no waveform data), so 8 KB covers ~40 entries at
// ~200 bytes each with comfortable headroom.
#define MANIFEST_DOC_CAPACITY (8 * 1024)

// Maximum total size for a PATCH body (metadata fields only).
// Callers should never need more than a handful of small string fields.
#define PATCH_META_MAX_BODY 1024

static const char* MANIFEST_FILENAME = "_index.json";

// ---------------------------------------------------------------------------
// Constructor / destructor
// ---------------------------------------------------------------------------

FsIndexedStore::FsIndexedStore(const char* base_path)
    : _base_path(base_path),
      _mutex(nullptr),
      _manifest_doc(nullptr),
      _loaded(false)
{
    _mutex = xSemaphoreCreateMutex();
}

// ---------------------------------------------------------------------------
// begin()
// ---------------------------------------------------------------------------

bool FsIndexedStore::begin() {
    if (!_mutex) {
        LOGE(TAG, "Mutex creation failed for %s", _base_path);
        return false;
    }

    if (!LittleFS.exists(_base_path)) {
        if (!LittleFS.mkdir(_base_path)) {
            LOGE(TAG, "Failed to create directory %s", _base_path);
            return false;
        }
        LOGI(TAG, "Created directory %s", _base_path);
    }

    // Manifest loading is deferred to the first list()/get()/count() call.
    return true;
}

// ---------------------------------------------------------------------------
// Private: path helpers
// ---------------------------------------------------------------------------

void FsIndexedStore::_data_path(const char* id, char* out, size_t out_len) const {
    snprintf(out, out_len, "%s/%s.json", _base_path, id);
}

void FsIndexedStore::_manifest_path(char* out, size_t out_len) const {
    snprintf(out, out_len, "%s/%s", _base_path, MANIFEST_FILENAME);
}

// ---------------------------------------------------------------------------
// Private: atomic write
// ---------------------------------------------------------------------------

bool FsIndexedStore::_atomic_write(const char* path, const String& content) {
    String tmp_path = String(path) + ".tmp";

    File f = LittleFS.open(tmp_path.c_str(), "w");
    if (!f) {
        LOGE(TAG, "Cannot open %s for writing", tmp_path.c_str());
        return false;
    }
    size_t written = f.print(content);
    f.close();

    if (written != content.length()) {
        LOGE(TAG, "Incomplete write to %s (%u/%u bytes)",
             tmp_path.c_str(), (unsigned)written, (unsigned)content.length());
        LittleFS.remove(tmp_path.c_str());
        return false;
    }

    if (!LittleFS.rename(tmp_path.c_str(), path)) {
        LOGE(TAG, "Rename %s -> %s failed", tmp_path.c_str(), path);
        LittleFS.remove(tmp_path.c_str());
        return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Private: _write_manifest
// ---------------------------------------------------------------------------

bool FsIndexedStore::_write_manifest() {
    if (!_manifest_doc) return false;

    // Serialise to cache string
    _manifest_cache = "";
    serializeJson(*_manifest_doc, _manifest_cache);

    // Flush to disk atomically
    char path[128];
    _manifest_path(path, sizeof(path));
    return _atomic_write(path, _manifest_cache);
}

// ---------------------------------------------------------------------------
// Private: _rebuild_manifest
// ---------------------------------------------------------------------------

void FsIndexedStore::_rebuild_manifest() {
    LOGW(TAG, "Rebuilding manifest for %s", _base_path);

    if (!_manifest_doc) {
        _manifest_doc = new DynamicJsonDocument(MANIFEST_DOC_CAPACITY);
    } else {
        _manifest_doc->clear();
    }

    JsonArray entries = _manifest_doc->createNestedArray("entries");

    File dir = LittleFS.open(_base_path);
    if (!dir || !dir.isDirectory()) {
        LOGE(TAG, "Cannot open directory %s for rebuild", _base_path);
        return;
    }

    // Collect entries during scan, then sort before adding to JsonArray
    // (JsonArrayIterator is not a random-access iterator so std::sort cannot
    // be used directly on the array — we sort a std::vector instead).
    struct RebuildEntry { uint32_t ts; String json_str; };
    std::vector<RebuildEntry> collected;

    File file = dir.openNextFile();
    while (file) {
        const char* fname = file.name();

        // Skip manifest itself and any .tmp files
        if (strcmp(fname, MANIFEST_FILENAME) == 0 ||
            strstr(fname, ".tmp") != nullptr) {
            file = dir.openNextFile();
            continue;
        }

        // Derive ID by stripping ".json" suffix
        const char* ext = strstr(fname, ".json");
        if (!ext) {
            file = dir.openNextFile();
            continue;
        }

        // Build id from filename without extension
        size_t id_len = (size_t)(ext - fname);
        if (id_len == 0 || id_len >= 128) {
            file = dir.openNextFile();
            continue;
        }

        char id[128];
        memcpy(id, fname, id_len);
        id[id_len] = '\0';

        // Parse the data file to extract metadata
        char full_path[256];
        snprintf(full_path, sizeof(full_path), "%s/%s", _base_path, fname);
        File df = LittleFS.open(full_path, "r");
        if (!df) {
            file = dir.openNextFile();
            continue;
        }

        DynamicJsonDocument data_doc(4096);
        DeserializationError err = deserializeJson(data_doc, df);
        df.close();

        // Build a temporary entry object and serialize it for deferred sorting
        DynamicJsonDocument entry_doc(1024);
        JsonObject entry = entry_doc.to<JsonObject>();
        entry["id"] = id;

        if (!err) {
            // Copy scalar fields from the data document root into the entry
            for (JsonPair kv : data_doc.as<JsonObject>()) {
                // Skip large nested objects/arrays (e.g. waveform data)
                if (kv.value().is<JsonObject>() || kv.value().is<JsonArray>()) continue;
                entry[kv.key()] = kv.value();
            }
        } else {
            LOGW(TAG, "Cannot parse %s during rebuild: %s", full_path, err.c_str());
            // Keep entry with id only — better than losing track of the file
        }

        String entry_str;
        serializeJson(entry_doc, entry_str);
        collected.push_back({entry["created_at"].as<uint32_t>(), entry_str});

        file = dir.openNextFile();
    }

    // Sort collected entries descending by created_at
    std::sort(collected.begin(), collected.end(),
        [](const RebuildEntry& a, const RebuildEntry& b) {
            return a.ts > b.ts;
        });

    // Populate JsonArray in sorted order
    for (const auto& ce : collected) {
        DynamicJsonDocument tmp(ce.json_str.length() * 2 + 128);
        if (deserializeJson(tmp, ce.json_str) == DeserializationError::Ok) {
            entries.add(tmp.as<JsonVariant>());
        }
    }

    _write_manifest();
    LOGI(TAG, "Rebuild complete: %u entries", (unsigned)entries.size());
}

// ---------------------------------------------------------------------------
// Private: _ensure_loaded
// ---------------------------------------------------------------------------

void FsIndexedStore::_ensure_loaded() {
    if (_loaded) return;

    char path[128];
    _manifest_path(path, sizeof(path));

    bool do_rebuild = false;

    if (!LittleFS.exists(path)) {
        LOGW(TAG, "Manifest missing for %s — will rebuild", _base_path);
        do_rebuild = true;
    } else {
        if (!_manifest_doc) {
            _manifest_doc = new DynamicJsonDocument(MANIFEST_DOC_CAPACITY);
        }
        File f = LittleFS.open(path, "r");
        if (!f) {
            do_rebuild = true;
        } else {
            DeserializationError err = deserializeJson(*_manifest_doc, f);
            f.close();
            if (err || !_manifest_doc->containsKey("entries")) {
                LOGW(TAG, "Corrupt manifest for %s (%s) — rebuilding",
                     _base_path, err ? err.c_str() : "missing entries");
                do_rebuild = true;
            } else {
                // Cache is the serialised form
                _manifest_cache = "";
                serializeJson(*_manifest_doc, _manifest_cache);
            }
        }
    }

    if (do_rebuild) {
        _rebuild_manifest();
    }

    _loaded = true;
}

// ---------------------------------------------------------------------------
// Private: _sort_entries
// ---------------------------------------------------------------------------

void FsIndexedStore::_sort_entries() {
    if (!_manifest_doc) return;
    JsonArray entries = (*_manifest_doc)["entries"].as<JsonArray>();
    if (entries.isNull() || entries.size() < 2) return;

    // JsonArrayIterator is a forward-only iterator; std::sort requires random
    // access.  Extract to a std::vector of (created_at, serialized_entry),
    // sort the vector, then rebuild the JsonArray in sorted order.
    struct SE { uint32_t ts; String s; };
    std::vector<SE> vec;
    vec.reserve(entries.size());
    for (JsonVariant e : entries) {
        String s;
        serializeJson(e, s);
        vec.push_back({e["created_at"].as<uint32_t>(), s});
    }

    std::sort(vec.begin(), vec.end(),
        [](const SE& a, const SE& b) { return a.ts > b.ts; });

    _manifest_doc->clear();
    JsonArray sorted = _manifest_doc->createNestedArray("entries");
    for (const auto& item : vec) {
        DynamicJsonDocument tmp(item.s.length() * 2 + 128);
        if (deserializeJson(tmp, item.s) == DeserializationError::Ok) {
            sorted.add(tmp.as<JsonVariant>());
        }
    }
}

// ---------------------------------------------------------------------------
// add()
// ---------------------------------------------------------------------------

bool FsIndexedStore::add(const char* id, const String& json_content, const JsonObject& index_meta) {
    if (!id || !id[0]) return false;

    if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) return false;

    _ensure_loaded();

    // Write data file atomically
    char data_path[256];
    _data_path(id, data_path, sizeof(data_path));
    if (!_atomic_write(data_path, json_content)) {
        xSemaphoreGive(_mutex);
        return false;
    }

    // Build manifest entry
    JsonArray entries = (*_manifest_doc)["entries"].as<JsonArray>();
    if (entries.isNull()) {
        entries = _manifest_doc->createNestedArray("entries");
    }

    JsonObject entry = entries.createNestedObject();
    entry["id"] = id;
    for (JsonPair kv : index_meta) {
        entry[kv.key()] = kv.value();
    }

    // Sort descending by created_at
    _sort_entries();

    bool ok = _write_manifest();
    xSemaphoreGive(_mutex);
    return ok;
}

// ---------------------------------------------------------------------------
// get()
// ---------------------------------------------------------------------------

String FsIndexedStore::get(const char* id) {
    if (!id || !id[0]) return "";

    char data_path[256];
    _data_path(id, data_path, sizeof(data_path));

    // No need to hold mutex across the entire file read — we only need it to
    // derive the path (done above without shared state).  But we should still
    // confirm the entry exists in the manifest so that we don't accidentally
    // serve orphaned files.
    if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) return "";
    _ensure_loaded();
    bool exists_in_manifest = false;
    JsonArray entries = (*_manifest_doc)["entries"].as<JsonArray>();
    if (!entries.isNull()) {
        for (JsonVariant entry : entries) {
            if (strcmp(entry["id"] | "", id) == 0) {
                exists_in_manifest = true;
                break;
            }
        }
    }
    xSemaphoreGive(_mutex);

    if (!exists_in_manifest) return "";
    if (!LittleFS.exists(data_path)) return "";

    File f = LittleFS.open(data_path, "r");
    if (!f) return "";

    String content = f.readString();
    f.close();
    return content;
}

// ---------------------------------------------------------------------------
// remove()
// ---------------------------------------------------------------------------

bool FsIndexedStore::remove(const char* id) {
    if (!id || !id[0]) return false;

    if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) return false;

    _ensure_loaded();

    // Remove from manifest
    JsonArray entries = (*_manifest_doc)["entries"].as<JsonArray>();
    if (!entries.isNull()) {
        for (size_t i = 0; i < entries.size(); i++) {
            if (strcmp(entries[i]["id"] | "", id) == 0) {
                entries.remove(i);
                break;
            }
        }
    }

    _write_manifest();

    // Delete data file (best-effort — manifest is already updated)
    char data_path[256];
    _data_path(id, data_path, sizeof(data_path));
    if (LittleFS.exists(data_path)) {
        LittleFS.remove(data_path);
    }

    xSemaphoreGive(_mutex);
    return true;
}

// ---------------------------------------------------------------------------
// patch_meta()
// ---------------------------------------------------------------------------

bool FsIndexedStore::patch_meta(const char* id, const JsonObject& fields) {
    if (!id || !id[0]) return false;

    if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) return false;

    _ensure_loaded();

    // Update manifest entry
    JsonArray entries = (*_manifest_doc)["entries"].as<JsonArray>();
    if (!entries.isNull()) {
        for (JsonVariant entry : entries) {
            if (strcmp(entry["id"] | "", id) == 0) {
                for (JsonPair kv : fields) {
                    entry.as<JsonObject>()[kv.key()] = kv.value();
                }
                break;
            }
        }
    }

    // Patch data file: load, merge, write back atomically
    char data_path[256];
    _data_path(id, data_path, sizeof(data_path));

    bool ok = false;
    if (LittleFS.exists(data_path)) {
        File f = LittleFS.open(data_path, "r");
        if (f) {
            DynamicJsonDocument data_doc(16 * 1024);
            DeserializationError err = deserializeJson(data_doc, f);
            f.close();
            if (!err) {
                for (JsonPair kv : fields) {
                    data_doc[kv.key()] = kv.value();
                }
                String updated;
                serializeJson(data_doc, updated);
                ok = _atomic_write(data_path, updated);
            } else {
                LOGE(TAG, "patch_meta: cannot parse %s: %s", data_path, err.c_str());
            }
        }
    } else {
        LOGW(TAG, "patch_meta: data file missing for id '%s'", id);
    }

    _write_manifest();
    xSemaphoreGive(_mutex);
    return ok;
}

bool FsIndexedStore::patch_meta(const char* id, const String& json_patch) {
    if (json_patch.isEmpty()) return false;

    DynamicJsonDocument patch_doc(PATCH_META_MAX_BODY);
    DeserializationError err = deserializeJson(patch_doc, json_patch);
    if (err) {
        LOGW(TAG, "patch_meta: invalid JSON patch for '%s': %s", id, err.c_str());
        return false;
    }

    return patch_meta(id, patch_doc.as<JsonObject>());
}

// ---------------------------------------------------------------------------
// list()
// ---------------------------------------------------------------------------

String FsIndexedStore::list() {
    if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) return "{}";
    _ensure_loaded();
    String result = _manifest_cache;
    xSemaphoreGive(_mutex);
    return result;
}

// ---------------------------------------------------------------------------
// count()
// ---------------------------------------------------------------------------

int FsIndexedStore::count() {
    if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) return 0;
    _ensure_loaded();
    int n = 0;
    JsonArray entries = (*_manifest_doc)["entries"].as<JsonArray>();
    if (!entries.isNull()) {
        n = (int)entries.size();
    }
    xSemaphoreGive(_mutex);
    return n;
}
