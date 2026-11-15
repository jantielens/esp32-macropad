#include "action_registry.h"
#include "voice_notes.h"
#include "log_manager.h"

#if HAS_VOICE_NOTES
namespace {
bool parse_command(const char* text, VoiceNoteCommand& command) {
    if (!text) return false;
    if (strcmp(text, "start") == 0) command = VoiceNoteCommand::Start;
    else if (strcmp(text, "pause") == 0) command = VoiceNoteCommand::Pause;
    else if (strcmp(text, "resume") == 0) command = VoiceNoteCommand::Resume;
    else if (strcmp(text, "save") == 0) command = VoiceNoteCommand::Save;
    else if (strcmp(text, "discard") == 0) command = VoiceNoteCommand::Discard;
    else return false;
    return true;
}

bool valid_scenario(JsonObjectConst json, VoiceNoteCommand command) {
    if (!json.containsKey("scenario")) return true;
    return command == VoiceNoteCommand::Start && json["scenario"].is<const char*>() &&
           strcmp(json["scenario"].as<const char*>(), "general") == 0;
}

void parse_voice_note(const JsonObject& json, ButtonAction& action) {
    const char* text = json["command"] | "";
    VoiceNoteCommand command;
    if (!parse_command(text, command) || !valid_scenario(json, command)) {
        memset(&action, 0, sizeof(action));
        return;
    }
    strlcpy(action.payload.voice_note.command, text, sizeof(action.payload.voice_note.command));
    if (command == VoiceNoteCommand::Start)
        strlcpy(action.payload.voice_note.scenario, "general", sizeof(action.payload.voice_note.scenario));
}

void serialize_voice_note(const ButtonAction& action, JsonObject json) {
    json["command"] = action.payload.voice_note.command;
    if (strcmp(action.payload.voice_note.command, "start") == 0)
        json["scenario"] = action.payload.voice_note.scenario;
}

ActionResult dispatch_voice_note(const ButtonAction& action, const char* label, uint32_t token) {
    VoiceNoteCommand command;
    if (!parse_command(action.payload.voice_note.command, command) ||
        !token || !voice_notes_command(command, token)) {
        LOGW("Action", "%s voice note command rejected", label);
        return ACTION_FAILED;
    }
    return ACTION_PENDING;
}

const char* validate_voice_note(JsonObjectConst json) {
    for (JsonPairConst field : json) {
        const char* name = field.key().c_str();
        if (strcmp(name, "type") != 0 && strcmp(name, "command") != 0 && strcmp(name, "scenario") != 0)
            return "voice_note accepts only command and scenario";
    }
    VoiceNoteCommand command;
    if (!json["command"].is<const char*>() ||
        !parse_command(json["command"].as<const char*>(), command))
        return "voice_note command must be start, pause, resume, save, or discard";
    if (!valid_scenario(json, command))
        return "voice_note scenario must be general and is only allowed on start";
    return nullptr;
}

void describe_voice_note(JsonObject& action) {
    action["group"] = "Audio";
    action["label"] = "Voice note";
    JsonArray fields = action.createNestedArray("fields");
    JsonObject field = fields.createNestedObject();
    field["name"] = "command";
    field["description"] = "start, pause, resume, save, or discard";
    field = fields.createNestedObject();
    field["name"] = "scenario";
    field["description"] = "Optional on start; general is the only supported scenario";
    JsonArray editor = action.createNestedArray("editor_fields");
    JsonObject select = editor.createNestedObject();
    select["name"] = "command";
    select["label"] = "Command";
    select["type"] = "select";
    select["default"] = "start";
    JsonArray options = select.createNestedArray("options");
    const char* const ids[] = {"start", "pause", "resume", "save", "discard"};
    const char* const labels[] = {"Start recording", "Pause recording", "Resume recording", "Save recording", "Discard recording"};
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        JsonObject option = options.createNestedObject();
        option["id"] = ids[i];
        option["label"] = labels[i];
    }
}

DEFINE_AND_REGISTER_ACTION_TYPE(kVoiceNoteActionType, ACTION_TYPE_VOICE_NOTE,
    parse_voice_note, serialize_voice_note, dispatch_voice_note, nullptr,
    describe_voice_note, nullptr, validate_voice_note, ACTION_EXECUTION_PAUSABLE);
}
#endif
