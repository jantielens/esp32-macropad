#include "action_registry.h"
#include "log_manager.h"
#include "pad_cycle.h"
#include <ctype.h>
#if HAS_DISPLAY && HAS_PSRAM
#include "item_binding.h"
#endif
#if defined(ARDUINO) && HAS_DISPLAY
#include "display_manager.h"
#endif

#if HAS_DISPLAY || HAS_BUTTON
namespace {
constexpr const char* kCyclePadActionTag = "Action";
const char* const cycle_directions[] = {"next", "previous"};
const char* const cycle_direction_labels[] = {"Next", "Previous"};

const char* validate_cycle_exclusions(const char* text) {
    const char* cursor = text;
    while (*cursor) {
        while (isspace(static_cast<unsigned char>(*cursor))) ++cursor;
        if (!isdigit(static_cast<unsigned char>(*cursor))) return "excluded pads must be comma-separated pad numbers";
        unsigned value = 0;
        while (isdigit(static_cast<unsigned char>(*cursor))) {
            value = value * 10 + static_cast<unsigned>(*cursor++ - '0');
            if (value > MAX_PADS) return "excluded pad number out of range";
        }
        if (!value) return "excluded pad number out of range";
        while (isspace(static_cast<unsigned char>(*cursor))) ++cursor;
        if (!*cursor) break;
        if (*cursor++ != ',' || !*cursor) return "excluded pads must be comma-separated pad numbers";
    }
    return nullptr;
}

const ActionArgumentDef kCyclePadArguments[] = {
    {"direction", "Direction", ACTION_ARGUMENT_ENUM, 0, 0, 8, true, "[var:direction]", "select",
     cycle_directions, 2, false, nullptr, cycle_direction_labels},
    {"wrap", "Wrap at first or last pad", ACTION_ARGUMENT_BOOLEAN, 0, 0, 0, true, "[var:wrap]", "toggle",
     nullptr, 0, false, nullptr, nullptr, 0, nullptr, 1, true},
    {"excluded_pads", "Excluded pads", ACTION_ARGUMENT_STRING, 0, 0, MAX_PADS * 3, true,
     "[var:excluded_pads]", "pad_exclusions", nullptr, 0, true, validate_cycle_exclusions},
};

ActionResult dispatch_cycle_pad_operation(const ActionResolvedArgument* arguments, const char* label, uint32_t) {
#if defined(ARDUINO) && HAS_DISPLAY
#if HAS_PSRAM
    ItemBindingScope::end_current();
#endif
    const int8_t direction = strcmp(arguments[0].text, "previous") == 0 ? -1 : 1;
    if (display_manager_cycle_pad(direction, arguments[1].integer != 0,
                                  pad_cycle_parse_exclusions(arguments[2].text), true))
        return ACTION_COMPLETE;
    LOGD(kCyclePadActionTag, "%s cycle_pad: no eligible destination", label);
#else
    (void)arguments;
    LOGW(kCyclePadActionTag, "%s cycle_pad: no display", label);
#endif
    return ACTION_FAILED;
}

const ActionOperationDef kCyclePadOperations[] = {
    {"pad.cycle", "Navigate pad sequence", kCyclePadArguments, 3, dispatch_cycle_pad_operation},
};
bool cycle_pad_available() { return HAS_DISPLAY; }
void describe_cycle_pad(JsonObject& action) { action["group"] = "Navigation"; action["label"] = "Navigate pad sequence"; }
DEFINE_AND_REGISTER_ACTION_TYPE(kCyclePadActionType, ACTION_TYPE_CYCLE_PAD, describe_cycle_pad, cycle_pad_available, ACTION_EXECUTION_SYNC, true, false, kCyclePadOperations, 1);
} // namespace
#endif // HAS_DISPLAY || HAS_BUTTON