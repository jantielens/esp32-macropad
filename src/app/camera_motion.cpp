#include "camera_motion.h"

#if HAS_CAMERA

#include "camera.h"
#include "log_manager.h"
#include "ota_activity.h"

#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <time.h>

namespace {

constexpr uint16_t kGridWidth = 80;
constexpr uint16_t kGridHeight = 45;
constexpr size_t kGridSize = static_cast<size_t>(kGridWidth) * kGridHeight;
constexpr uint16_t kRaw10RowBytes = static_cast<uint16_t>(CAMERA_CAPTURE_WIDTH * 5 / 4);
constexpr uint8_t kConfirmFrames = 2;
constexpr uint16_t kGlobalChangeTileCount = static_cast<uint16_t>(kGridSize * 7 / 10);

struct SensitivityThresholds {
    uint16_t minimum_changed_tiles;
    uint32_t minimum_score;
};

CameraMotionSettings s_settings = {
    .enabled = false,
    .sample_fps = CAMERA_MOTION_FPS_DEFAULT,
    .sensitivity = CAMERA_MOTION_SENSITIVITY_DEFAULT,
    .presence_hold_seconds = CAMERA_PRESENCE_HOLD_SECONDS_DEFAULT,
};
CameraMotionStatus s_status = {};
uint8_t* s_previous_grid = nullptr;
CameraRawFrame s_raw_frame = {};
bool s_have_previous_grid = false;
uint8_t s_active_frames = 0;
uint32_t s_last_sample_ms = 0;
uint32_t s_last_motion_ms = 0;
bool s_presence_changed = false;
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

SensitivityThresholds camera_motion_thresholds(uint8_t sensitivity) {
    // Sensitivity grows from 1 (large movement only) to 10 (small movement).
    // Observed static noise is about 40-85 tiles / 900-2100 score, while a
    // hand movement is about 319-723 tiles / 24000-61000 score.
    if (sensitivity <= CAMERA_MOTION_SENSITIVITY_DEFAULT) {
        const uint8_t offset = sensitivity - CAMERA_MOTION_SENSITIVITY_MIN;
        return {
            .minimum_changed_tiles = static_cast<uint16_t>(400 - offset * 50),
            .minimum_score = 25000U - static_cast<uint32_t>(offset) * 3250U,
        };
    }
    const uint8_t offset = sensitivity - CAMERA_MOTION_SENSITIVITY_DEFAULT;
    return {
        .minimum_changed_tiles = static_cast<uint16_t>(200 - offset * 25),
        .minimum_score = 12000U - static_cast<uint32_t>(offset) * 1500U,
    };
}

bool camera_motion_settings_are_valid(const CameraMotionSettings& settings) {
    return settings.sample_fps >= CAMERA_MOTION_FPS_MIN &&
           settings.sample_fps <= CAMERA_MOTION_FPS_MAX &&
           settings.sensitivity >= CAMERA_MOTION_SENSITIVITY_MIN &&
           settings.sensitivity <= CAMERA_MOTION_SENSITIVITY_MAX &&
           settings.presence_hold_seconds >= CAMERA_PRESENCE_HOLD_SECONDS_MIN &&
           settings.presence_hold_seconds <= CAMERA_PRESENCE_HOLD_SECONDS_MAX;
}

void camera_motion_release_grid() {
    if (s_previous_grid) free(s_previous_grid);
    s_previous_grid = nullptr;
    s_have_previous_grid = false;
    s_active_frames = 0;
}

bool camera_motion_ensure_grid() {
    if (s_previous_grid) return true;
    s_previous_grid = static_cast<uint8_t*>(
        heap_caps_malloc(kGridSize, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!s_previous_grid) {
        LOGE("Camera", "Motion grid allocation failed: %u bytes", static_cast<unsigned>(kGridSize));
        return false;
    }
    return true;
}

uint8_t camera_motion_raw10_high_byte(const CameraRawFrame& raw, uint16_t x, uint16_t y) {
    return raw.data[static_cast<size_t>(y) * kRaw10RowBytes +
                    static_cast<size_t>(x / 4) * 5 + x % 4];
}

void camera_motion_set_presence(bool presence) {
    portENTER_CRITICAL(&s_mux);
    if (s_status.presence == presence) {
        portEXIT_CRITICAL(&s_mux);
        return;
    }
    s_status.presence = presence;
    s_presence_changed = true;
    portEXIT_CRITICAL(&s_mux);
    LOGI("Camera", "Motion-derived presence: %s", presence ? "on" : "off");
}

void camera_motion_update_presence_timeout(uint32_t now) {
    const CameraMotionStatus status = camera_motion_get_status();
    const CameraMotionSettings settings = camera_motion_get_settings();
    if (!status.presence || !s_last_motion_ms) return;
    const uint32_t hold_ms = static_cast<uint32_t>(settings.presence_hold_seconds) * 1000U;
    if (now - s_last_motion_ms >= hold_ms) {
        LOGI("Camera", "Motion presence timed out after %us", settings.presence_hold_seconds);
        camera_motion_set_presence(false);
    }
}

void camera_motion_process_raw(const CameraRawFrame& raw, uint32_t now) {
    if (raw.width != CAMERA_CAPTURE_WIDTH || raw.height != CAMERA_CAPTURE_HEIGHT ||
        raw.size < static_cast<size_t>(raw.width) * raw.height * 5 / 4) {
        LOGW("Camera", "Motion analysis skipped: unexpected RAW10 frame");
        return;
    }

    const CameraMotionSettings settings = camera_motion_get_settings();
    const SensitivityThresholds thresholds = camera_motion_thresholds(settings.sensitivity);
    uint16_t changed_tiles = 0;
    uint32_t score = 0;
    size_t index = 0;
    for (uint16_t grid_y = 0; grid_y < kGridHeight; ++grid_y) {
        const uint16_t source_y = static_cast<uint16_t>(
            (static_cast<uint32_t>(grid_y) * raw.height + raw.height / 2) / kGridHeight);
        for (uint16_t grid_x = 0; grid_x < kGridWidth; ++grid_x, ++index) {
            const uint16_t source_x = static_cast<uint16_t>(
                (static_cast<uint32_t>(grid_x) * raw.width + raw.width / 2) / kGridWidth);
            const uint8_t sample = camera_motion_raw10_high_byte(raw, source_x, source_y);
            if (s_have_previous_grid) {
                const uint8_t difference = sample > s_previous_grid[index]
                    ? sample - s_previous_grid[index] : s_previous_grid[index] - sample;
                if (difference >= 20) {
                    ++changed_tiles;
                    score += difference;
                }
            }
            s_previous_grid[index] = sample;
        }
    }

    if (!s_have_previous_grid) {
        s_have_previous_grid = true;
        LOGI("Camera", "Motion sample: baseline captured (%ux%u grid)", kGridWidth, kGridHeight);
        return;
    }

    portENTER_CRITICAL(&s_mux);
    s_status.changed_tiles = changed_tiles;
    s_status.score = score;
    portEXIT_CRITICAL(&s_mux);
    // A nearly frame-wide change is normally illumination or exposure, not
    // localized motion. The new frame remains the baseline for the next sample.
    const bool global_change = changed_tiles >= kGlobalChangeTileCount;
    const bool motion = !global_change && changed_tiles >= thresholds.minimum_changed_tiles &&
                        score >= thresholds.minimum_score;
    s_active_frames = motion ? static_cast<uint8_t>(
        s_active_frames < kConfirmFrames ? s_active_frames + 1 : kConfirmFrames) : 0;
    LOGI("Camera", "Motion sample: tiles=%u score=%u threshold=%u/%u global=%s confirm=%u/%u",
         changed_tiles, static_cast<unsigned>(score), thresholds.minimum_changed_tiles,
         static_cast<unsigned>(thresholds.minimum_score), global_change ? "yes" : "no",
         s_active_frames, kConfirmFrames);
    if (s_active_frames < kConfirmFrames) return;

    s_last_motion_ms = now;
    const time_t epoch = time(nullptr);
    portENTER_CRITICAL(&s_mux);
    s_status.has_last_motion = true;
    s_status.last_motion_epoch = epoch > 0 ? static_cast<uint32_t>(epoch) : 0;
    portEXIT_CRITICAL(&s_mux);
    camera_motion_set_presence(true);
}

} // namespace

CameraMotionSettings camera_motion_get_settings() {
    portENTER_CRITICAL(&s_mux);
    const CameraMotionSettings settings = s_settings;
    portEXIT_CRITICAL(&s_mux);
    return settings;
}

bool camera_motion_set_settings(const CameraMotionSettings& settings) {
    if (!camera_motion_settings_are_valid(settings)) return false;
    portENTER_CRITICAL(&s_mux);
    const bool disabling = s_settings.enabled && !settings.enabled;
    s_settings = settings;
    s_status.enabled = settings.enabled;
    portEXIT_CRITICAL(&s_mux);
    if (disabling) {
        camera_motion_release_grid();
        camera_release_raw(&s_raw_frame);
        s_last_sample_ms = 0;
        s_last_motion_ms = 0;
        portENTER_CRITICAL(&s_mux);
        s_status.changed_tiles = 0;
        s_status.score = 0;
        portEXIT_CRITICAL(&s_mux);
        camera_motion_set_presence(false);
    }
    return true;
}

CameraMotionStatus camera_motion_get_status() {
    portENTER_CRITICAL(&s_mux);
    const CameraMotionStatus status = s_status;
    portEXIT_CRITICAL(&s_mux);
    return status;
}

bool camera_motion_is_enabled() {
    return camera_motion_get_settings().enabled;
}

void camera_motion_deinit() {
    camera_motion_release_grid();
    camera_release_raw(&s_raw_frame);
}

void camera_motion_loop() {
    const CameraMotionSettings settings = camera_motion_get_settings();
    if (!settings.enabled) return;
    const uint32_t now = millis();
    camera_motion_update_presence_timeout(now);
    const uint32_t interval_ms = 1000U / settings.sample_fps;
    if (now - s_last_sample_ms < interval_ms || ota_activity_is_active() || !camera_is_detected() ||
        !camera_motion_ensure_grid()) {
        return;
    }

    const int64_t capture_started_us = esp_timer_get_time();
    if (!camera_capture_raw_reuse(&s_raw_frame)) {
        LOGW("Camera", "Motion sample: RAW10 capture failed");
        return;
    }
    const uint32_t capture_ms = static_cast<uint32_t>((esp_timer_get_time() - capture_started_us) / 1000);
    s_last_sample_ms = now;
    const int64_t analysis_started_us = esp_timer_get_time();
    camera_motion_process_raw(s_raw_frame, now);
    LOGI("Camera", "Motion sample: RAW10 capture=%ums analysis=%ums", static_cast<unsigned>(capture_ms),
         static_cast<unsigned>((esp_timer_get_time() - analysis_started_us) / 1000));
}

bool camera_motion_take_presence_change(bool* presence) {
    if (!presence) return false;
    portENTER_CRITICAL(&s_mux);
    if (!s_presence_changed) {
        portEXIT_CRITICAL(&s_mux);
        return false;
    }
    *presence = s_status.presence;
    s_presence_changed = false;
    portEXIT_CRITICAL(&s_mux);
    return true;
}

#endif // HAS_CAMERA