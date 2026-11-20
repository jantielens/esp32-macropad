#include "action_registry.h"
#include "log_manager.h"
#include <limits.h>
#if defined(ARDUINO) && HAS_AUDIO
#include "audio.h"
#endif

#if HAS_DISPLAY || HAS_BUTTON

namespace {

constexpr const char* kVolumeActionTag = "Action";

const ActionArgumentDef kVolumeSetArguments[] = {
    {"percent", "Volume (%)", ACTION_ARGUMENT_INTEGER, 0, 100, 0, true,
     "[var:volume]", "number"},
};
const ActionArgumentDef kVolumeAdjustArguments[] = {
    {"percent", "Adjust (%)", ACTION_ARGUMENT_INTEGER, INT32_MIN, INT32_MAX, 0, true,
     "[event:step]", "number"},
};

ActionResult set_volume(const ActionResolvedArgument* arguments, const char* label, bool adjust) {
#if defined(ARDUINO) && HAS_AUDIO
    int64_t value = arguments[0].integer;
    if (adjust) value += audio_get_volume();
    if (value < 0) value = 0;
    if (value > 100) value = 100;
    audio_set_volume(static_cast<uint8_t>(value));
    LOGI(kVolumeActionTag, "%s volume -> %u%%", label, static_cast<unsigned>(value));
    return ACTION_COMPLETE;
#else
    (void)arguments;
    (void)adjust;
    LOGW(kVolumeActionTag, "%s volume: not compiled", label);
    return ACTION_FAILED;
#endif
}

ActionResult dispatch_volume_set(const ActionResolvedArgument* arguments, const char* label, uint32_t) {
    return set_volume(arguments, label, false);
}
ActionResult dispatch_volume_adjust(const ActionResolvedArgument* arguments, const char* label, uint32_t) {
    return set_volume(arguments, label, true);
}
const ActionOperationDef kVolumeOperations[] = {
    {"audio.volume.set", "Set volume", kVolumeSetArguments, 1, dispatch_volume_set},
    {"audio.volume.adjust", "Adjust volume", kVolumeAdjustArguments, 1, dispatch_volume_adjust},
};

bool volume_available() {
#if HAS_AUDIO
    return true;
#else
    return false;
#endif
}

void describe_volume(JsonObject& action) {
    action["group"] = "Audio";
    action["label"] = "Volume";
}

DEFINE_AND_REGISTER_ACTION_TYPE(kVolumeActionType, ACTION_TYPE_VOLUME, describe_volume, volume_available, ACTION_EXECUTION_SYNC, true, false,
    kVolumeOperations, sizeof(kVolumeOperations) / sizeof(kVolumeOperations[0]));

} // namespace

#endif // HAS_DISPLAY || HAS_BUTTON