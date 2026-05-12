#include "fs_indexed_store.h"

#include "log_manager.h"
#include "psram_json_allocator.h"

#include <LittleFS.h>
#include <algorithm>
#include <string.h>
#include <time.h>
#include <vector>

#define TAG "FsStore"

// PSRAM-backed manifest capacity used during rebuild (no existing file to size from).
// 64 KB comfortably holds ~300 entries at ~200 bytes each with ArduinoJson overhead.
#define MANIFEST_REBUILD_CAPACITY (64 * 1024)

static const char* MANIFEST_FILENAME = "_index.json";

// ---------------------------------------------------------------------------
// File-scope sort helpers (shared by _rebuild_manifest and _sort_entries)
// ---------------------------------------------------------------------------

struct SortEntry { uint32_t ts; String s; };

// Sort vec descending by ts and populate target JsonArray.
// tmp is a caller-provided scratch document reused across iterations.
static void sort_and_populate(JsonArray target,
                               std::vector<SortEntry>& vec,
                               DynamicJsonDocument& tmp) {
    std::sort(vec.begin(), vec.end(),
        [](const SortEntry& a, const SortEntry& b) { return a.ts > b.ts; });
    for (const auto& item : vec) {
        tmp.clear();
        if (deserializeJson(tmp, item.s) == DeserializationError::Ok) {
            target.add(tmp.as<JsonVariant>());
        }
    }
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

FsIndexedStore::FsIndexedStore(const char* base_path,
                                const char* const* index_fields,
                                size_t num_index_fields,
                                const FsIndexedStoreRootField* root_fields,
                                size_t num_root_fields)
    : _base_path(base_path),
      _index_fields(index_fields),
      _num_index_fields(num_index_fields),
      _root_fields(root_fields),
      _num_root_fields(num_root_fields),
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

    // Manifest loading is deferred to the first list()/get()/exists()/count() call.
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

// Public path helper — returns LittleFS path for use in AsyncFileResponse
String FsIndexedStore::data_path(const char* id) const {
    char buf[256];
    _data_path(id, buf, sizeof(buf));
    return String(buf);
}

// ---------------------------------------------------------------------------
// Private: atomic write (String content)
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
// Private: atomic write (JsonDocument — no intermediate String allocation)
// ---------------------------------------------------------------------------

bool FsIndexedStore::_atomic_write_from_doc(const char* path, JsonDocument& doc) {
    String tmp_path = String(path) + ".tmp";

    File f = LittleFS.open(tmp_path.c_str(), "w");
    if (!f) {
        LOGE(TAG, "Cannot open %s for writing", tmp_path.c_str());
        return false;
    }
    size_t expected = measureJson(doc);
    size_t written  = serializeJson(doc, f);
    f.close();

    if (written != expected) {
        LOGE(TAG, "Incomplete write to %s (%u/%u bytes)",
             tmp_path.c_str(), (unsigned)written, (unsigned)expected);
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

    delete _manifest_doc;
    _manifest_doc = new BasicJsonDocument<PsramJsonAllocator>(MANIFEST_REBUILD_CAPACITY);

    // Build a small filter document for file parsing.
    // created_at is always included (needed for sorting) even when not in
    // the caller-configured index_fields list.
    // All other configured fields are added so waveform/payload arrays are skipped.
    StaticJsonDocument<256> filter_doc;
    filter_doc["created_at"] = true;  // always needed for sort
    for (size_t i = 0; i < _num_index_fields; i++) {
        filter_doc[_index_fields[i]] = true;
    }

    JsonArray entries = _manifest_doc->createNestedArray("entries");

    // Inject root field defaults into the freshly-built manifest.
    // Values will be overwritten by set_root_*() calls once the caller
    // has performed any necessary recovery (e.g. scanning entries for max id).
    if (_root_fields) {
        for (size_t i = 0; i < _num_root_fields; i++) {
            const FsIndexedStoreRootField& rf = _root_fields[i];
            if (rf.type == FsIndexedStoreRootField::TYPE_UINT32) {
                (*_manifest_doc)[rf.name] = rf.default_uint32;
            }
        }
    }

    File dir = LittleFS.open(_base_path);
    if (!dir || !dir.isDirectory()) {
        LOGE(TAG, "Cannot open directory %s for rebuild", _base_path);
        return;
    }

    std::vector<SortEntry> collected;

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
        if (!ext) { file = dir.openNextFile(); continue; }

        size_t id_len = (size_t)(ext - fname);
        if (id_len == 0 || id_len >= 128) { file = dir.openNextFile(); continue; }

        char id[128];
        memcpy(id, fname, id_len);
        id[id_len] = '\0';

        char full_path[256];
        snprintf(full_path, sizeof(full_path), "%s/%s", _base_path, fname);
        File df = LittleFS.open(full_path, "r");
        if (!df) { file = dir.openNextFile(); continue; }

        // Parse only the configured index fields — skips waveform/payload data
        // entirely, so data_doc stays small even for 30-50 KB session files.
        DynamicJsonDocument data_doc(2048);
        DeserializationError err = deserializeJson(data_doc, df,
                                                    DeserializationOption::Filter(filter_doc));
        df.close();

        // Build the manifest entry for this file
        DynamicJsonDocument entry_doc(1024);
        JsonObject entry = entry_doc.to<JsonObject>();
        entry["id"] = id;

        if (!err) {
            // created_at is always extracted for sorting, regardless of index_fields.
            if (data_doc.containsKey("created_at")) {
                entry["created_at"] = data_doc["created_at"];
            }
            for (size_t fi = 0; fi < _num_index_fields; fi++) {
                if (strcmp(_index_fields[fi], "created_at") == 0) continue; // already set
                if (data_doc.containsKey(_index_fields[fi])) {
                    entry[_index_fields[fi]] = data_doc[_index_fields[fi]];
                }
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

    DynamicJsonDocument tmp(1024);
    sort_and_populate(entries, collected, tmp);

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
        LOGW(TAG, "Manifest missing for %s \u2014 will rebuild", _base_path);
        do_rebuild = true;
    } else {
        File f = LittleFS.open(path, "r");
        if (!f) {
            do_rebuild = true;
        } else {
            // Size the manifest document dynamically from the actual file size.
            size_t file_size = f.size();
            size_t capacity  = (file_size < 1024) ? 4 * 1024 : file_size * 2 + 512;
            delete _manifest_doc;
            _manifest_doc = new BasicJsonDocument<PsramJsonAllocator>(capacity);

            DeserializationError err = deserializeJson(*_manifest_doc, f);
            f.close();
            if (err || !_manifest_doc->containsKey("entries")) {
                LOGW(TAG, "Corrupt manifest for %s (%s) \u2014 rebuilding",
                     _base_path, err ? err.c_str() : "missing entries");
                do_rebuild = true;
            } else {
                // Inject defaults for any root fields absent from an existing manifest
                // (backward compatibility: manifests written before root fields were added).
                _inject_missing_root_fields();
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

    // Serialize entries to strings BEFORE clearing the document, then
    // rebuild in sorted order. Hoists the scratch doc outside the loop.
    std::vector<SortEntry> vec;
    vec.reserve(entries.size());
    for (JsonVariant e : entries) {
        String s;
        serializeJson(e, s);
        vec.push_back({e["created_at"].as<uint32_t>(), s});
    }

    // Remove only the entries array, preserving all other root-level fields
    // (e.g. next_id). Clearing the whole document would silently discard them.
    _manifest_doc->as<JsonObject>().remove("entries");
    JsonArray sorted = _manifest_doc->createNestedArray("entries");
    DynamicJsonDocument tmp(1024);
    sort_and_populate(sorted, vec, tmp);
}

// ---------------------------------------------------------------------------
// add()
// ---------------------------------------------------------------------------

bool FsIndexedStore::add(const char* id, const String& json_content, const JsonObject& index_meta) {
    if (!id || !id[0]) return false;

    if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) return false;

    _ensure_loaded();

    // Determine created_at: use caller-provided value or auto-generate.
    // Auto-generation is the uncommon path (firmware normally provides it).
    uint32_t created_at = index_meta["created_at"] | (uint32_t)0;
    bool auto_generated = false;
    if (created_at == 0) {
        created_at = (uint32_t)time(nullptr);
        if (created_at < 1000000UL) created_at = (uint32_t)(millis() / 1000);
        auto_generated = true;
        LOGW(TAG, "add(): auto-generated created_at=%u for id '%s'", created_at, id);
    }

    // Write data file atomically.
    // If created_at was auto-generated, inject it into the document so that a
    // manifest rebuild (e.g. after power loss) recovers the same value from disk.
    char dp[256];
    _data_path(id, dp, sizeof(dp));
    bool data_ok;
    if (auto_generated) {
        size_t cap = json_content.length() * 2 + 512;
        BasicJsonDocument<PsramJsonAllocator> data_doc(cap);
        DeserializationError err = deserializeJson(data_doc, json_content);
        if (!err) {
            data_doc["created_at"] = created_at;
            data_ok = _atomic_write_from_doc(dp, data_doc);
        } else {
            LOGE(TAG, "add(): cannot inject created_at into '%s': %s — writing as-is", id, err.c_str());
            data_ok = _atomic_write(dp, json_content);
        }
    } else {
        data_ok = _atomic_write(dp, json_content);
    }

    if (!data_ok) {
        xSemaphoreGive(_mutex);
        return false;
    }

    // Build manifest entry: created_at always present, then configured index fields.
    JsonArray entries = (*_manifest_doc)["entries"].as<JsonArray>();
    if (entries.isNull()) {
        entries = _manifest_doc->createNestedArray("entries");
    }

    JsonObject entry = entries.createNestedObject();
    entry["id"] = String(id);  // Force copy — caller buffer may be transient
    entry["created_at"] = created_at;
    for (size_t i = 0; i < _num_index_fields; i++) {
        if (strcmp(_index_fields[i], "created_at") == 0) continue;  // already set
        if (index_meta.containsKey(_index_fields[i])) {
            entry[_index_fields[i]] = index_meta[_index_fields[i]];
        }
    }

    _sort_entries();

    bool ok = _write_manifest();
    xSemaphoreGive(_mutex);
    return ok;
}

// ---------------------------------------------------------------------------
// register_pre_written()
// ---------------------------------------------------------------------------

bool FsIndexedStore::register_pre_written(const char* id, uint32_t created_at, const JsonObject& index_meta) {
    if (!id || !id[0] || created_at == 0) return false;

    if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) return false;

    _ensure_loaded();

    JsonArray entries = (*_manifest_doc)["entries"].as<JsonArray>();
    if (entries.isNull()) {
        entries = _manifest_doc->createNestedArray("entries");
    }

    JsonObject entry = entries.createNestedObject();
    entry["id"] = String(id);  // Force copy — caller buffer may be transient
    entry["created_at"] = created_at;
    for (size_t i = 0; i < _num_index_fields; i++) {
        if (strcmp(_index_fields[i], "created_at") == 0) continue;
        if (index_meta.containsKey(_index_fields[i])) {
            entry[_index_fields[i]] = index_meta[_index_fields[i]];
        }
    }

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

    if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) return "";
    _ensure_loaded();
    bool found = false;
    JsonArray entries = (*_manifest_doc)["entries"].as<JsonArray>();
    if (!entries.isNull()) {
        for (JsonVariant entry : entries) {
            if (strcmp(entry["id"] | "", id) == 0) { found = true; break; }
        }
    }
    xSemaphoreGive(_mutex);

    if (!found) return "";

    char dp[256];
    _data_path(id, dp, sizeof(dp));
    File f = LittleFS.open(dp, "r");
    if (!f) return "";

    String content = f.readString();
    f.close();
    return content;
}

// ---------------------------------------------------------------------------
// exists()
// ---------------------------------------------------------------------------

bool FsIndexedStore::exists(const char* id) {
    if (!id || !id[0]) return false;

    if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) return false;
    _ensure_loaded();
    bool found = false;
    JsonArray entries = (*_manifest_doc)["entries"].as<JsonArray>();
    if (!entries.isNull()) {
        for (JsonVariant entry : entries) {
            if (strcmp(entry["id"] | "", id) == 0) { found = true; break; }
        }
    }
    xSemaphoreGive(_mutex);
    return found;
}

// ---------------------------------------------------------------------------
// remove()
// ---------------------------------------------------------------------------

bool FsIndexedStore::remove(const char* id) {
    if (!id || !id[0]) return false;

    if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) return false;

    _ensure_loaded();

    bool found = false;
    JsonArray entries = (*_manifest_doc)["entries"].as<JsonArray>();
    if (!entries.isNull()) {
        for (size_t i = 0; i < entries.size(); i++) {
            if (strcmp(entries[i]["id"] | "", id) == 0) {
                entries.remove(i);
                found = true;
                break;
            }
        }
    }

    if (!found) {
        xSemaphoreGive(_mutex);
        return false;  // entry not found — caller should return 404
    }

    // Write manifest first. Only delete the data file if the manifest was
    // successfully committed — this prevents the store from losing track of
    // the file if the flash write fails mid-operation.
    bool ok = _write_manifest();
    if (ok) {
        char dp[256];
        _data_path(id, dp, sizeof(dp));
        LittleFS.remove(dp);  // best-effort; ignore error (orphan is harmless)
    }

    xSemaphoreGive(_mutex);
    return ok;
}

// ---------------------------------------------------------------------------
// clear_all()
// ---------------------------------------------------------------------------

bool FsIndexedStore::clear_all() {
    if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) return false;

    // Delete all data files in the store directory.
    File dir = LittleFS.open(_base_path);
    if (dir) {
        File f = dir.openNextFile();
        while (f) {
            const char* fname = f.name();
            // Skip the manifest itself; delete everything else.
            if (strcmp(fname, MANIFEST_FILENAME) != 0) {
                char full[256];
                snprintf(full, sizeof(full), "%s/%s", _base_path, fname);
                f.close();
                LittleFS.remove(full);
            } else {
                f.close();
            }
            f = dir.openNextFile();
        }
        dir.close();
    }

    // Reset manifest to empty state with root fields at their defaults.
    if (!_manifest_doc) {
        xSemaphoreGive(_mutex);
        return false;
    }
    _manifest_doc->clear();
    JsonObject root = _manifest_doc->to<JsonObject>();
    root.createNestedArray("entries");
    for (size_t i = 0; i < _num_root_fields; i++) {
        const FsIndexedStoreRootField& rf = _root_fields[i];
        if (rf.type == FsIndexedStoreRootField::TYPE_UINT32) {
            root[rf.name] = rf.default_uint32;
        }
    }
    _loaded = true;

    bool ok = _write_manifest();
    xSemaphoreGive(_mutex);
    return ok;
}

// ---------------------------------------------------------------------------
// patch_meta()
// ---------------------------------------------------------------------------

bool FsIndexedStore::patch_meta(const char* id, const JsonObject& fields) {
    if (!id || !id[0]) return false;

    if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) return false;

    _ensure_loaded();

    // Patch the data file FIRST — only update the manifest if file write succeeds.
    char dp[256];
    _data_path(id, dp, sizeof(dp));

    bool ok = false;
    if (!LittleFS.exists(dp)) {
        LOGW(TAG, "patch_meta: data file missing for id '%s'", id);
    } else {
        File f = LittleFS.open(dp, "r");
        if (f) {
            size_t file_size = f.size();
            // Size doc from actual file; use PSRAM to avoid internal-RAM pressure.
            BasicJsonDocument<PsramJsonAllocator> data_doc(file_size * 2 + 512);
            DeserializationError err = deserializeJson(data_doc, f);
            f.close();
            if (!err) {
                for (JsonPair kv : fields) {
                    data_doc[kv.key()] = kv.value();
                }
                // Serialize directly to disk — no intermediate String allocation.
                ok = _atomic_write_from_doc(dp, data_doc);
            } else {
                LOGE(TAG, "patch_meta: cannot parse %s: %s", dp, err.c_str());
            }
        }
    }

    // Only commit manifest changes after the data file write succeeded.
    if (ok) {
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
        bool manifest_ok = _write_manifest();
        if (!manifest_ok) {
            // Manifest write failed — in-memory state no longer matches disk.
            // Invalidate so the next access reloads from the on-disk manifest
            // (which still has the pre-patch values), restoring consistency.
            delete _manifest_doc;
            _manifest_doc = nullptr;
            _loaded = false;
            _manifest_cache = "";
        }
        ok = manifest_ok;
    }

    xSemaphoreGive(_mutex);
    return ok;
}

bool FsIndexedStore::patch_meta(const char* id, const String& json_patch) {
    if (json_patch.isEmpty()) return false;

    DynamicJsonDocument patch_doc(FS_STORE_PATCH_MAX_BYTES);
    DeserializationError err = deserializeJson(patch_doc, json_patch);
    if (err) {
        LOGW(TAG, "patch_meta: invalid JSON patch for '%s': %s", id, err.c_str());
        return false;
    }

    return patch_meta(id, patch_doc.as<JsonObject>());
}

// ---------------------------------------------------------------------------
// patch_entry()
// ---------------------------------------------------------------------------

bool FsIndexedStore::patch_entry(const char* id, const JsonObject& fields) {
    if (!id || !id[0]) return false;

    if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) return false;

    _ensure_loaded();

    bool found = false;
    JsonArray entries = (*_manifest_doc)["entries"].as<JsonArray>();
    if (!entries.isNull()) {
        for (JsonVariant entry : entries) {
            if (strcmp(entry["id"] | "", id) == 0) {
                for (JsonPair kv : fields) {
                    entry.as<JsonObject>()[kv.key()] = kv.value();
                }
                found = true;
                break;
            }
        }
    }

    if (!found) {
        xSemaphoreGive(_mutex);
        return false;
    }

    bool ok = _write_manifest();
    if (!ok) {
        delete _manifest_doc;
        _manifest_doc = nullptr;
        _loaded = false;
        _manifest_cache = "";
    } else {
        // Patch fields may alias caller's string buffers (zero-copy storage).
        // Force reload from disk on next access so we never read dangling pointers.
        _loaded = false;
    }

    xSemaphoreGive(_mutex);
    return ok;
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

// ---------------------------------------------------------------------------
// Private: _inject_missing_root_fields
// ---------------------------------------------------------------------------

// Injects default values for any configured root fields not present in
// _manifest_doc. Called after loading an existing manifest (backward compat)
// and after rebuild (where all root fields start missing from the fresh doc).
// Updates _manifest_cache if any field was injected.
// Must be called with the mutex held (or from a path where _loaded is false).
void FsIndexedStore::_inject_missing_root_fields() {
    if (!_manifest_doc || !_root_fields || _num_root_fields == 0) return;

    bool any_injected = false;
    for (size_t i = 0; i < _num_root_fields; i++) {
        const FsIndexedStoreRootField& rf = _root_fields[i];
        if (_manifest_doc->containsKey(rf.name)) continue;
        switch (rf.type) {
            case FsIndexedStoreRootField::TYPE_UINT32:
                (*_manifest_doc)[rf.name] = rf.default_uint32;
                break;
        }
        LOGW(TAG, "Injected missing root field '%s' with default for %s",
             rf.name, _base_path);
        any_injected = true;
    }

    if (any_injected) {
        // Re-serialise cache to include injected fields.
        // No disk write here — the next _write_manifest() call persists them.
        _manifest_cache = "";
        serializeJson(*_manifest_doc, _manifest_cache);
    }
}

// ---------------------------------------------------------------------------
// get_root_uint32()
// ---------------------------------------------------------------------------

bool FsIndexedStore::get_root_uint32(const char* name, uint32_t& out) {
    if (!name || !name[0]) return false;
    if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) return false;

    _ensure_loaded();

    bool found = false;
    if (_manifest_doc && _manifest_doc->containsKey(name)) {
        out   = (*_manifest_doc)[name].as<uint32_t>();
        found = true;
    }
    xSemaphoreGive(_mutex);
    return found;
}

// ---------------------------------------------------------------------------
// set_root_uint32()
// ---------------------------------------------------------------------------

bool FsIndexedStore::set_root_uint32(const char* name, uint32_t value) {
    if (!name || !name[0]) return false;
    if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) return false;

    _ensure_loaded();

    bool ok = false;
    if (_manifest_doc) {
        (*_manifest_doc)[name] = value;
        ok = _write_manifest();
        if (!ok) {
            // Manifest write failed — in-memory state diverged from disk.
            // Invalidate so the next access reloads from the on-disk version.
            delete _manifest_doc;
            _manifest_doc = nullptr;
            _loaded       = false;
            _manifest_cache = "";
        }
    }
    xSemaphoreGive(_mutex);
    return ok;
}

// ---------------------------------------------------------------------------
// allocate_id()
// ---------------------------------------------------------------------------

uint32_t FsIndexedStore::allocate_id(const char* field_name, const char* prefix,
                                      char* buf, size_t buf_size) {
    if (!field_name || !field_name[0] || !buf || buf_size < 16) return 0;
    if (xSemaphoreTake(_mutex, portMAX_DELAY) != pdTRUE) return 0;

    _ensure_loaded();

    if (!_manifest_doc || !_manifest_doc->containsKey(field_name)) {
        xSemaphoreGive(_mutex);
        return 0;
    }

    uint32_t id = (*_manifest_doc)[field_name].as<uint32_t>();
    (*_manifest_doc)[field_name] = id + 1;

    bool ok = _write_manifest();
    if (!ok) {
        // Rollback in-memory state on disk failure.
        delete _manifest_doc;
        _manifest_doc = nullptr;
        _loaded       = false;
        _manifest_cache = "";
        xSemaphoreGive(_mutex);
        return 0;
    }

    xSemaphoreGive(_mutex);

    snprintf(buf, buf_size, "%s%lu", prefix ? prefix : "", (unsigned long)id);
    return id;
}
