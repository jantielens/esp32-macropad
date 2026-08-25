// ============================================================================
// mcp_tools_camera.cpp — MCP controls for camera-enabled boards only.
//
// Camera frames stay on the authenticated portal/storage surfaces. MCP is
// JSON-only, so it reports those routes and controls capture/persistence
// without base64-encoding potentially large images into a tool response.
// ============================================================================

#include "board_config.h"

#if HAS_MCP && HAS_CAMERA

#include "camera.h"
#include "config_manager.h"
#include "mcp_tool_registry.h"
#include "mcp_tool_util.h"
#include "web_portal_state.h"

#include <ArduinoJson.h>

namespace {

constexpr uint32_t kCameraControlTimeoutMs = 5000;

void camera_settings_to_json(JsonObject& result) {
    const CameraCapabilities* capabilities = camera_get_capabilities();
    const CameraCaptureSettings settings = camera_get_capture_settings();

    result["detected"] = camera_is_detected();
    result["jpeg_quality"] = settings.jpeg_quality;
    result["feed_target_fps"] = settings.feed_target_fps;
    result["rotation"] = settings.rotation;
    result["output_width"] = settings.output_width;
    result["output_height"] = settings.output_height;
    result["exposure_lines"] = settings.exposure_lines;
    result["white_balance_red_q8"] = settings.white_balance_red_q8;
    result["white_balance_blue_q8"] = settings.white_balance_blue_q8;

    if (!capabilities) return;
    JsonObject limits = result.createNestedObject("limits");
    limits["raw_pixel_format"] = capabilities->raw_pixel_format;
    limits["raw_width"] = capabilities->raw_width;
    limits["raw_height"] = capabilities->raw_height;
    limits["jpeg_quality_min"] = capabilities->jpeg_quality_min;
    limits["jpeg_quality_max"] = capabilities->jpeg_quality_max;
    limits["feed_target_fps_min"] = capabilities->feed_target_fps_min;
    limits["feed_target_fps_max"] = capabilities->feed_target_fps_max;
    limits["exposure_lines_min"] = capabilities->exposure_lines_min;
    limits["exposure_lines_max"] = capabilities->exposure_lines_max;
    limits["exposure_line_time_us"] = capabilities->exposure_line_time_us;
    limits["white_balance_q8_min"] = capabilities->white_balance_q8_min;
    limits["white_balance_q8_max"] = capabilities->white_balance_q8_max;
    JsonArray dimensions = limits.createNestedArray("output_dimensions");
    for (size_t index = 0; index < capabilities->output_dimensions_count; ++index) {
        JsonObject dimension = dimensions.createNestedObject();
        dimension["width"] = capabilities->output_dimensions[index].width;
        dimension["height"] = capabilities->output_dimensions[index].height;
    }
}

bool camera_fail(JsonObject& result, String& err, int code, const char* message) {
    return mcp_tool_fail(result, err, code, message);
}

bool tool_get_camera_status(const JsonObject& args, JsonObject& result, String& err) {
    (void)args;
    (void)err;
    camera_settings_to_json(result);
    JsonObject endpoints = result.createNestedObject("endpoints");
    endpoints["jpeg_snapshot"] = "/api/camera/snapshot.jpg";
    endpoints["raw_snapshot"] = "/api/camera/snapshot.raw";
    endpoints["mjpeg_stream"] = "/api/camera/stream";
    endpoints["latest_saved_image"] = "/camera/latest.jpg";
    result["endpoint_note"] =
        "Portal endpoints use portal authentication, not the MCP bearer token. "
        "Use capture_camera_snapshot to save an image, then use the Storage browser or portal to inspect it.";
    return true;
}

struct CameraConfigRequest {
    CameraCaptureSettings settings;
};

void exec_set_camera_config(const void* opaque, bool* ok, char* message, size_t message_len) {
    const CameraConfigRequest* request = static_cast<const CameraConfigRequest*>(opaque);
    DeviceConfig* config = web_portal_get_current_config();
    if (!request || !config) {
        strlcpy(message, "camera configuration unavailable", message_len);
        return;
    }

    const CameraCaptureSettings previous = camera_get_capture_settings();
    if (!camera_set_capture_settings(request->settings)) {
        strlcpy(message, "invalid camera settings", message_len);
        return;
    }

    const CameraCaptureSettings previous_persisted = {
        .jpeg_quality = config->camera_jpeg_quality,
        .feed_target_fps = config->camera_feed_target_fps,
        .rotation = config->camera_rotation,
        .output_width = config->camera_output_width,
        .output_height = config->camera_output_height,
        .exposure_lines = config->camera_exposure_lines,
        .white_balance_red_q8 = config->camera_white_balance_red_q8,
        .white_balance_blue_q8 = config->camera_white_balance_blue_q8,
    };
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
        strlcpy(message, "camera settings saved", message_len);
        return;
    }

    config->camera_jpeg_quality = previous_persisted.jpeg_quality;
    config->camera_feed_target_fps = previous_persisted.feed_target_fps;
    config->camera_rotation = previous_persisted.rotation;
    config->camera_output_width = previous_persisted.output_width;
    config->camera_output_height = previous_persisted.output_height;
    config->camera_exposure_lines = previous_persisted.exposure_lines;
    config->camera_white_balance_red_q8 = previous_persisted.white_balance_red_q8;
    config->camera_white_balance_blue_q8 = previous_persisted.white_balance_blue_q8;
    camera_set_capture_settings(previous);
    strlcpy(message, "camera settings could not be saved", message_len);
}

bool tool_set_camera_config(const JsonObject& args, JsonObject& result, String& err) {
    CameraConfigRequest request = {.settings = camera_get_capture_settings()};
    bool changed = false;

    if (args.containsKey("jpeg_quality")) {
        const int value = args["jpeg_quality"] | -1;
        if (value < 0 || value > UINT8_MAX) return camera_fail(result, err, MCP_RPC_ERR_PARAMS, "jpeg_quality must be an unsigned 8-bit integer");
        request.settings.jpeg_quality = static_cast<uint8_t>(value);
        changed = true;
    }
    if (args.containsKey("feed_target_fps")) {
        const int value = args["feed_target_fps"] | -1;
        if (value < 0 || value > UINT8_MAX) return camera_fail(result, err, MCP_RPC_ERR_PARAMS, "feed_target_fps must be an unsigned 8-bit integer");
        request.settings.feed_target_fps = static_cast<uint8_t>(value);
        changed = true;
    }
    if (args.containsKey("rotation")) {
        const int value = args["rotation"] | -1;
        if (value < 0 || value > UINT16_MAX) return camera_fail(result, err, MCP_RPC_ERR_PARAMS, "rotation must be an unsigned 16-bit integer");
        request.settings.rotation = static_cast<CameraRotation>(value);
        changed = true;
    }
    if (args.containsKey("output_width")) {
        const int value = args["output_width"] | -1;
        if (value < 0 || value > UINT16_MAX) return camera_fail(result, err, MCP_RPC_ERR_PARAMS, "output_width must be an unsigned 16-bit integer");
        request.settings.output_width = static_cast<uint16_t>(value);
        changed = true;
    }
    if (args.containsKey("output_height")) {
        const int value = args["output_height"] | -1;
        if (value < 0 || value > UINT16_MAX) return camera_fail(result, err, MCP_RPC_ERR_PARAMS, "output_height must be an unsigned 16-bit integer");
        request.settings.output_height = static_cast<uint16_t>(value);
        changed = true;
    }
    if (args.containsKey("exposure_lines")) {
        const int value = args["exposure_lines"] | -1;
        if (value < 0 || value > UINT16_MAX) return camera_fail(result, err, MCP_RPC_ERR_PARAMS, "exposure_lines must be an unsigned 16-bit integer");
        request.settings.exposure_lines = static_cast<uint16_t>(value);
        changed = true;
    }
    if (args.containsKey("white_balance_red_q8")) {
        const int value = args["white_balance_red_q8"] | -1;
        if (value < 0 || value > UINT16_MAX) return camera_fail(result, err, MCP_RPC_ERR_PARAMS, "white_balance_red_q8 must be an unsigned 16-bit integer");
        request.settings.white_balance_red_q8 = static_cast<uint16_t>(value);
        changed = true;
    }
    if (args.containsKey("white_balance_blue_q8")) {
        const int value = args["white_balance_blue_q8"] | -1;
        if (value < 0 || value > UINT16_MAX) return camera_fail(result, err, MCP_RPC_ERR_PARAMS, "white_balance_blue_q8 must be an unsigned 16-bit integer");
        request.settings.white_balance_blue_q8 = static_cast<uint16_t>(value);
        changed = true;
    }
    if (!changed) return camera_fail(result, err, MCP_RPC_ERR_PARAMS, "provide at least one camera setting");

    if (!mcp_run_control(exec_set_camera_config, &request, sizeof(request),
                         kCameraControlTimeoutMs, result, err)) {
        return false;
    }
    camera_settings_to_json(result);
    return true;
}

struct CameraCaptureRequest {
    CameraCaptureSaveTo save_to;
};

void exec_capture_camera_snapshot(const void* opaque, bool* ok, char* message, size_t message_len) {
    const CameraCaptureRequest* request = static_cast<const CameraCaptureRequest*>(opaque);
    if (!request || !camera_capture_save(request->save_to)) {
        strlcpy(message, "camera snapshot could not be saved", message_len);
        return;
    }
    *ok = true;
    strlcpy(message, "camera snapshot saved", message_len);
}

bool tool_capture_camera_snapshot(const JsonObject& args, JsonObject& result, String& err) {
    const char* save_to = args["save_to"] | "both";
    CameraCaptureSaveTo destination = CAMERA_CAPTURE_SAVE_BOTH;
    if (strcmp(save_to, "latest") == 0) {
        destination = CAMERA_CAPTURE_SAVE_LATEST;
    } else if (strcmp(save_to, "roll") == 0) {
        destination = CAMERA_CAPTURE_SAVE_ROLL;
    } else if (strcmp(save_to, "both") != 0) {
        return camera_fail(result, err, MCP_RPC_ERR_PARAMS, "save_to must be latest, roll, or both");
    }

    const CameraCaptureRequest request = {.save_to = destination};
    if (!mcp_run_control(exec_capture_camera_snapshot, &request, sizeof(request),
                         kCameraControlTimeoutMs, result, err)) {
        return false;
    }
    result["save_to"] = save_to;
    if (destination != CAMERA_CAPTURE_SAVE_ROLL) {
        result["latest_saved_image"] = "/camera/latest.jpg";
    }
    if (destination != CAMERA_CAPTURE_SAVE_LATEST) {
        result["roll_note"] = "Saved in /camera/YYYYMMDD/ when NTP time is valid.";
    }
    return true;
}

} // namespace

static const McpTool s_tool_get_camera_status = {
    "get_camera_status",
    "Read the camera's detection state, active capture settings, hardware limits, and authenticated portal endpoints for JPEG, RAW10, and MJPEG output.",
    "{\"type\":\"object\",\"properties\":{}}",
    tool_get_camera_status, true, false, false
};
REGISTER_MCP_TOOL(s_tool_get_camera_status);

static const McpTool s_tool_set_camera_config = {
    "set_camera_config",
    "Persist one or more camera capture settings. Omitted fields retain their current values. Read get_camera_status first for board-specific limits and output dimensions.",
    "{\"type\":\"object\",\"properties\":{\"jpeg_quality\":{\"type\":\"integer\"},\"feed_target_fps\":{\"type\":\"integer\"},\"rotation\":{\"type\":\"integer\",\"enum\":[0,90,180,270]},\"output_width\":{\"type\":\"integer\"},\"output_height\":{\"type\":\"integer\"},\"exposure_lines\":{\"type\":\"integer\"},\"white_balance_red_q8\":{\"type\":\"integer\"},\"white_balance_blue_q8\":{\"type\":\"integer\"}}}",
    tool_set_camera_config, false, false, true
};
REGISTER_MCP_TOOL(s_tool_set_camera_config);

static const McpTool s_tool_capture_camera_snapshot = {
    "capture_camera_snapshot",
    "Capture and save a JPEG image to the latest snapshot, the dated camera roll, or both. Returns storage paths; image bytes remain available through the authenticated portal or Storage browser.",
    "{\"type\":\"object\",\"properties\":{\"save_to\":{\"type\":\"string\",\"enum\":[\"latest\",\"roll\",\"both\"],\"default\":\"both\"}}}",
    tool_capture_camera_snapshot, false, false, true
};
REGISTER_MCP_TOOL(s_tool_capture_camera_snapshot);

void mcp_camera_capabilities(JsonObject& out) {
    JsonObject camera = out.createNestedObject("camera");
    camera["status_tool"] = "get_camera_status";
    camera["config_tool"] = "set_camera_config";
    camera["capture_tool"] = "capture_camera_snapshot";
    camera["image_transport"] = "camera image bytes remain on authenticated portal/storage endpoints";
}

#endif // HAS_MCP && HAS_CAMERA