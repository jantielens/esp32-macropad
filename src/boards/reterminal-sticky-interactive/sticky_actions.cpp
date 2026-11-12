#include "action_registry.h"
#include "sticky_hardware.h"

#if HAS_DISPLAY || HAS_BUTTON
namespace {

ActionResult dispatch_sticky_shutdown(const ButtonAction&, const char*, uint32_t token) {
    return sticky_request_shutdown(token) ? ACTION_PENDING : ACTION_FAILED;
}

void describe_sticky_shutdown(JsonObject& action) {
    action["group"] = "reTerminal Sticky";
    action["label"] = "Shutdown";
    action["max_pending_actions"] = ACTION_CONTINUATION_SLOTS;
    action["fields"].to<JsonArray>();
    action["editor_fields"].to<JsonArray>();
}

DEFINE_AND_REGISTER_ACTION_TYPE(kStickyShutdownActionType, "shutdown",
    nullptr, nullptr, dispatch_sticky_shutdown, nullptr,
    describe_sticky_shutdown, nullptr, nullptr, ACTION_EXECUTION_PAUSABLE, false);

}
#endif
