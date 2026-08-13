#include "action_continuation.h"
#include "action_registry.h"
#include "log_manager.h"

#if HAS_DISPLAY || HAS_BUTTON

namespace {

constexpr const char* TAG = "Action";

void parse_delay(const JsonObject& action, ButtonAction& act) {
    if (!action["duration_ms"].is<uint32_t>()) {
        memset(&act, 0, sizeof(act));
        return;
    }
    act.payload.delay.duration_ms = action["duration_ms"].as<uint32_t>();
    if (!action_delay_duration_is_valid(act.payload.delay.duration_ms)) {
        memset(&act, 0, sizeof(act));
    }
}

void serialize_delay(const ButtonAction& act, JsonObject action) {
    action["duration_ms"] = act.payload.delay.duration_ms;
}

ActionResult dispatch_delay(const ButtonAction& act, const char* label,
                            uint32_t continuation_token) {
    const uint32_t duration_ms = act.payload.delay.duration_ms;
    if (!action_delay_duration_is_valid(duration_ms)) {
        LOGW(TAG, "%s delay: duration must be 1-%u ms", label,
             (unsigned)ACTION_DELAY_MAX_DURATION_MS);
        return ACTION_FAILED;
    }
    if (!continuation_token) {
        LOGW(TAG, "%s delay: %s", label,
             action_continuation_is_full()
                 ? "all pausable action slots are occupied"
                 : "must be used in an action list");
        return ACTION_FAILED;
    }
    if (!action_continuation_schedule_success(continuation_token, duration_ms)) {
        LOGW(TAG, "%s delay: continuation is no longer available", label);
        return ACTION_FAILED;
    }
    LOGI(TAG, "%s delay: %lu ms", label, (unsigned long)duration_ms);
    return ACTION_PENDING;
}

const char* validate_delay(JsonObjectConst action) {
    if (!action.containsKey("duration_ms") || !action["duration_ms"].is<uint32_t>()) {
        return "delay duration_ms must be a whole number";
    }
    return action_delay_duration_is_valid(action["duration_ms"].as<uint32_t>())
        ? nullptr : "delay duration_ms must be 1-55000";
}

void describe_delay(JsonObject& action) {
    action["group"] = "Timer";
    action["label"] = "Delay";
    action["max_pending_actions"] = ACTION_CONTINUATION_SLOTS;
    JsonArray fields = action.createNestedArray("fields");
    JsonObject duration = fields.createNestedObject();
    duration["name"] = "duration_ms";
    duration["description"] = "whole milliseconds, 1-55000";
}

DEFINE_AND_REGISTER_ACTION_TYPE(kDelayActionType,
    ACTION_TYPE_DELAY, parse_delay, serialize_delay, dispatch_delay,
    nullptr, describe_delay, nullptr, validate_delay
);

} // namespace

#endif // HAS_DISPLAY || HAS_BUTTON