#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// FsIndexedStore — manifest-indexed JSON document collection on LittleFS.
//
// Maintains a _index.json manifest alongside data files so that list
// operations read a single file rather than iterating every document.
//
// Thread-safe: all public methods are mutex-protected and may be called from
// the web server task and the application task concurrently.
//
// Usage:
//   static FsIndexedStore store("/storage/sessions");
//
//   // After LittleFS.begin():
//   store.begin();
//
//   // Add a document (firmware writes data; portal never POSTs raw documents):
//   DynamicJsonDocument meta(256);
//   meta["duration_ms"] = 1234;
//   store.add("sess_1714900000", full_json_string, meta.as<JsonObject>());
//
//   // List (fast — returns cached manifest JSON):
//   String manifest = store.list();
//
//   // Get full document (returns empty string on missing ID):
//   String doc = store.get("sess_1714900000");
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
    // base_path: LittleFS directory, e.g. "/storage/sessions" (no trailing slash).
    explicit FsIndexedStore(const char* base_path);
    ~FsIndexedStore() = default;

    // Call once after LittleFS.begin().  Creates the base directory if
    // missing.  Manifest loading is deferred to the first list()/get() call.
    bool begin();

    // Write a new document to disk and append its entry to the manifest.
    // id          — unique document identifier (caller-managed, e.g. timestamp string).
    // json_content — full JSON body to write as the data file.
    // index_meta  — metadata fields to store in the manifest entry
    //               (must contain at least "created_at" as a uint32).
    bool add(const char* id, const String& json_content, const JsonObject& index_meta);

    // Return the full JSON content of a single document.
    // Returns an empty string if the document does not exist.
    String get(const char* id);

    // Delete the data file and remove the entry from the manifest.
    bool remove(const char* id);

    // Update metadata fields in both the manifest entry and the data file.
    // fields — JsonObject with the fields to merge/overwrite.
    bool patch_meta(const char* id, const JsonObject& fields);

    // Convenience overload for web handlers that have a raw JSON string.
    bool patch_meta(const char* id, const String& json_patch);

    // Return the manifest JSON as a string (fast — serves from RAM cache).
    // Triggers a rebuild if the manifest has not yet been loaded.
    String list();

    // Return the number of entries in the manifest without full parsing.
    int count();

private:
    // Ensure the manifest is loaded (or rebuilt) before any operation that
    // reads it.  Must be called under the mutex.
    void _ensure_loaded();

    // Rebuild the manifest by iterating data files on disk.  Adds orphaned
    // files and removes entries referencing missing files.
    void _rebuild_manifest();

    // Serialize the in-memory manifest document to _manifest_cache and flush
    // to disk atomically.
    bool _write_manifest();

    // Write content to path atomically via a .tmp rename.
    bool _atomic_write(const char* path, const String& content);

    // Sort the in-memory manifest entries descending by created_at.
    // Operates by extracting to a std::vector, sorting, and rebuilding the
    // JsonArray (JsonArrayIterator is not a random-access iterator).
    void _sort_entries();

    // Build the full path for a data file given its id.
    void _data_path(const char* id, char* out, size_t out_len) const;

    // Build the full path for the manifest file.
    void _manifest_path(char* out, size_t out_len) const;

    const char* _base_path;
    SemaphoreHandle_t _mutex;

    // In-memory state — protected by _mutex
    DynamicJsonDocument* _manifest_doc; // owning pointer; allocated in _ensure_loaded
    String               _manifest_cache;
    bool                 _loaded;
};
