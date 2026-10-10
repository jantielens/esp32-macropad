#include "font_store.h"
#include <string.h>
#include <new>
#include <memory>
#include <limits.h>

bool font_store_validate_name(const char* name) {
    if (!name || !*name || strlen(name) >= FONT_NAME_MAX_LEN) return false;
    for (const char* character = name; *character; ++character) {
        if (!((*character >= 'a' && *character <= 'z') ||
              (*character >= '0' && *character <= '9') ||
              *character == '_' || *character == '-')) return false;
    }
    return true;
}

static uint16_t font_read16(const uint8_t* data) {
    return FontTtfReader::read16(data);
}

static uint32_t font_read32(const uint8_t* data) {
    return FontTtfReader::read32(data);
}

class FontGlyphValidator {
public:
    FontGlyphValidator(const FontTtfReader& reader, const FontTtfTable& loca,
                       const FontTtfTable& glyf, uint16_t count, bool long_offsets)
        : reader(reader), loca(loca), glyf(glyf), count(count), long_offsets(long_offsets),
          states(new (std::nothrow) uint8_t[count]()), depths(new (std::nothrow) uint8_t[count]()),
          vertices(new (std::nothrow) uint32_t[count]()) {}

    bool validate() {
        if (!states || !depths || !vertices) return false;
        for (uint32_t index = 0; index < count; ++index) {
            if (!glyph(static_cast<uint16_t>(index), 0)) return false;
        }
        return true;
    }
private:
    bool location(uint32_t index, size_t& offset) const {
        uint8_t bytes[4];
        const size_t width = long_offsets ? 4 : 2;
        if (!reader.read(loca.offset + index * width, bytes, width)) return false;
        offset = long_offsets ? font_read32(bytes) : static_cast<size_t>(font_read16(bytes)) * 2;
        return offset <= glyf.length;
    }

    bool glyph(uint16_t index, unsigned nesting) {
        if (index >= count || nesting >= 32 || states[index] == 1) return false;
        if (states[index] == 2) return nesting + depths[index] <= 32;
        states[index] = 1;
        size_t start, end;
        if (!location(index, start) || !location(static_cast<uint32_t>(index) + 1, end) || start > end) return false;
        if (start == end) {
            states[index] = 2;
            depths[index] = 1;
            return true;
        }
        const size_t length = end - start;
        size_t cursor = 0;
        const auto take = [&](uint8_t* bytes, size_t size) -> bool {
            if (cursor > length || size > length - cursor || !reader.read(glyf.offset + start + cursor, bytes, size)) return false;
            cursor += size;
            return true;
        };
        const auto skip = [&](size_t size) -> bool {
            if (cursor > length || size > length - cursor) return false;
            cursor += size;
            return true;
        };
        uint8_t header[10];
        if (!take(header, sizeof(header))) return false;
        const int contours = static_cast<int16_t>(font_read16(header));
        if (static_cast<int16_t>(font_read16(header + 2)) > static_cast<int16_t>(font_read16(header + 6)) ||
            static_cast<int16_t>(font_read16(header + 4)) > static_cast<int16_t>(font_read16(header + 8))) return false;
        uint32_t vertex_count = 0;
        unsigned depth = 1;
        if (contours > 0) {
            uint32_t points = 0;
            uint8_t bytes[2];
            for (int contour = 0; contour < contours; ++contour) {
                if (!take(bytes, 2)) return false;
                const uint32_t next = static_cast<uint32_t>(font_read16(bytes)) + 1;
                if (next <= points) return false;
                points = next;
            }
            if (!take(bytes, 2) || !skip(font_read16(bytes))) return false;
            size_t coordinates = 0;
            for (uint32_t point = 0; point < points;) {
                uint8_t flag;
                if (!take(&flag, 1)) return false;
                uint32_t repeat = 1;
                if (flag & 8) {
                    uint8_t repetitions;
                    if (!take(&repetitions, 1)) return false;
                    repeat += repetitions;
                }
                if (repeat > points - point) return false;
                coordinates += repeat * ((flag & 2 ? 1 : flag & 16 ? 0 : 2) +
                                         (flag & 4 ? 1 : flag & 32 ? 0 : 2));
                point += repeat;
            }
            if (!skip(coordinates)) return false;
            vertex_count = points + static_cast<uint32_t>(contours) * 2;
        } else if (contours == -1) {
            uint16_t flags = 0;
            bool instructions = false;
            do {
                uint8_t component[4];
                if (!take(component, sizeof(component))) return false;
                flags = font_read16(component);
                const uint16_t child = font_read16(component + 2);
                if (!(flags & 2) || !glyph(child, nesting + 1)) return false;
                depth = depth > static_cast<unsigned>(depths[child]) + 1 ? depth : depths[child] + 1;
                if (nesting + depth > 32) return false;
                if (vertices[child] > static_cast<uint32_t>(INT_MAX / 16) - vertex_count) return false;
                vertex_count += vertices[child];
                if (!skip(flags & 1 ? 4 : 2)) return false;
                const unsigned transforms = ((flags & 8) != 0) + ((flags & 64) != 0) + ((flags & 128) != 0);
                if (transforms > 1 || !skip(flags & 8 ? 2 : flags & 64 ? 4 : flags & 128 ? 8 : 0)) return false;
                instructions = instructions || (flags & 256);
            } while (flags & 32);
            if (instructions) {
                uint8_t bytes[2];
                if (!take(bytes, 2) || !skip(font_read16(bytes))) return false;
            }
        } else if (contours != 0) return false;
        vertices[index] = vertex_count;
        depths[index] = static_cast<uint8_t>(depth);
        states[index] = 2;
        return true;
    }

    const FontTtfReader& reader;
    FontTtfTable loca;
    FontTtfTable glyf;
    uint16_t count;
    bool long_offsets;
    std::unique_ptr<uint8_t[]> states;
    std::unique_ptr<uint8_t[]> depths;
    std::unique_ptr<uint32_t[]> vertices;
};

static bool font_cmap_validate(const FontTtfReader& reader, const FontTtfTable& table, uint16_t glyphs) {
    uint8_t header[16];
    const auto read = [&](size_t offset, uint8_t* bytes, size_t size) -> bool {
        return offset <= table.length && size <= table.length - offset && reader.read(table.offset + offset, bytes, size);
    };
    if (!read(0, header, 4) || font_read16(header) != 0) return false;
    const uint16_t encodings = font_read16(header + 2);
    if (!encodings || encodings > (table.length - 4) / 8) return false;
    size_t selected = 0;
    for (uint16_t index = 0; index < encodings; ++index) {
        if (!read(4 + static_cast<size_t>(index) * 8, header, 8)) return false;
        const size_t offset = font_read32(header + 4);
        if (offset < 4 + static_cast<size_t>(encodings) * 8 || offset > table.length || table.length - offset < 2) return false;
        const uint16_t platform = font_read16(header);
        const uint16_t encoding = font_read16(header + 2);
        if (platform == 0 || (platform == 3 && (encoding == 1 || encoding == 10))) selected = offset;
    }
    if (!selected || !read(selected, header, 4)) return false;
    const uint16_t format = font_read16(header);
    size_t length = font_read16(header + 2);
    if (format == 12 || format == 13) {
        if (!read(selected, header, 16)) return false;
        length = font_read32(header + 4);
    }
    if (length > table.length - selected) return false;
    const auto subread = [&](size_t offset, uint8_t* bytes, size_t size) -> bool {
        return offset <= length && size <= length - offset && read(selected + offset, bytes, size);
    };
    if (format == 0) {
        if (length != 262) return false;
        for (size_t index = 6; index < length; ++index) {
            if (!subread(index, header, 1) || header[0] >= glyphs) return false;
        }
    } else if (format == 6) {
        if (!subread(0, header, 10)) return false;
        const uint16_t count = font_read16(header + 8);
        if (length < 10 + static_cast<size_t>(count) * 2 ||
            static_cast<uint32_t>(font_read16(header + 6)) + count > 65536) return false;
        for (uint32_t index = 0; index < count; ++index) {
            if (!subread(10 + index * 2, header, 2) || font_read16(header) >= glyphs) return false;
        }
    } else if (format == 4) {
        if (!subread(0, header, 14)) return false;
        const uint16_t segments = font_read16(header + 6) / 2;
        if (!segments || (font_read16(header + 6) & 1) || length < 16 + static_cast<size_t>(segments) * 8) return false;
        unsigned power = 1, selector = 0;
        while (power * 2 <= segments) { power *= 2; ++selector; }
        if (font_read16(header + 8) != power * 2 || font_read16(header + 10) != selector ||
            font_read16(header + 12) != segments * 2 - power * 2) return false;
        uint32_t previous = 0;
        for (uint32_t segment = 0; segment < segments; ++segment) {
            uint8_t bytes[2];
            if (!subread(14 + segment * 2, bytes, 2)) return false;
            const uint32_t end = font_read16(bytes);
            if (!subread(16 + segments * 2 + segment * 2, bytes, 2)) return false;
            const uint32_t start = font_read16(bytes);
            if (start > end || (segment && start <= previous)) return false;
            previous = end;
            if (!subread(16 + segments * 4 + segment * 2, bytes, 2)) return false;
            const uint16_t delta = font_read16(bytes);
            const size_t range_position = 16 + segments * 6 + segment * 2;
            if (!subread(range_position, bytes, 2)) return false;
            const uint16_t offset = font_read16(bytes);
            if (offset & 1) return false;
            for (uint32_t code = start; code <= end; ++code) {
                uint16_t glyph;
                if (offset) {
                    if (!subread(range_position + offset + (code - start) * 2, bytes, 2)) return false;
                    glyph = font_read16(bytes);
                } else glyph = static_cast<uint16_t>(code + delta);
                if (glyph >= glyphs) return false;
            }
        }
        if (previous != 65535) return false;
    } else if (format == 12 || format == 13) {
        if (!subread(0, header, 16)) return false;
        const uint32_t groups = font_read32(header + 12);
        if (length < 16 || groups > (length - 16) / 12) return false;
        uint32_t previous = 0;
        for (uint32_t index = 0; index < groups; ++index) {
            if (!subread(16 + static_cast<size_t>(index) * 12, header, 12)) return false;
            const uint32_t start = font_read32(header), end = font_read32(header + 4), glyph = font_read32(header + 8);
            if (start > end || end > 0x10ffff || (index && start <= previous) || glyph >= glyphs ||
                (format == 12 && end - start >= static_cast<uint32_t>(glyphs) - glyph)) return false;
            previous = end;
        }
    } else return false;
    return true;
}

bool font_ttf_validate(size_t length, FontReadAt read, void* context) {
    const FontTtfReader reader(length, read, context);
    uint8_t header[54];
    if (!read || length < 12 || !read(context, 0, header, 12) ||
        font_read32(header) != 0x00010000) return false;
    const uint16_t table_count = font_read16(header + 4);
    if (!table_count || table_count > (length - 12) / 16) return false;
    struct Table { const char* tag; size_t minimum; size_t offset; size_t length; };
    Table required[] = {
        {"head", 54, 0, 0}, {"hhea", 36, 0, 0}, {"maxp", 32, 0, 0},
        {"hmtx", 4, 0, 0}, {"cmap", 12, 0, 0}, {"loca", 4, 0, 0}, {"glyf", 10, 0, 0},
    };
    for (uint16_t index = 0; index < table_count; ++index) {
        uint8_t entry[4];
        FontTtfTable parsed;
        if (!reader.table_entry(index, entry, parsed)) return false;
        const size_t offset = parsed.offset;
        const size_t size = parsed.length;
        if (offset < 12 + static_cast<size_t>(table_count) * 16 ||
            offset > INT_MAX || size > static_cast<size_t>(INT_MAX) - offset) return false;
        if (memcmp(entry, "fvar", 4) == 0 || memcmp(entry, "CFF ", 4) == 0 ||
            memcmp(entry, "CFF2", 4) == 0) return false;
        for (Table& table : required) {
            if (memcmp(entry, table.tag, 4) != 0) continue;
            if (table.length || size < table.minimum) return false;
            table.offset = offset;
            table.length = size;
        }
    }
    for (const Table& table : required) if (!table.length) return false;
    if (!read(context, required[0].offset, header, 54) ||
        font_read32(header + 12) != 0x5f0f3cf5 || !font_read16(header + 18)) return false;
    const uint16_t loca_format = font_read16(header + 50);
    if (loca_format > 1) return false;
    if (!read(context, required[2].offset, header, 6) || font_read32(header) != 0x00010000) return false;
    const uint16_t glyphs = font_read16(header + 4);
    if (!glyphs || required[5].length < (static_cast<size_t>(glyphs) + 1) * (loca_format ? 4 : 2)) return false;
    if (!read(context, required[1].offset, header, 36)) return false;
    const uint16_t metrics = font_read16(header + 34);
    if (!metrics || metrics > glyphs || required[3].length <
        static_cast<size_t>(metrics) * 4 + static_cast<size_t>(glyphs - metrics) * 2) return false;
    FontTtfTable loca, glyf, cmap;
    if (!reader.table("loca", loca) || !reader.table("glyf", glyf) || !reader.table("cmap", cmap)) return false;
    FontGlyphValidator validator(reader, loca, glyf, glyphs, loca_format != 0);
    return font_cmap_validate(reader, cmap, glyphs) && validator.validate();
}

#if HAS_TTF_FONTS
#include "log_manager.h"
#include "ota_activity.h"
#include <Arduino.h>
#include <atomic>
#include <mutex>
#include <esp_heap_caps.h>

static std::mutex font_store_mutex;
static std::atomic<uint32_t> font_generation{0};
static FontUpload* active_upload = nullptr;
static const char* font_temp_path = "/fonts/.upload.tmp";

struct FontLoadOwner {
    const char* name;
    FontLoadOwner* next = nullptr;
    bool registered = false;
    explicit FontLoadOwner(const char* name) : name(name) {}
    ~FontLoadOwner();
};
static FontLoadOwner* active_loads = nullptr;

FontLoadOwner::~FontLoadOwner() {
    if (!registered) return;
    std::lock_guard<std::mutex> lock(font_store_mutex);
    FontLoadOwner** owner = &active_loads;
    while (*owner && *owner != this) owner = &(*owner)->next;
    if (*owner) *owner = next;
}

bool font_store_available() {
    return heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0;
}

uint32_t font_store_generation() { return font_generation.load(); }

static bool font_store_mount() {
    const bool ready = storage_mount() && (Storage.exists("/fonts") || Storage.mkdir("/fonts"));
    if (!ready) LOGW("Fonts", "Font storage mount or directory creation failed");
    return ready;
}

static void font_store_path(const char* name, char* path, size_t size) {
    snprintf(path, size, "/fonts/%s.ttf", name);
}

static bool font_file_read(void* context, size_t offset, uint8_t* data, size_t length) {
    File* file = static_cast<File*>(context);
    return !ota_activity_is_active() && offset <= UINT32_MAX && file->seek(static_cast<uint32_t>(offset)) &&
           file->read(data, length) == length;
}

static void font_abort_locked(FontUpload& upload) {
    if (active_upload != &upload) return;
    LOGD("Fonts", "Upload '%s' aborted after %zu bytes", upload.name, upload.received);
    upload.file.close();
    Storage.remove(font_temp_path);
    active_upload = nullptr;
    storage_publish_usage();
}

FontStoreResult font_store_begin(FontUpload& upload, const char* name) {
    std::lock_guard<std::mutex> lock(font_store_mutex);
    if (!font_store_validate_name(name)) return FontStoreResult::Invalid;
    if (!font_store_available()) return FontStoreResult::Unavailable;
    if (active_upload) return FontStoreResult::Busy;
    if (!font_store_mount()) return FontStoreResult::IoError;
    char path[48];
    font_store_path(name, path, sizeof(path));
    if (Storage.exists(path)) return FontStoreResult::Duplicate;
    if (Storage.exists(font_temp_path) && !Storage.remove(font_temp_path)) return FontStoreResult::IoError;
    upload.file = Storage.open(font_temp_path, "w");
    if (!upload.file) return FontStoreResult::IoError;
    strcpy(upload.name, name);
    upload.received = 0;
    active_upload = &upload;
    LOGD("Fonts", "Upload '%s' started", upload.name);
    return FontStoreResult::Ok;
}

FontStoreResult font_store_write(FontUpload& upload, const uint8_t* data, size_t length) {
    std::lock_guard<std::mutex> lock(font_store_mutex);
    if (active_upload != &upload) return FontStoreResult::Busy;
    if (ota_activity_is_active() || length > SIZE_MAX - upload.received ||
        upload.file.write(data, length) != length) {
        LOGW("Fonts", "Upload '%s' interrupted or short write after %zu bytes", upload.name, upload.received);
        font_abort_locked(upload);
        return FontStoreResult::IoError;
    }
    upload.received += length;
    return FontStoreResult::Ok;
}

FontStoreResult font_store_finish(FontUpload& upload, size_t expected) {
    std::lock_guard<std::mutex> lock(font_store_mutex);
    if (active_upload != &upload) return FontStoreResult::Busy;
    upload.file.flush();
    upload.file.close();
    File file = Storage.open(font_temp_path, "r");
    FontStoreResult result = FontStoreResult::Ok;
    if (!file || file.size() != expected || upload.received != expected) result = FontStoreResult::IoError;
    else if (!font_ttf_validate(file.size(), font_file_read, &file)) result = FontStoreResult::Invalid;
    file.close();
    char path[48];
    font_store_path(upload.name, path, sizeof(path));
    if (result == FontStoreResult::Ok && Storage.exists(path)) result = FontStoreResult::Duplicate;
    if (result == FontStoreResult::Ok && !Storage.rename(font_temp_path, path)) result = FontStoreResult::IoError;
    if (result != FontStoreResult::Ok) Storage.remove(font_temp_path);
    else font_generation.fetch_add(1);
    if (result == FontStoreResult::Ok) {
        LOGI("Fonts", "Uploaded '%s': %zu bytes, generation=%lu", upload.name, expected, static_cast<unsigned long>(font_generation.load()));
    } else {
        LOGW("Fonts", "Upload '%s' failed: %s (received=%zu expected=%zu)", upload.name,
             result == FontStoreResult::Invalid ? "invalid TrueType" :
             result == FontStoreResult::Duplicate ? "duplicate ID" : "incomplete upload or storage error",
             upload.received, expected);
    }
    active_upload = nullptr;
    storage_publish_usage();
    return result;
}

void font_store_abort(FontUpload& upload) {
    std::lock_guard<std::mutex> lock(font_store_mutex);
    font_abort_locked(upload);
}

FontStoreResult font_store_delete(const char* name) {
    std::lock_guard<std::mutex> lock(font_store_mutex);
    if (!font_store_validate_name(name)) return FontStoreResult::Invalid;
    if (!font_store_available()) return FontStoreResult::Unavailable;
    for (FontLoadOwner* owner = active_loads; owner; owner = owner->next) {
        if (strcmp(owner->name, name) == 0) return FontStoreResult::Busy;
    }
    if (!font_store_mount()) return FontStoreResult::IoError;
    char path[48];
    font_store_path(name, path, sizeof(path));
    if (!Storage.exists(path)) return FontStoreResult::NotFound;
    if (!Storage.remove(path)) return FontStoreResult::IoError;
    font_generation.fetch_add(1);
    LOGI("Fonts", "Deleted '%s', generation=%lu", name, static_cast<unsigned long>(font_generation.load()));
    storage_publish_usage();
    return FontStoreResult::Ok;
}

FontStoreResult font_store_list(bool (*visitor)(const char*, size_t, void*), void* context) {
    std::lock_guard<std::mutex> lock(font_store_mutex);
    if (!font_store_available()) return FontStoreResult::Unavailable;
    if (!font_store_mount()) return FontStoreResult::IoError;
    File directory = Storage.open("/fonts");
    if (!directory || !directory.isDirectory()) return FontStoreResult::IoError;
    for (File file = directory.openNextFile(); file; file = directory.openNextFile()) {
        const char* name = strrchr(file.name(), '/');
        name = name ? name + 1 : file.name();
        const size_t length = strlen(name);
        if (file.isDirectory() || length <= 4 || length - 4 >= FONT_NAME_MAX_LEN ||
            strcmp(name + length - 4, ".ttf") != 0) continue;
        char id[FONT_NAME_MAX_LEN];
        memcpy(id, name, length - 4);
        id[length - 4] = '\0';
        if (font_store_validate_name(id) && !visitor(id, file.size(), context)) return FontStoreResult::IoError;
    }
    return FontStoreResult::Ok;
}

uint8_t* font_store_load(const char* name, size_t& length) {
    length = 0;
    if (!font_store_validate_name(name) || !font_store_available() ||
        ota_activity_is_active()) return nullptr;
    const unsigned long started = millis();
#if USE_SD_STORAGE
    LOGD("Fonts", "Load '%s': begin (SD)", name);
#else
    LOGD("Fonts", "Load '%s': begin (LittleFS)", name);
#endif
    FontLoadOwner owner(name);
    File file;
    size_t size;
    {
        std::lock_guard<std::mutex> lock(font_store_mutex);
        if (!font_store_mount()) return nullptr;
        char path[48];
        font_store_path(name, path, sizeof(path));
        file = Storage.open(path, "r");
        if (!file) {
            LOGD("Fonts", "Load '%s': file missing or unreadable", name);
            return nullptr;
        }
        size = file.size();
        owner.next = active_loads;
        active_loads = &owner;
        owner.registered = true;
    }
    uint8_t* data = static_cast<uint8_t*>(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!data) {
        LOGW("Fonts", "Load '%s': PSRAM allocation failed for %zu bytes (free=%zu largest=%zu)", name, size,
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
             heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        return nullptr;
    }
    if (!file.seek(0)) {
        LOGW("Fonts", "Load '%s': rewind failed", name);
        heap_caps_free(data);
        return nullptr;
    }
    for (size_t offset = 0; offset < size;) {
        const size_t chunk = size - offset > 4096 ? 4096 : size - offset;
        if (ota_activity_is_active() || file.read(data + offset, chunk) != chunk) {
            LOGW("Fonts", "Load '%s': interrupted or short read at %zu/%zu bytes", name, offset, size);
            heap_caps_free(data);
            return nullptr;
        }
        offset += chunk;
    }
    if (!font_ttf_validate(size, [](void* context, size_t offset, uint8_t* target, size_t count) {
        if (ota_activity_is_active()) return false;
        memcpy(target, static_cast<const uint8_t*>(context) + offset, count);
        return true;
    }, data)) {
        LOGW("Fonts", "Load '%s': TrueType validation failed or interrupted", name);
        heap_caps_free(data);
        return nullptr;
    }
    file.close();
    length = size;
    LOGD("Fonts", "Load '%s': %zu bytes in %lums", name, size, millis() - started);
    return data;
}
#endif