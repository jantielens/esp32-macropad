// ============================================================================
// Unit tests for widget common utilities (clamp_val, etc.)
// ============================================================================
// Compiled without LVGL / ArduinoJson — we pre-define the board_config guard
// so that widget.h skips display-only includes.

#include <cassert>
#include <cfloat>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

// Block board_config.h and disable display/mqtt to avoid LVGL dependencies.
#define BOARD_CONFIG_H
#define HAS_DISPLAY 0
#define HAS_MQTT    0

#include "widgets/widget.h"
#include "widgets/epub_pagination.h"

static_assert(widget_default_rgb("#4CAF50") == 0x4CAF50, "Uppercase palette conversion");
static_assert(widget_default_rgb("#b0b0d0") == 0xb0b0d0, "Lowercase palette conversion");
static_assert(widget_default_rgb("#000000") == 0, "Black palette conversion");
static_assert(widget_default_rgb("#FFFFFF") == 0xFFFFFF, "White palette conversion");

// ---------------------------------------------------------------------------
// Minimal test harness
// ---------------------------------------------------------------------------
static int g_tests = 0;
static int g_passed = 0;

#define TEST(name) static void name()
#define RUN(name) do {                                       \
    g_tests++;                                               \
    printf("  %-50s ", #name);                               \
    name();                                                  \
    g_passed++;                                              \
    printf("PASS\n");                                        \
} while (0)

#define ASSERT_EQ(a, b) do {                                 \
    auto _a = (a); auto _b = (b);                            \
    if (_a != _b) {                                          \
        printf("FAIL\n    %s:%d: %s != %s\n",               \
               __FILE__, __LINE__, #a, #b);                  \
        assert(false);                                       \
    }                                                        \
} while (0)

#define ASSERT_FLOAT_EQ(a, b) do {                           \
    float _a = (a); float _b = (b);                          \
    float _d = (_a > _b) ? (_a - _b) : (_b - _a);           \
    if (_d > 1e-6f) {                                        \
        printf("FAIL\n    %s:%d: %s (%.6f) != %s (%.6f)\n", \
               __FILE__, __LINE__, #a, _a, #b, _b);         \
        assert(false);                                       \
    }                                                        \
} while (0)

// ===========================================================================
// clamp_val<int>
// ===========================================================================
TEST(clamp_int_mid)          { ASSERT_EQ(clamp_val(5, 0, 10), 5); }
TEST(clamp_int_below)        { ASSERT_EQ(clamp_val(-3, 0, 10), 0); }
TEST(clamp_int_above)        { ASSERT_EQ(clamp_val(15, 0, 10), 10); }
TEST(clamp_int_at_lo)        { ASSERT_EQ(clamp_val(0, 0, 10), 0); }
TEST(clamp_int_at_hi)        { ASSERT_EQ(clamp_val(10, 0, 10), 10); }
TEST(clamp_int_negative_range) { ASSERT_EQ(clamp_val(-5, -10, -1), -5); }
TEST(clamp_int_single_point) { ASSERT_EQ(clamp_val(99, 7, 7), 7); }

// ===========================================================================
// clamp_val<uint8_t>  (matches real widget usage)
// ===========================================================================
TEST(clamp_u8_mid)           { ASSERT_EQ(clamp_val<uint8_t>(50, 1, 100), (uint8_t)50); }
TEST(clamp_u8_below)         { ASSERT_EQ(clamp_val<uint8_t>(0, 1, 100), (uint8_t)1); }
TEST(clamp_u8_above)         { ASSERT_EQ(clamp_val<uint8_t>(200, 1, 100), (uint8_t)100); }
TEST(clamp_u8_at_lo)         { ASSERT_EQ(clamp_val<uint8_t>(1, 1, 100), (uint8_t)1); }
TEST(clamp_u8_at_hi)         { ASSERT_EQ(clamp_val<uint8_t>(100, 1, 100), (uint8_t)100); }

// Exact ranges from widget parse functions
TEST(clamp_u8_bar_width)     { ASSERT_EQ(clamp_val<uint8_t>(0, 1, 100), (uint8_t)1); }
TEST(clamp_u8_arc_width)     { ASSERT_EQ(clamp_val<uint8_t>(3, 5, 50), (uint8_t)5); }
TEST(clamp_u8_tick_width)    { ASSERT_EQ(clamp_val<uint8_t>(0, 1, 5), (uint8_t)1); }
TEST(clamp_u8_tick_width_hi) { ASSERT_EQ(clamp_val<uint8_t>(9, 1, 5), (uint8_t)5); }

// ===========================================================================
// clamp_val<float>
// ===========================================================================
TEST(clamp_float_mid)        { ASSERT_FLOAT_EQ(clamp_val(3.14f, 0.0f, 10.0f), 3.14f); }
TEST(clamp_float_below)      { ASSERT_FLOAT_EQ(clamp_val(-0.5f, 0.0f, 10.0f), 0.0f); }
TEST(clamp_float_above)      { ASSERT_FLOAT_EQ(clamp_val(99.9f, 0.0f, 10.0f), 10.0f); }
TEST(clamp_float_at_lo)      { ASSERT_FLOAT_EQ(clamp_val(0.0f, 0.0f, 10.0f), 0.0f); }
TEST(clamp_float_at_hi)      { ASSERT_FLOAT_EQ(clamp_val(10.0f, 0.0f, 10.0f), 10.0f); }

// ===========================================================================
// clamp_val<uint16_t>  (gauge arc_degrees uses int → uint16_t cast)
// ===========================================================================
TEST(clamp_u16_mid)          { ASSERT_EQ(clamp_val<uint16_t>(180, 10, 360), (uint16_t)180); }
TEST(clamp_u16_below)        { ASSERT_EQ(clamp_val<uint16_t>(5, 10, 360), (uint16_t)10); }
TEST(clamp_u16_above)        { ASSERT_EQ(clamp_val<uint16_t>(500, 10, 360), (uint16_t)360); }

// ===========================================================================
// clamp_val with int (gauge degrees path: clamp_val(deg, 10, 360))
// ===========================================================================
TEST(clamp_degrees_mid)      { ASSERT_EQ(clamp_val(180, 10, 360), 180); }
TEST(clamp_degrees_lo)       { ASSERT_EQ(clamp_val(5, 10, 360), 10); }
TEST(clamp_degrees_hi)       { ASSERT_EQ(clamp_val(999, 10, 360), 360); }

static EpubPageMetrics epub_test_metrics(const char* text, uint16_t width, uint16_t rows) {
    static EpubGlyphCache cache;
    memset(&cache, 0, sizeof(cache));
    const size_t length = strlen(text);
    size_t offset = 0;
    uint32_t previous = 0;
    while (offset < length) {
        const uint32_t code = epub_decode_utf8(text, length, offset);
        if (code == '\n') { previous = 0; continue; }
        epub_glyph_entry(cache, code, 0, true)->width = 1;
        if (previous) epub_glyph_entry(cache, previous, code, true)->width = 1;
        previous = code;
    }
    EpubPageMetrics metrics{};
    metrics.glyphs = &cache;
    metrics.line_width = width;
    metrics.rows = rows;
    return metrics;
}

TEST(epub_pages_preserve_utf8_and_words) {
    const char* text = "one \xc3\xa9" "clair two three";
    char page[64];
    bool overflow = false;
    auto metrics = epub_test_metrics(text, 6, 1);
    size_t offset = epub_make_page(text, strlen(text), 0, metrics, page, sizeof(page), &overflow);
    ASSERT_EQ(overflow, false);
    ASSERT_EQ(strcmp(page, "one\n"), 0);
    offset = epub_make_page(text, strlen(text), offset, metrics, page, sizeof(page), &overflow);
    ASSERT_EQ(strcmp(page, "\xc3\xa9" "clair\n"), 0);
    metrics.rows = 2;
    offset = epub_make_page(text, strlen(text), offset, metrics, page, sizeof(page), &overflow);
    ASSERT_EQ(strcmp(page, "two\nthree\n"), 0);
    ASSERT_EQ(offset, strlen(text));
}

TEST(epub_page_buffer_overflow_is_reported) {
    char page[4];
    bool overflow = false;
    const auto metrics = epub_test_metrics("abcd", 4, 1);
    const size_t offset = epub_make_page("abcd", 4, 0, metrics, page, sizeof(page), &overflow);
    ASSERT_EQ(overflow, true);
    ASSERT_EQ(offset, static_cast<size_t>(0));
}

TEST(epub_large_chapter_keeps_all_words) {
    std::string text;
    for (int i = 0; i < 20000; ++i) text += "reading chapter ";
    std::string actual;
    char page[512];
    bool overflow = false;
    const auto metrics = epub_test_metrics(text.c_str(), 40, 8);
    size_t offset = 0;
    unsigned pages = 0;
    while (offset < text.size()) {
        const size_t next = epub_make_page(text.c_str(), text.size(), offset, metrics,
                                           page, sizeof(page), &overflow);
        ASSERT_EQ(overflow, false);
        assert(next > offset);
        for (const char* p = page; *p; ++p) if (*p != ' ' && *p != '\n') actual += *p;
        offset = next;
        ++pages;
    }
    std::string expected;
    for (char c : text) if (c != ' ') expected += c;
    ASSERT_EQ(actual, expected);
    assert(pages > 100);
}

TEST(epub_page_respects_glyph_widths) {
    const char* text = "WiWi \xc3\xa9" "a";
    auto metrics = epub_test_metrics(text, 9, 1);
    epub_glyph_entry(*metrics.glyphs, 'W', 0, false)->width = 4;
    epub_glyph_entry(*metrics.glyphs, 'W', 'i', false)->width = 4;
    epub_glyph_entry(*metrics.glyphs, 0xe9, 0, false)->width = 3;
    epub_glyph_entry(*metrics.glyphs, 0xe9, 'a', false)->width = 3;
    char page[32];
    bool overflow = false;
    size_t offset = epub_make_page(text, strlen(text), 0, metrics, page, sizeof(page), &overflow);
    ASSERT_EQ(strcmp(page, "WiW\n"), 0);
    ASSERT_EQ(overflow, false);
    offset = epub_make_page(text, strlen(text), offset, metrics, page, sizeof(page), &overflow);
    ASSERT_EQ(strcmp(page, "i \xc3\xa9" "a\n"), 0);
    ASSERT_EQ(offset, strlen(text));
    metrics.line_width = 2;
    ASSERT_EQ(epub_make_page(text, strlen(text), 0, metrics, page, sizeof(page), &overflow),
              static_cast<size_t>(0));
}

TEST(epub_page_uses_actual_kerning) {
    const char* text = "AVAX";
    auto metrics = epub_test_metrics(text, 5, 1);
    epub_glyph_entry(*metrics.glyphs, 'A', 0, false)->width = 3;
    epub_glyph_entry(*metrics.glyphs, 'V', 0, false)->width = 3;
    epub_glyph_entry(*metrics.glyphs, 'A', 'V', false)->width = 2;
    char page[32];
    bool overflow = false;
    size_t offset = epub_make_page(text, strlen(text), 0, metrics, page, sizeof(page), &overflow);
    ASSERT_EQ(strcmp(page, "AV\n"), 0);
    ASSERT_EQ(offset, static_cast<size_t>(2));
    ASSERT_EQ(overflow, false);
}

TEST(epub_empty_chapter_navigation) {
    uint32_t spine = 0;
    ASSERT_EQ(epub_skip_empty_chapter(spine, 3, true, false, 1), EpubEmptyChapter::Restore);
    ASSERT_EQ(spine, static_cast<uint32_t>(1));
    spine = 2;
    ASSERT_EQ(epub_skip_empty_chapter(spine, 3, false, false, 1), EpubEmptyChapter::Restore);
    ASSERT_EQ(spine, static_cast<uint32_t>(1));
    spine = 0;
    ASSERT_EQ(epub_skip_empty_chapter(spine, 3, false, true, 0), EpubEmptyChapter::Continue);
    ASSERT_EQ(spine, static_cast<uint32_t>(1));
    spine = 2;
    ASSERT_EQ(epub_skip_empty_chapter(spine, 3, false, true, 0), EpubEmptyChapter::NoText);
}

// ===========================================================================
int main() {
    printf("=== widget_common tests ===\n");

    // int
    RUN(clamp_int_mid);
    RUN(clamp_int_below);
    RUN(clamp_int_above);
    RUN(clamp_int_at_lo);
    RUN(clamp_int_at_hi);
    RUN(clamp_int_negative_range);
    RUN(clamp_int_single_point);

    // uint8_t
    RUN(clamp_u8_mid);
    RUN(clamp_u8_below);
    RUN(clamp_u8_above);
    RUN(clamp_u8_at_lo);
    RUN(clamp_u8_at_hi);
    RUN(clamp_u8_bar_width);
    RUN(clamp_u8_arc_width);
    RUN(clamp_u8_tick_width);
    RUN(clamp_u8_tick_width_hi);

    // float
    RUN(clamp_float_mid);
    RUN(clamp_float_below);
    RUN(clamp_float_above);
    RUN(clamp_float_at_lo);
    RUN(clamp_float_at_hi);

    // uint16_t
    RUN(clamp_u16_mid);
    RUN(clamp_u16_below);
    RUN(clamp_u16_above);

    // int (degrees path)
    RUN(clamp_degrees_mid);
    RUN(clamp_degrees_lo);
    RUN(clamp_degrees_hi);
    RUN(epub_pages_preserve_utf8_and_words);
    RUN(epub_page_buffer_overflow_is_reported);
    RUN(epub_large_chapter_keeps_all_words);
    RUN(epub_page_respects_glyph_widths);
    RUN(epub_page_uses_actual_kerning);
    RUN(epub_empty_chapter_navigation);

    printf("\n%d / %d tests passed\n", g_passed, g_tests);
    return (g_passed == g_tests) ? 0 : 1;
}
