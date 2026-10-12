#include "freetype_mono_font.h"

#if HAS_DISPLAY && HAS_TTF_FONTS && HAS_FREETYPE_FONTS
#include <MacropadFreeType.h>
#include "ota_activity.h"
#include <limits.h>
#include <new>

namespace {
constexpr size_t glyph_cache_size = 16;

struct MonoGlyph {
    uint32_t index = 0;
    uint32_t age = 0;
    unsigned references = 0;
    bool valid = false;
    FT_Pos left_delta = 0;
    FT_Pos right_delta = 0;
    lv_font_glyph_dsc_t descriptor{};
    lv_draw_buf_t* bitmap = nullptr;
};

struct MonoFont {
    lv_font_t font{};
    FT_MemoryRec_ memory{};
    FT_Library library = nullptr;
    FT_Face face = nullptr;
    const uint8_t* data = nullptr;
    size_t length = 0;
    uint16_t size = 0;
    bool allocation_failed = false;
    uint32_t clock = 0;
    MonoGlyph glyphs[glyph_cache_size];
};

void* font_allocate(FT_Memory memory, long size) {
    void* block = size > 0 ? lv_malloc(static_cast<size_t>(size)) : nullptr;
    if (size > 0 && !block) static_cast<MonoFont*>(memory->user)->allocation_failed = true;
    return block;
}

void font_free(FT_Memory, void* block) {
    lv_free(block);
}

void* font_reallocate(FT_Memory memory, long, long size, void* block) {
    void* resized = size > 0 ? lv_realloc(block, static_cast<size_t>(size)) : nullptr;
    if (size > 0 && !resized) static_cast<MonoFont*>(memory->user)->allocation_failed = true;
    return resized;
}

void close_face(MonoFont* owner) {
    if (owner->face) FT_Done_Face(owner->face);
    if (owner->library) FT_Done_Library(owner->library);
    owner->face = nullptr;
    owner->library = nullptr;
}

bool prepare_face(MonoFont* owner) {
    if (owner->face && !owner->allocation_failed) return true;
    close_face(owner);
    owner->allocation_failed = false;
    if (!FT_New_Library(&owner->memory, &owner->library)) {
        FT_Add_Default_Modules(owner->library);
        if (!owner->allocation_failed &&
            !FT_New_Memory_Face(owner->library, owner->data, static_cast<FT_Long>(owner->length), 0, &owner->face) &&
            owner->face->units_per_EM && !FT_Set_Pixel_Sizes(owner->face, 0, owner->size) &&
            !owner->allocation_failed) return true;
    }
    close_face(owner);
    return false;
}

void destroy_bitmap(lv_draw_buf_t* bitmap) {
    if (!bitmap) return;
    lv_free(bitmap->unaligned_data);
    lv_free(bitmap);
}

lv_draw_buf_t* create_bitmap(unsigned width, unsigned height) {
    const uint32_t stride = lv_draw_buf_width_to_stride(width, LV_COLOR_FORMAT_A8);
    const uint64_t bytes = static_cast<uint64_t>(stride) * height + LV_DRAW_BUF_ALIGN;
    if (stride > UINT16_MAX || bytes > UINT32_MAX) return nullptr;
    auto* bitmap = static_cast<lv_draw_buf_t*>(lv_malloc(sizeof(lv_draw_buf_t)));
    if (!bitmap) return nullptr;
    void* pixels = lv_malloc(static_cast<size_t>(bytes));
    if (!pixels) {
        lv_free(bitmap);
        return nullptr;
    }
    auto* aligned = static_cast<uint8_t*>(lv_draw_buf_align(pixels, LV_COLOR_FORMAT_A8));
    const size_t available = static_cast<size_t>(bytes) - (aligned - static_cast<uint8_t*>(pixels));
    if (lv_draw_buf_init(bitmap, width, height, LV_COLOR_FORMAT_A8, stride, aligned, available) != LV_RESULT_OK) {
        lv_free(pixels);
        lv_free(bitmap);
        return nullptr;
    }
    bitmap->unaligned_data = pixels;
    return bitmap;
}

MonoGlyph* find_glyph(MonoFont* owner, uint32_t index) {
    if (ota_activity_is_active()) return nullptr;
    MonoGlyph* replacement = nullptr;
    for (MonoGlyph& cached : owner->glyphs) {
        if (cached.valid && cached.index == index) {
            cached.age = ++owner->clock;
            return &cached;
        }
        if (!cached.references && (!replacement || !cached.valid ||
                                  (replacement->valid && cached.age < replacement->age)))
            replacement = &cached;
    }
    if (!replacement || !prepare_face(owner)) return nullptr;
    if (FT_Load_Glyph(owner->face, index, FT_LOAD_FORCE_AUTOHINT | FT_LOAD_TARGET_MONO | FT_LOAD_NO_BITMAP) ||
        owner->allocation_failed || FT_Render_Glyph(owner->face->glyph, FT_RENDER_MODE_MONO) ||
        owner->allocation_failed) return nullptr;
    const FT_GlyphSlot slot = owner->face->glyph;
    const FT_Bitmap& source = slot->bitmap;
    const FT_Pos advance = (slot->advance.x + 32) / 64;
    const int offset_y = slot->bitmap_top - static_cast<int>(source.rows);
    if (source.pixel_mode != FT_PIXEL_MODE_MONO || source.width > UINT16_MAX || source.rows > UINT16_MAX ||
        advance < 0 || advance > UINT16_MAX || slot->bitmap_left < INT16_MIN || slot->bitmap_left > INT16_MAX ||
        offset_y < INT16_MIN || offset_y > INT16_MAX) return nullptr;
    lv_draw_buf_t* bitmap = nullptr;
    if (source.width && source.rows) {
        bitmap = create_bitmap(source.width, source.rows);
        if (!bitmap) return nullptr;
        lv_draw_buf_clear(bitmap, nullptr);
        const unsigned pitch = source.pitch < 0 ? static_cast<unsigned>(-source.pitch) : static_cast<unsigned>(source.pitch);
        for (unsigned row = 0; row < source.rows; ++row) {
            const unsigned source_row = source.pitch < 0 ? source.rows - 1 - row : row;
            const uint8_t* input = source.buffer + source_row * pitch;
            uint8_t* output = bitmap->data + row * bitmap->header.stride;
            for (unsigned column = 0; column < source.width; ++column)
                output[column] = (input[column / 8] & (0x80 >> (column & 7))) ? 255 : 0;
        }
        lv_draw_buf_flush_cache(bitmap, nullptr);
    }
    destroy_bitmap(replacement->bitmap);
    *replacement = MonoGlyph{};
    replacement->index = index;
    replacement->age = ++owner->clock;
    replacement->valid = true;
    replacement->left_delta = slot->lsb_delta;
    replacement->right_delta = slot->rsb_delta;
    replacement->bitmap = bitmap;
    replacement->descriptor.adv_w = static_cast<uint16_t>(advance);
    replacement->descriptor.box_w = static_cast<uint16_t>(source.width);
    replacement->descriptor.box_h = static_cast<uint16_t>(source.rows);
    replacement->descriptor.ofs_x = static_cast<int16_t>(slot->bitmap_left);
    replacement->descriptor.ofs_y = static_cast<int16_t>(offset_y);
    replacement->descriptor.stride = bitmap ? bitmap->header.stride : 0;
    replacement->descriptor.format = LV_FONT_GLYPH_FORMAT_A8;
    replacement->descriptor.gid.index = index;
    return replacement;
}

bool glyph_descriptor(const lv_font_t* font, lv_font_glyph_dsc_t* descriptor,
                      uint32_t character, uint32_t next_character) {
    if (ota_activity_is_active()) return false;
    if (character < 0x20) {
        *descriptor = lv_font_glyph_dsc_t{};
        descriptor->format = LV_FONT_GLYPH_FORMAT_NONE;
        return true;
    }
    auto* owner = static_cast<MonoFont*>(const_cast<void*>(font->dsc));
    if (!prepare_face(owner)) return false;
    const FT_UInt index = FT_Get_Char_Index(owner->face, character);
    if (!index) return false;
    const MonoGlyph* cached = find_glyph(owner, index);
    if (!cached) return false;
    *descriptor = cached->descriptor;
    const FT_Pos right_delta = cached->right_delta;
    int advance = descriptor->adv_w;
    if (next_character >= 0x20 && font->kerning == LV_FONT_KERNING_NORMAL) {
        const FT_UInt next_index = FT_Get_Char_Index(owner->face, next_character);
        const MonoGlyph* next = next_index ? find_glyph(owner, next_index) : nullptr;
        if (next) {
            const FT_Pos difference = right_delta - next->left_delta;
            if (difference > 32) --advance;
            else if (difference < -31) ++advance;
            FT_Vector kerning{};
            if (!FT_Get_Kerning(owner->face, index, next_index, FT_KERNING_DEFAULT, &kerning))
                advance += static_cast<int>(kerning.x / 64);
        }
    }
    if (owner->allocation_failed || advance < 0 || advance > UINT16_MAX) return false;
    descriptor->adv_w = static_cast<uint16_t>(advance);
    return true;
}

const void* glyph_bitmap(lv_font_glyph_dsc_t* descriptor, lv_draw_buf_t*) {
    auto* owner = static_cast<MonoFont*>(const_cast<void*>(descriptor->resolved_font->dsc));
    MonoGlyph* cached = find_glyph(owner, descriptor->gid.index);
    if (!cached || !cached->bitmap) return nullptr;
    ++cached->references;
    descriptor->entry = reinterpret_cast<lv_cache_entry_t*>(cached);
    return cached->bitmap;
}

void release_glyph(const lv_font_t*, lv_font_glyph_dsc_t* descriptor) {
    if (descriptor->entry) {
        auto* cached = reinterpret_cast<MonoGlyph*>(descriptor->entry);
        if (cached->references) --cached->references;
        descriptor->entry = nullptr;
    }
}
}

lv_font_t* freetype_mono_font_create(const uint8_t* data, size_t length, uint16_t size) {
    if (FT_AutoMono_Heap_Workspaces() != 1) return nullptr;
    if (!data || !length || length > LONG_MAX || !size || ota_activity_is_active()) return nullptr;
    void* storage = lv_malloc(sizeof(MonoFont));
    if (!storage) return nullptr;
    auto* owner = new (storage) MonoFont;
    owner->data = data;
    owner->length = length;
    owner->size = size;
    owner->memory.user = owner;
    owner->memory.alloc = font_allocate;
    owner->memory.free = font_free;
    owner->memory.realloc = font_reallocate;
    if (!prepare_face(owner)) {
        freetype_mono_font_destroy(&owner->font);
        return nullptr;
    }
    owner->font.dsc = owner;
    owner->font.get_glyph_dsc = glyph_descriptor;
    owner->font.get_glyph_bitmap = glyph_bitmap;
    owner->font.release_glyph = release_glyph;
    owner->font.kerning = LV_FONT_KERNING_NORMAL;
    owner->font.line_height = static_cast<int32_t>(static_cast<int64_t>(size) * owner->face->height / owner->face->units_per_EM);
    owner->font.base_line = static_cast<int32_t>(static_cast<int64_t>(size) * (owner->face->height - owner->face->ascender) / owner->face->units_per_EM);
    return &owner->font;
}

void freetype_mono_font_destroy(lv_font_t* font) {
    if (!font) return;
    auto* owner = reinterpret_cast<MonoFont*>(font);
    for (MonoGlyph& glyph : owner->glyphs)
        destroy_bitmap(glyph.bitmap);
    close_face(owner);
    owner->~MonoFont();
    lv_free(owner);
}
#endif