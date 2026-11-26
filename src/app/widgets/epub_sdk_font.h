#pragma once

namespace epub_reader_impl {

class BuiltinFont : public freeink::font::RasterFont {
public:
    BuiltinFont(uint32_t generation, freeink::font::Arena& arena) : generation_(generation), arena_(arena) {}

    bool prepare() {
        slots_ = static_cast<Slot*>(heap_caps_calloc(slot_count, sizeof(Slot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        pixels_ = static_cast<uint8_t*>(heap_caps_malloc(pixel_capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        glyphs_ = arena_.allocArray<GlyphSlot>(glyph_slot_count);
        if (!slots_ || !pixels_ || !glyphs_) return false;
        for (size_t i = 0; i < glyph_slot_count; ++i) glyphs_[i] = GlyphSlot{};
        glyph_base_ = arena_.mark();
        return true;
    }

    ~BuiltinFont() override {
        heap_caps_free(slots_);
        heap_caps_free(pixels_);
    }

    bool ready() const { return slots_ && pixels_; }
    bool failed() const { return failed_; }

    int16_t advance(uint32_t code, uint16_t size, uint8_t) override {
        return width(code, 0, size);
    }
    int16_t kerning(uint32_t left, uint32_t right, uint16_t size, uint8_t) override {
        return width(left, right, size) - width(left, 0, size);
    }
    int16_t lineHeight(uint16_t size) override {
        return metrics(size).height;
    }
    int16_t ascent(uint16_t size) override {
        return metrics(size).ascent;
    }
    bool hasGlyph(uint32_t code) const override {
        bool available = false;
        width(code, 0, 14, &available);
        return available;
    }
    const freeink::font::GlyphBitmap* rasterize(uint32_t code, uint16_t size) override {
        const uint64_t key = (static_cast<uint64_t>(code) << 16) | size;
        GlyphSlot& slot = glyphs_[(code * 31 + size) % glyph_slot_count];
        if (slot.key == key && key) return &slot.bitmap;
        FontRequest request{};
        request.kind = FontRequestKind::Bitmap;
        request.code = code;
        request.size = size;
        request.pixels = pixels_;
        request.pixel_capacity = pixel_capacity;
        if (!query(request) || !request.available) return nullptr;
        const size_t count = static_cast<size_t>(request.bitmap.width) * request.bitmap.height;
        uint8_t* copy = count ? static_cast<uint8_t*>(arena_.alloc(count, 1)) : nullptr;
        if (count && !copy) {
            arena_.release(glyph_base_);
            for (size_t i = 0; i < glyph_slot_count; ++i) glyphs_[i] = GlyphSlot{};
            copy = static_cast<uint8_t*>(arena_.alloc(count, 1));
            if (!copy) { failed_ = true; return nullptr; }
        }
        if (count) memcpy(copy, pixels_, count);
        slot.key = key;
        slot.bitmap = request.bitmap;
        slot.bitmap.pixels = copy;
        return &slot.bitmap;
    }

private:
    struct Slot { uint64_t key; int16_t width; bool available; };
    struct GlyphSlot { uint64_t key = 0; freeink::font::GlyphBitmap bitmap{}; };
    struct Metrics { uint16_t size; int16_t height; int16_t ascent; };
    static constexpr size_t slot_count = 4096;
    static constexpr size_t pixel_capacity = 4096;
    static constexpr size_t glyph_slot_count = 128;

    bool query(FontRequest& request) const {
        if (!ready() || !current(generation_) || ota_activity_is_active()) {
            failed_ = true;
            return false;
        }
        request.generation = generation_;
        if (xSemaphoreTake(service->mutex, portMAX_DELAY) != pdTRUE) {
            failed_ = true;
            return false;
        }
        if (!current(generation_)) {
            xSemaphoreGive(service->mutex);
            failed_ = true;
            return false;
        }
        service->font_request = request;
        xSemaphoreGive(service->mutex);
        const uint32_t epoch = ota_activity_epoch();
        bool done = false;
        while (current(generation_) && !ota_activity_is_active() && ota_activity_epoch() == epoch) {
            if (xSemaphoreTake(service->mutex, portMAX_DELAY) == pdTRUE) {
                if (service->font_request.generation == generation_ && service->font_request.done) {
                    request = service->font_request;
                    service->font_request.kind = FontRequestKind::None;
                    done = true;
                }
                xSemaphoreGive(service->mutex);
            }
            if (done) break;
            vTaskDelay(pdMS_TO_TICKS(1));
        }
        if (!done) {
            if (xSemaphoreTake(service->mutex, portMAX_DELAY) == pdTRUE) {
                if (service->font_request.generation == generation_)
                    service->font_request.kind = FontRequestKind::None;
                xSemaphoreGive(service->mutex);
            }
            failed_ = true;
        }
        return done;
    }

    int16_t width(uint32_t code, uint32_t next, uint16_t size, bool* available = nullptr) const {
        if (!slots_ || code > 0x1fffff || next > 0x1fffff) { failed_ = true; return 0; }
        const uint64_t key = (static_cast<uint64_t>(size) << 42) |
                             (static_cast<uint64_t>(code) << 21) | next;
        Slot& slot = slots_[(code * 16777619u ^ next * 2166136261u ^ size) & (slot_count - 1)];
        if (slot.key == key && key) {
            if (available) *available = slot.available;
            return slot.width;
        }
        FontRequest request{};
        request.kind = FontRequestKind::Width;
        request.code = code;
        request.next = next;
        request.size = size;
        if (!query(request)) return 0;
        slot = {key, request.width, request.available};
        if (available) *available = request.available;
        return request.width;
    }

    Metrics metrics(uint16_t size) {
        Metrics& cached = metrics_[size % 32];
        if (cached.size == size && size) return cached;
        FontRequest request{};
        request.kind = FontRequestKind::Metrics;
        request.size = size;
        if (!query(request)) return {};
        cached = {size, request.height, request.ascent};
        return cached;
    }

    uint32_t generation_;
    freeink::font::Arena& arena_;
    Slot* slots_ = nullptr;
    uint8_t* pixels_ = nullptr;
    GlyphSlot* glyphs_ = nullptr;
    size_t glyph_base_ = 0;
    Metrics metrics_[32]{};
    mutable bool failed_ = false;
};

void process_font_request() {
    FontRequest& request = service->font_request;
    if (request.kind == FontRequestKind::None || request.done ||
        !current(request.generation) || ota_activity_is_active()) return;
    const lv_font_t* font = pad_nearest_bitmap_font(request.size ? request.size : 14);
    if (request.kind == FontRequestKind::Metrics) {
        request.height = lv_font_get_line_height(font);
        request.ascent = font->line_height - font->base_line;
        request.available = true;
    } else {
        lv_font_glyph_dsc_t glyph{};
        request.available = lv_font_get_glyph_dsc(font, &glyph, request.code, request.next);
        request.width = request.available ? glyph.adv_w : 0;
        if (request.available && request.kind == FontRequestKind::Bitmap) {
            const uint8_t* bitmap = nullptr;
            uint8_t bpp = 0;
            if (lv_font_has_static_bitmap(glyph.resolved_font)) {
                switch (glyph.format) {
                    case LV_FONT_GLYPH_FORMAT_A1: bpp = 1; break;
                    case LV_FONT_GLYPH_FORMAT_A2: bpp = 2; break;
                    case LV_FONT_GLYPH_FORMAT_A4: bpp = 4; break;
                    case LV_FONT_GLYPH_FORMAT_A8: bpp = 8; break;
                    default: break;
                }
                if (bpp) bitmap = static_cast<const uint8_t*>(lv_font_get_glyph_static_bitmap(&glyph));
            }
            const size_t count = static_cast<size_t>(glyph.box_w) * glyph.box_h;
            if (count > request.pixel_capacity || (count && !bitmap)) {
                request.available = false;
            } else {
                const uint32_t mask = (1u << bpp) - 1u;
                for (uint32_t y = 0; y < glyph.box_h; ++y) {
                    for (uint32_t x = 0; x < glyph.box_w; ++x) {
                        const size_t bit = glyph.stride ? static_cast<size_t>(y) * glyph.stride * 8 + x * bpp
                                                        : (static_cast<size_t>(y) * glyph.box_w + x) * bpp;
                        request.pixels[static_cast<size_t>(y) * glyph.box_w + x] =
                            ((bitmap[bit / 8] >> (8 - bpp - bit % 8)) & mask) * 255u / mask;
                    }
                }
                request.bitmap = {request.pixels, glyph.box_w, glyph.box_h, glyph.ofs_x,
                                  static_cast<int16_t>(-glyph.box_h - glyph.ofs_y),
                                  static_cast<int16_t>(glyph.adv_w)};
            }
        }
        if (glyph.resolved_font) lv_font_glyph_release_draw_data(&glyph);
    }
    request.done = true;
}

}  // namespace epub_reader_impl
