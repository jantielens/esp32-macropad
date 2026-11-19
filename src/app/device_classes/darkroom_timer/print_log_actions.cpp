// Print log operations, aggregated by darkroom_timer_device_class.cpp.

#include "../../action_registry.h"

#if HAS_DISPLAY && IS_DARKROOM_TIMER

#include "darkroom_timer_payload.h"
#include "print_log.h"
static const ActionArgumentDef print_star_arguments[] = {
    {"starred", "Starred", ACTION_ARGUMENT_BOOLEAN, 0, 0, 0,
     true, "[var:starred]", "boolean"},
};
static ActionResult print_toggle_star(const ActionResolvedArgument*, const char*, uint32_t) {
    print_log_dispatch("toggle_star", "");
    return ACTION_COMPLETE;
}
static ActionResult print_set_star(const ActionResolvedArgument* args, const char*, uint32_t) {
    print_log_dispatch("set_star", args[0].integer ? "1" : "0");
    return ACTION_COMPLETE;
}
static const ActionOperationDef print_operations[] = {
    {"print.star.toggle", "Toggle print star", nullptr, 0, print_toggle_star},
    {"print.star.set", "Set print star", print_star_arguments, 1, print_set_star},
};

static void print_describe(JsonObject& out) {
    out["group"] = "Darkroom Timer";
    out["label"] = "Print log";
}

DEFINE_AND_REGISTER_ACTION_TYPE(print_action_type, ACTION_TYPE_PRINT, print_describe, nullptr, ACTION_EXECUTION_SYNC,
    true, false, print_operations, sizeof(print_operations) / sizeof(print_operations[0]));

#endif // HAS_DISPLAY && IS_DARKROOM_TIMER
