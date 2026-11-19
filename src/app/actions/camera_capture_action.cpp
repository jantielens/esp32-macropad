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

const char* const kCameraDestinations[] = {"both", "latest", "roll"};
const char* const kCameraDestinationLabels[] = {
    "Latest and camera roll", "Latest image", "Camera roll image"
};
const ActionArgumentDef kCameraCaptureArguments[] = {
    {"save_to", "Save to", ACTION_ARGUMENT_ENUM, 0, 0, 6, true,
     "[var:camera_destination]", "select", kCameraDestinations, 3, false, nullptr,
     kCameraDestinationLabels},
};

bool camera_capture_available() {
    return true;
}

void execute_camera_capture(const void* opaque, bool* ok, char*, size_t) {
    const CameraCaptureJob* job = static_cast<const CameraCaptureJob*>(opaque);
    if (!job || !job->continuation_token) return;
    *ok = camera_capture_save(job->save_to);
    action_continuation_complete(job->continuation_token, *ok);
}

ActionResult queue_camera_capture(CameraCaptureSaveTo save_to, const char* label,
                                  uint32_t continuation_token) {
    if (!continuation_token) {
        LOGW(kCameraCaptureActionTag, "%s camera capture: must be used in an action list", label);
        return ACTION_FAILED;
    }
    const CameraCaptureJob job = {
        .continuation_token = continuation_token,
        .save_to = save_to,
    };
    const LoopBridgeResult result = loop_bridge_enqueue(
        execute_camera_capture, &job, sizeof(job));
    if (result != LOOP_BRIDGE_OK) {
        LOGW(kCameraCaptureActionTag, "%s camera capture: main loop is busy", label);
        return ACTION_FAILED;
    }
    return ACTION_PENDING;
}

ActionResult dispatch_camera_capture_operation(const ActionResolvedArgument* arguments,
                                               const char* label, uint32_t continuation_token) {
    return queue_camera_capture(camera_capture_save_to_parse(arguments[0].text), label, continuation_token);
}

const ActionOperationDef kCameraCaptureOperations[] = {
    {"camera.capture", "Save camera snapshot", kCameraCaptureArguments, 1,
     dispatch_camera_capture_operation, ACTION_EXECUTION_PAUSABLE},
};

void describe_camera_capture(JsonObject& action) {
    action["group"] = "Camera";
    action["label"] = "Save camera snapshot";
}

DEFINE_AND_REGISTER_ACTION_TYPE(kCameraCaptureActionType, ACTION_TYPE_CAMERA_CAPTURE, describe_camera_capture, camera_capture_available, ACTION_EXECUTION_PAUSABLE, true, false,
    kCameraCaptureOperations, sizeof(kCameraCaptureOperations) / sizeof(kCameraCaptureOperations[0]));

} // namespace

#endif // HAS_CAMERA