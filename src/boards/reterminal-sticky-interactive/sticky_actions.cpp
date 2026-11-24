#include "action_registry.h"
#include "sticky_hardware.h"

#if HAS_DISPLAY || HAS_BUTTON
namespace {

const ActionArgumentDef sticky_shutdown_arguments[] = {
    {"message", "Shutdown message (optional)", ACTION_ARGUMENT_STRING, 0, 0,
     CONFIG_DIALOG_MESSAGE_MAX_LEN - 1, true, "Powered off", "multiline",
     nullptr, 0, true, nullptr, nullptr, 0, "", 0, false, nullptr,
     "Leave empty to keep the current screen. On e-paper, a message requests a full refresh and stays visible after power-off.",
     true},
    {"delay_ms", "Delay before shutdown (ms)", ACTION_ARGUMENT_INTEGER, 0, STICKY_SHUTDOWN_MAX_DELAY_MS,
     0, true, "5000", "number", nullptr, 0, false, nullptr, nullptr, 0,
     nullptr, 0, true, nullptr,
     "Minimum wait after showing the message; for example, 5000 ms for button feedback. Shutdown also waits for message presentation.",
     true},
};

ActionResult run_sticky_shutdown(const ActionResolvedArgument* arguments, const char*, uint32_t token) {
    return sticky_request_shutdown(token, arguments[0].text,
        static_cast<uint32_t>(arguments[1].integer)) ? ACTION_PENDING : ACTION_FAILED;
}

const ActionOperationDef sticky_shutdown_operations[] = {
    {"device.shutdown", "Shutdown", sticky_shutdown_arguments, 2, run_sticky_shutdown, ACTION_EXECUTION_PAUSABLE},
};

void describe_sticky_shutdown(JsonObject& action) {
    action["group"] = "reTerminal Sticky";
    action["label"] = "Shutdown";
    action["max_pending_actions"] = ACTION_CONTINUATION_SLOTS;
}

DEFINE_AND_REGISTER_ACTION_TYPE(kStickyShutdownActionType, "shutdown", describe_sticky_shutdown, nullptr, ACTION_EXECUTION_PAUSABLE, false, false, sticky_shutdown_operations, 1);

}
#endif
