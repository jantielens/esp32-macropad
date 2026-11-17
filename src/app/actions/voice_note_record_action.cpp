#include "action_registry.h"
#include "voice_notes.h"
#include "log_manager.h"

#if HAS_VOICE_NOTES
namespace {
bool parse_record_command(const char* text, VoiceNoteCommand& command) {
    if (!text) return false;
    if (strcmp(text, "toggle") == 0) command = VoiceNoteCommand::Toggle;
    else if (strcmp(text, "start") == 0) command = VoiceNoteCommand::Start;
    else if (strcmp(text, "pause") == 0) command = VoiceNoteCommand::Pause;
    else if (strcmp(text, "resume") == 0) command = VoiceNoteCommand::Resume;
    else if (strcmp(text, "save") == 0) command = VoiceNoteCommand::Save;
    else if (strcmp(text, "discard") == 0) command = VoiceNoteCommand::Discard;
    else return false;
    return true;
}

const char* validate_voice_note_record(JsonObjectConst json) {
    for (JsonPairConst field : json) {
        const char* name = field.key().c_str();
        if (strcmp(name, "type") != 0 && strcmp(name, "command") != 0)
            return "vnote_record accepts only command";
    }
    VoiceNoteCommand command;
    if (!json["command"].is<const char*>() ||
        !parse_record_command(json["command"].as<const char*>(), command))
        return "vnote_record command must be toggle, start, pause, resume, save, or discard";
    return nullptr;
}

void parse_voice_note_record(const JsonObject& json, ButtonAction& action) {
    if (validate_voice_note_record(json)) {
        memset(&action, 0, sizeof(action));
        return;
    }
    strlcpy(action.payload.voice_note_record.command, json["command"].as<const char*>(),
            sizeof(action.payload.voice_note_record.command));
}

void serialize_voice_note_record(const ButtonAction& action, JsonObject json) {
    json["command"] = action.payload.voice_note_record.command;
}

ActionResult dispatch_voice_note_record(const ButtonAction& action, const char* label, uint32_t token) {
    VoiceNoteCommand command;
    if (!parse_record_command(action.payload.voice_note_record.command, command) ||
        !token || !voice_notes_command(command, token)) {
        LOGW("Action", "%s voice note recording command rejected", label);
        return ACTION_FAILED;
    }
    return ACTION_PENDING;
}

void describe_voice_note_record(JsonObject& action) {
    action["group"] = "Voice notes";
    action["label"] = "Recording";
    action["note"] = "Toggle starts a recording when idle, or stops and saves a recording, including when paused. Commands are rejected during transitions.";
    action["command_field"] = "command";
    JsonArray commands = action.createNestedArray("commands");
    const char* const ids[] = {"toggle", "start", "pause", "resume", "save", "discard"};
    const char* const labels[] = {"Toggle recording (start/save)", "Start recording", "Pause recording",
        "Resume recording", "Stop and save recording", "Discard recording"};
    for (size_t i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i) {
        JsonObject command = commands.createNestedObject();
        command["id"] = ids[i];
        command["label"] = labels[i];
    }
    JsonObject field = action.createNestedArray("fields").createNestedObject();
    field["name"] = "command";
    field["description"] = "toggle, start, pause, resume, save, or discard";
    JsonObject select = action.createNestedArray("editor_fields").createNestedObject();
    select["name"] = "command";
    select["label"] = "Command";
    select["type"] = "select";
    select["default"] = "toggle";
    select["command_options"] = true;
}

DEFINE_AND_REGISTER_ACTION_TYPE(kVoiceNoteRecordActionType, ACTION_TYPE_VOICE_NOTE_RECORD,
    parse_voice_note_record, serialize_voice_note_record, dispatch_voice_note_record, nullptr,
    describe_voice_note_record, nullptr, validate_voice_note_record, ACTION_EXECUTION_PAUSABLE);
}
#endif
