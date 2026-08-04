# FsIndexedStore — Manifest-Indexed JSON Collection

`FsIndexedStore` is a reusable infrastructure module for storing and retrieving collections of JSON documents on the active persistent storage backend. It maintains a `_index.json` manifest so that list operations read a single file instead of iterating every document.

## Problem it Solves

Without a manifest, listing a collection requires opening, parsing, and extracting metadata from every file on disk. With 20+ files this takes seconds. `FsIndexedStore` reduces a `GET /api/.../list` response to a single file read regardless of collection size.

## Directory Layout

```
/storage/sessions/
├── _index.json              ← manifest (fast list)
├── sess_1714900000.json     ← full document
├── sess_1714900120.json
└── ...
```

## Manifest Format

```json
{
  "entries": [
    {
      "id": "sess_1714900120",
      "created_at": 1714900120,
      "duration_ms": 45200,
      "result": "pass"
    },
    {
      "id": "sess_1714900000",
      "created_at": 1714900000,
      "duration_ms": 38100,
      "result": "fail"
    }
  ]
}
```

Entries are sorted by `created_at` descending (most recent first). Additional metadata fields beyond `id` and `created_at` are feature-defined.

## API

### Constructor

```cpp
FsIndexedStore(const char* base_path)
```

`base_path` is the storage directory (no trailing slash), e.g. `"/storage/sessions"`.

### `begin()`

```cpp
bool begin();
```

Call once after `storage_mount()`. Creates the base directory if missing. Manifest loading is deferred to the first `list()` or `get()` call.

### `add(id, json_content, index_meta)`

```cpp
bool add(const char* id, const String& json_content, const JsonObject& index_meta);
```

Writes `json_content` to `<base_path>/<id>.json` (atomic via `.tmp` rename) and appends an entry to the manifest.

`index_meta` must be a `JsonObject` with at minimum `created_at` (unix timestamp as `uint32`). Additional fields appear in `list()` responses without requiring clients to fetch individual documents.

**IDs are caller-managed.** Recommend timestamp-based strings like `sess_1714900000` to guarantee uniqueness and natural sort order.

### `get(id)`

```cpp
String get(const char* id);
```

Returns the full JSON content of the document. Returns an empty string if the ID is not found in the manifest or the file is missing.

### `remove(id)`

```cpp
bool remove(const char* id);
```

Removes the entry from the manifest and deletes the data file. Returns `true`. Callers can treat a `false` return as "not found" — the manifest is updated regardless.

### `patch_meta(id, fields)` / `patch_meta(id, json_patch)`

```cpp
bool patch_meta(const char* id, const JsonObject& fields);
bool patch_meta(const char* id, const String& json_patch);  // web handler convenience
```

Merges `fields` into both the manifest entry and the data file. Use this to update user-editable fields like notes, tags, or camera name without re-writing the entire document.

The data file is read fully into RAM, patched, and written back atomically. For documents with large nested arrays (e.g. waveform data), this involves a heap allocation proportional to the file size. Keep individual documents under a few hundred KB.

### `list()`

```cpp
String list();
```

Returns the manifest JSON as a string. Served from the in-RAM cache — no disk access after the first call.

### `count()`

```cpp
int count();
```

Returns the number of entries in the manifest.

## REST Helper

`fs_indexed_store_register_routes` registers standard CRUD endpoints on an `AsyncWebServer`.

```cpp
#include "web_portal_fs_store.h"

void register_shutter_session_routes(AsyncWebServer& server) {
    fs_indexed_store_register_routes(server, s_sessions, "/api/shutter/sessions");
    // Add feature-specific endpoints alongside:
    server.on("/api/shutter/sessions", HTTP_POST,
              handle_post_session_request, nullptr, handle_post_session_body);
}
```

### Registered Endpoints

| Method | Path | Description |
|--------|------|-------------|
| `GET` | `{base_url}` | Returns manifest JSON |
| `GET` | `{base_url}/{id}` | Streams full document from flash (`AsyncFileResponse`) |
| `DELETE` | `{base_url}/{id}` | Deletes document + updates manifest |
| `PATCH` | `{base_url}/{id}` | Patches metadata fields in manifest + data file |

`POST` is intentionally excluded. Document creation is feature-specific (firmware writes, not the portal).

### Response Codes

| Code | Meaning |
|------|---------|
| 200 | Success |
| 400 | Missing or invalid parameter |
| 404 | Document not found |
| 413 | Request body too large |
| 500 | Internal error (manifest read failure) |

## Self-Healing Manifest

If `_index.json` is missing or corrupt (e.g. after a power loss mid-write), the manifest is rebuilt on the next `list()` or `get()` call. Rebuild iterates all `.json` files in the directory, parses each, and extracts metadata. A `LOGW` warning is emitted so the event is visible in diagnostics.

Two-step atomicity: each file write is atomic (`.tmp` + rename), but the sequence of writing the data file then updating the manifest is not a transaction. A power loss between the two leaves an orphaned data file. Rebuild recovers this state automatically.

## Thread Safety

All public methods are protected by a FreeRTOS mutex created with `xSemaphoreCreateMutex()` (supports priority inheritance). The store may be accessed concurrently from web server handlers and application tasks.

## Memory Notes

- The manifest document is allocated with `DynamicJsonDocument(8192)` — sufficient for ~40 entries at ~200 bytes each.
- `patch_meta()` allocates up to 16 KB on the heap for the data file parse. For larger documents, ensure PSRAM is available.
- `list()` and `count()` serve from the cached manifest string — no additional allocations.

## Integration Example

```cpp
// shutter_session.cpp
#include "fs_indexed_store.h"

static FsIndexedStore s_sessions("/storage/sessions");

void shutter_session_init() {
    s_sessions.begin();
}

void shutter_session_save(uint32_t ts, const String& json, uint32_t duration_ms) {
    char id[32];
    snprintf(id, sizeof(id), "sess_%lu", (unsigned long)ts);

    DynamicJsonDocument meta(256);
    meta["created_at"]   = ts;
    meta["duration_ms"]  = duration_ms;

    s_sessions.add(id, json, meta.as<JsonObject>());
}
```

```cpp
// web_portal_shutter.cpp
#include "web_portal_fs_store.h"
extern FsIndexedStore s_sessions;  // or expose via accessor

void web_portal_shutter_register_routes(AsyncWebServer& server) {
    fs_indexed_store_register_routes(server, s_sessions, "/api/shutter/sessions");
}
```
