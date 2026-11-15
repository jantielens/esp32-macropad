#include "action_registry.h"
#include "voice_clips.h"
#include "log_manager.h"

#if HAS_VOICE_CLIPS
namespace {
bool parse_command(const char* text, VoiceClipCommand& command) {
    if (!text) return false;
    if (strcmp(text, "start") == 0) command = VoiceClipCommand::Start;
    else if (strcmp(text, "pause") == 0) command = VoiceClipCommand::Pause;
    else if (strcmp(text, "resume") == 0) command = VoiceClipCommand::Resume;
    else if (strcmp(text, "save") == 0) command = VoiceClipCommand::Save;
    else if (strcmp(text, "discard") == 0) command = VoiceClipCommand::Discard;
    else return false;
    return true;
}

void parse_voice_clip(const JsonObject& json, ButtonAction& action) {
    const char* text = json["command"] | "";
    VoiceClipCommand command;
    if (!parse_command(text, command)) {
        memset(&action, 0, sizeof(action));
        return;
    }
    strlcpy(action.payload.voice_clip.command, text, sizeof(action.payload.voice_clip.command));
}

void serialize_voice_clip(const ButtonAction& action, JsonObject json) {
    json["command"] = action.payload.voice_clip.command;
}

ActionResult dispatch_voice_clip(const ButtonAction& action, const char* label, uint32_t token) {
    VoiceClipCommand command;
    if (!parse_command(action.payload.voice_clip.command, command) ||
        !token || !voice_clips_command(command, token)) {
        LOGW("Action", "%s voice clip command rejected", label);
        return ACTION_FAILED;
    }
    return ACTION_PENDING;
}

const char* validate_voice_clip(JsonObjectConst json) {
    for (JsonPairConst field : json) {
        const char* name = field.key().c_str();
        if (strcmp(name, "type") != 0 && strcmp(name, "command") != 0)
            return "voice_clip accepts only command";
    }
    VoiceClipCommand command;
    if (!json["command"].is<const char*>() ||
        !parse_command(json["command"].as<const char*>(), command))
        return "voice_clip command must be start, pause, resume, save, or discard";
    return nullptr;
}

void describe_voice_clip(JsonObject& action) {
    action["group"] = "Audio";
    action["label"] = "Voice clip";
    JsonArray fields = action.createNestedArray("fields");
    JsonObject field = fields.createNestedObject();
    field["name"] = "command";
    field["description"] = "start, pause, resume, save, or discard";
    JsonArray editor = action.createNestedArray("editor_fields");
    JsonObject select = editor.createNestedObject();
    select["name"] = "command";
    select["label"] = "Command";
    select["type"] = "select";
    select["default"] = "start";
    JsonArray options = select.createNestedArray("options");
    const char* const ids[] = {"start", "pause", "resume", "save", "discard"};
    const char* const labels[] = {"Start", "Pause", "Resume", "Save", "Discard"};
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        JsonObject option = options.createNestedObject();
        option["id"] = ids[i];
        option["label"] = labels[i];
    }
}

DEFINE_AND_REGISTER_ACTION_TYPE(kVoiceClipActionType, ACTION_TYPE_VOICE_CLIP,
    parse_voice_clip, serialize_voice_clip, dispatch_voice_clip, nullptr,
    describe_voice_clip, nullptr, validate_voice_clip, ACTION_EXECUTION_PAUSABLE);
}
#endif
