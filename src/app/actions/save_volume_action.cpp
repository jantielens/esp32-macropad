#include "action_registry.h"
#include "log_manager.h"
#if defined(ARDUINO) && HAS_AUDIO
#include "config_manager.h"
extern DeviceConfig device_config;
#endif

#if HAS_DISPLAY || HAS_BUTTON
namespace {
ActionResult dispatch_save_volume(const ButtonAction&, const char* label, uint32_t) {
#if defined(ARDUINO) && HAS_AUDIO
    if (!config_manager_save_current_volume(&device_config)) {
        LOGW("Action", "%s: failed to save volume", label);
        return ACTION_FAILED;
    }
#else
    (void)label;
#endif
    return ACTION_COMPLETE;
}
bool save_volume_available() {
#if HAS_AUDIO
    return true;
#else
    return false;
#endif
}
void describe_save_volume(JsonObject& action) {
    action["group"] = "Audio";
    action["label"] = "Save volume";
}
DEFINE_AND_REGISTER_ACTION_TYPE(kSaveVolumeActionType, ACTION_TYPE_SAVE_VOLUME, nullptr, nullptr, dispatch_save_volume, nullptr, describe_save_volume, save_volume_available, nullptr, ACTION_EXECUTION_SYNC, false);
}
#endif