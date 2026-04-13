#include "image_decoder.h"

#if HAS_IMAGE_FETCH

#include "log_manager.h"
#include <esp_heap_caps.h>
#include <string.h>
#include <math.h>

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
// ESP32-P4: Hardware JPEG decoder + PPA scaling
// ============================================================================
// Replaces software tjpgd decode + CPU bilinear scale for JPEG images.
//
// Pipeline (P4 only):
//   1. jpeg_decoder_process()      — HW JPEG → full-res RGB565  (~3 ms)
//   2. ppa_do_scale_rotate_mirror() — HW PPA SRM crop+scale      (~2 ms)
//
// Falls back transparently to the software path (below) on any error.
// Both handles are lazy-initialised on first call and kept alive for reuse.
//
// Cover scale: finds the smallest PPA-precision scale (m/16, m∈[1..64]) that:
//   (a) is ≥ the required cover scale,
//   (b) produces integer block_w and block_h from target dimensions, and
//   (c) both blocks fit within the decoded source image.
// This guarantees the PPA output is exactly target_w × target_h pixels.
// If no such m exists (unusual source/target ratio), falls back to SW.
//
// Letterbox scale: picks the largest m/16 ≤ min_scale so the scaled image
// fits inside target dimensions.  Black padding is written by memset before
// the PPA call, so the output buffer is always fully initialised.
// ============================================================================

#ifdef CONFIG_IDF_TARGET_ESP32P4
#include "driver/jpeg_decode.h"
#include "driver/ppa.h"

static jpeg_decoder_handle_t g_hw_jpeg  = nullptr;
static ppa_client_handle_t   g_ppa_srm  = nullptr;
static bool g_hw_init_done              = false;
static bool g_hw_ready                  = false;

static bool hw_init_once() {
    if (g_hw_init_done) return g_hw_ready;
    g_hw_init_done = true;

    jpeg_decode_engine_cfg_t jcfg = {};
    jcfg.intr_priority = 0;
    jcfg.timeout_ms    = 500;
    if (jpeg_new_decoder_engine(&jcfg, &g_hw_jpeg) != ESP_OK) {
        LOGE(TAG, "HW JPEG: engine init failed");
        return false;
    }

    ppa_client_config_t pcfg = {};
    pcfg.oper_type = PPA_OPERATION_SRM;
    if (ppa_register_client(&pcfg, &g_ppa_srm) != ESP_OK) {
        LOGE(TAG, "PPA SRM: client init failed");
        jpeg_del_decoder_engine(g_hw_jpeg);
        g_hw_jpeg = nullptr;
        return false;
    }

    LOGI(TAG, "HW JPEG decoder + PPA SRM client ready");
    g_hw_ready = true;
    return true;
}

// Decode JPEG bytes into full-resolution RGB565 using the HW decoder.
// Allocated output buffer must be freed with free() / heap_caps_free().
// *decoded_w reflects the HW-aligned stride (16-px padded); *actual_w/h = true image size.
static bool hw_decode_jpeg(
    const uint8_t* data, size_t len,
    uint16_t** out_buf,
    int* actual_w, int* actual_h,
    int* decoded_w)
{
    // Parse header without hardware (CPU-only).
    jpeg_decode_picture_info_t info = {};
    if (jpeg_decoder_get_info(data, (uint32_t)len, &info) != ESP_OK) {
        LOGE(TAG, "HW JPEG: get_info failed");
        return false;
    }
    int aw = (int)info.width;
    int ah = (int)info.height;
    if (aw <= 0 || ah <= 0 || aw > 4096 || ah > 4096) {
        LOGE(TAG, "HW JPEG: invalid dims %dx%d", aw, ah);
        return false;
    }

    // HW decoder pads width to 16-pixel boundaries for YUV420/YUV422 MCU alignment.
    int dw = (aw + 15) & ~15;
    int dh = (ah + 15) & ~15;

    // Allocate HW-aligned input buffer and copy JPEG bitstream.
    jpeg_decode_memory_alloc_cfg_t tx_cfg = {};
    tx_cfg.buffer_direction = JPEG_DEC_ALLOC_INPUT_BUFFER;
    size_t tx_size = 0;
    uint8_t* in_buf = (uint8_t*)jpeg_alloc_decoder_mem(len, &tx_cfg, &tx_size);
    if (!in_buf) {
        LOGE(TAG, "HW JPEG: OOM input buf (%u bytes)", (unsigned)len);
        return false;
    }
    memcpy(in_buf, data, len);

    // Allocate HW-aligned output buffer (RGB565, padded dims).
    jpeg_decode_memory_alloc_cfg_t rx_cfg = {};
    rx_cfg.buffer_direction = JPEG_DEC_ALLOC_OUTPUT_BUFFER;
    size_t rx_size   = 0;
    size_t out_bytes = (size_t)dw * dh * 2;  // RGB565
    uint8_t* rgb565  = (uint8_t*)jpeg_alloc_decoder_mem(out_bytes, &rx_cfg, &rx_size);
    if (!rgb565) {
        LOGE(TAG, "HW JPEG: OOM output buf (%ux%u = %u bytes)", dw, dh, (unsigned)out_bytes);
        free(in_buf);
        return false;
    }

    // Decode: JPEG compressed → RGB565.
    // JPEG_DEC_RGB_ELEMENT_ORDER_BGR = "small endian" in Espressif terms.
    // In practice this means the uint16_t RGB565 value is stored in the
    // standard little-endian layout that all LVGL color operations expect:
    //   byte[0] = low byte  = GGGBBBBB
    //   byte[1] = high byte = RRRRRGGG
    // The confusingly named RGB order ("big endian") places R at byte[0],
    // which is byte-swapped relative to what LVGL / the display expects.
    jpeg_decode_cfg_t dcfg = {};
    dcfg.output_format = JPEG_DECODE_OUT_FORMAT_RGB565;
    dcfg.rgb_order     = JPEG_DEC_RGB_ELEMENT_ORDER_BGR;  // little-endian RGB565 = LVGL format
    uint32_t decoded_size = 0;
    esp_err_t err = jpeg_decoder_process(
        g_hw_jpeg, &dcfg,
        in_buf, (uint32_t)len,
        rgb565, (uint32_t)rx_size,
        &decoded_size);
    free(in_buf);

    if (err != ESP_OK) {
        LOGE(TAG, "HW JPEG: decode failed (0x%x)", err);
        free(rgb565);
        return false;
    }

    *out_buf   = (uint16_t*)rgb565;
    *actual_w  = aw;
    *actual_h  = ah;
    *decoded_w = dw;
    return true;
}

// Cover scale via PPA SRM: crop source + scale → exact target_w × target_h.
// Returns false if the 1/16-precision constraint prevents an exact match.
static bool ppa_cover_scale(
    const uint16_t* src, int src_w, int src_h, int hw_src_w,
    uint16_t* out, size_t out_aligned_size,
    uint16_t target_w, uint16_t target_h)
{
    float raw_scale_x = (float)target_w / src_w;
    float raw_scale_y = (float)target_h / src_h;
    float raw_scale   = raw_scale_x > raw_scale_y ? raw_scale_x : raw_scale_y;

    // Find the smallest m ∈ [1..64] (PPA scale = m/16) such that:
    //   m/16 ≥ raw_scale,
    //   block_w = target_w * 16 / m is integer and ≤ src_w,
    //   block_h = target_h * 16 / m is integer and ≤ src_h.
    // This guarantees floor(block_w * m/16) == target_w exactly.
    int min_m = (int)ceilf(raw_scale * 16.0f);
    if (min_m < 1) min_m = 1;

    int found_m = 0, block_w = 0, block_h = 0;
    for (int m = min_m; m <= 64; m++) {
        if ((target_w * 16) % m != 0) continue;
        if ((target_h * 16) % m != 0) continue;
        int bw = (int)(target_w * 16 / m);
        int bh = (int)(target_h * 16 / m);
        if (bw > src_w || bh > src_h) continue;
        if (bw <= 0 || bh <= 0) continue;
        found_m = m;  block_w = bw;  block_h = bh;
        break;
    }
    if (!found_m) {
        LOGD(TAG, "PPA cover: no exact m for %dx%d→%dx%d, SW fallback",
             src_w, src_h, target_w, target_h);
        return false;
    }

    int off_x = (src_w - block_w) / 2;
    int off_y = (src_h - block_h) / 2;
    float ppa_scale = found_m / 16.0f;

    ppa_srm_oper_config_t srm = {};
    srm.in.buffer        = src;
    srm.in.pic_w         = (uint32_t)hw_src_w;   // stride = HW-padded width
    srm.in.pic_h         = (uint32_t)src_h;       // actual content rows
    srm.in.block_w       = (uint32_t)block_w;
    srm.in.block_h       = (uint32_t)block_h;
    srm.in.block_offset_x = (uint32_t)off_x;
    srm.in.block_offset_y = (uint32_t)off_y;
    srm.in.srm_cm        = PPA_SRM_COLOR_MODE_RGB565;

    srm.out.buffer       = out;
    srm.out.buffer_size  = out_aligned_size;
    srm.out.pic_w        = (uint32_t)target_w;
    srm.out.pic_h        = (uint32_t)target_h;
    srm.out.block_offset_x = 0;
    srm.out.block_offset_y = 0;
    srm.out.srm_cm       = PPA_SRM_COLOR_MODE_RGB565;

    srm.rotation_angle   = PPA_SRM_ROTATION_ANGLE_0;
    srm.scale_x          = ppa_scale;
    srm.scale_y          = ppa_scale;
    srm.mode             = PPA_TRANS_MODE_BLOCKING;

    esp_err_t err = ppa_do_scale_rotate_mirror(g_ppa_srm, &srm);
    if (err != ESP_OK) {
        LOGE(TAG, "PPA cover: SRM failed 0x%x", err);
        return false;
    }
    return true;
}

// Letterbox scale via PPA SRM: scale whole source to fit, black bars fill rest.
static bool ppa_letterbox_scale(
    const uint16_t* src, int src_w, int src_h, int hw_src_w,
    uint16_t* out, size_t out_aligned_size,
    uint16_t target_w, uint16_t target_h)
{
    float raw_scale_x = (float)target_w / src_w;
    float raw_scale_y = (float)target_h / src_h;
    float raw_scale   = raw_scale_x < raw_scale_y ? raw_scale_x : raw_scale_y;

    // Find the largest m ∈ [1..64] where the scaled output fits in target.
    int max_m = (int)floorf(raw_scale * 16.0f);
    if (max_m < 1) max_m = 1;

    int found_m = 0, out_block_w = 0, out_block_h = 0;
    for (int m = max_m; m >= 1; m--) {
        int obw = (int)((src_w * m) / 16);
        int obh = (int)((src_h * m) / 16);
        if (obw > target_w || obh > target_h) continue;
        if (obw <= 0 || obh <= 0) continue;
        found_m = m;  out_block_w = obw;  out_block_h = obh;
        break;
    }
    if (!found_m) {
        LOGD(TAG, "PPA letterbox: no valid m for %dx%d→%dx%d, SW fallback",
             src_w, src_h, target_w, target_h);
        return false;
    }

    int off_x = (target_w  - out_block_w) / 2;
    int off_y = (target_h - out_block_h) / 2;
    float ppa_scale = found_m / 16.0f;

    // Pre-fill with black so the bars are zeroed.
    memset(out, 0, out_aligned_size);

    ppa_srm_oper_config_t srm = {};
    srm.in.buffer        = src;
    srm.in.pic_w         = (uint32_t)hw_src_w;
    srm.in.pic_h         = (uint32_t)src_h;
    srm.in.block_w       = (uint32_t)src_w;
    srm.in.block_h       = (uint32_t)src_h;
    srm.in.block_offset_x = 0;
    srm.in.block_offset_y = 0;
    srm.in.srm_cm        = PPA_SRM_COLOR_MODE_RGB565;

    srm.out.buffer       = out;
    srm.out.buffer_size  = out_aligned_size;
    srm.out.pic_w        = (uint32_t)target_w;
    srm.out.pic_h        = (uint32_t)target_h;
    srm.out.block_offset_x = (uint32_t)off_x;
    srm.out.block_offset_y = (uint32_t)off_y;
    srm.out.srm_cm       = PPA_SRM_COLOR_MODE_RGB565;

    srm.rotation_angle   = PPA_SRM_ROTATION_ANGLE_0;
    srm.scale_x          = ppa_scale;
    srm.scale_y          = ppa_scale;
    srm.mode             = PPA_TRANS_MODE_BLOCKING;

    esp_err_t err = ppa_do_scale_rotate_mirror(g_ppa_srm, &srm);
    if (err != ESP_OK) {
        LOGE(TAG, "PPA letterbox: SRM failed 0x%x", err);
        return false;
    }
    return true;
}

// Letterbox scale from HW-decoded RGB565 (with stride) using CPU bilinear.
// Replaces PPA for letterbox because PPA's m/16 quantised scale undershoots
// on most aspect ratios, producing unwanted black bars on both axes.
static bool letterbox_scale_rgb565_to_565(
    const uint16_t* src, int src_w, int src_h, int src_stride,
    uint16_t* dst, int dst_w, int dst_h)
{
    if (!src || !dst || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) return false;

    memset(dst, 0, (size_t)dst_w * dst_h * 2);

    float scale_x = (float)dst_w / (float)src_w;
    float scale_y = (float)dst_h / (float)src_h;
    float scale = (scale_x < scale_y) ? scale_x : scale_y;

    int scaled_w = (int)(src_w * scale + 0.5f);
    int scaled_h = (int)(src_h * scale + 0.5f);
    if (scaled_w > dst_w) scaled_w = dst_w;
    if (scaled_h > dst_h) scaled_h = dst_h;
    int offset_x = (dst_w - scaled_w) / 2;
    int offset_y = (dst_h - scaled_h) / 2;

    for (int dy = 0; dy < scaled_h; dy++) {
        float src_yf = (float)dy / scale;
        int sy0 = (int)src_yf;
        float fy = src_yf - sy0;
        int sy1 = sy0 + 1;
        if (sy0 < 0) { sy0 = 0; fy = 0; }
        if (sy1 >= src_h) sy1 = src_h - 1;

        uint16_t* row_out = dst + (size_t)(dy + offset_y) * dst_w + offset_x;

        for (int dx = 0; dx < scaled_w; dx++) {
            float src_xf = (float)dx / scale;
            int sx0 = (int)src_xf;
            float fx = src_xf - sx0;
            int sx1 = sx0 + 1;
            if (sx0 < 0) { sx0 = 0; fx = 0; }
            if (sx1 >= src_w) sx1 = src_w - 1;

            uint16_t c00 = src[(size_t)sy0 * src_stride + sx0];
            uint16_t c10 = src[(size_t)sy0 * src_stride + sx1];
            uint16_t c01 = src[(size_t)sy1 * src_stride + sx0];
            uint16_t c11 = src[(size_t)sy1 * src_stride + sx1];

            float w00 = (1.0f - fx) * (1.0f - fy);
            float w10 = fx * (1.0f - fy);
            float w01 = (1.0f - fx) * fy;
            float w11 = fx * fy;

            float r = ((c00 >> 11) & 0x1F) * w00 + ((c10 >> 11) & 0x1F) * w10
                    + ((c01 >> 11) & 0x1F) * w01 + ((c11 >> 11) & 0x1F) * w11;
            float g = ((c00 >> 5) & 0x3F) * w00 + ((c10 >> 5) & 0x3F) * w10
                    + ((c01 >> 5) & 0x3F) * w01 + ((c11 >> 5) & 0x3F) * w11;
            float b = (c00 & 0x1F) * w00 + (c10 & 0x1F) * w10
                    + (c01 & 0x1F) * w01 + (c11 & 0x1F) * w11;

            uint8_t ri = (uint8_t)(r + 0.5f); if (ri > 31) ri = 31;
            uint8_t gi = (uint8_t)(g + 0.5f); if (gi > 63) gi = 63;
            uint8_t bi = (uint8_t)(b + 0.5f); if (bi > 31) bi = 31;

            row_out[dx] = (uint16_t)((ri << 11) | (gi << 5) | bi);
        }

        if ((dy & 0x0F) == 0) taskYIELD();
    }
    return true;
}

// Orchestrate full P4 hardware path.  Returns true and fills *out on success.
// Returns false on any failure so the caller can use the software path.
static bool hw_decode_and_scale(
    const uint8_t* data, size_t len,
    uint16_t target_w, uint16_t target_h,
    ImageScaleMode scale_mode,
    uint16_t* out, size_t out_aligned_size)
{
    if (!hw_init_once()) return false;

    uint16_t* decoded = nullptr;
    int actual_w = 0, actual_h = 0;
    int decoded_w = 0;

    if (!hw_decode_jpeg(data, len, &decoded,
                        &actual_w, &actual_h, &decoded_w)) {
        return false;
    }

    bool ok = false;
    if (scale_mode == IMAGE_SCALE_LETTERBOX) {
        // CPU bilinear for letterbox — PPA's m/16 quantisation undershoots
        // on most aspect ratios, producing unwanted bars on both axes.
        ok = letterbox_scale_rgb565_to_565(decoded, actual_w, actual_h, decoded_w,
                                           out, target_w, target_h);
    } else {
        ok = ppa_cover_scale(decoded, actual_w, actual_h, decoded_w,
                              out, out_aligned_size, target_w, target_h);
    }

    free(decoded);
    return ok;
}
#endif  // CONFIG_IDF_TARGET_ESP32P4

// ============================================================================
// End of P4 hardware section
// ============================================================================

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
// Letterbox-mode scale: bilinear resample + fit inside + black bars → RGB565
// ============================================================================
// Source is RGB888 (3 bytes/pixel, BGR byte order from tjpgd).

static bool letterbox_scale_rgb888_to_565(
    const uint8_t* src, int src_w, int src_h,
    uint16_t* dst, int dst_w, int dst_h)
{
    if (!src || !dst || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) return false;

    // Pre-fill with black
    memset(dst, 0, (size_t)dst_w * dst_h * 2);

    // Fit: use min scale so the entire image is visible
    float scale_x = (float)dst_w / (float)src_w;
    float scale_y = (float)dst_h / (float)src_h;
    float scale = (scale_x < scale_y) ? scale_x : scale_y;

    int scaled_w = (int)(src_w * scale + 0.5f);
    int scaled_h = (int)(src_h * scale + 0.5f);
    // Clamp: rounding can push scaled dims 1 px past target
    if (scaled_w > dst_w) scaled_w = dst_w;
    if (scaled_h > dst_h) scaled_h = dst_h;
    int offset_x = (dst_w - scaled_w) / 2;
    int offset_y = (dst_h - scaled_h) / 2;

    for (int dy = 0; dy < scaled_h; dy++) {
        float src_yf = (float)dy / scale;
        int sy0 = (int)src_yf;
        float fy = src_yf - sy0;
        int sy1 = sy0 + 1;
        if (sy0 < 0) { sy0 = 0; fy = 0; }
        if (sy1 >= src_h) sy1 = src_h - 1;

        uint16_t* row_out = dst + (size_t)(dy + offset_y) * dst_w + offset_x;

        for (int dx = 0; dx < scaled_w; dx++) {
            float src_xf = (float)dx / scale;
            int sx0 = (int)src_xf;
            float fx = src_xf - sx0;
            int sx1 = sx0 + 1;
            if (sx0 < 0) { sx0 = 0; fx = 0; }
            if (sx1 >= src_w) sx1 = src_w - 1;

            // BGR byte order from tjpgd
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

        if ((dy & 0x0F) == 0) taskYIELD();
    }
    return true;
}

// Same but for RGBA8888 source (4 bytes/pixel, alpha discarded)
static bool letterbox_scale_rgba8888_to_565(
    const uint8_t* src, int src_w, int src_h,
    uint16_t* dst, int dst_w, int dst_h)
{
    if (!src || !dst || src_w <= 0 || src_h <= 0 || dst_w <= 0 || dst_h <= 0) return false;

    memset(dst, 0, (size_t)dst_w * dst_h * 2);

    float scale_x = (float)dst_w / (float)src_w;
    float scale_y = (float)dst_h / (float)src_h;
    float scale = (scale_x < scale_y) ? scale_x : scale_y;

    int scaled_w = (int)(src_w * scale + 0.5f);
    int scaled_h = (int)(src_h * scale + 0.5f);
    // Clamp: rounding can push scaled dims 1 px past target
    if (scaled_w > dst_w) scaled_w = dst_w;
    if (scaled_h > dst_h) scaled_h = dst_h;
    int offset_x = (dst_w - scaled_w) / 2;
    int offset_y = (dst_h - scaled_h) / 2;

    for (int dy = 0; dy < scaled_h; dy++) {
        float src_yf = (float)dy / scale;
        int sy0 = (int)src_yf;
        float fy = src_yf - sy0;
        int sy1 = sy0 + 1;
        if (sy0 < 0) { sy0 = 0; fy = 0; }
        if (sy1 >= src_h) sy1 = src_h - 1;

        uint16_t* row_out = dst + (size_t)(dy + offset_y) * dst_w + offset_x;

        for (int dx = 0; dx < scaled_w; dx++) {
            float src_xf = (float)dx / scale;
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

    // Clip the MCU block to the image boundary instead of dropping it.
    // tjpgd may deliver blocks that extend 1-7 pixels beyond the image
    // dimensions (JPEG MCUs are 8- or 16-pixel aligned).
    int left  = (int)rect->left;
    int top   = (int)rect->top;
    int right = (int)rect->right;
    int bottom = (int)rect->bottom;
    if (left >= out->dst_w || top >= out->dst_h) return 0;
    if (right  >= out->dst_w)  right  = out->dst_w - 1;
    if (bottom >= out->dst_h)  bottom = out->dst_h - 1;

    int rw = right - left + 1;
    int rh = bottom - top + 1;
    if (rw <= 0 || rh <= 0) return 0;

    int block_w = (int)(rect->right - rect->left + 1);  // original MCU width for stride
    uint8_t* src = (uint8_t*)bitmap;
    for (int row = 0; row < rh; row++) {
        int y = top + row;
        uint8_t* dst_row = out->dst + ((size_t)y * out->dst_w + left) * 3;
        memcpy(dst_row, src, rw * 3);
        src += block_w * 3;
    }
    return 1;  // continue
}

static bool decode_jpeg(const uint8_t* data, size_t len,
                        uint8_t** out_rgb888, int* out_w, int* out_h) {
    static const size_t kWorkSize = 8192;
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
        lv_free(pixels);  // lodepng uses lv_malloc
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
    ImageScaleMode scale_mode,
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

    // Allocate output RGB565 buffer.
    // On ESP32-P4 the buffer is 64-byte cache-line aligned so PPA can write
    // directly into it.  heap_caps_aligned_alloc is compatible with
    // heap_caps_free used by the caller (image_fetch.cpp).
    size_t out_bytes = (size_t)target_w * target_h * 2;
#ifdef CONFIG_IDF_TARGET_ESP32P4
    size_t out_aligned_size = (out_bytes + 63) & ~63UL;
    uint16_t* out = (uint16_t*)heap_caps_aligned_alloc(
        64, out_aligned_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    size_t out_aligned_size = out_bytes;
    uint16_t* out = (uint16_t*)psram_alloc(out_bytes);
#endif
    if (!out) {
        LOGE(TAG, "OOM for output %ux%u RGB565 (%u bytes)", target_w, target_h, (unsigned)out_bytes);
        return false;
    }

    bool ok = false;

    if (fmt == IMAGE_FORMAT_JPEG) {
#ifdef CONFIG_IDF_TARGET_ESP32P4
        // Try hardware decode + PPA scale first; fall through to SW on failure.
        if (hw_decode_and_scale(data, len, target_w, target_h, scale_mode,
                                out, out_aligned_size)) {
            if (out_pixels) *out_pixels = out;
            if (out_size)   *out_size   = out_bytes;
            return true;
        }
        LOGD(TAG, "JPEG HW path failed, using SW fallback");
#endif
        uint8_t* rgb888 = nullptr;
        int src_w = 0, src_h = 0;
        if (decode_jpeg(data, len, &rgb888, &src_w, &src_h)) {
            if (scale_mode == IMAGE_SCALE_LETTERBOX) {
                ok = letterbox_scale_rgb888_to_565(rgb888, src_w, src_h, out, target_w, target_h);
            } else {
                ok = cover_scale_rgb888_to_565(rgb888, src_w, src_h, out, target_w, target_h);
            }
            heap_caps_free(rgb888);
        }
    } else if (fmt == IMAGE_FORMAT_PNG) {
        uint8_t* rgba = nullptr;
        int src_w = 0, src_h = 0;
        if (decode_png(data, len, &rgba, &src_w, &src_h)) {
            if (scale_mode == IMAGE_SCALE_LETTERBOX) {
                ok = letterbox_scale_rgba8888_to_565(rgba, src_w, src_h, out, target_w, target_h);
            } else {
                ok = cover_scale_rgba8888_to_565(rgba, src_w, src_h, out, target_w, target_h);
            }
            lv_free(rgba);  // lodepng uses lv_malloc
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
