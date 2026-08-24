#include "jpeg_encoder_service.h"

#include <sdkconfig.h>

#ifdef CONFIG_IDF_TARGET_ESP32P4

#include "log_manager.h"

#include "driver/jpeg_encode.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#define TAG "JpegEncoder"

static jpeg_encoder_handle_t s_encoder = nullptr;
static bool s_encoder_initialized = false;
static bool s_encoder_available = false;

static SemaphoreHandle_t jpeg_encoder_mutex() {
    static SemaphoreHandle_t mutex = xSemaphoreCreateMutex();
    return mutex;
}

static bool jpeg_encoder_init() {
    if (s_encoder_initialized) return s_encoder_available;
    s_encoder_initialized = true;

    const jpeg_encode_engine_cfg_t config = {
        .intr_priority = 0,
        .timeout_ms = 500,
    };
    if (jpeg_new_encoder_engine(&config, &s_encoder) != ESP_OK) {
        LOGE(TAG, "Hardware JPEG encoder initialization failed");
        return false;
    }

    s_encoder_available = true;
    LOGI(TAG, "Hardware JPEG encoder ready");
    return true;
}

static bool jpeg_encode(const uint8_t* pixels, uint16_t width, uint16_t height,
                        size_t bytes_per_pixel, jpeg_enc_input_format_t format,
                        jpeg_down_sampling_type_t sub_sample, uint8_t quality,
                        uint8_t** jpeg_data, size_t* jpeg_size) {
    if (!pixels || !width || !height || !jpeg_data || !jpeg_size) return false;
    *jpeg_data = nullptr;
    *jpeg_size = 0;

    SemaphoreHandle_t mutex = jpeg_encoder_mutex();
    if (!mutex || xSemaphoreTake(mutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        LOGW(TAG, "Hardware JPEG encoder busy");
        return false;
    }

    bool success = false;
    uint8_t* input = nullptr;
    uint8_t* output = nullptr;
    do {
        if (!jpeg_encoder_init()) break;

        const size_t input_size = (size_t)width * height * bytes_per_pixel;
        const jpeg_encode_memory_alloc_cfg_t input_config = {
            .buffer_direction = JPEG_ENC_ALLOC_INPUT_BUFFER,
        };
        size_t allocated_input_size = 0;
        input = static_cast<uint8_t*>(
            jpeg_alloc_encoder_mem(input_size, &input_config, &allocated_input_size));
        if (!input) {
            LOGE(TAG, "JPEG input allocation failed (%u bytes)", static_cast<unsigned>(input_size));
            break;
        }
        memcpy(input, pixels, input_size);

        const jpeg_encode_memory_alloc_cfg_t output_config = {
            .buffer_direction = JPEG_ENC_ALLOC_OUTPUT_BUFFER,
        };
        size_t allocated_output_size = 0;
        output = static_cast<uint8_t*>(
            jpeg_alloc_encoder_mem(input_size, &output_config, &allocated_output_size));
        if (!output) {
            LOGE(TAG, "JPEG output allocation failed (%u bytes)", static_cast<unsigned>(input_size));
            break;
        }

        const jpeg_encode_cfg_t config = {
            .height = height,
            .width = width,
            .src_type = format,
            .sub_sample = sub_sample,
            .image_quality = quality,
        };
        uint32_t encoded_size = 0;
        const esp_err_t error = jpeg_encoder_process(
            s_encoder, &config, input, static_cast<uint32_t>(input_size), output,
            static_cast<uint32_t>(allocated_output_size), &encoded_size);
        if (error != ESP_OK || !encoded_size) {
            LOGE(TAG, "JPEG encode failed: %s", esp_err_to_name(error));
            break;
        }

        *jpeg_data = output;
        *jpeg_size = encoded_size;
        output = nullptr;
        success = true;
    } while (false);

    if (input) free(input);
    if (output) free(output);
    xSemaphoreGive(mutex);
    return success;
}

bool jpeg_encode_gray8(const uint8_t* pixels, uint16_t width, uint16_t height,
                       uint8_t quality, uint8_t** jpeg_data, size_t* jpeg_size) {
    return jpeg_encode(pixels, width, height, 1, JPEG_ENCODE_IN_FORMAT_GRAY,
                       JPEG_DOWN_SAMPLING_GRAY, quality, jpeg_data, jpeg_size);
}

bool jpeg_encode_rgb565(const uint8_t* pixels, uint16_t width, uint16_t height,
                        uint8_t quality, uint8_t** jpeg_data, size_t* jpeg_size) {
    return jpeg_encode(pixels, width, height, sizeof(uint16_t), JPEG_ENCODE_IN_FORMAT_RGB565,
                       JPEG_DOWN_SAMPLING_YUV420, quality, jpeg_data, jpeg_size);
}

#else

bool jpeg_encode_gray8(const uint8_t*, uint16_t, uint16_t, uint8_t, uint8_t**, size_t*) {
    return false;
}

bool jpeg_encode_rgb565(const uint8_t*, uint16_t, uint16_t, uint8_t, uint8_t**, size_t*) {
    return false;
}

#endif