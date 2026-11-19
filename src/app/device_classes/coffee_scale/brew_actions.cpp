// Brew operations, aggregated by coffee_scale_device_class.cpp.

#include "../../action_registry.h"

#if HAS_DISPLAY && IS_COFFEE_SCALE

#include "../../log_manager.h"
#include "coffee_scale_payload.h"
#include "scale_hal.h"
#include "brew/brew_manager.h"
#include "brew/brew_templates.h"

#include <string.h>

#define TAG "BrewAction"

static void brew_describe(JsonObject& out) {
    out["group"] = "Coffee Scale";
    out["label"] = "Brew";
}

#if HAS_SCALE
static const ActionArgumentDef brew_template_arguments[] = {
    {"template", "Brew template", ACTION_ARGUMENT_STRING, 0, 0, CONFIG_BINDABLE_SHORT_LEN - 1,
     true, "[brew:tpl_0_name]", "brew_templates"},
};
static ActionResult brew_set_template_operation(const ActionResolvedArgument* args, const char* label, uint32_t) {
    const BrewTemplate* found = brew_template_find(args[0].text);
    if (!found || strcmp(found->name, args[0].text)) {
        LOGW(TAG, "%s brew: template not found", label);
        return ACTION_FAILED;
    }
    brew_hint_template(args[0].text);
    return ACTION_COMPLETE;
}
#define BREW_OPERATION(name, call) \
    static ActionResult name(const ActionResolvedArgument*, const char*, uint32_t) { \
        call; return ACTION_COMPLETE; \
    }
BREW_OPERATION(brew_advance_operation, brew_advance(nullptr))
BREW_OPERATION(brew_start_operation, brew_start(nullptr))
BREW_OPERATION(brew_next_operation, brew_next())
BREW_OPERATION(brew_stop_operation, brew_stop())
BREW_OPERATION(brew_reset_operation, brew_reset())
BREW_OPERATION(brew_tare_operation, scale_request_tare_no_persist())
#undef BREW_OPERATION
static const ActionOperationDef brew_operations[] = {
    {"brew.advance", "Advance brew", nullptr, 0, brew_advance_operation},
    {"brew.start", "Start brew", nullptr, 0, brew_start_operation},
    {"brew.next", "Next brew step", nullptr, 0, brew_next_operation},
    {"brew.stop", "Stop brew", nullptr, 0, brew_stop_operation},
    {"brew.reset", "Reset brew", nullptr, 0, brew_reset_operation},
    {"brew.tare", "Tare for brew", nullptr, 0, brew_tare_operation},
    {"brew.template.set", "Set brew template", brew_template_arguments, 1, brew_set_template_operation},
};

#endif

#if HAS_SCALE
DEFINE_AND_REGISTER_ACTION_TYPE(brew_action_type, ACTION_TYPE_BREW, brew_describe, nullptr, ACTION_EXECUTION_SYNC,
    true, false, brew_operations, sizeof(brew_operations) / sizeof(brew_operations[0]));
#else
DEFINE_AND_REGISTER_ACTION_TYPE(brew_action_type, ACTION_TYPE_BREW, brew_describe, nullptr, ACTION_EXECUTION_SYNC);
#endif

#endif // HAS_DISPLAY && IS_COFFEE_SCALE
