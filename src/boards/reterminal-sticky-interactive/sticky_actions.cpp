#include "action_registry.h"
#include "sticky_hardware.h"

#if HAS_DISPLAY || HAS_BUTTON
namespace {

ActionResult run_sticky_shutdown(const ActionResolvedArgument*, const char*, uint32_t token) {
    return sticky_request_shutdown(token) ? ACTION_PENDING : ACTION_FAILED;
}

const ActionOperationDef sticky_shutdown_operations[] = {
    {"device.shutdown", "Shutdown", nullptr, 0, run_sticky_shutdown, ACTION_EXECUTION_PAUSABLE},
};

void describe_sticky_shutdown(JsonObject& action) {
    action["group"] = "reTerminal Sticky";
    action["label"] = "Shutdown";
    action["max_pending_actions"] = ACTION_CONTINUATION_SLOTS;
}

DEFINE_AND_REGISTER_ACTION_TYPE(kStickyShutdownActionType, "shutdown", describe_sticky_shutdown, nullptr, ACTION_EXECUTION_PAUSABLE, false, false, sticky_shutdown_operations, 1);

}
#endif
