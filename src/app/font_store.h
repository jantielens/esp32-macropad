#pragma once

#include "board_config.h"
#include <stddef.h>
#include <stdint.h>
#include <string.h>

constexpr size_t FONT_NAME_MAX_LEN = 32;
bool font_store_validate_name(const char* name);
using FontReadAt = bool (*)(void* context, size_t offset, uint8_t* data, size_t length);
struct FontTtfTable {
    size_t offset = 0;
    size_t length = 0;
};

class FontTtfReader {
public:
    FontTtfReader(size_t length, FontReadAt read, void* context)
        : length(length), callback(read), context(context) {}
    bool read(size_t offset, uint8_t* data, size_t count) const {
        return callback && offset <= length && count <= length - offset && callback(context, offset, data, count);
    }
    static uint16_t read16(const uint8_t* data) {
        return (static_cast<uint16_t>(data[0]) << 8) | data[1];
    }
    static uint32_t read32(const uint8_t* data) {
        return (static_cast<uint32_t>(data[0]) << 24) | (static_cast<uint32_t>(data[1]) << 16) |
               (static_cast<uint32_t>(data[2]) << 8) | data[3];
    }
    bool table_entry(uint16_t index, uint8_t* tag, FontTtfTable& result) const {
        uint8_t entry[16];
        if (!read(12 + static_cast<size_t>(index) * 16, entry, sizeof(entry))) return false;
        memcpy(tag, entry, 4);
        result.offset = read32(entry + 8);
        result.length = read32(entry + 12);
        return result.offset <= length && result.length <= length - result.offset;
    }
    bool table(const char* tag, FontTtfTable& result) const {
        uint8_t header[12];
        if (!read(0, header, sizeof(header)) || read32(header) != 0x00010000) return false;
        const uint16_t count = read16(header + 4);
        if (!count || count > (length - 12) / 16) return false;
        bool found = false;
        for (uint16_t index = 0; index < count; ++index) {
            uint8_t entry_tag[4];
            FontTtfTable entry;
            if (!table_entry(index, entry_tag, entry)) return false;
            if (memcmp(entry_tag, tag, 4) != 0) continue;
            if (found) return false;
            result = entry;
            found = true;
        }
        return found;
    }
private:
    size_t length;
    FontReadAt callback;
    void* context;
};
bool font_ttf_validate(size_t length, FontReadAt read, void* context);

#if HAS_TTF_FONTS
#include "storage.h"

enum class FontStoreResult { Ok, Invalid, NotFound, Duplicate, Busy, Unavailable, IoError };

struct FontUpload {
    File file;
    char name[FONT_NAME_MAX_LEN]{};
    size_t received = 0;
};

bool font_store_available();
uint32_t font_store_generation();
FontStoreResult font_store_begin(FontUpload& upload, const char* name);
FontStoreResult font_store_write(FontUpload& upload, const uint8_t* data, size_t length);
FontStoreResult font_store_finish(FontUpload& upload, size_t expected);
void font_store_abort(FontUpload& upload);
FontStoreResult font_store_delete(const char* name);
FontStoreResult font_store_list(bool (*visitor)(const char*, size_t, void*), void* context);
uint8_t* font_store_load(const char* name, size_t& length);
#endif