#include "action_registry.h"
#include "action_dispatch.h"
#include "log_manager.h"
#include "music_command.h"
#if defined(ARDUINO) && HAS_SOUND_PLAYER
#include "audio.h"
#endif

#if HAS_DISPLAY || HAS_BUTTON
namespace {
constexpr const char* kMusicActionTag = "Action";

ActionResult submit_music_command(MusicCommand command, const char* label) {
#if defined(ARDUINO) && HAS_SOUND_PLAYER
    const ActionDispatchContext context = action_dispatch_context();
    if (audio_music_command(command, context.work_guard, context.generation) == AUDIO_MUSIC_SUBMIT_QUEUED)
        return ACTION_COMPLETE;
    LOGW(kMusicActionTag, "%s music: audio worker rejected command", label);
#else
    (void)command;
    LOGW(kMusicActionTag, "%s music: not compiled", label);
#endif
    return ACTION_FAILED;
}

ActionResult dispatch_music_play_pause(const ActionResolvedArgument*, const char* label, uint32_t) {
    return submit_music_command(MUSIC_COMMAND_PLAY_PAUSE, label);
}
ActionResult dispatch_music_next(const ActionResolvedArgument*, const char* label, uint32_t) {
    return submit_music_command(MUSIC_COMMAND_NEXT, label);
}
ActionResult dispatch_music_previous(const ActionResolvedArgument*, const char* label, uint32_t) {
    return submit_music_command(MUSIC_COMMAND_PREVIOUS, label);
}
ActionResult dispatch_music_stop(const ActionResolvedArgument*, const char* label, uint32_t) {
    return submit_music_command(MUSIC_COMMAND_STOP, label);
}
const ActionOperationDef kMusicOperations[] = {
    {"music.play_pause", "Play/Pause", nullptr, 0, dispatch_music_play_pause},
    {"music.next", "Next track", nullptr, 0, dispatch_music_next},
    {"music.previous", "Previous track", nullptr, 0, dispatch_music_previous},
    {"music.stop", "Stop music", nullptr, 0, dispatch_music_stop},
};

bool music_available() {
#if HAS_SOUND_PLAYER
    return true;
#else
    return false;
#endif
}
void describe_music(JsonObject& action) {
    action["group"] = "Audio"; action["label"] = "Music";
}
DEFINE_AND_REGISTER_ACTION_TYPE(kMusicActionType, ACTION_TYPE_MUSIC, describe_music, music_available, ACTION_EXECUTION_SYNC, true, false,
    kMusicOperations, sizeof(kMusicOperations) / sizeof(kMusicOperations[0]));
} // namespace
#endif // HAS_DISPLAY || HAS_BUTTON