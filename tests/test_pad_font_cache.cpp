#include "pad_layout.h"
#include "native_extension_text.h"
#include "font_store.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdarg>
#include <string>
#include <vector>
#include <unordered_set>
#include <fstream>
#include <iterator>
#if HAS_FREETYPE_FONTS
#include "freetype_mono_font.h"
#endif

static const uint8_t fixture[] = {
#include "fixtures/BebasNeue.inc"
};
static unsigned loads = 0;
static bool ota_active = false;
static bool missing_available = false;
static int presentation_mode = EPAPER_RENDER_MODE_BW;
static std::unordered_set<void*> buffers;
static unsigned failures = 0;
static std::vector<std::string> log_lines;
static std::vector<uint8_t> external_font;

#if HAS_FREETYPE_FONTS
static int allocation_budget = -1;
#if HAS_DISPLAY_DIAGNOSTICS
static uint32_t glyph_clock_us = UINT32_MAX - 75;
extern "C" unsigned long micros() {
    glyph_clock_us += 50;
    return glyph_clock_us;
}
#endif
extern "C" void* __real_lv_malloc_core(size_t size);
extern "C" void* __real_lv_realloc_core(void* block, size_t size);
extern "C" void* __wrap_lv_malloc_core(size_t size) {
    if (allocation_budget == 0) return nullptr;
    if (allocation_budget > 0) --allocation_budget;
    return __real_lv_malloc_core(size);
}
extern "C" void* __wrap_lv_realloc_core(void* block, size_t size) {
    if (allocation_budget == 0) return nullptr;
    if (allocation_budget > 0) --allocation_budget;
    return __real_lv_realloc_core(block, size);
}
#endif

extern "C" unsigned long millis() { return 0; }
size_t heap_caps_get_free_size(uint32_t) { return 8192; }
size_t heap_caps_get_largest_free_block(uint32_t) { return 4096; }

void log_noop(const char*, const char* format, ...) {
    char line[288];
    va_list arguments;
    va_start(arguments, format);
    vsnprintf(line, sizeof(line), format, arguments);
    va_end(arguments);
    log_lines.emplace_back(line);
}

static size_t matching_logs(const char* text) {
    size_t count = 0;
    for (const auto& line : log_lines) if (line.find(text) != std::string::npos) ++count;
    return count;
}

#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL %d: %s\n", __LINE__, #condition); ++failures; } } while (0)

#if HAS_FREETYPE_FONTS
struct GlyphImage {
    lv_font_glyph_dsc_t descriptor{};
    std::vector<uint8_t> pixels;
    bool operator==(const GlyphImage& other) const {
        return descriptor.adv_w == other.descriptor.adv_w &&
            descriptor.box_w == other.descriptor.box_w && descriptor.box_h == other.descriptor.box_h &&
            descriptor.ofs_x == other.descriptor.ofs_x && descriptor.ofs_y == other.descriptor.ofs_y &&
            pixels == other.pixels;
    }
};

static GlyphImage glyph_image(const lv_font_t* font, uint32_t character) {
    GlyphImage image;
    CHECK(lv_font_get_glyph_dsc(font, &image.descriptor, character, 0));
    const auto* bitmap = static_cast<const lv_draw_buf_t*>(lv_font_get_glyph_bitmap(&image.descriptor, nullptr));
    CHECK(bitmap != nullptr);
    if (bitmap) {
        for (unsigned row = 0; row < image.descriptor.box_h; ++row)
            for (unsigned column = 0; column < image.descriptor.box_w; ++column)
                image.pixels.push_back(bitmap->data[row * bitmap->header.stride + column]);
    }
    lv_font_glyph_release_draw_data(&image.descriptor);
    return image;
}
#endif

bool ota_activity_is_active() { return ota_active; }
int display_manager_get_presentation_mode() { return presentation_mode; }
bool font_store_validate_name(const char* name) { return name && *name && strlen(name) < FONT_NAME_MAX_LEN; }
uint8_t* font_store_load(const char* name, size_t& length) {
    ++loads;
    if (strcmp(name, "missing") == 0 && !missing_available) return nullptr;
    const bool external = strcmp(name, "broken-metrics") == 0 && !external_font.empty();
    length = external ? external_font.size() : sizeof(fixture);
    auto* data = static_cast<uint8_t*>(malloc(length));
    if (data) {
        memcpy(data, external ? external_font.data() : fixture, length);
        if (strcmp(name, "broken-metrics") == 0 && !external) {
            const unsigned count = (static_cast<unsigned>(data[4]) << 8) | data[5];
            for (unsigned index = 0; index < count; ++index) {
                const uint8_t* entry = data + 12 + index * 16;
                if (memcmp(entry, "hhea", 4) != 0) continue;
                const uint32_t offset = (static_cast<uint32_t>(entry[8]) << 24) |
                    (static_cast<uint32_t>(entry[9]) << 16) |
                    (static_cast<uint32_t>(entry[10]) << 8) | entry[11];
                data[offset + 4] = 0;
                data[offset + 5] = 100;
                data[offset + 6] = 0xff;
                data[offset + 7] = 0x9c;
            }
        }
        buffers.insert(data);
    }
    return data;
}
void heap_caps_free(void* data) { buffers.erase(data); free(data); }

int main(int argument_count, char** arguments) {
    if (argument_count > 1) {
        std::ifstream input(arguments[1], std::ios::binary);
        external_font.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
        if (external_font.empty()) return 1;
    }
    lv_init();
    {
        device_typography_init("test-font", 0);
        const lv_font_t* font = native_extension_text_font("default", 24);
        CHECK(font != &lv_font_montserrat_24);
        CHECK(native_extension_text_font(nullptr, 24) == font);
        CHECK(native_extension_text_font("@builtin", 24) == &lv_font_montserrat_24);
        const lv_font_t* explicit_font = native_extension_text_font("test-font", 18);
        CHECK(explicit_font != font && explicit_font != &lv_font_montserrat_18);
        CHECK(native_extension_text_font("missing", 24) == &lv_font_montserrat_24);
        constexpr uint32_t width = 64, height = 64;
        std::vector<uint16_t> actual(width * height + 2, 0);
        actual.front() = actual.back() = 0x1234;
        const lv_color_t white = lv_color_white();
        lv_font_t fallback_only{};
        fallback_only.line_height = font->line_height;
        fallback_only.base_line = font->base_line;
        fallback_only.fallback = &lv_font_montserrat_24;
        fallback_only.get_glyph_dsc = [](const lv_font_t*, lv_font_glyph_dsc_t*, uint32_t, uint32_t) { return false; };
        const lv_font_t* render_fonts[] = {font, &lv_font_montserrat_24, &fallback_only};
        for (const lv_font_t* render_font : render_fonts) {
            lv_font_glyph_dsc_t glyph{};
            CHECK(lv_font_get_glyph_dsc(render_font, &glyph, 'W', 0));
            CHECK(native_extension_text_measure("W", render_font) == glyph.adv_w);
            lv_draw_buf_t* scratch = lv_draw_buf_create(glyph.box_w, glyph.box_h, LV_COLOR_FORMAT_A8, LV_STRIDE_AUTO);
            CHECK(scratch != nullptr);
            const auto* bitmap = static_cast<const lv_draw_buf_t*>(lv_font_get_glyph_bitmap(&glyph, scratch));
            CHECK(bitmap != nullptr);
            std::fill(actual.begin() + 1, actual.end() - 1, 0);
            native_extension_text_draw(actual.data() + 1, width, height, 3, 4, "W", render_font, white);
            unsigned visible = 0;
            for (uint32_t row = 0; row < height; ++row) {
                for (uint32_t column = 0; column < width; ++column) {
                    const int glyph_x = static_cast<int>(column) - 3 - glyph.ofs_x;
                    const int glyph_y = static_cast<int>(row) - 4 - render_font->line_height + render_font->base_line + glyph.box_h + glyph.ofs_y;
                    const uint8_t coverage = bitmap && glyph_x >= 0 && glyph_y >= 0 && glyph_x < glyph.box_w && glyph_y < glyph.box_h
                        ? bitmap->data[glyph_y * bitmap->header.stride + glyph_x] : 0;
                    const uint16_t expected = lv_color_to_u16(lv_color_mix(white, lv_color_black(), coverage));
                    CHECK(actual[1 + row * width + column] == expected);
                    if (expected) ++visible;
                }
            }
            CHECK(visible > 0);
            lv_font_glyph_release_draw_data(&glyph);
            lv_draw_buf_destroy(scratch);
        }
        for (unsigned iteration = 0; iteration < 64; ++iteration) {
            native_extension_text_draw(actual.data() + 1, width, height, -8, -8, "W ", font, white);
            native_extension_text_draw(actual.data() + 1, width, height, 60, 60, "W", font, white);
            native_extension_text_draw(actual.data() + 1, width, height, 0, 0, "W", &lv_font_montserrat_24, white);
        }
        native_extension_text_draw(actual.data() + 1, width, height, 0, 0, "ABCDEFGHIJKLMNOPQRSTUVWXYZ", font, white);
        lv_font_glyph_dsc_t reused{};
        CHECK(lv_font_get_glyph_dsc(font, &reused, 'W', 0));
        CHECK(reused.resolved_font == font);
        CHECK(actual.front() == 0x1234 && actual.back() == 0x1234);
        device_typography_fonts().clear();
        device_typography_init("", 0);
        loads = 0;
        log_lines.clear();
    }
    {
        PadFontCache first;
        PadFontCache second;
        LabelStyle style{};
        strcpy(style.font_name, "test-font");
        style.font_size = 37;
        const lv_font_t* font = pad_resolve_font(style, &lv_font_montserrat_14, &first);
        CHECK(font != &lv_font_montserrat_14);
        CHECK(pad_resolve_font(style, &lv_font_montserrat_14, &first) == font);
        CHECK(loads == 1 && buffers.size() == 1);
        CHECK(matching_logs("internal free=8192 largest=4096; PSRAM free=8192 largest=4096") == 1);
        CHECK(pad_resolve_font(style, &lv_font_montserrat_14, &second) != font);
        CHECK(buffers.size() == 2);
        CHECK(pad_resolve_font(style, &lv_font_montserrat_18, &first) == font);
        CHECK(font->fallback == &lv_font_montserrat_36);
    #if HAS_FREETYPE_FONTS
        const auto mono_image = glyph_image(font, '8');
        CHECK(!mono_image.pixels.empty());
        for (uint8_t pixel : mono_image.pixels) CHECK(pixel == 0 || pixel == 255);
        presentation_mode = EPAPER_RENDER_MODE_GRAYSCALE;
        const lv_font_t* grayscale = pad_resolve_font(style, &lv_font_montserrat_14, &first);
        CHECK(grayscale != font && grayscale != &lv_font_montserrat_14);
        lv_font_t* stb_reference = lv_tiny_ttf_create_data_ex(fixture, sizeof(fixture), 37, LV_FONT_KERNING_NORMAL, 16);
        CHECK(stb_reference != nullptr);
        CHECK(glyph_image(grayscale, '8') == glyph_image(stb_reference, '8'));
        lv_tiny_ttf_destroy(stb_reference);
        CHECK(pad_resolve_font(style, &lv_font_montserrat_14, &first) == grayscale);
        presentation_mode = EPAPER_RENDER_MODE_BW;
        CHECK(pad_resolve_font(style, &lv_font_montserrat_14, &first) == font);
    #endif
        style.font_metrics = 2;
        const lv_font_t* doubled = pad_resolve_font(style, &lv_font_montserrat_14, &first);
        CHECK(doubled != font && doubled != &lv_font_montserrat_14);
        CHECK(doubled->line_height == font->line_height * 2);
        CHECK(doubled->base_line == font->base_line * 2);
        CHECK(pad_resolve_font(style, &lv_font_montserrat_14, &first) == doubled);
        style.font_metrics = -1;
        const lv_font_t* bounded = pad_resolve_font(style, &lv_font_montserrat_14, &first);
        CHECK(bounded != font && bounded != doubled && bounded != &lv_font_montserrat_14);
        CHECK(bounded->line_height >= font->line_height);
        CHECK(pad_resolve_font(style, &lv_font_montserrat_14, &first) == bounded);
        style.font_metrics = 65535;
        CHECK(pad_resolve_font(style, &lv_font_montserrat_14, &first) == &lv_font_montserrat_36);
        style.font_metrics = 0;
        lv_display_t* display = lv_display_create(320, 240);
        lv_obj_t* label = lv_label_create(lv_display_get_screen_active(display));
        lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
        lv_label_set_text(label, "88");
        lv_obj_update_layout(label);
        CHECK(lv_obj_get_style_text_font(label, LV_PART_MAIN) == font);
        lv_obj_set_hidden(label, true);
        CHECK(buffers.size() == 2);
        CHECK(pad_resolve_font(style, &lv_font_montserrat_14, &first) == font);
        lv_obj_set_hidden(label, false);
        lv_font_glyph_dsc_t glyph{};
        CHECK(lv_font_get_glyph_dsc(font, &glyph, '8', 0));
        CHECK(glyph.format == LV_FONT_GLYPH_FORMAT_A8);
        const void* bitmap = lv_font_get_glyph_bitmap(&glyph, nullptr);
        CHECK(bitmap != nullptr);
        lv_font_glyph_release_draw_data(&glyph);
        CHECK(lv_font_get_glyph_dsc(font, &glyph, '8', 0));
        CHECK(lv_font_get_glyph_bitmap(&glyph, nullptr) == bitmap);
        lv_font_glyph_release_draw_data(&glyph);
        lv_mem_monitor_t warmed{};
        lv_mem_monitor(&warmed);
        const size_t glyph_log_count = log_lines.size();
        for (unsigned iteration = 0; iteration < 64; ++iteration) {
            CHECK(lv_font_get_glyph_dsc(font, &glyph, '8', 0));
            CHECK(lv_font_get_glyph_bitmap(&glyph, nullptr) == bitmap);
            lv_font_glyph_release_draw_data(&glyph);
        }
        lv_mem_monitor_t repeated{};
        lv_mem_monitor(&repeated);
        CHECK(log_lines.size() == glyph_log_count);
        CHECK(repeated.used_cnt == warmed.used_cnt);
        CHECK(repeated.free_size + 256 >= warmed.free_size);
        CHECK(warmed.free_size + 256 >= repeated.free_size);
        const size_t warm_log_count = log_lines.size();
        for (unsigned iteration = 0; iteration < 64; ++iteration)
            CHECK(pad_resolve_font(style, &lv_font_montserrat_14, &first) == font);
        CHECK(log_lines.size() == warm_log_count);
        ota_active = true;
        CHECK(pad_resolve_font(style, &lv_font_montserrat_14, &first) == &lv_font_montserrat_36);
        CHECK(font->get_glyph_bitmap(&glyph, nullptr) == nullptr);
    #if HAS_FREETYPE_FONTS
        lv_font_glyph_dsc_t blocked{};
        CHECK(!font->get_glyph_dsc(font, &blocked, '8', 0));
    #endif
        ota_active = false;
        CHECK(lv_font_get_glyph_bitmap(&glyph, nullptr) == bitmap);
        lv_font_glyph_release_draw_data(&glyph);
        style.font_size = 300;
        CHECK(pad_resolve_font(style, &lv_font_montserrat_14, &first) != &lv_font_montserrat_14);
        style.font_size = 0;
        CHECK(pad_resolve_font(style, &lv_font_montserrat_14, &first) != &lv_font_montserrat_14);
        CHECK(loads == 2);
        strcpy(style.font_name, "missing");
        style.font_size = 36;
        CHECK(pad_resolve_font(style, &lv_font_montserrat_14, &first) == &lv_font_montserrat_36);
        CHECK(pad_resolve_font(style, &lv_font_montserrat_14, &first) == &lv_font_montserrat_36);
        CHECK(loads == 3);
        CHECK(matching_logs("'missing' unavailable") == 1);
        CHECK(pad_resolve_font(style, &lv_font_montserrat_14) == &lv_font_montserrat_36);
        lv_display_delete(display);
        first.clear();
        const size_t clear_log_count = log_lines.size();
        first.clear();
        CHECK(log_lines.size() == clear_log_count);
        CHECK(matching_logs("cleared") == 1);
        CHECK(buffers.size() == 1);
    }
    CHECK(buffers.empty());
    CHECK(matching_logs("cleared") == 2);
    {
        device_typography_init("test-font", 30);
        LabelStyle inherited{};
        const uint16_t medium = pad_font_pixel_size(pad_get_scale_info().font_medium);
        const lv_font_t* automatic = pad_resolve_font(inherited, &lv_font_montserrat_24);
        CHECK(automatic != &lv_font_montserrat_24);
        PadFontCache pad_owner;
        pad_owner.clear();
        CHECK(pad_resolve_font(inherited, &lv_font_montserrat_24) == automatic);
        CHECK(automatic == device_typography_fonts().resolve("test-font", (24 * 30 + medium / 2) / medium, pad_nearest_bitmap_font(24 * 30, medium)));
        inherited.font_size = 60;
        CHECK(pad_resolve_font(inherited, &lv_font_montserrat_14) == device_typography_fonts().resolve("test-font", 60, &lv_font_montserrat_36));
        strcpy(inherited.font_name, "@builtin");
        CHECK(pad_resolve_font(inherited, &lv_font_montserrat_14) == &lv_font_montserrat_36);
        inherited.font_size = 0;
        CHECK(pad_resolve_font(inherited, &lv_font_montserrat_14) == pad_nearest_bitmap_font(14 * 30, medium));
        CHECK(pad_nearest_bitmap_font(1) == &lv_font_montserrat_12);
        CHECK(pad_nearest_bitmap_font(65535) == &lv_font_montserrat_36);
        CHECK(pad_nearest_bitmap_font(208, 10) == &lv_font_montserrat_18);
        CHECK(pad_nearest_bitmap_font(21) == &lv_font_montserrat_24);
        lv_display_t* display = lv_display_create(320, 240);
        lv_obj_t* label = lv_label_create(lv_display_get_screen_active(display));
        inherited.font_upscale = 384;
        inherited.font_name[0] = '\0';
        lv_obj_set_style_text_font(label, pad_resolve_font(inherited, &lv_font_montserrat_14), 0);
        pad_apply_font_upscale(label, inherited, PAD_LABEL_ANCHOR_CENTER);
        CHECK(lv_obj_get_style_transform_scale_x(label, LV_PART_MAIN) == 256);
        strcpy(inherited.font_name, "@builtin");
        lv_obj_set_style_text_font(label, pad_resolve_font(inherited, &lv_font_montserrat_14), 0);
        pad_apply_font_upscale(label, inherited, PAD_LABEL_ANCHOR_CENTER);
        CHECK(lv_obj_get_style_transform_scale_x(label, LV_PART_MAIN) == 384);
        lv_display_delete(display);
        device_typography_init("test-font", 65535);
        CHECK(pad_resolve_font(LabelStyle{}, &lv_font_montserrat_36) == &lv_font_montserrat_36);
        device_typography_init("missing", 0);
        CHECK(pad_resolve_font(LabelStyle{}, &lv_font_montserrat_14) == &lv_font_montserrat_14);
        missing_available = true;
        CHECK(pad_resolve_font(LabelStyle{}, &lv_font_montserrat_14) == &lv_font_montserrat_14);
        device_typography_fonts().clear();
        CHECK(pad_resolve_font(LabelStyle{}, &lv_font_montserrat_14) != &lv_font_montserrat_14);
        device_typography_fonts().clear();
        missing_available = false;
        device_typography_init("@builtin", 0);
    }
    CHECK(buffers.empty());
    {
        PadFontCache owner;
        PadFontCache bounded_owner(2);
        const lv_font_t* retained = bounded_owner.resolve("test-font", 24, &lv_font_montserrat_24);
        CHECK(retained != &lv_font_montserrat_24);
        CHECK(bounded_owner.resolve("test-font", 32, &lv_font_montserrat_32) != &lv_font_montserrat_32);
        CHECK(bounded_owner.resolve("test-font", 36, &lv_font_montserrat_36) == &lv_font_montserrat_36);
        CHECK(bounded_owner.resolve("test-font", 24, &lv_font_montserrat_24) == retained);
        const lv_font_t* normal = owner.resolve("broken-metrics", 40, &lv_font_montserrat_14);
        const lv_font_t* corrected = owner.resolve("broken-metrics", 40, &lv_font_montserrat_14, -1);
        CHECK(normal != &lv_font_montserrat_14 && corrected != &lv_font_montserrat_14);
        lv_font_glyph_dsc_t original{};
        CHECK(lv_font_get_glyph_dsc(normal, &original, '8', 0));
        CHECK(normal->line_height - normal->base_line - original.box_h - original.ofs_y < 0);
        for (const char* character = "0123456789:"; *character; ++character) {
            lv_font_glyph_dsc_t before{};
            lv_font_glyph_dsc_t after{};
            CHECK(lv_font_get_glyph_dsc(normal, &before, *character, 0));
            CHECK(lv_font_get_glyph_dsc(corrected, &after, *character, 0));
            CHECK(before.box_w == after.box_w && before.box_h == after.box_h);
            CHECK(before.adv_w == after.adv_w && before.ofs_y == after.ofs_y);
            const int top = corrected->line_height - corrected->base_line - after.box_h - after.ofs_y;
            CHECK(top >= 0 && top + after.box_h <= corrected->line_height);
        }
        const lv_font_t* fractional = owner.resolve("broken-metrics", 40, &lv_font_montserrat_14, 1.5f);
        CHECK(fractional->line_height >= normal->line_height * 1.5f);
        CHECK(owner.resolve("broken-metrics", 40, &lv_font_montserrat_14) == normal);
    }
    CHECK(buffers.empty());
#if HAS_FREETYPE_FONTS
    lv_mem_monitor_t baseline{};
    lv_mem_monitor(&baseline);
    CHECK(freetype_mono_font_create(nullptr, sizeof(fixture), 40) == nullptr);
    CHECK(freetype_mono_font_create(fixture, 0, 40) == nullptr);
    CHECK(freetype_mono_font_create(fixture, sizeof(fixture), 0) == nullptr);
    lv_font_t* reference = freetype_mono_font_create(fixture, sizeof(fixture), 40);
    CHECK(reference != nullptr);
#if HAS_DISPLAY_DIAGNOSTICS
    auto glyph_before = freetype_mono_font_get_stats();
#endif
    const auto expected = glyph_image(reference, 'W');
#if HAS_DISPLAY_DIAGNOSTICS
    auto glyph_delta = freetype_mono_font_get_stats().since(glyph_before);
    CHECK(glyph_delta.misses == 1 && glyph_delta.hits == 1);
    CHECK(glyph_delta.evictions == 0 && glyph_delta.failures == 0 && glyph_delta.miss_us == 50);
    glyph_before = freetype_mono_font_get_stats();
    CHECK(glyph_image(reference, 'W') == expected);
    glyph_delta = freetype_mono_font_get_stats().since(glyph_before);
    CHECK(glyph_delta.hits == 2 && glyph_delta.misses == 0 && glyph_delta.miss_us == 0);
#endif
    lv_font_glyph_dsc_t held[16]{};
    for (unsigned index = 0; index < 16; ++index) {
        CHECK(lv_font_get_glyph_dsc(reference, &held[index], 'A' + index, 0));
        CHECK(lv_font_get_glyph_bitmap(&held[index], nullptr) != nullptr);
    }
    lv_font_glyph_dsc_t full{};
#if HAS_DISPLAY_DIAGNOSTICS
    glyph_before = freetype_mono_font_get_stats();
#endif
    CHECK(!reference->get_glyph_dsc(reference, &full, 'Q', 0));
#if HAS_DISPLAY_DIAGNOSTICS
    glyph_delta = freetype_mono_font_get_stats().since(glyph_before);
    CHECK(glyph_delta.misses == 1 && glyph_delta.failures == 1 && glyph_delta.evictions == 0);
    CHECK(glyph_delta.miss_us == 50);
#endif
    lv_font_glyph_release_draw_data(&held[0]);
#if HAS_DISPLAY_DIAGNOSTICS
    glyph_before = freetype_mono_font_get_stats();
#endif
    CHECK(!glyph_image(reference, 'Q').pixels.empty());
#if HAS_DISPLAY_DIAGNOSTICS
    glyph_delta = freetype_mono_font_get_stats().since(glyph_before);
    CHECK(glyph_delta.misses == 1 && glyph_delta.evictions == 1 && glyph_delta.failures == 0);
#endif
    for (unsigned index = 1; index < 16; ++index) lv_font_glyph_release_draw_data(&held[index]);
    freetype_mono_font_destroy(reference);
    for (int budget = 0; budget < 100; ++budget) {
        allocation_budget = budget;
        lv_font_t* font = freetype_mono_font_create(fixture, sizeof(fixture), 40);
        allocation_budget = -1;
        freetype_mono_font_destroy(font);
        lv_mem_monitor_t after{};
        lv_mem_monitor(&after);
        CHECK(after.used_cnt == baseline.used_cnt && after.free_size == baseline.free_size);
    }
    for (int budget = 0; budget < 100; ++budget) {
        lv_font_t* font = freetype_mono_font_create(fixture, sizeof(fixture), 40);
        CHECK(font != nullptr);
        allocation_budget = budget;
        lv_font_glyph_dsc_t failed{};
#if HAS_DISPLAY_DIAGNOSTICS
        glyph_before = freetype_mono_font_get_stats();
#endif
        font->get_glyph_dsc(font, &failed, 'W', 0);
#if HAS_DISPLAY_DIAGNOSTICS
        if (budget == 0) {
            glyph_delta = freetype_mono_font_get_stats().since(glyph_before);
            CHECK(glyph_delta.misses == 1 && glyph_delta.failures == 1 && glyph_delta.miss_us == 50);
        }
#endif
        allocation_budget = -1;
        CHECK(glyph_image(font, 'W') == expected);
        lv_font_glyph_dsc_t pinned{};
        CHECK(lv_font_get_glyph_dsc(font, &pinned, '8', 0));
        const auto* bitmap = static_cast<const lv_draw_buf_t*>(lv_font_get_glyph_bitmap(&pinned, nullptr));
        CHECK(bitmap != nullptr);
        const std::vector<uint8_t> saved(bitmap->data, bitmap->data + bitmap->header.stride * bitmap->header.h);
        allocation_budget = 0;
        font->get_glyph_dsc(font, &failed, 'A', 0);
        allocation_budget = -1;
        CHECK(glyph_image(font, 'W') == expected);
        CHECK(glyph_image(font, 'A').pixels.size() > 0);
        for (unsigned character = 'B'; character <= 'Z'; ++character) glyph_image(font, character);
        CHECK(!memcmp(saved.data(), bitmap->data, saved.size()));
        lv_font_glyph_release_draw_data(&pinned);
        freetype_mono_font_destroy(font);
        lv_mem_monitor_t after{};
        lv_mem_monitor(&after);
        CHECK(after.used_cnt == baseline.used_cnt && after.free_size == baseline.free_size);
    }
    std::ifstream unicode_input(TEST_UNICODE_FONT_PATH, std::ios::binary);
    const std::vector<uint8_t> unicode_font{std::istreambuf_iterator<char>(unicode_input), std::istreambuf_iterator<char>()};
    CHECK(!unicode_font.empty());
    reference = freetype_mono_font_create(unicode_font.data(), unicode_font.size(), 16);
    CHECK(reference != nullptr);
    if (reference) {
        const auto unicode_expected = glyph_image(reference, 0x915);
        freetype_mono_font_destroy(reference);
        for (int budget = 0; budget < 100; ++budget) {
            lv_font_t* font = freetype_mono_font_create(unicode_font.data(), unicode_font.size(), 16);
            CHECK(font != nullptr);
            lv_font_glyph_dsc_t pinned{};
            CHECK(lv_font_get_glyph_dsc(font, &pinned, '8', 0));
            const auto* bitmap = static_cast<const lv_draw_buf_t*>(lv_font_get_glyph_bitmap(&pinned, nullptr));
            CHECK(bitmap != nullptr);
            const std::vector<uint8_t> saved(bitmap->data, bitmap->data + bitmap->header.stride * bitmap->header.h);
            allocation_budget = budget;
            lv_font_glyph_dsc_t failed{};
            font->get_glyph_dsc(font, &failed, 0x915, 0);
            allocation_budget = -1;
            CHECK(glyph_image(font, 0x915) == unicode_expected);
            CHECK(!memcmp(saved.data(), bitmap->data, saved.size()));
            lv_font_glyph_release_draw_data(&pinned);
            freetype_mono_font_destroy(font);
            lv_mem_monitor_t after{};
            lv_mem_monitor(&after);
            CHECK(after.used_cnt == baseline.used_cnt && after.free_size == baseline.free_size);
        }
    }
#endif
    lv_deinit();
    return failures ? 1 : 0;
}