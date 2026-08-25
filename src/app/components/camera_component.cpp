#include "component_registry.h"

#if HAS_CAMERA

#include "board_config.h"
#include "camera.h"
#include "config_manager.h"
#include "main_loop_bridge.h"
#include "web_portal_json.h"
#include "web_portal_state.h"

#include <ArduinoJson.h>

namespace {

constexpr uint32_t kCameraConfigSaveTimeoutMs = 5000;
constexpr const char* kCameraPortalScript = "/portal-camera.js";
constexpr const char* kCameraPortalStyle = "/portal-camera.css";

struct CameraConfigSaveRequest {
    CameraCaptureSettings settings;
};

void camera_get_config(AsyncWebServerRequest* request) {
    const CameraCapabilities* capabilities = camera_get_capabilities();
    if (!capabilities) {
        web_portal_send_json_error(request, 503, "Camera unavailable");
        return;
    }

    const CameraCaptureSettings settings = camera_get_capture_settings();
    auto doc = make_psram_json_doc(512);
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
    if (!camera_set_capture_settings(request->settings)) {
        strlcpy(message, "invalid camera settings", message_len);
        return;
    }
    config->camera_jpeg_quality = request->settings.jpeg_quality;
    config->camera_feed_target_fps = request->settings.feed_target_fps;
    config->camera_rotation = request->settings.rotation;
    config->camera_output_width = request->settings.output_width;
    config->camera_output_height = request->settings.output_height;
    config->camera_exposure_lines = request->settings.exposure_lines;
    config->camera_white_balance_red_q8 = request->settings.white_balance_red_q8;
    config->camera_white_balance_blue_q8 = request->settings.white_balance_blue_q8;
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
    camera_set_capture_settings(previous);
    strlcpy(message, "camera settings could not be saved", message_len);
}

bool camera_save_config_raw(const uint8_t* data, size_t len) {
    StaticJsonDocument<192> doc;
    if (deserializeJson(doc, data, len)) return false;
    if (!doc.containsKey("jpeg_quality") || !doc.containsKey("feed_target_fps") || !doc.containsKey("rotation") ||
        !doc.containsKey("output_width") || !doc.containsKey("output_height") ||
        !doc.containsKey("exposure_lines") || !doc.containsKey("white_balance_red_q8") ||
        !doc.containsKey("white_balance_blue_q8")) {
        return false;
    }

    const CameraConfigSaveRequest request = {
        .settings = {
            .jpeg_quality = static_cast<uint8_t>(doc["jpeg_quality"] | 0),
            .feed_target_fps = static_cast<uint8_t>(doc["feed_target_fps"] | 0),
            .rotation = static_cast<CameraRotation>(doc["rotation"] | 0),
            .output_width = static_cast<uint16_t>(doc["output_width"] | 0),
            .output_height = static_cast<uint16_t>(doc["output_height"] | 0),
            .exposure_lines = static_cast<uint16_t>(doc["exposure_lines"] | 0),
            .white_balance_red_q8 = static_cast<uint16_t>(doc["white_balance_red_q8"] | 0),
            .white_balance_blue_q8 = static_cast<uint16_t>(doc["white_balance_blue_q8"] | 0),
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
    component_handle_save_body(request, data, len, index, total, camera_save_config_raw, 192);
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