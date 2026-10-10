#include "font_store.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <cstdlib>
#include <cstdarg>
#include <string>
#include <fstream>
#include <iterator>

static const uint8_t fixture[] = {
#include "fixtures/BebasNeue.inc"
};

#if HAS_TTF_FONTS
FakeFontStorage LittleFS;
FakeFontStorage SD_MMC;
static bool psram_ready = true;
static bool ota_active = false;
static bool allocation_failure = false;
static void (*allocation_hook)() = nullptr;
static std::vector<std::string> log_lines;
void log_noop(const char*, const char* format, ...) {
    char line[288];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    log_lines.emplace_back(line);
}
extern "C" unsigned long millis() { return 0; }
size_t heap_caps_get_free_size(uint32_t) { return 8192; }
size_t heap_caps_get_largest_free_block(uint32_t) { return 4096; }
bool storage_mount() { return Storage.mounted; }
void storage_publish_usage(bool) {}
bool ota_activity_is_active() { return ota_active; }
size_t heap_caps_get_total_size(uint32_t) { return psram_ready ? 8 * 1024 * 1024 : 0; }
void* heap_caps_malloc(size_t size, uint32_t) {
    if (allocation_hook) allocation_hook();
    return allocation_failure ? nullptr : malloc(size);
}
void heap_caps_free(void* pointer) { free(pointer); }
#endif

static bool read_bytes(void* context, size_t offset, uint8_t* data, size_t length) {
    const auto& bytes = *static_cast<std::vector<uint8_t>*>(context);
    if (offset > bytes.size() || length > bytes.size() - offset) return false;
    memcpy(data, bytes.data() + offset, length);
    return true;
}

static void write16(std::vector<uint8_t>& bytes, size_t offset, uint16_t value) {
    bytes[offset] = value >> 8;
    bytes[offset + 1] = value;
}

static void write32(std::vector<uint8_t>& bytes, size_t offset, uint32_t value) {
    write16(bytes, offset, value >> 16);
    write16(bytes, offset + 2, value);
}

static bool malformed_tests() {
    std::vector<uint8_t> bytes(fixture, fixture + sizeof(fixture));
    const FontTtfReader reader(bytes.size(), read_bytes, &bytes);
    FontTtfTable head, loca, glyf, cmap, maxp;
    if (!reader.table("head", head) || !reader.table("loca", loca) || !reader.table("glyf", glyf) ||
        !reader.table("cmap", cmap) || !reader.table("maxp", maxp)) return false;
    const uint16_t glyphs = FontTtfReader::read16(bytes.data() + maxp.offset + 4);
    const bool long_offsets = FontTtfReader::read16(bytes.data() + head.offset + 50) != 0;
    const size_t width = long_offsets ? 4 : 2;
    const auto location = [&](uint32_t index) -> size_t {
        const uint8_t* entry = fixture + loca.offset + index * width;
        return long_offsets ? FontTtfReader::read32(entry) : FontTtfReader::read16(entry) * 2;
    };
    const auto set_location = [&](uint32_t index, size_t offset) {
        if (long_offsets) write32(bytes, loca.offset + index * width, offset);
        else write16(bytes, loca.offset + index * width, offset / 2);
    };
    const auto rejected = [&](const char* name) -> bool {
        if (!font_ttf_validate(bytes.size(), read_bytes, &bytes)) return true;
        std::fprintf(stderr, "Accepted malformed font: %s\n", name);
        return false;
    };
    set_location(glyphs, glyf.length + 100);
    if (!rejected("loca outside glyf")) return false;
    bytes.assign(fixture, fixture + sizeof(fixture));
    set_location(0, glyf.length);
    if (!rejected("descending loca")) return false;
    uint32_t first = glyphs, second = glyphs;
    for (uint32_t index = 0; index < glyphs; ++index) {
        if (location(index + 1) - location(index) < 20) continue;
        if (first == glyphs) first = index;
        else { second = index; break; }
    }
    if (second == glyphs) return false;
    const size_t start = glyf.offset + location(first);
    bytes.assign(fixture, fixture + sizeof(fixture));
    set_location(first + 1, location(first) + 8);
    if (!rejected("truncated glyph header")) return false;
    bytes.assign(fixture, fixture + sizeof(fixture));
    write16(bytes, start, 1);
    write16(bytes, start + 10, 0);
    write16(bytes, start + 12, 0);
    bytes[start + 14] = 8;
    bytes[start + 15] = 255;
    if (!rejected("flag repeat exceeds point count")) return false;
    bytes.assign(fixture, fixture + sizeof(fixture));
    write16(bytes, start, 1);
    write16(bytes, start + 10, 0xffff);
    write16(bytes, start + 12, 0);
    if (!rejected("truncated coordinate stream")) return false;
    bytes.assign(fixture, fixture + sizeof(fixture));
    write16(bytes, start, 1);
    write16(bytes, start + 10, 0);
    write16(bytes, start + 12, 0xffff);
    if (!rejected("truncated instructions")) return false;
    const auto component = [&](uint32_t index, uint16_t child, uint16_t flags = 2) {
        const size_t offset = glyf.offset + location(index);
        write16(bytes, offset, 0xffff);
        write16(bytes, offset + 10, flags);
        write16(bytes, offset + 12, child);
    };
    bytes.assign(fixture, fixture + sizeof(fixture));
    component(first, glyphs);
    if (!rejected("component index out of range")) return false;
    bytes.assign(fixture, fixture + sizeof(fixture));
    component(first, first);
    if (!rejected("composite self cycle")) return false;
    bytes.assign(fixture, fixture + sizeof(fixture));
    component(first, second);
    component(second, first);
    if (!rejected("composite mutual cycle")) return false;
    bytes.assign(fixture, fixture + sizeof(fixture));
    component(first, second, 8 | 64 | 2);
    if (!rejected("conflicting component transforms")) return false;
    bytes.assign(fixture, fixture + sizeof(fixture));
    component(first, second, 0);
    if (!rejected("unsupported point-attached component")) return false;
    std::vector<uint16_t> chain;
    for (uint32_t index = 0; index < glyphs && chain.size() < 34; ++index) {
        if (location(index + 1) - location(index) >= 20) chain.push_back(index);
    }
    if (chain.size() < 34) return false;
    bytes.assign(fixture, fixture + sizeof(fixture));
    for (size_t index = 0; index < 33; ++index) component(chain[index], chain[index + 1]);
    if (!rejected("excessive composite nesting")) return false;
    size_t selected = 0;
    const uint16_t encodings = FontTtfReader::read16(fixture + cmap.offset + 2);
    for (uint16_t index = 0; index < encodings; ++index) {
        const uint8_t* entry = fixture + cmap.offset + 4 + index * 8;
        const uint16_t platform = FontTtfReader::read16(entry), encoding = FontTtfReader::read16(entry + 2);
        if (platform == 0 || (platform == 3 && (encoding == 1 || encoding == 10)))
            selected = cmap.offset + FontTtfReader::read32(entry + 4);
    }
    if (!selected || FontTtfReader::read16(fixture + selected) != 4) return false;
    bytes.assign(fixture, fixture + sizeof(fixture));
    write16(bytes, selected, 2);
    if (!rejected("unsupported cmap format")) return false;
    bytes.assign(fixture, fixture + sizeof(fixture));
    write16(bytes, selected + 10, 0xffff);
    if (!rejected("invalid cmap search metadata")) return false;
    bytes.assign(fixture, fixture + sizeof(fixture));
    const uint16_t segments = FontTtfReader::read16(fixture + selected + 6) / 2;
    write16(bytes, selected + 16 + segments * 6, 0xfffe);
    if (!rejected("cmap glyph array outside subtable")) return false;
    bytes.assign(fixture, fixture + sizeof(fixture));
    write16(bytes, selected + 16 + segments * 4, glyphs);
    write16(bytes, selected + 16 + segments * 6, 0);
    if (!rejected("cmap glyph outside maxp")) return false;
    return true;
}

int main(int argument_count, char** arguments) {
    if (argument_count > 1) {
        std::ifstream input(arguments[1], std::ios::binary);
        std::vector<uint8_t> external{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
        if (external.empty() || !font_ttf_validate(external.size(), read_bytes, &external)) return 1;
    }
    if (!malformed_tests()) return 1;
    std::vector<uint8_t> bytes(fixture, fixture + sizeof(fixture));
    if (!font_ttf_validate(bytes.size(), read_bytes, &bytes)) return 1;
    if (!font_store_validate_name("my-font_1") || font_store_validate_name("../font") ||
        font_store_validate_name("Font") || font_store_validate_name("font.ttf") ||
        font_store_validate_name("") || font_store_validate_name("abcdefghijklmnopqrstuvwxyz123456")) return 1;
    if (font_ttf_validate(11, read_bytes, &bytes) ||
        font_ttf_validate(bytes.size() - 100, read_bytes, &bytes)) return 1;
    for (const char* tag : {"OTTO", "ttcf", "wOFF", "wOF2"}) {
        memcpy(bytes.data(), tag, 4);
        if (font_ttf_validate(bytes.size(), read_bytes, &bytes)) return 1;
    }
    bytes.assign(fixture, fixture + sizeof(fixture));
    memcpy(bytes.data() + 12, "fvar", 4);
    if (font_ttf_validate(bytes.size(), read_bytes, &bytes)) return 1;
    bytes.assign(fixture, fixture + sizeof(fixture));
    memset(bytes.data() + 20, 0xff, 8);
    if (font_ttf_validate(bytes.size(), read_bytes, &bytes)) return 1;
#if HAS_TTF_FONTS
    FontUpload first;
    FontUpload second;
    if (font_store_begin(first, "test-font") != FontStoreResult::Ok) return 1;
    if (font_store_begin(second, "second") != FontStoreResult::Busy) return 1;
    font_store_abort(second);
    const size_t upload_log_count = log_lines.size();
    if (font_store_write(first, fixture, 16) != FontStoreResult::Ok ||
        font_store_write(first, fixture + 16, sizeof(fixture) - 16) != FontStoreResult::Ok ||
        log_lines.size() != upload_log_count) return 1;
    if (Storage.exists("/fonts/test-font.ttf")) return 1;
    if (font_store_finish(first, sizeof(fixture)) != FontStoreResult::Ok || font_store_generation() != 1) return 1;
    if (log_lines.back().find("Uploaded 'test-font': 61400 bytes, generation=1") == std::string::npos) return 1;
    if (font_store_begin(second, "test-font") != FontStoreResult::Duplicate) return 1;
    size_t length = 0;
    uint8_t* data = font_store_load("test-font", length);
    if (!data || length != sizeof(fixture) || memcmp(data, fixture, length) != 0) return 1;
    heap_caps_free(data);
    bool concurrent_ok = false;
    allocation_hook = []() {
        allocation_hook = nullptr;
        if (font_store_delete("test-font") != FontStoreResult::Busy) std::abort();
        if (font_store_list([](const char*, size_t, void*) { return true; }, nullptr) != FontStoreResult::Ok) std::abort();
        FontUpload upload;
        if (font_store_begin(upload, "unrelated") != FontStoreResult::Ok ||
            font_store_write(upload, fixture, sizeof(fixture)) != FontStoreResult::Ok ||
            font_store_finish(upload, sizeof(fixture)) != FontStoreResult::Ok ||
            font_store_delete("unrelated") != FontStoreResult::Ok) std::abort();
    };
    data = font_store_load("test-font", length);
    concurrent_ok = data && !allocation_hook;
    heap_caps_free(data);
    if (!concurrent_ok) return 1;
    Storage.read_hook = []() {
        Storage.read_hook = nullptr;
        if (font_store_delete("test-font") != FontStoreResult::Busy ||
            font_store_list([](const char*, size_t, void*) { return true; }, nullptr) != FontStoreResult::Ok) std::abort();
    };
    data = font_store_load("test-font", length);
    if (!data || Storage.read_hook) return 1;
    heap_caps_free(data);
    allocation_failure = true;
    length = 123;
    if (font_store_load("test-font", length) || length) return 1;
    allocation_failure = false;
    Storage.short_read = true;
    if (font_store_load("test-font", length) || length) return 1;
    Storage.short_read = false;
    allocation_hook = []() { allocation_hook = nullptr; ota_active = true; };
    if (font_store_load("test-font", length) || length) return 1;
    ota_active = false;
    unsigned count = 0;
    if (font_store_list([](const char*, size_t size, void* context) {
        if (size != sizeof(fixture)) return false;
        ++*static_cast<unsigned*>(context); return true;
    }, &count) != FontStoreResult::Ok || count != 1) return 1;
    Storage.short_write = true;
    if (font_store_begin(second, "short") != FontStoreResult::Ok ||
        font_store_write(second, fixture, sizeof(fixture)) != FontStoreResult::IoError ||
        Storage.exists("/fonts/.upload.tmp")) return 1;
    Storage.short_write = false;
    if (font_store_begin(second, "partial") != FontStoreResult::Ok) return 1;
    font_store_abort(second);
    if (Storage.exists("/fonts/.upload.tmp")) return 1;
    if (font_store_begin(second, "invalid") != FontStoreResult::Ok ||
        font_store_write(second, bytes.data(), bytes.size()) != FontStoreResult::Ok ||
        font_store_finish(second, bytes.size()) != FontStoreResult::Invalid ||
        Storage.exists("/fonts/invalid.ttf") || Storage.exists("/fonts/.upload.tmp")) return 1;
    if (font_store_delete("test-font") != FontStoreResult::Ok ||
        font_store_delete("test-font") != FontStoreResult::NotFound || font_store_generation() != 4) return 1;
    if (log_lines.back().find("Deleted 'test-font', generation=4") == std::string::npos) return 1;
    Storage.mounted = false;
    if (font_store_delete("absent") != FontStoreResult::IoError) return 1;
    Storage.mounted = true;
    psram_ready = false;
    if (font_store_begin(second, "unavailable") != FontStoreResult::Unavailable) return 1;
#endif
    std::puts("Font names and static TrueType bounds validated");
    return 0;
}