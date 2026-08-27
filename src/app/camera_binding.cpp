#include "camera_binding.h"
#include "board_config.h"

#if HAS_CAMERA && HAS_DISPLAY

#include "binding_template.h"
#include "camera_motion.h"
#include "log_manager.h"

#include <Arduino.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

struct CameraBindingKey {
    const char* key;
};

constexpr CameraBindingKey kCameraBindingKeys[] = {
    {"presence"},
    {"motion_enabled"},
    {"changed_tiles"},
    {"score"},
    {"tile_threshold"},
    {"score_threshold"},
    {"global_change"},
    {"confirm_frames"},
    {"baseline_ready"},
    {"sample_age"},
    {"last_motion"},
    {"motion_age"},
};

void parse_camera_params(const char* params, char* key, size_t key_len,
                         char* format, size_t format_len) {
    key[0] = '\0';
    format[0] = '\0';
    if (!params || !params[0]) return;
    const char* separator = strchr(params, ';');
    if (!separator) {
        strlcpy(key, params, key_len);
        return;
    }
    const size_t length = static_cast<size_t>(separator - params);
    memcpy(key, params, length < key_len - 1 ? length : key_len - 1);
    key[length < key_len - 1 ? length : key_len - 1] = '\0';
    strlcpy(format, separator + 1, format_len);
}

bool camera_binding_key_known(const char* key) {
    for (const CameraBindingKey& entry : kCameraBindingKeys) {
        if (strcmp(key, entry.key) == 0) return true;
    }
    return false;
}

void write_unsigned(char* out, size_t out_len, const char* format, uint32_t value) {
    if (format[0]) {
        snprintf(out, out_len, format, static_cast<unsigned long>(value));
    } else {
        snprintf(out, out_len, "%lu", static_cast<unsigned long>(value));
    }
}

void write_text(char* out, size_t out_len, const char* format, const char* value) {
    if (format[0]) {
        snprintf(out, out_len, format, value);
    } else {
        strlcpy(out, value, out_len);
    }
}

BindingResolverStatus camera_binding_resolve(const char* params, char* out, size_t out_len) {
    char key[24];
    char format[32];
    parse_camera_params(params, key, sizeof(key), format, sizeof(format));
    if (!camera_binding_key_known(key)) return BINDING_RESOLVER_UNKNOWN;

    const CameraMotionStatus status = camera_motion_get_status();
    const CameraMotionSettings settings = camera_motion_get_settings();
    const CameraMotionThresholds thresholds = camera_motion_get_thresholds(settings.sensitivity);
    if (strcmp(key, "presence") == 0) {
        write_text(out, out_len, format, status.presence ? "ON" : "OFF");
    } else if (strcmp(key, "motion_enabled") == 0) {
        write_text(out, out_len, format, status.enabled ? "ON" : "OFF");
    } else if (strcmp(key, "changed_tiles") == 0) {
        write_unsigned(out, out_len, format, status.changed_tiles);
    } else if (strcmp(key, "score") == 0) {
        write_unsigned(out, out_len, format, status.score);
    } else if (strcmp(key, "tile_threshold") == 0) {
        write_unsigned(out, out_len, format, thresholds.changed_tiles);
    } else if (strcmp(key, "score_threshold") == 0) {
        write_unsigned(out, out_len, format, thresholds.score);
    } else if (strcmp(key, "global_change") == 0) {
        write_text(out, out_len, format, status.global_change ? "ON" : "OFF");
    } else if (strcmp(key, "confirm_frames") == 0) {
        write_unsigned(out, out_len, format, status.confirm_frames);
    } else if (strcmp(key, "baseline_ready") == 0) {
        write_text(out, out_len, format, status.baseline_ready ? "ON" : "OFF");
    } else if (strcmp(key, "sample_age") == 0) {
        write_unsigned(out, out_len, format,
                       status.has_last_sample ? (millis() - status.last_sample_ms) / 1000U : 0);
    } else if (strcmp(key, "last_motion") == 0) {
        write_unsigned(out, out_len, format, status.last_motion_epoch);
    } else if (strcmp(key, "motion_age") == 0) {
        write_unsigned(out, out_len, format,
                       status.has_last_motion ? (millis() - status.last_motion_ms) / 1000U : 0);
    }
    return BINDING_RESOLVER_RESOLVED;
}

void camera_binding_collect(const char* params, void* user_data) {
    (void)params;
    (void)user_data;
}

} // namespace

uint8_t camera_binding_key_count() {
    return static_cast<uint8_t>(sizeof(kCameraBindingKeys) / sizeof(kCameraBindingKeys[0]));
}

const char* camera_binding_key_at(uint8_t index) {
    return index < camera_binding_key_count() ? kCameraBindingKeys[index].key : nullptr;
}

void camera_binding_init() {
    if (!binding_template_register("camera", camera_binding_resolve, camera_binding_collect,
                                   {1, 2, 1, 1, BINDING_VALIDATION_STANDARD, false,
                                    camera_binding_key_count, camera_binding_key_at})) {
        LOGE("Camera", "Failed to register camera binding scheme");
    }
}

#else

uint8_t camera_binding_key_count() { return 0; }
const char* camera_binding_key_at(uint8_t index) { (void)index; return nullptr; }
void camera_binding_init() {}

#endif