#include "action_continuation.h"
#include "action_registry.h"

#if HAS_CAMERA

#include "camera.h"
#include "log_manager.h"
#include "main_loop_bridge.h"

#include <string.h>

namespace {

constexpr const char* kCameraCaptureActionTag = "Action";

struct CameraCaptureJob {
    uint32_t continuation_token;
    CameraCaptureSaveTo save_to;
};

CameraCaptureSaveTo camera_capture_save_to_parse(const char* save_to) {
    if (strcmp(save_to, "latest") == 0) return CAMERA_CAPTURE_SAVE_LATEST;
    if (strcmp(save_to, "roll") == 0) return CAMERA_CAPTURE_SAVE_ROLL;
    return CAMERA_CAPTURE_SAVE_BOTH;
}

bool camera_capture_save_to_is_valid(const char* save_to) {
    return strcmp(save_to, "latest") == 0 || strcmp(save_to, "roll") == 0 ||
           strcmp(save_to, "both") == 0;
}

void parse_camera_capture(const JsonObject& action, ButtonAction& act) {
    const char* save_to = action["save_to"] | "both";
    if (!camera_capture_save_to_is_valid(save_to)) {
        memset(&act, 0, sizeof(act));
        return;
    }
    strlcpy(act.payload.camera_capture.save_to, save_to,
            sizeof(act.payload.camera_capture.save_to));
}

void serialize_camera_capture(const ButtonAction& act, JsonObject action) {
    if (act.payload.camera_capture.save_to[0] &&
        strcmp(act.payload.camera_capture.save_to, "both") != 0) {
        action["save_to"] = act.payload.camera_capture.save_to;
    }
}

bool camera_capture_available() {
    return true;
}

const char* validate_camera_capture(JsonObjectConst action) {
    for (JsonPairConst field : action) {
        const char* name = field.key().c_str();
        if (strcmp(name, "type") != 0 && strcmp(name, "save_to") != 0) {
            return "camera_capture accepts only save_to";
        }
    }
    if (action.containsKey("save_to") && !action["save_to"].is<const char*>()) {
        return "camera_capture save_to must be a string";
    }
    if (action.containsKey("save_to") &&
        !camera_capture_save_to_is_valid(action["save_to"].as<const char*>())) {
        return "camera_capture save_to must be latest, roll, or both";
    }
    return nullptr;
}

void execute_camera_capture(const void* opaque, bool* ok, char*, size_t) {
    const CameraCaptureJob* job = static_cast<const CameraCaptureJob*>(opaque);
    if (!job || !job->continuation_token) return;
    *ok = camera_capture_save(job->save_to);
    action_continuation_complete(job->continuation_token, *ok);
}

ActionResult dispatch_camera_capture(const ButtonAction& action, const char* label,
                                     uint32_t continuation_token) {
    if (!continuation_token) {
        LOGW(kCameraCaptureActionTag, "%s camera capture: must be used in an action list", label);
        return ACTION_FAILED;
    }
    const CameraCaptureJob job = {
        .continuation_token = continuation_token,
        .save_to = camera_capture_save_to_parse(action.payload.camera_capture.save_to),
    };
    const LoopBridgeResult result = loop_bridge_enqueue(
        execute_camera_capture, &job, sizeof(job));
    if (result != LOOP_BRIDGE_OK) {
        LOGW(kCameraCaptureActionTag, "%s camera capture: main loop is busy", label);
        return ACTION_FAILED;
    }
    return ACTION_PENDING;
}

void describe_camera_capture(JsonObject& action) {
    action["group"] = "Camera";
    action["label"] = "Save camera snapshot";
    JsonArray fields = action.createNestedArray("fields");
    JsonObject save_to = fields.createNestedObject();
    save_to["name"] = "save_to";
    save_to["description"] = "latest, roll, or both; defaults to both";
    JsonArray editor_fields = action.createNestedArray("editor_fields");
    JsonObject editor_save_to = editor_fields.createNestedObject();
    editor_save_to["name"] = "save_to";
    editor_save_to["label"] = "Save to";
    editor_save_to["type"] = "select";
    editor_save_to["default"] = "both";
    JsonArray options = editor_save_to.createNestedArray("options");
    JsonObject latest = options.createNestedObject();
    latest["id"] = "latest";
    latest["label"] = "Latest image";
    JsonObject roll = options.createNestedObject();
    roll["id"] = "roll";
    roll["label"] = "Camera roll image";
    JsonObject both = options.createNestedObject();
    both["id"] = "both";
    both["label"] = "Latest and camera roll";
}

DEFINE_AND_REGISTER_ACTION_TYPE(kCameraCaptureActionType,
    ACTION_TYPE_CAMERA_CAPTURE, parse_camera_capture, serialize_camera_capture,
    dispatch_camera_capture, nullptr, describe_camera_capture,
    camera_capture_available, validate_camera_capture
);

} // namespace

#endif // HAS_CAMERA