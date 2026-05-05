#pragma once

#include "psram_json_allocator.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Maximum PATCH body size accepted (metadata fields only).
// Shared between fs_indexed_store.cpp and web_portal_fs_store.cpp.
#define FS_STORE_PATCH_MAX_BYTES 1024

// FsIndexedStore — manifest-indexed JSON document collection on LittleFS.
//
// Maintains a _index.json manifest alongside data files so that list
// operations read a single file rather than iterating every document.
//
// Thread-safe: all public methods are mutex-protected and may be called from
// the web server task and the application task concurrently.
//
// Usage:
//   static const char* kFields[] = {"created_at", "duration_ms"};
//   static FsIndexedStore store("/storage/sessions", kFields, 2);
//
//   // After LittleFS.begin():
//   store.begin();
//
//   // Add a document (firmware writes data; portal never POSTs raw documents):
//   DynamicJsonDocument meta(256);
//   meta["created_at"] = (uint32_t)time(nullptr);
//   meta["duration_ms"] = 1234;
//   store.add("sess_1714900000", full_json_string, meta.as<JsonObject>());
//
//   // List (fast — returns cached manifest JSON):
//   String manifest = store.list();
//
//   // Check existence (manifest-only, no file I/O):
//   if (store.exists("sess_1714900000")) { ... }
//
//   // Get the filesystem path for streaming via AsyncFileResponse:
//   String path = store.data_path("sess_1714900000");
//
//   // Delete:
//   store.remove("sess_1714900000");
//
//   // Update metadata in both manifest and data file:
//   DynamicJsonDocument patch(128);
//   patch["notes"] = "Updated";
//   store.patch_meta("sess_1714900000", patch.as<JsonObject>());
//   // Or from a raw JSON string (web handler convenience):
//   store.patch_meta("sess_1714900000", "{\"notes\":\"Updated\"}");

class FsIndexedStore {
public:
    // base_path:        LittleFS directory, e.g. "/storage/sessions" (no trailing slash).
    // index_fields:     Array of field names to extract from data documents into the
    //                   manifest entry. Must remain valid for the lifetime of this object
    //                   (use static string literals). "created_at" is always included
    //                   in every manifest entry regardless of this list.
    // num_index_fields: Number of entries in index_fields.
    FsIndexedStore(const char* base_path,
                   const char* const* index_fields,
                   size_t num_index_fields);
    ~FsIndexedStore() = default;

    // Call once after LittleFS.begin(). Creates the base directory if missing.
    // Manifest loading is deferred to the first list(), get(), exists(), or count() call.
    bool begin();

    // Write a new document to disk and append its entry to the manifest.
    // id           — unique document identifier (caller-managed, e.g. timestamp string).
    // json_content — full JSON body to write as the data file.
    // index_meta   — metadata fields to store in the manifest entry. Only fields
    //                matching index_fields[] are copied. "created_at" is auto-generated
    //                from the system clock if the caller does not provide it.
    bool add(const char* id, const String& json_content, const JsonObject& index_meta);

    // Return the full JSON content of a single document, or "" if not found.
    String get(const char* id);

    // Return true if an entry with the given id exists in the manifest.
    // Manifest-only lookup — no file I/O.
    bool exists(const char* id);

    // Delete the data file and remove the entry from the manifest.
    // Returns false if the entry was not found or the manifest write failed.
    bool remove(const char* id);

    // Update metadata fields in both the manifest entry and the data file.
    // Only commits manifest changes after the data file write succeeds (rollback on failure).
    bool patch_meta(const char* id, const JsonObject& fields);

    // Convenience overload for web handlers that have a raw JSON string.
    bool patch_meta(const char* id, const String& json_patch);

    // Return the manifest JSON as a string (fast — serves from RAM cache).
    // Triggers a rebuild if the manifest has not yet been loaded.
    String list();

    // Return the number of entries in the manifest.
    int count();

    // Return the LittleFS filesystem path for a data file given its id.
    // Useful for AsyncFileResponse streaming in web handlers.
    String data_path(const char* id) const;

private:
    void _ensure_loaded();
    void _rebuild_manifest();
    bool _write_manifest();
    bool _atomic_write(const char* path, const String& content);
    bool _atomic_write_from_doc(const char* path, JsonDocument& doc);
    void _sort_entries();
    void _data_path(const char* id, char* out, size_t out_len) const;
    void _manifest_path(char* out, size_t out_len) const;

    const char*        _base_path;
    const char* const* _index_fields;
    size_t             _num_index_fields;
    SemaphoreHandle_t  _mutex;

    // In-memory state — protected by _mutex.
    // Stored as the concrete PSRAM-backed type so that delete is well-defined
    // (JsonDocument has no virtual destructor). Sized from manifest file on load,
    // or MANIFEST_REBUILD_CAPACITY on rebuild.
    BasicJsonDocument<PsramJsonAllocator>* _manifest_doc;
    String        _manifest_cache;
    bool          _loaded;
};
