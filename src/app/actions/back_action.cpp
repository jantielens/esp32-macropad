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

ActionResult dispatch_back(const ButtonAction&, const char* label, uint32_t) {
#if !defined(ARDUINO)
    (void)label;
    return ACTION_COMPLETE;
#elif HAS_DISPLAY
    if (!display_manager_go_back(true)) {
        LOGW(kBackActionTag, "%s back: no previous screen", label);
    }
#else
    LOGW(kBackActionTag, "%s back: no display", label);
#endif
    return ACTION_COMPLETE;
}

void describe_back(JsonObject& action) {
    action["group"] = "Navigation";
    action["label"] = "Navigate back";
}

DEFINE_AND_REGISTER_ACTION_TYPE(kBackActionType, ACTION_TYPE_BACK, nullptr, nullptr, dispatch_back, nullptr, describe_back, nullptr, nullptr, ACTION_EXECUTION_SYNC, true, true, false,
    [](const ButtonAction&) { return true; }, kBackOperations, 1);

} // namespace

#endif // HAS_DISPLAY || HAS_BUTTON