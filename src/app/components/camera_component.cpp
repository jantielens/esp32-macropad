#include "component_registry.h"

#if HAS_CAMERA

#include "board_config.h"
#include "camera.h"
#include "camera_motion.h"
#include "config_manager.h"
#include "main_loop_bridge.h"
#include "web_portal_json.h"
#include "web_portal_state.h"

#include <ArduinoJson.h>

namespace {

constexpr uint32_t kCameraConfigSaveTimeoutMs = 5000;
constexpr size_t kCameraConfigMaxBodyBytes = 512;
constexpr const char* kCameraPortalScript = "/portal-camera.js";
constexpr const char* kCameraPortalStyle = "/portal-camera.css";

struct CameraConfigSaveRequest {
    bool update_capture_settings;
    bool update_motion_settings;
    CameraCaptureSettings settings;
    CameraMotionSettings motion_settings;
};

void camera_get_config(AsyncWebServerRequest* request) {
    const CameraCapabilities* capabilities = camera_get_capabilities();
    if (!capabilities) {
        web_portal_send_json_error(request, 503, "Camera unavailable");
        return;
    }

    const CameraCaptureSettings settings = camera_get_capture_settings();
    const CameraMotionSettings motion_settings = camera_motion_get_settings();
    const CameraMotionStatus motion_status = camera_motion_get_status();
    auto doc = make_psram_json_doc(768);
    (*doc)["detected"] = camera_is_detected();
    (*doc)["raw_pixel_format"] = capabilities->raw_pixel_format;
    (*doc)["raw_width"] = capabilities->raw_width;
    (*doc)["raw_height"] = capabilities->raw_height;
    (*doc)["jpeg_quality"] = settings.jpeg_quality;
    (*doc)["jpeg_quality_min"] = capabilities->jpeg_quality_min;
    (*doc)["jpeg_quality_max"] = capabilities->jpeg_quality_max;
    (*doc)["feed_target_fps"] = settings.feed_target_fps;
    (*doc)["feed_target_fps_min"] = capabilities->feed_target_fps_min;
    (*doc)["feed_target_fps_max"] = capabilities->feed_target_fps_max;
    (*doc)["mjpeg_max_clients"] = CAMERA_MJPEG_MAX_CLIENTS;
    (*doc)["rotation"] = settings.rotation;
    (*doc)["exposure_lines"] = settings.exposure_lines;
    (*doc)["exposure_lines_min"] = capabilities->exposure_lines_min;
    (*doc)["exposure_lines_max"] = capabilities->exposure_lines_max;
    (*doc)["exposure_line_time_us"] = capabilities->exposure_line_time_us;
    (*doc)["white_balance_red_q8"] = settings.white_balance_red_q8;
    (*doc)["white_balance_blue_q8"] = settings.white_balance_blue_q8;
    (*doc)["white_balance_q8_min"] = capabilities->white_balance_q8_min;
    (*doc)["white_balance_q8_max"] = capabilities->white_balance_q8_max;
    (*doc)["output_width"] = settings.output_width;
    (*doc)["output_height"] = settings.output_height;
    (*doc)["motion_enabled"] = motion_settings.enabled;
    (*doc)["motion_analyze_every_nth_frame"] = motion_settings.analyze_every_nth_frame;
    (*doc)["motion_analyze_every_nth_frame_min"] = CAMERA_MOTION_ANALYZE_EVERY_MIN;
    (*doc)["motion_analyze_every_nth_frame_max"] = CAMERA_MOTION_ANALYZE_EVERY_MAX;
    (*doc)["capture_fps"] = settings.feed_target_fps;
    (*doc)["motion_sensitivity"] = motion_settings.sensitivity;
    (*doc)["motion_sensitivity_min"] = CAMERA_MOTION_SENSITIVITY_MIN;
    (*doc)["motion_sensitivity_max"] = CAMERA_MOTION_SENSITIVITY_MAX;
    (*doc)["presence_hold_seconds"] = motion_settings.presence_hold_seconds;
    (*doc)["presence_hold_seconds_min"] = CAMERA_PRESENCE_HOLD_SECONDS_MIN;
    (*doc)["presence_hold_seconds_max"] = CAMERA_PRESENCE_HOLD_SECONDS_MAX;
    (*doc)["presence"] = motion_status.presence;
    (*doc)["last_motion"] = motion_status.has_last_motion ? motion_status.last_motion_epoch : 0;
    JsonArray output_dimensions = doc->createNestedArray("output_dimensions");
    for (size_t index = 0; index < capabilities->output_dimensions_count; ++index) {
        JsonObject dimensions = output_dimensions.createNestedObject();
        dimensions["width"] = capabilities->output_dimensions[index].width;
        dimensions["height"] = capabilities->output_dimensions[index].height;
    }
    web_portal_send_json_chunked(request, doc);
}

void camera_save_config_on_main(const void* opaque, bool* ok, char* message, size_t message_len) {
    const CameraConfigSaveRequest* request = static_cast<const CameraConfigSaveRequest*>(opaque);
    if (!request) {
        strlcpy(message, "invalid camera settings", message_len);
        return;
    }
    DeviceConfig* config = web_portal_get_current_config();
    if (!config) {
        strlcpy(message, "configuration unavailable", message_len);
        return;
    }
    const CameraCaptureSettings previous = {
        .jpeg_quality = config->camera_jpeg_quality,
        .feed_target_fps = config->camera_feed_target_fps,
        .rotation = config->camera_rotation,
        .output_width = config->camera_output_width,
        .output_height = config->camera_output_height,
        .exposure_lines = config->camera_exposure_lines,
        .white_balance_red_q8 = config->camera_white_balance_red_q8,
        .white_balance_blue_q8 = config->camera_white_balance_blue_q8,
    };
    const CameraMotionSettings previous_motion = {
        .enabled = config->camera_motion_enabled,
        .analyze_every_nth_frame = config->camera_motion_analyze_every_nth_frame,
        .sensitivity = config->camera_motion_sensitivity,
        .presence_hold_seconds = config->camera_presence_hold_seconds,
    };
    if ((request->update_capture_settings && !camera_set_capture_settings(request->settings)) ||
        (request->update_motion_settings && !camera_motion_set_settings(request->motion_settings))) {
        camera_set_capture_settings(previous);
        camera_motion_set_settings(previous_motion);
        strlcpy(message, "invalid camera settings", message_len);
        return;
    }
    if (request->update_capture_settings) {
        config->camera_jpeg_quality = request->settings.jpeg_quality;
        config->camera_feed_target_fps = request->settings.feed_target_fps;
        config->camera_rotation = request->settings.rotation;
        config->camera_output_width = request->settings.output_width;
        config->camera_output_height = request->settings.output_height;
        config->camera_exposure_lines = request->settings.exposure_lines;
        config->camera_white_balance_red_q8 = request->settings.white_balance_red_q8;
        config->camera_white_balance_blue_q8 = request->settings.white_balance_blue_q8;
    }
    if (request->update_motion_settings) {
        config->camera_motion_enabled = request->motion_settings.enabled;
        config->camera_motion_analyze_every_nth_frame = request->motion_settings.analyze_every_nth_frame;
        config->camera_motion_sensitivity = request->motion_settings.sensitivity;
        config->camera_presence_hold_seconds = request->motion_settings.presence_hold_seconds;
    }
    if (config_manager_save(config)) {
        *ok = true;
        return;
    }

    config->camera_jpeg_quality = previous.jpeg_quality;
    config->camera_feed_target_fps = previous.feed_target_fps;
    config->camera_rotation = previous.rotation;
    config->camera_output_width = previous.output_width;
    config->camera_output_height = previous.output_height;
    config->camera_exposure_lines = previous.exposure_lines;
    config->camera_white_balance_red_q8 = previous.white_balance_red_q8;
    config->camera_white_balance_blue_q8 = previous.white_balance_blue_q8;
    config->camera_motion_enabled = previous_motion.enabled;
    config->camera_motion_analyze_every_nth_frame = previous_motion.analyze_every_nth_frame;
    config->camera_motion_sensitivity = previous_motion.sensitivity;
    config->camera_presence_hold_seconds = previous_motion.presence_hold_seconds;
    camera_set_capture_settings(previous);
    camera_motion_set_settings(previous_motion);
    strlcpy(message, "camera settings could not be saved", message_len);
}

bool camera_save_config_raw(const uint8_t* data, size_t len) {
    StaticJsonDocument<384> doc;
    if (deserializeJson(doc, data, len)) return false;
    const bool capture_settings = doc.containsKey("jpeg_quality") || doc.containsKey("feed_target_fps") ||
                                  doc.containsKey("rotation") || doc.containsKey("output_width") ||
                                  doc.containsKey("output_height") || doc.containsKey("exposure_lines") ||
                                  doc.containsKey("white_balance_red_q8") || doc.containsKey("white_balance_blue_q8");
    const bool motion_settings = doc.containsKey("motion_enabled") || doc.containsKey("motion_analyze_every_nth_frame") ||
                                 doc.containsKey("motion_sensitivity") || doc.containsKey("presence_hold_seconds");
    if (!capture_settings && !motion_settings) return false;
    if (capture_settings && (!doc.containsKey("jpeg_quality") || !doc.containsKey("feed_target_fps") || !doc.containsKey("rotation") ||
        !doc.containsKey("output_width") || !doc.containsKey("output_height") ||
        !doc.containsKey("exposure_lines") || !doc.containsKey("white_balance_red_q8") ||
        !doc.containsKey("white_balance_blue_q8"))) {
        return false;
    }
    if (motion_settings && (!doc.containsKey("motion_enabled") || !doc.containsKey("motion_analyze_every_nth_frame") ||
        !doc.containsKey("motion_sensitivity") || !doc.containsKey("presence_hold_seconds"))) {
        return false;
    }

    const CameraCaptureSettings current_capture = camera_get_capture_settings();
    const CameraMotionSettings current_motion = camera_motion_get_settings();
    const CameraConfigSaveRequest request = {
        .update_capture_settings = capture_settings,
        .update_motion_settings = motion_settings,
        .settings = {
            .jpeg_quality = static_cast<uint8_t>(doc["jpeg_quality"] | current_capture.jpeg_quality),
            .feed_target_fps = static_cast<uint8_t>(doc["feed_target_fps"] | current_capture.feed_target_fps),
            .rotation = static_cast<CameraRotation>(doc["rotation"] | current_capture.rotation),
            .output_width = static_cast<uint16_t>(doc["output_width"] | current_capture.output_width),
            .output_height = static_cast<uint16_t>(doc["output_height"] | current_capture.output_height),
            .exposure_lines = static_cast<uint16_t>(doc["exposure_lines"] | current_capture.exposure_lines),
            .white_balance_red_q8 = static_cast<uint16_t>(doc["white_balance_red_q8"] | current_capture.white_balance_red_q8),
            .white_balance_blue_q8 = static_cast<uint16_t>(doc["white_balance_blue_q8"] | current_capture.white_balance_blue_q8),
        },
        .motion_settings = {
            .enabled = doc["motion_enabled"] | current_motion.enabled,
            .analyze_every_nth_frame = static_cast<uint8_t>(doc["motion_analyze_every_nth_frame"] |
                                                              current_motion.analyze_every_nth_frame),
            .sensitivity = static_cast<uint8_t>(doc["motion_sensitivity"] | current_motion.sensitivity),
            .presence_hold_seconds = static_cast<uint16_t>(doc["presence_hold_seconds"] | current_motion.presence_hold_seconds),
        },
    };
    bool saved = false;
    char message[64] = {};
    return loop_bridge_dispatch(camera_save_config_on_main, &request, sizeof(request),
                                kCameraConfigSaveTimeoutMs, &saved, message, sizeof(message)) == LOOP_BRIDGE_OK &&
           saved;
}

void camera_save_config(AsyncWebServerRequest* request, uint8_t* data, size_t len,
                        size_t index, size_t total) {
    component_handle_save_body(request, data, len, index, total, camera_save_config_raw,
                               kCameraConfigMaxBodyBytes);
}

} // namespace

static ComponentDef camera_component = {
    .id = "camera",
    .category = "camera",
    .display_name = "Camera settings",
    .nav_order = 30,
    .get_config = camera_get_config,
    .save_config = nullptr,
    .save_config_body = camera_save_config,
    .delete_config = nullptr,
    .custom_actions = nullptr,
    .num_custom_actions = 0,
    .fragment_id = "camera",
    .portal_script = kCameraPortalScript,
    .portal_style = kCameraPortalStyle,
};

REGISTER_COMPONENT(camera);

static ComponentDef camera_motion_component = {
    .id = "camera-motion",
    .category = "camera",
    .display_name = "Motion sensing",
    .nav_order = 35,
    .get_config = camera_get_config,
    .save_config = nullptr,
    .save_config_body = camera_save_config,
    .delete_config = nullptr,
    .custom_actions = nullptr,
    .num_custom_actions = 0,
    .fragment_id = "camera-motion",
    .portal_script = kCameraPortalScript,
    .portal_style = kCameraPortalStyle,
};

REGISTER_COMPONENT(camera_motion);

#if HAS_STORAGE_BROWSER
static ComponentDef camera_snapshots_component = {
    .id = "camera-snapshots",
    .category = "camera",
    .display_name = "Snapshots",
    .nav_order = 40,
    .get_config = nullptr,
    .save_config = nullptr,
    .save_config_body = nullptr,
    .delete_config = nullptr,
    .custom_actions = nullptr,
    .num_custom_actions = 0,
    .fragment_id = "camera-snapshots",
    .portal_script = kCameraPortalScript,
    .portal_style = kCameraPortalStyle,
};

REGISTER_COMPONENT(camera_snapshots);
#endif

#endif // HAS_CAMERA