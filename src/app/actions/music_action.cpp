#include "action_registry.h"
#include "log_manager.h"
#include "music_command.h"
#if defined(ARDUINO) && HAS_SOUND_PLAYER
#include "audio.h"
#endif

#if HAS_DISPLAY || HAS_BUTTON
namespace {
constexpr const char* kMusicActionTag = "Action";
void parse_music(const JsonObject& action, ButtonAction& act) {
    const char* command = action["music_command"] | "";
    MusicCommand parsed;
    if (!music_command_parse(command, &parsed)) { memset(&act, 0, sizeof(act)); return; }
    strlcpy(act.payload.music.music_command, command, sizeof(act.payload.music.music_command));
}
void serialize_music(const ButtonAction& act, JsonObject action) {
    if (act.payload.music.music_command[0]) action["music_command"] = act.payload.music.music_command;
}
ActionResult dispatch_music(const ButtonAction& act, const char* label, uint32_t) {
#if defined(ARDUINO) && HAS_SOUND_PLAYER
    MusicCommand command;
    if (!music_command_parse(act.payload.music.music_command, &command)) LOGW(kMusicActionTag, "%s music: invalid command", label);
    else if (audio_music_command(command) != AUDIO_MUSIC_SUBMIT_QUEUED) LOGW(kMusicActionTag, "%s music: audio worker busy", label);
#else
    (void)act;
    LOGW(kMusicActionTag, "%s music: not compiled", label);
#endif
    return ACTION_COMPLETE;
}
bool music_available() {
#if HAS_SOUND_PLAYER
    return true;
#else
    return false;
#endif
}
const char* validate_music(const JsonObjectConst action) {
    if (!action.containsKey("music_command")) return "music missing music_command";
    if (!action["music_command"].is<const char*>()) return "music_command must be a string";
    MusicCommand command;
    return music_command_parse(action["music_command"].as<const char*>(), &command) ? nullptr : "music_command must be play_pause, next, previous, or stop";
}
void describe_music(JsonObject& action) {
    action["group"] = "Audio"; action["label"] = "Music";
    const MusicCommand values[] = { MUSIC_COMMAND_PLAY_PAUSE, MUSIC_COMMAND_NEXT, MUSIC_COMMAND_PREVIOUS, MUSIC_COMMAND_STOP };
    const char* labels[] = { "Play/Pause", "Next track", "Previous track", "Stop" };
    JsonArray commands = action.createNestedArray("commands");
    for (size_t index = 0; index < 4; ++index) { JsonObject command = commands.createNestedObject(); command["id"] = music_command_name(values[index]); command["label"] = labels[index]; }
    JsonArray editor_fields = action.createNestedArray("editor_fields");
    JsonObject command = editor_fields.createNestedObject(); command["name"] = "music_command"; command["label"] = "Command"; command["type"] = "select"; command["command_options"] = true;
}
DEFINE_AND_REGISTER_ACTION_TYPE(kMusicActionType, ACTION_TYPE_MUSIC, parse_music, serialize_music, dispatch_music, nullptr, describe_music, music_available, validate_music);
} // namespace
#endif // HAS_DISPLAY || HAS_BUTTON