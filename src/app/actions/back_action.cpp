#include "action_registry.h"
#include "log_manager.h"

#if HAS_DISPLAY || HAS_BUTTON

#if HAS_DISPLAY && defined(ARDUINO)
#include "display_manager.h"
#endif

namespace {

constexpr const char* kBackActionTag = "Action";

ActionResult dispatch_screen_back(const ActionResolvedArgument*, const char* label, uint32_t) {
#if defined(ARDUINO) && HAS_DISPLAY
    if (!display_manager_go_back(true)) {
        LOGW(kBackActionTag, "%s back: no previous screen", label);
    }
    return ACTION_COMPLETE;
#else
    LOGW(kBackActionTag, "%s back: no display", label);
    return ACTION_FAILED;
#endif
}

const ActionOperationDef kBackOperations[] = {
    {"screen.back", "Navigate back", nullptr, 0, dispatch_screen_back},
};

void describe_back(JsonObject& action) {
    action["group"] = "Navigation";
    action["label"] = "Navigate back";
}

DEFINE_AND_REGISTER_ACTION_TYPE(kBackActionType, ACTION_TYPE_BACK, describe_back, nullptr, ACTION_EXECUTION_SYNC, true, false, kBackOperations, 1);

} // namespace

#endif // HAS_DISPLAY || HAS_BUTTON