#pragma once

#include "action_continuation.h"
#include "config_manager.h"
#include "log_manager.h"
#include "main_loop_bridge.h"

extern DeviceConfig device_config;

namespace {
struct SaveLevelJob {
    uint32_t token;
    uint8_t value;
    bool (*save)(DeviceConfig*, uint8_t);
    const char* level;
};

void execute_save_level(const void* opaque, bool* ok, char*, size_t) {
    const auto* job = static_cast<const SaveLevelJob*>(opaque);
    *ok = job->save(&device_config, job->value);
    if (!*ok) LOGW("Action", "Failed to save %s", job->level);
    if (!action_continuation_complete(job->token, *ok))
        LOGW("Action", "Save %s could not complete its action sequence", job->level);
}

ActionResult queue_save_level(uint8_t value, bool (*save)(DeviceConfig*, uint8_t),
                              const char* level, const char* label, uint32_t token) {
    if (loop_bridge_is_consumer()) {
        if (save(&device_config, value)) return ACTION_COMPLETE;
        LOGW("Action", "%s: failed to save %s", label, level);
        return ACTION_FAILED;
    }
    if (!token || !action_continuation_set_timeout(token, 0)) {
        LOGW("Action", "%s: save %s rejected", label, level);
        return ACTION_FAILED;
    }
    const SaveLevelJob job = {token, value, save, level};
    const LoopBridgeResult result = loop_bridge_enqueue(execute_save_level, &job, sizeof(job));
    if (result != LOOP_BRIDGE_OK) {
        LOGW("Action", "%s: could not queue save %s (%d)", label, level, result);
        return ACTION_FAILED;
    }
    return ACTION_PENDING;
}
}
