#include "action_registry.h"
#include "log_manager.h"
#if defined(ARDUINO) && HAS_AUDIO
#include "audio.h"
#include "save_level_action.h"
#endif

#if HAS_DISPLAY || HAS_BUTTON
namespace {
ActionResult dispatch_save_volume_operation(const ActionResolvedArgument*, const char* label, uint32_t token) {
#if defined(ARDUINO) && HAS_AUDIO
    return queue_save_level(audio_get_volume(), config_manager_save_volume, "volume", label, token);
#else
    (void)token;
    LOGW("Action", "%s: volume saving unavailable", label);
    return ACTION_FAILED;
#endif
}
const ActionOperationDef kSaveVolumeOperations[] = {
    {"audio.volume.save", "Save volume", nullptr, 0, dispatch_save_volume_operation, ACTION_EXECUTION_PAUSABLE},
};
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
DEFINE_AND_REGISTER_ACTION_TYPE(kSaveVolumeActionType, ACTION_TYPE_SAVE_VOLUME, describe_save_volume, save_volume_available, ACTION_EXECUTION_PAUSABLE, false, false, kSaveVolumeOperations, 1);
}
#endif