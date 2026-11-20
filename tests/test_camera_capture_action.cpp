#include <cstdio>
#include <cstring>

#include <ArduinoJson.h>

#include "action_continuation.h"
#include "action_catalog.h"
#include "action_registry.h"
#include "action_validate.h"
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
static bool g_queue_busy = false;

LoopBridgeResult loop_bridge_enqueue(LoopBridgeExec exec, const void* context,
                                     size_t context_len) {
    if (!exec || !context || context_len != sizeof(g_queued_job)) return LOOP_BRIDGE_INVALID;
    if (g_queue_busy) return LOOP_BRIDGE_BUSY;
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
    JsonDocument catalog;
    action_catalog_emit(catalog.to<JsonArray>());
    JsonObjectConst argument = catalog[0]["operations"][0]["arguments"][0];
    CHECK(strcmp(argument["choices"][0] | "", "both") == 0);
    CHECK(strcmp(argument["choices"][2] | "", "roll") == 0);
    CHECK(strcmp(argument["choice_labels"][2] | "", "Camera roll image") == 0);
    CHECK(type && type->operations[0].execution == ACTION_EXECUTION_PAUSABLE);

    const ActionOperationDef* operation = action_operation_find("camera.capture");
    CHECK(operation != nullptr);
    if (!operation) return 1;
    const char* normalized = nullptr;
    CHECK(action_argument_normalize_text(operation->arguments[0], "archive", &normalized, nullptr));
    struct CaptureCase { const char* destination; CameraCaptureSaveTo expected; };
    for (const CaptureCase& capture : {CaptureCase{"both", CAMERA_CAPTURE_SAVE_BOTH},
                                        CaptureCase{"latest", CAMERA_CAPTURE_SAVE_LATEST},
                                        CaptureCase{"roll", CAMERA_CAPTURE_SAVE_ROLL}}) {
        CHECK(!action_argument_normalize_text(operation->arguments[0], capture.destination, &normalized, nullptr));
        ActionResolvedArgument arguments[] = {{normalized, 0, 0}};
        uint32_t token = 0;
        CHECK(action_continuation_begin(nullptr, 0, "Camera test", &token));
        CHECK(operation->dispatch(arguments, "Camera test", token) == ACTION_PENDING);
        CHECK(g_queued_exec != nullptr);
        g_capture_result = true;
        bool completed = false;
        char message[1] = {};
        g_queued_exec(&g_queued_job, &completed, message, sizeof(message));
        CHECK(completed);
        CHECK(g_save_to == capture.expected);
        action_continuation_mark_pending(token);
        CHECK(action_continuation_take(nullptr, nullptr, nullptr, 0) ==
              ACTION_CONTINUATION_SUCCESS);
    }

    ActionResolvedArgument arguments[] = {{"both", 0, 0}};
    CHECK(operation->dispatch(arguments, "Camera test", 0) == ACTION_FAILED);
    uint32_t token = 0;
    CHECK(action_continuation_begin(nullptr, 0, "Camera test", &token));
    g_queue_busy = true;
    CHECK(operation->dispatch(arguments, "Camera test", token) == ACTION_FAILED);
    g_queue_busy = false;
    action_continuation_release(token);

    token = 0;
    CHECK(action_continuation_begin(nullptr, 0, "Camera failure", &token));
    CHECK(operation->dispatch(arguments, "Camera failure", token) == ACTION_PENDING);
    g_capture_result = false;
    bool completed = true;
    g_queued_exec(&g_queued_job, &completed, nullptr, 0);
    CHECK(!completed);
    action_continuation_mark_pending(token);
    CHECK(action_continuation_take(nullptr, nullptr, nullptr, 0) ==
          ACTION_CONTINUATION_FAILED);

    return g_failures ? 1 : 0;
}