#include "action_registry.h"
#include "m5stack_core2_hardware.h"

#if HAS_DISPLAY || HAS_BUTTON
namespace {
ActionResult dispatch_shutdown(const ButtonAction&, const char*, uint32_t continuation_token) {
    return m5stack_core2_request_shutdown(continuation_token) ? ACTION_PENDING : ACTION_FAILED;
}

void describe_shutdown(JsonObject& action) {
    action["group"] = "M5Stack Core2";
    action["label"] = "Shutdown";
    action["max_pending_actions"] = ACTION_CONTINUATION_SLOTS;
    action.createNestedArray("fields");
    action.createNestedArray("editor_fields");
}

DEFINE_AND_REGISTER_ACTION_TYPE(kCore2ShutdownActionType, "shutdown",
    nullptr, nullptr, dispatch_shutdown, nullptr,
    describe_shutdown, nullptr, nullptr, nullptr, ACTION_EXECUTION_PAUSABLE, false);
}
#endif