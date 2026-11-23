#ifndef FILL_DRAW_H
#define FILL_DRAW_H

#include "fill_pattern.h"
#include "board_config.h"
#include <lvgl.h>
#include <math.h>

#if LVGL_CACHE_PATTERNS
#include <esp_heap_caps.h>
#include <misc/cache/instance/lv_image_cache.h>
#include "log_manager.h"
#endif

inline bool fill_rounded_pixel(const lv_area_t& area, int radius, int horizontal, int vertical) {
    if (horizontal < area.x1 || horizontal > area.x2 || vertical < area.y1 || vertical > area.y2) return false;
    int limit = (lv_area_get_width(&area) < lv_area_get_height(&area) ?
                 lv_area_get_width(&area) : lv_area_get_height(&area)) / 2;
    if (radius > limit) radius = limit;
    if (radius <= 0) return true;
    int delta_x = horizontal < area.x1 + radius ? area.x1 + radius - horizontal :
                  horizontal > area.x2 - radius ? horizontal - area.x2 + radius : 0;
    int delta_y = vertical < area.y1 + radius ? area.y1 + radius - vertical :
                  vertical > area.y2 - radius ? vertical - area.y2 + radius : 0;
    return !delta_x || !delta_y || delta_x * delta_x + delta_y * delta_y <= radius * radius;
}

#if LVGL_CACHE_PATTERNS
// All cache allocations and accounting are owned by the LVGL task.
inline size_t& fill_pattern_cache_used() {
    static size_t used = 0;
    return used;
}

inline void* fill_pattern_cache_alloc(size_t bytes, size_t& charge) {
    constexpr size_t allocator_overhead = 64;
    constexpr uint32_t caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    const size_t used = fill_pattern_cache_used();
    const size_t cap = LVGL_PATTERN_CACHE_BYTES;
    const size_t reserve = LVGL_PATTERN_CACHE_RESERVE;
    const size_t free_bytes = heap_caps_get_free_size(caps);
    if (used > cap || bytes > cap - used || allocator_overhead > cap - used - bytes ||
        free_bytes < reserve || bytes > free_bytes - reserve ||
        allocator_overhead > free_bytes - reserve - bytes) return nullptr;
    void* memory = heap_caps_malloc(bytes, caps);
    if (!memory) return nullptr;
    const size_t allocated = heap_caps_get_allocated_size(memory);
    if (allocated > cap - used - allocator_overhead ||
        heap_caps_get_free_size(caps) < reserve) {
        heap_caps_free(memory);
        return nullptr;
    }
    charge = allocated + allocator_overhead;
    fill_pattern_cache_used() += charge;
    return memory;
}

inline void fill_pattern_cache_free(void* memory, size_t charge) {
    if (!memory) return;
    heap_caps_free(memory);
    fill_pattern_cache_used() -= charge;
}

struct FillPatternCache {
    lv_draw_buf_t mask{};
    lv_image_dsc_t image{};
    void* pixels = nullptr;
    size_t pixel_charge = 0;
    int width = 0;
    int height = 0;
    int radius = 0;
    lv_point_t offset{};
    FillPattern pattern = FillPattern::None;
    int spacing = 0;
    int stroke = 0;
    bool attempted = false;

    FillPatternCache() = default;
    FillPatternCache(const FillPatternCache&) = delete;
    FillPatternCache& operator=(const FillPatternCache&) = delete;
    ~FillPatternCache() { clear(); }

    void clear() {
        if (pixels) {
            lv_image_cache_drop(&image);
            fill_pattern_cache_free(pixels, pixel_charge);
            pixels = nullptr;
            pixel_charge = 0;
        }
        attempted = false;
    }

    bool prepare(const lv_area_t& bounds, int new_radius, const lv_point_t& origin,
                 FillPattern new_pattern, int new_spacing, int new_stroke) {
        const int new_width = lv_area_get_width(&bounds);
        const int new_height = lv_area_get_height(&bounds);
        const lv_point_t new_offset = {bounds.x1 - origin.x, bounds.y1 - origin.y};
        if (attempted && width == new_width && height == new_height &&
            radius == new_radius && offset.x == new_offset.x && offset.y == new_offset.y &&
            pattern == new_pattern && spacing == new_spacing && stroke == new_stroke) return pixels != nullptr;
        clear();
        attempted = true;
        width = new_width;
        height = new_height;
        radius = new_radius;
        offset = new_offset;
        pattern = new_pattern;
        spacing = new_spacing;
        stroke = new_stroke;
        if (width <= 0 || height <= 0 || width > UINT16_MAX || height > UINT16_MAX) {
            LOGW("Fill", "Pattern cache dimensions invalid (%dx%d); using uncached drawing", width, height);
            return false;
        }
        const uint32_t stride = lv_draw_buf_width_to_stride(width, LV_COLOR_FORMAT_A8);
        if (!stride || stride > UINT16_MAX ||
            size_t(height) > (SIZE_MAX - LV_DRAW_BUF_ALIGN) / stride ||
            uint32_t(height) > UINT32_MAX / stride) {
            LOGW("Fill", "Pattern cache dimensions invalid (%dx%d); using uncached drawing", width, height);
            return false;
        }
        const size_t bytes = size_t(stride) * size_t(height);
        pixels = fill_pattern_cache_alloc(bytes + LV_DRAW_BUF_ALIGN, pixel_charge);
        if (!pixels) {
            LOGW("Fill", "Pattern cache unavailable (%dx%d); using uncached drawing", width, height);
            return false;
        }
        auto* aligned = static_cast<uint8_t*>(lv_draw_buf_align(pixels, LV_COLOR_FORMAT_A8));
        const size_t padding = reinterpret_cast<uintptr_t>(aligned) - reinterpret_cast<uintptr_t>(pixels);
        if (padding > LV_DRAW_BUF_ALIGN ||
            lv_draw_buf_init(&mask, width, height, LV_COLOR_FORMAT_A8, stride, aligned, bytes) != LV_RESULT_OK) {
            LOGW("Fill", "Pattern cache initialization failed (%dx%d); using uncached drawing", width, height);
            fill_pattern_cache_free(pixels, pixel_charge);
            pixels = nullptr;
            pixel_charge = 0;
            return false;
        }
        memset(mask.data, 0, bytes);
        const lv_area_t local = {0, 0, width - 1, height - 1};
        for (int y = 0; y < height; ++y) {
            uint8_t* row = mask.data + y * stride;
            for (int x = 0; x < width; ++x)
                row[x] = fill_rounded_pixel(local, radius, x, y) &&
                         fill_pattern_pixel(pattern, x + offset.x, y + offset.y, spacing, stroke) ? 255 : 0;
#if defined(ARDUINO_ARCH_ESP32)
            if ((y & 15) == 15) taskYIELD();
#endif
        }
        lv_draw_buf_to_image(&mask, &image);
        return true;
    }
};
#endif

inline void fill_draw_pattern(lv_layer_t* layer, const lv_area_t& bounds, int radius,
                              const lv_point_t& origin, FillPattern pattern,
                              lv_color_t color, int spacing, int stroke
#if LVGL_CACHE_PATTERNS
                              , FillPatternCache* cache = nullptr
#endif
                              ) {
    if (pattern == FillPattern::None) return;
    const lv_area_t& clip = layer->_clip_area;
    lv_area_t visible = {bounds.x1 > clip.x1 ? bounds.x1 : clip.x1,
                         bounds.y1 > clip.y1 ? bounds.y1 : clip.y1,
                         bounds.x2 < clip.x2 ? bounds.x2 : clip.x2,
                         bounds.y2 < clip.y2 ? bounds.y2 : clip.y2};
    if (visible.x1 > visible.x2 || visible.y1 > visible.y2) return;
#if LVGL_CACHE_PATTERNS
    if (cache && cache->prepare(bounds, radius, origin, pattern, spacing, stroke)) {
        lv_draw_image_dsc_t image;
        lv_draw_image_dsc_init(&image);
        image.src = &cache->image;
        image.recolor = color;
        image.recolor_opa = LV_OPA_COVER;
        lv_draw_image(layer, &image, &bounds);
        return;
    }
#endif
    lv_draw_rect_dsc_t descriptor;
    lv_draw_rect_dsc_init(&descriptor);
    descriptor.bg_color = color;
    descriptor.bg_opa = LV_OPA_COVER;
    for (int vertical = visible.y1; vertical <= visible.y2; ++vertical) {
        int run_start = 0;
        bool run_open = false;
        for (int horizontal = visible.x1; horizontal <= visible.x2 + 1; ++horizontal) {
            bool marked = horizontal <= visible.x2 &&
                fill_rounded_pixel(bounds, radius, horizontal, vertical) &&
                fill_pattern_pixel(pattern, horizontal - origin.x, vertical - origin.y, spacing, stroke);
            if (marked && !run_open) {
                run_start = horizontal;
                run_open = true;
            }
            if (!marked && run_open) {
                lv_area_t run = {run_start, vertical, horizontal - 1, vertical};
                lv_draw_rect(layer, &descriptor, &run);
                run_open = false;
            }
        }
    }
}

inline void fill_draw_arc_outline(lv_layer_t* layer, const lv_point_t& center,
                                  int radius, int thickness, int start, int sweep,
                                  int width, lv_color_t color) {
    if (width <= 0 || radius <= 0 || thickness <= 0) return;
    int limit = thickness / 2;
    if (limit < 1) limit = 1;
    if (width > limit) width = limit;
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.center = center;
    arc.color = color;
    arc.width = width;
    arc.radius = radius;
    arc.start_angle = start;
    arc.end_angle = start + sweep;
    lv_draw_arc(layer, &arc);
    int inner_radius = radius - thickness + width;
    if (inner_radius > 0 && inner_radius < radius) {
        arc.radius = inner_radius;
        lv_draw_arc(layer, &arc);
    }
    if (sweep >= 360) return;
    lv_draw_line_dsc_t line;
    lv_draw_line_dsc_init(&line);
    line.color = color;
    line.width = width;
    for (unsigned end = 0; end < 2; ++end) {
        float angle = (start + (end ? sweep : 0)) * 0.017453292519943295f;
        float outer = radius - width * 0.5f;
        float inner = radius - thickness + width * 0.5f;
        line.p1.x = center.x + cosf(angle) * inner;
        line.p1.y = center.y + sinf(angle) * inner;
        line.p2.x = center.x + cosf(angle) * outer;
        line.p2.y = center.y + sinf(angle) * outer;
        lv_draw_line(layer, &line);
    }
}

#endif