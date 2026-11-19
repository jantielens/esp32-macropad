#include "action_continuation.h"
#include "action_registry.h"
#include "log_manager.h"

#if HAS_DISPLAY || HAS_BUTTON

namespace {

constexpr const char* TAG = "Action";

ActionResult run_delay(uint32_t duration_ms, const char* label, uint32_t continuation_token) {
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

const ActionArgumentDef kWaitArguments[] = {
    {"duration_ms", "Duration (ms)", ACTION_ARGUMENT_INTEGER, 1, ACTION_DELAY_MAX_DURATION_MS,
     0, true, "[expr:[event:step]*1000]", "number"},
};

ActionResult dispatch_wait(const ActionResolvedArgument* arguments, const char* label,
                           uint32_t continuation_token) {
    return run_delay(static_cast<uint32_t>(arguments[0].integer), label, continuation_token);
}

const ActionOperationDef kDelayOperations[] = {
    {"delay.wait", "Delay", kWaitArguments, 1, dispatch_wait, ACTION_EXECUTION_PAUSABLE},
};

void describe_delay(JsonObject& action) {
    action["group"] = "Timer";
    action["label"] = "Delay";
    action["max_pending_actions"] = ACTION_CONTINUATION_SLOTS;
}

DEFINE_AND_REGISTER_ACTION_TYPE(kDelayActionType, ACTION_TYPE_DELAY, describe_delay, nullptr, ACTION_EXECUTION_PAUSABLE, true, false, kDelayOperations, 1);

} // namespace

#endif // HAS_DISPLAY || HAS_BUTTON