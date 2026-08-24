#include <cstdio>
#include <cstring>

#include <ArduinoJson.h>

#include "action_continuation.h"
#include "action_registry.h"
#include "camera.h"
#include "main_loop_bridge.h"
#include "pad_config.h"

static bool g_capture_result = false;

extern "C" unsigned long millis() {
    return 0;
}

static CameraCaptureSaveTo g_save_to = CAMERA_CAPTURE_SAVE_BOTH;

bool camera_capture_save(CameraCaptureSaveTo save_to) {
    g_save_to = save_to;
    return g_capture_result;
}

#include "../src/app/actions/camera_capture_action.cpp"

static LoopBridgeExec g_queued_exec = nullptr;
static CameraCaptureJob g_queued_job = {};

LoopBridgeResult loop_bridge_enqueue(LoopBridgeExec exec, const void* context,
                                     size_t context_len) {
    if (!exec || !context || context_len != sizeof(g_queued_job)) return LOOP_BRIDGE_INVALID;
    g_queued_exec = exec;
    memcpy(&g_queued_job, context, sizeof(g_queued_job));
    return LOOP_BRIDGE_OK;
}

static int g_failures = 0;

#define CHECK(condition) do { if (!(condition)) { \
    std::printf("FAIL: %s\n", #condition); ++g_failures; } } while (0)

int main() {
    const ActionTypeDef* type = action_type_find(ACTION_TYPE_CAMERA_CAPTURE);
    CHECK(type != nullptr);
    CHECK(type && type->available && type->available());

    JsonDocument valid_doc;
    valid_doc["type"] = ACTION_TYPE_CAMERA_CAPTURE;
    CHECK(type && action_type_validate(type, valid_doc.as<JsonObject>()) == nullptr);

    JsonDocument invalid_doc;
    invalid_doc["type"] = ACTION_TYPE_CAMERA_CAPTURE;
    invalid_doc["save_to"] = "archive";
    CHECK(type && action_type_validate(type, invalid_doc.as<JsonObject>()) != nullptr);

    JsonDocument latest_doc;
    latest_doc["type"] = ACTION_TYPE_CAMERA_CAPTURE;
    latest_doc["save_to"] = "latest";
    ButtonAction latest_action = {};
    type->parse(latest_doc.as<JsonObject>(), latest_action);
    CHECK(strcmp(latest_action.payload.camera_capture.save_to, "latest") == 0);
    JsonDocument serialized_latest;
    type->serialize(latest_action, serialized_latest.to<JsonObject>());
    CHECK(strcmp(serialized_latest["save_to"] | "", "latest") == 0);

    ButtonAction action = {};
    strlcpy(action.type, ACTION_TYPE_CAMERA_CAPTURE, sizeof(action.type));
    uint32_t token = 0;
    CHECK(action_continuation_begin(nullptr, 0, "Camera test", &token));
    CHECK(type && type->dispatch(action, "Camera test", token) == ACTION_PENDING);
    CHECK(g_queued_exec != nullptr);

    g_capture_result = true;
    bool completed = false;
    char message[1] = {};
    g_queued_exec(&g_queued_job, &completed, message, sizeof(message));
    CHECK(completed);
    CHECK(g_save_to == CAMERA_CAPTURE_SAVE_BOTH);
    action_continuation_mark_pending(token);
    CHECK(action_continuation_take(nullptr, nullptr, nullptr, 0) ==
          ACTION_CONTINUATION_SUCCESS);

        ButtonAction roll_action = {};
        strlcpy(roll_action.type, ACTION_TYPE_CAMERA_CAPTURE, sizeof(roll_action.type));
        strlcpy(roll_action.payload.camera_capture.save_to, "roll",
            sizeof(roll_action.payload.camera_capture.save_to));
        token = 0;
        CHECK(action_continuation_begin(nullptr, 0, "Camera roll test", &token));
        CHECK(type && type->dispatch(roll_action, "Camera roll test", token) == ACTION_PENDING);
        completed = false;
        g_queued_exec(&g_queued_job, &completed, message, sizeof(message));
        CHECK(completed);
        CHECK(g_save_to == CAMERA_CAPTURE_SAVE_ROLL);

    return g_failures ? 1 : 0;
}