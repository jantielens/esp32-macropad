#include "camera.h"

#if HAS_CAMERA
#include "camera_driver.h"

#include <freertos/FreeRTOS.h>

static const CameraOutputDimensions kCameraOutputDimensions[] = {
    {320, 180},
    {640, 360},
};

static const CameraCapabilities kCameraCapabilities = {
    .raw_pixel_format = "RAW10",
    .raw_width = CAMERA_CAPTURE_WIDTH,
    .raw_height = CAMERA_CAPTURE_HEIGHT,
    .jpeg_quality_min = 30,
    .jpeg_quality_max = 90,
    .exposure_lines_min = 8,
    .exposure_lines_max = 1149,
    .white_balance_q8_min = 64,
    .white_balance_q8_max = 1024,
    .output_dimensions = kCameraOutputDimensions,
    .output_dimensions_count = sizeof(kCameraOutputDimensions) / sizeof(kCameraOutputDimensions[0]),
};

static CameraCaptureSettings s_capture_settings = {
    .jpeg_quality = CAMERA_JPEG_QUALITY_DEFAULT,
    .output_width = CAMERA_OUTPUT_WIDTH_DEFAULT,
    .output_height = CAMERA_OUTPUT_HEIGHT_DEFAULT,
    .exposure_lines = CAMERA_EXPOSURE_LINES_DEFAULT,
    .white_balance_red_q8 = CAMERA_WHITE_BALANCE_Q8_DEFAULT,
    .white_balance_blue_q8 = CAMERA_WHITE_BALANCE_Q8_DEFAULT,
};
static portMUX_TYPE s_capture_settings_mux = portMUX_INITIALIZER_UNLOCKED;

static bool camera_capture_settings_are_valid(const CameraCaptureSettings& settings) {
    if (settings.jpeg_quality < kCameraCapabilities.jpeg_quality_min ||
        settings.jpeg_quality > kCameraCapabilities.jpeg_quality_max) {
        return false;
    }
    if (settings.exposure_lines < kCameraCapabilities.exposure_lines_min ||
        settings.exposure_lines > kCameraCapabilities.exposure_lines_max ||
        settings.white_balance_red_q8 < kCameraCapabilities.white_balance_q8_min ||
        settings.white_balance_red_q8 > kCameraCapabilities.white_balance_q8_max ||
        settings.white_balance_blue_q8 < kCameraCapabilities.white_balance_q8_min ||
        settings.white_balance_blue_q8 > kCameraCapabilities.white_balance_q8_max) {
        return false;
    }
    for (size_t index = 0; index < kCameraCapabilities.output_dimensions_count; ++index) {
        const CameraOutputDimensions& dimensions = kCameraOutputDimensions[index];
        if (settings.output_width == dimensions.width && settings.output_height == dimensions.height) {
            return true;
        }
    }
    return false;
}
#endif

void camera_init() {
#if HAS_CAMERA
    camera_driver_init();
#endif
}

bool camera_is_detected() {
#if HAS_CAMERA
    return camera_driver_is_detected();
#else
    return false;
#endif
}

const CameraCapabilities* camera_get_capabilities() {
#if HAS_CAMERA
    return &kCameraCapabilities;
#else
    return nullptr;
#endif
}

CameraCaptureSettings camera_get_capture_settings() {
#if HAS_CAMERA
    portENTER_CRITICAL(&s_capture_settings_mux);
    const CameraCaptureSettings settings = s_capture_settings;
    portEXIT_CRITICAL(&s_capture_settings_mux);
    return settings;
#else
    return {};
#endif
}

bool camera_set_capture_settings(const CameraCaptureSettings& settings) {
#if HAS_CAMERA
    if (!camera_capture_settings_are_valid(settings)) return false;
    portENTER_CRITICAL(&s_capture_settings_mux);
    s_capture_settings = settings;
    portEXIT_CRITICAL(&s_capture_settings_mux);
    return true;
#else
    (void)settings;
    return false;
#endif
}

bool camera_capture_raw(CameraRawFrame* frame) {
#if HAS_CAMERA
    return camera_driver_capture_raw(frame);
#else
    if (frame) *frame = {};
    return false;
#endif
}

void camera_release_raw(CameraRawFrame* frame) {
#if HAS_CAMERA
    camera_driver_release_raw(frame);
#else
    if (frame) *frame = {};
#endif
}

bool camera_capture_jpeg(CameraJpegFrame* frame) {
#if HAS_CAMERA
    return camera_driver_capture_jpeg(frame, camera_get_capture_settings());
#else
    if (frame) *frame = {};
    return false;
#endif
}

void camera_release_jpeg(CameraJpegFrame* frame) {
#if HAS_CAMERA
    camera_driver_release_jpeg(frame);
#else
    if (frame) *frame = {};
#endif
}
