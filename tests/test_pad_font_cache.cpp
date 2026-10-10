#include "pad_layout.h"
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

static const uint8_t fixture[] = {
#include "fixtures/BebasNeue.inc"
};
static unsigned loads = 0;
static bool ota_active = false;
static std::unordered_set<void*> buffers;
static unsigned failures = 0;
static std::vector<std::string> log_lines;
static std::vector<uint8_t> external_font;

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

bool ota_activity_is_active() { return ota_active; }
bool font_store_validate_name(const char* name) { return name && *name && strlen(name) < FONT_NAME_MAX_LEN; }
uint8_t* font_store_load(const char* name, size_t& length) {
    ++loads;
    if (strcmp(name, "missing") == 0) return nullptr;
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
        CHECK(pad_resolve_font(style, &lv_font_montserrat_18, &first) != font);
        CHECK(font->fallback == &lv_font_montserrat_14);
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
        CHECK(pad_resolve_font(style, &lv_font_montserrat_14, &first) == &lv_font_montserrat_14);
        style.font_metrics = 0;
        lv_display_t* display = lv_display_create(320, 240);
        lv_obj_t* label = lv_label_create(lv_display_get_screen_active(display));
        lv_obj_set_style_text_font(label, font, LV_PART_MAIN);
        lv_label_set_text(label, "88");
        lv_obj_update_layout(label);
        CHECK(lv_obj_get_style_text_font(label, LV_PART_MAIN) == font);
        lv_obj_add_flag(label, LV_OBJ_FLAG_HIDDEN);
        CHECK(buffers.size() == 2);
        CHECK(pad_resolve_font(style, &lv_font_montserrat_14, &first) == font);
        lv_obj_remove_flag(label, LV_OBJ_FLAG_HIDDEN);
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
        CHECK(pad_resolve_font(style, &lv_font_montserrat_14, &first) == &lv_font_montserrat_14);
        ota_active = false;
        style.font_size = 300;
        CHECK(pad_resolve_font(style, &lv_font_montserrat_14, &first) != &lv_font_montserrat_14);
        style.font_size = 0;
        CHECK(pad_resolve_font(style, &lv_font_montserrat_14, &first) != &lv_font_montserrat_14);
        CHECK(loads == 2);
        strcpy(style.font_name, "missing");
        style.font_size = 36;
        CHECK(pad_resolve_font(style, &lv_font_montserrat_14, &first) == &lv_font_montserrat_14);
        CHECK(pad_resolve_font(style, &lv_font_montserrat_14, &first) == &lv_font_montserrat_14);
        CHECK(loads == 3);
        CHECK(matching_logs("'missing' unavailable") == 1);
        CHECK(pad_resolve_font(style, &lv_font_montserrat_14) == &lv_font_montserrat_14);
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
        PadFontCache owner;
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
    lv_deinit();
    return failures ? 1 : 0;
}