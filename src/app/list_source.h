#pragma once

#include "board_config.h"

#if HAS_DISPLAY && HAS_PSRAM

#include <ArduinoJson.h>
#include <atomic>
#include <stddef.h>
#include <stdint.h>

#include "bounded_psram_json_allocator.h"

constexpr size_t LIST_SOURCE_MAX_ITEMS = 20;
constexpr size_t LIST_SOURCE_MAX_SOURCES = 8;
constexpr size_t LIST_SOURCE_MAX_FIELDS = 8;
constexpr size_t LIST_SOURCE_MAX_ID_BYTES = 63;
constexpr size_t LIST_SOURCE_MAX_FIELD_BYTES = 32;
constexpr size_t LIST_SOURCE_MAX_TEXT_BYTES = 127;
constexpr size_t LIST_SOURCE_DOCUMENT_BYTES = 32 * 1024;

enum class ListSourceFieldType : uint8_t { String, Number, Boolean, Scalar };

struct ListSourceField {
    const char* name;
    const char* description;
    ListSourceFieldType type;
    bool optional;
};

class ListSourceSnapshot;
enum class ListSourceLoadStep : uint8_t { Pending, Deferred, Complete, Failed };

// Descriptors and their field arrays must remain valid for the program lifetime.
struct ListSourceDescriptor {
    constexpr ListSourceDescriptor(const char* token, const char* title,
                                   const ListSourceField* fields, uint8_t field_count,
                                   uint32_t (*revision)() = nullptr,
                                   ListSourceLoadStep (*step)(void**, ListSourceSnapshot*, const char**) = nullptr,
                                   void (*finish)(void*) = nullptr)
        : token(token), title(title), fields(fields), field_count(field_count),
          revision(revision), step(step), finish(finish) {}
    const char* token;
    const char* title;
    const ListSourceField* fields;
    uint8_t field_count;
    // Revision is a cheap thread-safe read. Each step runs on the main loop;
    // failures must return a static error string, and finish accepts null.
    uint32_t (*revision)();
    ListSourceLoadStep (*step)(void** job, ListSourceSnapshot* snapshot, const char** error);
    void (*finish)(void* job);
};

bool list_source_register(const ListSourceDescriptor* source);
const ListSourceDescriptor* list_source_find(const char* token);
const ListSourceField* list_source_field(const ListSourceDescriptor* source, const char* name);
uint8_t list_source_count();
const ListSourceDescriptor* list_source_at(uint8_t index);
void list_source_emit_metadata(JsonArray output);
// Validate the full UTF-8 value and return a prefix length that fits max_bytes.
bool list_source_preview_length(const char* text, size_t length, size_t max_bytes,
                                size_t* preview_length);

// A snapshot is built by one source job, then sealed before sharing.
// Callers must hold a reference while using a returned JsonArrayConst.
class ListSourceSnapshot {
public:
    static ListSourceSnapshot* create();
    void retain();
    void release();

    // Returns a static error message on failure. A failed build cannot be sealed.
    const char* append(JsonObjectConst item);
    const char* seal();
    JsonArrayConst items() const;
    size_t peakBytes() const;
    uint32_t references() const;

    ListSourceSnapshot(const ListSourceSnapshot&) = delete;
    ListSourceSnapshot& operator=(const ListSourceSnapshot&) = delete;

private:
    ListSourceSnapshot();
    ~ListSourceSnapshot() = default;

    std::atomic<uint32_t> references_;
    BoundedPsramJsonAllocator allocator_;
    JsonDocument document_;
    const char* error_;
    bool sealed_;
};

enum class ListSourceState : uint8_t { Loading, Ready, Error };
struct ListSourceHandle;

// Each visible List owns one demand; callers release every successful acquire.
ListSourceHandle* list_source_acquire(const ListSourceDescriptor* source);
void list_source_release(ListSourceHandle* handle);
// A returned snapshot is retained and must be released by the caller.
ListSourceState list_source_read(ListSourceHandle* handle, ListSourceSnapshot** snapshot,
                                 uint32_t* revision, const char** error);
void list_source_loop();

#endif
