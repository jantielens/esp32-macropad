#include "image_decoder.h"

#if HAS_IMAGE_FETCH

#include "log_manager.h"
#include <esp_heap_caps.h>
#include <string.h>

// LVGL's built-in tjpgd (JPEG)
#include <libs/tjpgd/tjpgd.h>

// LVGL's built-in lodepng (PNG)
// Disable C++ overloads — they conflict with the extern "C" wrapping in
// LVGL's copy of lodepng.h.
#define LODEPNG_NO_COMPILE_CPP
#include <libs/lodepng/lodepng.h>

#define TAG "ImgDec"

// ============================================================================
// PSRAM allocation helper
// ============================================================================

static void* psram_alloc(size_t bytes) {
    if (bytes == 0) return nullptr;
    void* p = heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!p) p = malloc(bytes);  // fallback to internal
    return p;
}

// ============================================================================
// Format detection
// ============================================================================

ImageFormat image_detect_format(const uint8_t* data, size_t len) {
    if (!data || len < 4) return IMAGE_FORMAT_UNKNOWN;
    // JPEG: starts with FF D8 FF
    if (data[0] == 0xFF && data[1] == 0xD8 && data[2] == 0xFF) {
        return IMAGE_FORMAT_JPEG;
    }
    // PNG: starts with 89 50 4E 47 (‰PNG)
    if (data[0] == 0x89 && data[1] == 0x50 && data[2] == 0x4E && data[3] == 0x47) {
        return IMAGE_FORMAT_PNG;
    }
    return IMAGE_FORMAT_UNKNOWN;
}

// ============================================================================
// RGB565 conversion helpers
// ============================================================================

static inline uint16_t pack_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// ============================================================================
// Cover-mode scale: bilinear resample + center-crop → RGB565
// ============================================================================
// Source is RGB888 (3 bytes/pixel), output is RGB565 (2 bytes/pixel).
// Computes which region of the source to sample so the output fills
// target_w × target_h with center-cropping.

static bool cover_scale_rgb888_to_565(
    const uint8_t* src, int src_w, int src_h,
    uint16_t* dst, int dst_w, int dst_h)
{
    if (!src || !dst || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) return false;

    // Compute cover-mode scale factor
    float scale_x = (float)dst_w / (float)src_w;
    float scale_y = (float)dst_h / (float)src_h;
    float scale = (scale_x > scale_y) ? scale_x : scale_y;

    // Scaled source dimensions (one will match target, the other >= target)
    float scaled_w = src_w * scale;
    float scaled_h = src_h * scale;

    // Crop offset in scaled space (center crop)
    float crop_x = (scaled_w - dst_w) * 0.5f;
    float crop_y = (scaled_h - dst_h) * 0.5f;

    // For each output pixel, map back to source coordinates
    for (int dy = 0; dy < dst_h; dy++) {
        float src_yf = (dy + crop_y) / scale;
        int sy0 = (int)src_yf;
        float fy = src_yf - sy0;
        int sy1 = sy0 + 1;
        if (sy0 < 0) { sy0 = 0; fy = 0; }
        if (sy1 >= src_h) sy1 = src_h - 1;

        uint16_t* row_out = dst + (size_t)dy * dst_w;

        for (int dx = 0; dx < dst_w; dx++) {
            float src_xf = (dx + crop_x) / scale;
            int sx0 = (int)src_xf;
            float fx = src_xf - sx0;
            int sx1 = sx0 + 1;
            if (sx0 < 0) { sx0 = 0; fx = 0; }
            if (sx1 >= src_w) sx1 = src_w - 1;

            // Bilinear interpolation of 4 source pixels.
            // LVGL's tjpgd outputs BGR888 (byte order: B, G, R), not RGB888.
            const uint8_t* p00 = src + ((size_t)sy0 * src_w + sx0) * 3;
            const uint8_t* p10 = src + ((size_t)sy0 * src_w + sx1) * 3;
            const uint8_t* p01 = src + ((size_t)sy1 * src_w + sx0) * 3;
            const uint8_t* p11 = src + ((size_t)sy1 * src_w + sx1) * 3;

            float w00 = (1.0f - fx) * (1.0f - fy);
            float w10 = fx * (1.0f - fy);
            float w01 = (1.0f - fx) * fy;
            float w11 = fx * fy;

            uint8_t b = (uint8_t)(p00[0] * w00 + p10[0] * w10 + p01[0] * w01 + p11[0] * w11 + 0.5f);
            uint8_t g = (uint8_t)(p00[1] * w00 + p10[1] * w10 + p01[1] * w01 + p11[1] * w11 + 0.5f);
            uint8_t r = (uint8_t)(p00[2] * w00 + p10[2] * w10 + p01[2] * w01 + p11[2] * w11 + 0.5f);

            row_out[dx] = pack_rgb565(r, g, b);
        }

        // Yield periodically to avoid starving other tasks
        if ((dy & 0x0F) == 0) taskYIELD();
    }
    return true;
}

// Same but for RGBA8888 source (4 bytes/pixel, alpha discarded)
static bool cover_scale_rgba8888_to_565(
    const uint8_t* src, int src_w, int src_h,
    uint16_t* dst, int dst_w, int dst_h)
{
    if (!src || !dst || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) return false;

    float scale_x = (float)dst_w / (float)src_w;
    float scale_y = (float)dst_h / (float)src_h;
    float scale = (scale_x > scale_y) ? scale_x : scale_y;

    float scaled_w = src_w * scale;
    float scaled_h = src_h * scale;
    float crop_x = (scaled_w - dst_w) * 0.5f;
    float crop_y = (scaled_h - dst_h) * 0.5f;

    for (int dy = 0; dy < dst_h; dy++) {
        float src_yf = (dy + crop_y) / scale;
        int sy0 = (int)src_yf;
        float fy = src_yf - sy0;
        int sy1 = sy0 + 1;
        if (sy0 < 0) { sy0 = 0; fy = 0; }
        if (sy1 >= src_h) sy1 = src_h - 1;

        uint16_t* row_out = dst + (size_t)dy * dst_w;

        for (int dx = 0; dx < dst_w; dx++) {
            float src_xf = (dx + crop_x) / scale;
            int sx0 = (int)src_xf;
            float fx = src_xf - sx0;
            int sx1 = sx0 + 1;
            if (sx0 < 0) { sx0 = 0; fx = 0; }
            if (sx1 >= src_w) sx1 = src_w - 1;

            const uint8_t* p00 = src + ((size_t)sy0 * src_w + sx0) * 4;
            const uint8_t* p10 = src + ((size_t)sy0 * src_w + sx1) * 4;
            const uint8_t* p01 = src + ((size_t)sy1 * src_w + sx0) * 4;
            const uint8_t* p11 = src + ((size_t)sy1 * src_w + sx1) * 4;

            float w00 = (1.0f - fx) * (1.0f - fy);
            float w10 = fx * (1.0f - fy);
            float w01 = (1.0f - fx) * fy;
            float w11 = fx * fy;

            uint8_t r = (uint8_t)(p00[0] * w00 + p10[0] * w10 + p01[0] * w01 + p11[0] * w11 + 0.5f);
            uint8_t g = (uint8_t)(p00[1] * w00 + p10[1] * w10 + p01[1] * w01 + p11[1] * w11 + 0.5f);
            uint8_t b = (uint8_t)(p00[2] * w00 + p10[2] * w10 + p01[2] * w01 + p11[2] * w11 + 0.5f);

            row_out[dx] = pack_rgb565(r, g, b);
        }

        if ((dy & 0x0F) == 0) taskYIELD();
    }
    return true;
}

// ============================================================================
// JPEG decode via tjpgd
// ============================================================================

struct JpegInputCtx {
    const uint8_t* data;
    size_t size;
    size_t pos;
};

struct JpegOutputCtx {
    uint8_t* dst;   // RGB888 buffer
    int dst_w;
    int dst_h;
};

struct JpegSession {
    JpegInputCtx input;
    JpegOutputCtx output;
};

static size_t jpeg_input_func(JDEC* jd, uint8_t* buff, size_t nbyte) {
    JpegSession* s = (JpegSession*)jd->device;
    if (!s) return 0;
    JpegInputCtx* ctx = &s->input;
    if (ctx->pos >= ctx->size) return 0;

    size_t remain = ctx->size - ctx->pos;
    size_t n = (nbyte < remain) ? nbyte : remain;
    if (buff && n > 0) memcpy(buff, ctx->data + ctx->pos, n);
    ctx->pos += n;
    return n;
}

static int jpeg_output_func(JDEC* jd, void* bitmap, JRECT* rect) {
    JpegSession* s = (JpegSession*)jd->device;
    if (!s) return 0;
    JpegOutputCtx* out = &s->output;
    if (!out->dst) return 0;

    int rw = rect->right - rect->left + 1;
    int rh = rect->bottom - rect->top + 1;
    if (rw <= 0 || rh <= 0) return 0;
    if (rect->right >= (unsigned)out->dst_w || rect->bottom >= (unsigned)out->dst_h) return 0;

    uint8_t* src = (uint8_t*)bitmap;
    for (int row = 0; row < rh; row++) {
        int y = rect->top + row;
        uint8_t* dst_row = out->dst + ((size_t)y * out->dst_w + rect->left) * 3;
        memcpy(dst_row, src, rw * 3);
        src += rw * 3;
    }
    return 1;  // continue
}

static bool decode_jpeg(const uint8_t* data, size_t len,
                        uint8_t** out_rgb888, int* out_w, int* out_h) {
    static const size_t kWorkSize = 4096;
    void* work = psram_alloc(kWorkSize);
    if (!work) {
        LOGE(TAG, "JPEG: OOM work buffer");
        return false;
    }

    JpegSession session;
    memset(&session, 0, sizeof(session));
    session.input.data = data;
    session.input.size = len;

    JDEC jd;
    JRESULT res = jd_prepare(&jd, jpeg_input_func, work, kWorkSize, &session);
    if (res != JDR_OK) {
        LOGE(TAG, "JPEG prepare err=%d", (int)res);
        heap_caps_free(work);
        return false;
    }

    int w = (int)jd.width;
    int h = (int)jd.height;
    if (w <= 0 || h <= 0 || w > 4096 || h > 4096) {
        LOGE(TAG, "JPEG invalid dims %dx%d", w, h);
        heap_caps_free(work);
        return false;
    }

    size_t rgb_size = (size_t)w * h * 3;
    uint8_t* rgb = (uint8_t*)psram_alloc(rgb_size);
    if (!rgb) {
        LOGE(TAG, "JPEG: OOM for %dx%d RGB888 (%u bytes)", w, h, (unsigned)rgb_size);
        heap_caps_free(work);
        return false;
    }

    session.output.dst = rgb;
    session.output.dst_w = w;
    session.output.dst_h = h;

    // Re-prepare since jd_decomp needs a fresh state
    session.input.pos = 0;
    res = jd_prepare(&jd, jpeg_input_func, work, kWorkSize, &session);
    if (res != JDR_OK) {
        LOGE(TAG, "JPEG re-prepare err=%d", (int)res);
        heap_caps_free(rgb);
        heap_caps_free(work);
        return false;
    }

    res = jd_decomp(&jd, jpeg_output_func, 0);
    heap_caps_free(work);

    if (res != JDR_OK) {
        LOGE(TAG, "JPEG decomp err=%d", (int)res);
        heap_caps_free(rgb);
        return false;
    }

    *out_rgb888 = rgb;
    *out_w = w;
    *out_h = h;
    return true;
}

// ============================================================================
// PNG decode via lodepng
// ============================================================================

static bool decode_png(const uint8_t* data, size_t len,
                       uint8_t** out_rgba, int* out_w, int* out_h) {
    unsigned char* pixels = nullptr;
    unsigned pw = 0, ph = 0;

    unsigned err = lodepng_decode32(&pixels, &pw, &ph, data, len);
    if (err) {
        LOGE(TAG, "PNG err=%u: %s", err, lodepng_error_text(err));
        return false;
    }

    if (pw == 0 || ph == 0 || pw > 4096 || ph > 4096) {
        LOGE(TAG, "PNG invalid dims %ux%u", pw, ph);
        free(pixels);
        return false;
    }

    *out_rgba = pixels;
    *out_w = (int)pw;
    *out_h = (int)ph;
    return true;
}

// ============================================================================
// Public API
// ============================================================================

bool image_decode_to_rgb565(
    const uint8_t* data, size_t len,
    uint16_t target_w, uint16_t target_h,
    uint16_t** out_pixels, size_t* out_size)
{
    if (out_pixels) *out_pixels = nullptr;
    if (out_size) *out_size = 0;
    if (!data || len < 4 || target_w == 0 || target_h == 0) return false;

    ImageFormat fmt = image_detect_format(data, len);
    if (fmt == IMAGE_FORMAT_UNKNOWN) {
        LOGW(TAG, "Unknown image format (magic: %02X %02X)", data[0], data[1]);
        return false;
    }

    // Allocate output RGB565 buffer
    size_t out_bytes = (size_t)target_w * target_h * 2;
    uint16_t* out = (uint16_t*)psram_alloc(out_bytes);
    if (!out) {
        LOGE(TAG, "OOM for output %ux%u RGB565 (%u bytes)", target_w, target_h, (unsigned)out_bytes);
        return false;
    }

    bool ok = false;

    if (fmt == IMAGE_FORMAT_JPEG) {
        uint8_t* rgb888 = nullptr;
        int src_w = 0, src_h = 0;
        if (decode_jpeg(data, len, &rgb888, &src_w, &src_h)) {
            LOGD(TAG, "JPEG %dx%d → cover %ux%u", src_w, src_h, target_w, target_h);
            ok = cover_scale_rgb888_to_565(rgb888, src_w, src_h, out, target_w, target_h);
            heap_caps_free(rgb888);
        }
    } else if (fmt == IMAGE_FORMAT_PNG) {
        uint8_t* rgba = nullptr;
        int src_w = 0, src_h = 0;
        if (decode_png(data, len, &rgba, &src_w, &src_h)) {
            LOGD(TAG, "PNG %dx%d → cover %ux%u", src_w, src_h, target_w, target_h);
            ok = cover_scale_rgba8888_to_565(rgba, src_w, src_h, out, target_w, target_h);
            free(rgba);  // lodepng uses malloc
        }
    }

    if (!ok) {
        heap_caps_free(out);
        return false;
    }

    if (out_pixels) *out_pixels = out;
    if (out_size) *out_size = out_bytes;
    return true;
}

#endif // HAS_IMAGE_FETCH
