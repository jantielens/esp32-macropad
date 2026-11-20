// Shelly operations, aggregated by darkroom_timer_device_class.cpp.

#include "../../action_registry.h"

#if HAS_DISPLAY && IS_DARKROOM_TIMER

#include "darkroom_timer_payload.h"
#include "relay_controller.h"
static const ActionArgumentDef shelly_arguments[] = {
    {"host", "Shelly host", ACTION_ARGUMENT_STRING, 0, 0, SHELLY_HOST_MAX_LEN - 1,
     true, "[var:shelly_host]", "text"},
    {"relay", "Relay index", ACTION_ARGUMENT_INTEGER, 0, 3, 0,
     true, "[var:relay]", "number"},
    {"enabled", "Relay", ACTION_ARGUMENT_BOOLEAN, 0, 0, 0,
     true, "[var:relay_enabled]", "boolean", nullptr, 0, false, nullptr, nullptr, 0,
     nullptr, 1, true},
};
static ActionResult shelly_operation(const ActionResolvedArgument* args, const char*, uint32_t) {
    relay_queue_shelly(args[0].text, static_cast<uint8_t>(args[1].integer), args[2].integer != 0);
    return ACTION_COMPLETE;
}
static const ActionOperationDef shelly_operations[] = {
    {"shelly.relay.set", "Set Shelly relay", shelly_arguments, 3, shelly_operation},
};

static void shelly_describe(JsonObject& out) {
    out["group"] = "Darkroom Timer";
    out["label"] = "Shelly relay";

}

DEFINE_AND_REGISTER_ACTION_TYPE(shelly_action_type, ACTION_TYPE_SHELLY, shelly_describe, nullptr, ACTION_EXECUTION_SYNC, true, false,
    shelly_operations, sizeof(shelly_operations) / sizeof(shelly_operations[0]));

#endif // HAS_DISPLAY && IS_DARKROOM_TIMER
