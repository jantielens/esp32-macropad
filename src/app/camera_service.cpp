#include "camera.h"

#if HAS_CAMERA
#include "camera_driver.h"
#include "log_manager.h"
#include "ota_activity.h"
#include "storage.h"

#include <freertos/FreeRTOS.h>
#include <time.h>
#include <string.h>

static const CameraOutputDimensions kCameraOutputDimensions[] = {
    {CAMERA_OUTPUT_WIDTH_DEFAULT, CAMERA_OUTPUT_HEIGHT_DEFAULT},
};
// Verified OV02C10 mode: 1164 timing rows per 30 FPS frame.
static const float kCameraExposureLineTimeUs = 1000000.0f / (30.0f * 1164.0f);

static const CameraCapabilities kCameraCapabilities = {
    .raw_pixel_format = "RAW10",
    .raw_width = CAMERA_CAPTURE_WIDTH,
    .raw_height = CAMERA_CAPTURE_HEIGHT,
    .jpeg_quality_min = 30,
    .jpeg_quality_max = 90,
    .feed_target_fps_min = CAMERA_FEED_TARGET_FPS_MIN,
    .feed_target_fps_max = CAMERA_FEED_TARGET_FPS_MAX,
    .exposure_lines_min = 8,
    .exposure_lines_max = 1149,
    .exposure_line_time_us = kCameraExposureLineTimeUs,
    .white_balance_q8_min = 64,
    .white_balance_q8_max = 1024,
    .output_dimensions = kCameraOutputDimensions,
    .output_dimensions_count = sizeof(kCameraOutputDimensions) / sizeof(kCameraOutputDimensions[0]),
};

static CameraCaptureSettings s_capture_settings = {
    .jpeg_quality = CAMERA_JPEG_QUALITY_DEFAULT,
    .feed_target_fps = CAMERA_FEED_TARGET_FPS_DEFAULT,
    .rotation = CAMERA_ROTATION_DEFAULT,
    .output_width = CAMERA_OUTPUT_WIDTH_DEFAULT,
    .output_height = CAMERA_OUTPUT_HEIGHT_DEFAULT,
    .exposure_lines = CAMERA_EXPOSURE_LINES_DEFAULT,
    .white_balance_red_q8 = CAMERA_WHITE_BALANCE_Q8_DEFAULT,
    .white_balance_blue_q8 = CAMERA_WHITE_BALANCE_Q8_DEFAULT,
};
static portMUX_TYPE s_capture_settings_mux = portMUX_INITIALIZER_UNLOCKED;

static const char* const kCameraDirectory = "/camera";
static const char* const kCameraLatestPath = "/camera/latest.jpg";
static const time_t kCameraNtpValidEpoch = 1704067200L;
static const uint32_t kCameraRollMaxSequence = 999999;

struct CameraRollState {
    char date[9];
    uint32_t next_sequence;
};

static CameraRollState s_roll_state = {};

static bool camera_ensure_directory(const char* path) {
    return Storage.exists(path) || Storage.mkdir(path);
}

static bool camera_write_jpeg_atomic(const char* path, const CameraJpegFrame& frame) {
    char temporary_path[48] = {};
    if (snprintf(temporary_path, sizeof(temporary_path), "%s.tmp", path) >=
        (int)sizeof(temporary_path)) {
        return false;
    }
    Storage.remove(temporary_path);
    File file = Storage.open(temporary_path, "w");
    if (!file) {
        LOGE("Camera", "Cannot open '%s' for writing", temporary_path);
        return false;
    }
    const size_t written = file.write(frame.data, frame.size);
    file.close();
    if (written != frame.size) {
        LOGE("Camera", "Incomplete JPEG write to '%s' (%u/%u)", path,
             (unsigned)written, (unsigned)frame.size);
        Storage.remove(temporary_path);
        return false;
    }
    if (Storage.exists(path) && !Storage.remove(path)) {
        LOGE("Camera", "Cannot replace '%s'", path);
        Storage.remove(temporary_path);
        return false;
    }
    if (!Storage.rename(temporary_path, path)) {
        LOGE("Camera", "Cannot rename '%s' to '%s'", temporary_path, path);
        Storage.remove(temporary_path);
        return false;
    }
    return true;
}

static bool camera_roll_filename_sequence(const char* name, uint32_t* sequence) {
    const char* filename = strrchr(name, '/');
    filename = filename ? filename + 1 : name;
    if (strlen(filename) != 10 || strcmp(filename + 6, ".jpg") != 0) return false;
    uint32_t value = 0;
    for (uint8_t index = 0; index < 6; ++index) {
        if (filename[index] < '0' || filename[index] > '9') return false;
        value = value * 10 + (uint32_t)(filename[index] - '0');
    }
    if (value == 0 || value > kCameraRollMaxSequence) return false;
    *sequence = value;
    return true;
}

static bool camera_roll_path(char* path, size_t path_len) {
    const time_t now = time(nullptr);
    if (now < kCameraNtpValidEpoch) {
        LOGW("Camera", "Camera roll requires NTP time; latest image was saved");
        return false;
    }
    struct tm date_time = {};
    gmtime_r(&now, &date_time);
    char date[9] = {};
    if (strftime(date, sizeof(date), "%Y%m%d", &date_time) != 8) return false;
    char directory[20] = {};
    if (snprintf(directory, sizeof(directory), "%s/%s", kCameraDirectory, date) >=
        (int)sizeof(directory) || !camera_ensure_directory(directory)) {
        LOGE("Camera", "Cannot create camera roll directory '%s'", directory);
        return false;
    }
    if (strcmp(s_roll_state.date, date) != 0) {
        uint32_t highest_sequence = 0;
        File directory_file = Storage.open(directory);
        if (!directory_file || !directory_file.isDirectory()) return false;
        for (File file = directory_file.openNextFile(); file; file = directory_file.openNextFile()) {
            uint32_t sequence = 0;
            if (!file.isDirectory() && camera_roll_filename_sequence(file.name(), &sequence) &&
                sequence > highest_sequence) {
                highest_sequence = sequence;
            }
            file.close();
        }
        directory_file.close();
        if (highest_sequence >= kCameraRollMaxSequence) {
            LOGE("Camera", "Camera roll '%s' is full", directory);
            return false;
        }
        strlcpy(s_roll_state.date, date, sizeof(s_roll_state.date));
        s_roll_state.next_sequence = highest_sequence + 1;
    }
    return snprintf(path, path_len, "%s/%06u.jpg", directory,
                    (unsigned)s_roll_state.next_sequence) < (int)path_len;
}

static bool camera_capture_settings_are_valid(const CameraCaptureSettings& settings) {
    if (settings.jpeg_quality < kCameraCapabilities.jpeg_quality_min ||
        settings.jpeg_quality > kCameraCapabilities.jpeg_quality_max) {
        return false;
    }
    if (settings.feed_target_fps < kCameraCapabilities.feed_target_fps_min ||
        settings.feed_target_fps > kCameraCapabilities.feed_target_fps_max) {
        return false;
    }
    if (settings.rotation != CAMERA_ROTATION_0 && settings.rotation != CAMERA_ROTATION_90 &&
        settings.rotation != CAMERA_ROTATION_180 && settings.rotation != CAMERA_ROTATION_270) {
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

bool camera_capture_rgb565(CameraRgb565Frame* rgb565, CameraJpegFrame* jpeg,
                           CameraCaptureTiming* timing) {
#if HAS_CAMERA
    return camera_driver_capture_rgb565(rgb565, jpeg, timing, camera_get_capture_settings());
#else
    if (jpeg) *jpeg = {};
    if (rgb565) *rgb565 = {};
    if (timing) *timing = {};
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

bool camera_capture_save(CameraCaptureSaveTo save_to) {
#if HAS_CAMERA
    if (ota_activity_is_active()) {
        LOGW("Camera", "Camera capture skipped while OTA is active");
        return false;
    }
    if (!camera_is_detected() || !camera_ensure_directory(kCameraDirectory)) {
        LOGE("Camera", "Camera or storage unavailable");
        return false;
    }
    CameraJpegFrame frame = {};
    if (!camera_capture_jpeg(&frame)) return false;
    bool saved = save_to == CAMERA_CAPTURE_SAVE_ROLL ||
                 camera_write_jpeg_atomic(kCameraLatestPath, frame);
    char roll_path[40] = {};
    if (saved && save_to != CAMERA_CAPTURE_SAVE_LATEST &&
        camera_roll_path(roll_path, sizeof(roll_path))) {
        saved = camera_write_jpeg_atomic(roll_path, frame);
        if (saved) s_roll_state.next_sequence++;
    } else if (save_to != CAMERA_CAPTURE_SAVE_LATEST) {
        saved = false;
    }
    camera_release_jpeg(&frame);
    return saved;
#else
    (void)save_to;
    return false;
#endif
}
