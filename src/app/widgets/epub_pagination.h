#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

constexpr size_t epub_glyph_slots = 8192;
constexpr size_t epub_glyph_limit = epub_glyph_slots * 3 / 4;

struct EpubGlyphEntry {
    uint64_t key;
    uint16_t width;
};

struct EpubGlyphCache {
    EpubGlyphEntry entries[epub_glyph_slots];
    size_t count;
};

inline uint32_t epub_decode_utf8(const char* text, size_t length, size_t& offset) {
    const uint8_t first = static_cast<uint8_t>(text[offset++]);
    if (first < 0x80) return first;
    unsigned extra = first >= 0xf0 && first <= 0xf4 ? 3 :
                     first >= 0xe0 && first <= 0xef ? 2 :
                     first >= 0xc2 && first <= 0xdf ? 1 : 0;
    if (!extra || length - offset < extra) return 0xfffd;
    uint32_t code = first & (0x7f >> (extra + 1));
    for (unsigned i = 0; i < extra; ++i) {
        const uint8_t c = static_cast<uint8_t>(text[offset + i]);
        if ((c & 0xc0) != 0x80) return 0xfffd;
        code = (code << 6) | (c & 0x3f);
    }
    offset += extra;
    return code;
}

inline EpubGlyphEntry* epub_glyph_entry(EpubGlyphCache& cache, uint32_t code, uint32_t next, bool insert) {
    const uint64_t key = (static_cast<uint64_t>(code) << 21) | next;
    if (!key) return nullptr;
    size_t slot = (code * 16777619u ^ next * 2166136261u) & (epub_glyph_slots - 1);
    for (size_t i = 0; i < epub_glyph_slots; ++i) {
        EpubGlyphEntry& entry = cache.entries[slot];
        if (entry.key == key) return &entry;
        if (!entry.key) {
            if (!insert || cache.count == epub_glyph_limit) return nullptr;
            entry.key = key;
            ++cache.count;
            return &entry;
        }
        slot = (slot + 1) & (epub_glyph_slots - 1);
    }
    return nullptr;
}

struct EpubPageMetrics {
    EpubGlyphCache* glyphs;
    uint16_t line_width;
    uint16_t rows;
};

enum class EpubEmptyChapter { Continue, Restore, NoText };

inline EpubEmptyChapter epub_skip_empty_chapter(uint32_t& spine, uint32_t spine_count,
                                                bool backwards, bool initial, uint32_t previous_spine) {
    if (backwards && spine > 0) { --spine; return EpubEmptyChapter::Continue; }
    if (!backwards && spine + 1 < spine_count) { ++spine; return EpubEmptyChapter::Continue; }
    if (!initial) { spine = previous_spine; return EpubEmptyChapter::Restore; }
    return EpubEmptyChapter::NoText;
}

inline size_t epub_make_page(const char* text, size_t length, size_t start,
                             const EpubPageMetrics& metrics, char* out,
                             size_t capacity, bool* overflow) {
    size_t at = start, used = 0;
    *overflow = false;
    for (uint16_t row = 0; row < metrics.rows && at < length; ++row) {
        while (at < length && text[at] == ' ') ++at;
        const size_t line_start = at;
        size_t end = at, last_space = at;
        uint32_t width = 0, previous = 0, previous_width = 0;
        while (end < length && text[end] != '\n') {
            size_t next_end = end;
            const uint32_t c = epub_decode_utf8(text, length, next_end);
            EpubGlyphEntry* single = epub_glyph_entry(*metrics.glyphs, c, 0, false);
            EpubGlyphEntry* pair = previous ? epub_glyph_entry(*metrics.glyphs, previous, c, false) : nullptr;
            if (!single || (previous && !pair)) return start;
            const uint32_t candidate = width - previous_width + (pair ? pair->width : 0) + single->width;
            if (candidate > metrics.line_width) break;
            width = candidate;
            previous = c;
            previous_width = single->width;
            if (text[end] == ' ') last_space = end;
            end = next_end;
        }
        if (end < length && text[end] != '\n' && text[end] != ' ' && last_space > line_start) end = last_space;
        if (end == line_start && at < length && text[at] != '\n') return start;
        const size_t bytes = end - line_start;
        if (out) {
            if (used + bytes + 2 > capacity) { *overflow = true; return start; }
            memcpy(out + used, text + line_start, bytes);
            used += bytes;
            out[used++] = '\n';
        }
        at = end;
        if (at < length && (text[at] == '\n' || text[at] == ' ')) ++at;
    }
    if (out) out[used] = 0;
    return at;
}
