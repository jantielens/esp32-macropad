#include "action_registry.h"
#include "log_manager.h"

#if HAS_DISPLAY || HAS_BUTTON

#if HAS_DISPLAY && defined(ARDUINO)
bool display_manager_go_back();
#endif

namespace {

constexpr const char* kBackActionTag = "Action";

ActionResult dispatch_back(const ButtonAction&, const char* label, uint32_t) {
#if !defined(ARDUINO)
    (void)label;
    return ACTION_COMPLETE;
#elif HAS_DISPLAY
    if (!display_manager_go_back()) {
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

DEFINE_AND_REGISTER_ACTION_TYPE(kBackActionType,
    ACTION_TYPE_BACK, nullptr, nullptr, dispatch_back, nullptr, describe_back
);

} // namespace

#endif // HAS_DISPLAY || HAS_BUTTON