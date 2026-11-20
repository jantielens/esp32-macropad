#include "action_registry.h"
#include "log_manager.h"
#if defined(ARDUINO) && HAS_DISPLAY
#include "display_manager.h"
#endif

#if HAS_DISPLAY || HAS_BUTTON
namespace {
constexpr const char* kScreenActionTag = "Action";
const ActionArgumentDef kOpenScreenArguments[] = {
    {"target", "Screen", ACTION_ARGUMENT_SCREEN, 0, 0, CONFIG_SCREEN_ID_MAX_LEN - 1,
     true, "[item:id]", "screens"},
};
ActionResult dispatch_open_screen(const ActionResolvedArgument* arguments, const char* label, uint32_t) {
#if defined(ARDUINO) && HAS_DISPLAY
    bool ok = false;
    display_manager_show_screen(arguments[0].text, &ok, true);
    if (!ok) LOGW(kScreenActionTag, "%s nav failed: '%s'", label, arguments[0].text);
    return ok ? ACTION_COMPLETE : ACTION_FAILED;
#else
    (void)arguments;
    LOGW(kScreenActionTag, "%s screen: no display", label);
    return ACTION_FAILED;
#endif
}
const ActionOperationDef kScreenOperations[] = {
    {"screen.open", "Navigate to screen", kOpenScreenArguments, 1, dispatch_open_screen},
};
bool screen_available() { return HAS_DISPLAY; }
void describe_screen(JsonObject& action) { action["group"] = "Navigation"; action["label"] = "Navigate to screen"; }
DEFINE_AND_REGISTER_ACTION_TYPE(kScreenActionType, ACTION_TYPE_SCREEN, describe_screen, screen_available, ACTION_EXECUTION_SYNC, true, false, kScreenOperations, 1);
} // namespace
#endif // HAS_DISPLAY || HAS_BUTTON