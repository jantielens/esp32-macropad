#include "board_config.h"
#if HAS_DISPLAY
#include "pad_font_cache.h"

#if HAS_TTF_FONTS
#include "font_store.h"
#include "log_manager.h"
#include "ota_activity.h"
#include <Arduino.h>
#include <esp_heap_caps.h>
#include <new>
#include <string.h>
#include <math.h>

struct PadFontCache::Asset {
    char name[FONT_NAME_MAX_LEN]{};
    uint8_t* data = nullptr;
    size_t length = 0;
    Asset* next = nullptr;
};

struct PadFontCache::Instance {
    Asset* asset = nullptr;
    uint16_t size = 0;
    float metrics = 0;
    const lv_font_t* fallback = nullptr;
    lv_font_t* font = nullptr;
    const void* (*bitmap)(lv_font_glyph_dsc_t*, lv_draw_buf_t*) = nullptr;
    Instance* next = nullptr;
};

static bool font_correct_metrics(lv_font_t* font, const uint8_t* data, size_t length,
                                 uint16_t size, float metrics) {
    if (!metrics) return true;
    double ascent = font->line_height - font->base_line;
    double descent = font->base_line;
    if (metrics == -1) {
        const FontTtfReader reader(length, [](void* context, size_t offset, uint8_t* target, size_t count) {
            memcpy(target, static_cast<const uint8_t*>(context) + offset, count);
            return true;
        }, const_cast<uint8_t*>(data));
        FontTtfTable table;
        uint8_t head[54];
        if (!reader.table("head", table) || table.length < sizeof(head) ||
            !reader.read(table.offset, head, sizeof(head))) return false;
        const unsigned units = FontTtfReader::read16(head + 18);
        const int minimum = static_cast<int16_t>(FontTtfReader::read16(head + 38));
        const int maximum = static_cast<int16_t>(FontTtfReader::read16(head + 42));
        if (!units || minimum > maximum) return false;
        const double scale = static_cast<double>(size) / units;
        ascent = fmax(ascent, ceil(maximum * scale) + 1);
        descent = fmax(descent, ceil(-minimum * scale) + 1);
    } else {
        if (!isfinite(metrics) || metrics < 1) return false;
        ascent = ceil(ascent * metrics);
        descent = ceil(descent * metrics);
    }
    const double height = ascent + descent;
    if (!isfinite(height) || ascent < 0 || descent < 0 || height < 1 || height > INT16_MAX) return false;
    font->line_height = static_cast<int32_t>(height);
    font->base_line = static_cast<int32_t>(descent);
    return true;
}

const lv_font_t* PadFontCache::resolve(const char* name, uint16_t size, const lv_font_t* fallback, float metrics) {
    if (!fallback || !font_store_validate_name(name) || ota_activity_is_active()) return fallback;
    if (!size) size = lv_font_get_line_height(fallback);
    Asset* asset = assets;
    while (asset && strcmp(asset->name, name) != 0) asset = asset->next;
    if (!asset) {
        asset = new (std::nothrow) Asset;
        if (!asset) {
            if (!allocation_warning_logged) LOGW("Fonts", "Cache %p: asset metadata allocation failed; using bitmap fallback", static_cast<void*>(this));
            allocation_warning_logged = true;
            return fallback;
        }
        strcpy(asset->name, name);
        asset->next = assets;
        assets = asset;
        asset->data = font_store_load(name, asset->length);
        if (!asset->data) LOGW("Fonts", "Cache %p: '%s' unavailable; using automatic bitmap fallback", static_cast<void*>(this), name);
    }
    if (!asset->data) return fallback;
    for (Instance* instance = instances; instance; instance = instance->next) {
        if (instance->asset == asset && instance->size == size && instance->fallback == fallback && instance->metrics == metrics)
            return instance->font ? instance->font : fallback;
    }
    Instance* instance = new (std::nothrow) Instance;
    if (!instance) {
        if (!allocation_warning_logged) LOGW("Fonts", "Cache %p: instance metadata allocation failed; using bitmap fallback", static_cast<void*>(this));
        allocation_warning_logged = true;
        return fallback;
    }
    instance->asset = asset;
    instance->size = size;
    instance->metrics = metrics;
    instance->fallback = fallback;
    instance->next = instances;
    instances = instance;
    const unsigned long started = millis();
    LOGD("Fonts", "Cache %p: create '%s' size=%u bytes=%zu; internal free=%zu largest=%zu; PSRAM free=%zu largest=%zu",
            static_cast<void*>(this), name, static_cast<unsigned>(size), asset->length,
            heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
            heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
            heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT),
            heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    instance->font = lv_tiny_ttf_create_data_ex(asset->data, asset->length, size, LV_FONT_KERNING_NORMAL, 16);
    if (!instance->font) {
        LOGW("Fonts", "Cache %p: '%s' size=%u creation failed; using automatic bitmap fallback", static_cast<void*>(this), name, static_cast<unsigned>(size));
        return fallback;
    }
    if (!font_correct_metrics(instance->font, asset->data, asset->length, size, metrics)) {
        LOGW("Fonts", "Cache %p: '%s' size=%u metrics=%g invalid or too large; using bitmap fallback", static_cast<void*>(this), name, static_cast<unsigned>(size), static_cast<double>(metrics));
        lv_tiny_ttf_destroy(instance->font);
        instance->font = nullptr;
        return fallback;
    }
    if (metrics) LOGD("Fonts", "Cache %p: '%s' metrics=%g line=%ld baseline=%ld", static_cast<void*>(this), name, static_cast<double>(metrics), static_cast<long>(instance->font->line_height), static_cast<long>(instance->font->base_line));
    LOGD("Fonts", "Cache %p: '%s' size=%u ready in %lums", static_cast<void*>(this), name, static_cast<unsigned>(size), millis() - started);
    instance->font->fallback = fallback;
    instance->bitmap = instance->font->get_glyph_bitmap;
    instance->font->user_data = instance;
    instance->font->get_glyph_bitmap = [](lv_font_glyph_dsc_t* glyph, lv_draw_buf_t* buffer) -> const void* {
        if (ota_activity_is_active()) return nullptr;
        const Instance* owner = static_cast<const Instance*>(glyph->resolved_font->user_data);
        return owner->bitmap(glyph, buffer);
    };
    return instance->font;
}

void PadFontCache::clear() {
    unsigned instance_count = 0;
    unsigned asset_count = 0;
    size_t source_bytes = 0;
    while (instances) {
        Instance* instance = instances;
        instances = instance->next;
        if (instance->font) lv_tiny_ttf_destroy(instance->font);
        delete instance;
        ++instance_count;
    }
    while (assets) {
        Asset* asset = assets;
        assets = asset->next;
        source_bytes += asset->length;
        heap_caps_free(asset->data);
        delete asset;
        ++asset_count;
    }
    if (asset_count || instance_count) LOGD("Fonts", "Cache %p: cleared %u assets, %u instances, %zu source bytes", static_cast<void*>(this), asset_count, instance_count, source_bytes);
    allocation_warning_logged = false;
}
#else
const lv_font_t* PadFontCache::resolve(const char*, uint16_t, const lv_font_t* fallback, float) { return fallback; }
void PadFontCache::clear() {}
#endif
#endif