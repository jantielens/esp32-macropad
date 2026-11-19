#include "action_registry.h"
#include "voice_notes.h"
#include "log_manager.h"

#if HAS_VOICE_NOTES
namespace {
ActionResult submit_record_command(VoiceNoteCommand command, const char* label, uint32_t token) {
    if (!token || !voice_notes_command(command, token)) {
        LOGW("Action", "%s voice note recording command rejected", label);
        return ACTION_FAILED;
    }
    return ACTION_PENDING;
}

#define RECORD_OPERATION(name, command) \
    ActionResult name(const ActionResolvedArgument*, const char* label, uint32_t token) { \
        return submit_record_command(VoiceNoteCommand::command, label, token); \
    }
RECORD_OPERATION(record_toggle, Toggle)
RECORD_OPERATION(record_start, Start)
RECORD_OPERATION(record_pause, Pause)
RECORD_OPERATION(record_resume, Resume)
RECORD_OPERATION(record_save, Save)
RECORD_OPERATION(record_discard, Discard)
#undef RECORD_OPERATION

const ActionOperationDef kRecordOperations[] = {
    {"voice_note.record.toggle", "Toggle recording (start/save)", nullptr, 0, record_toggle, ACTION_EXECUTION_PAUSABLE},
    {"voice_note.record.start", "Start recording", nullptr, 0, record_start, ACTION_EXECUTION_PAUSABLE},
    {"voice_note.record.pause", "Pause recording", nullptr, 0, record_pause, ACTION_EXECUTION_PAUSABLE},
    {"voice_note.record.resume", "Resume recording", nullptr, 0, record_resume, ACTION_EXECUTION_PAUSABLE},
    {"voice_note.record.save", "Stop and save recording", nullptr, 0, record_save, ACTION_EXECUTION_PAUSABLE},
    {"voice_note.record.discard", "Discard recording", nullptr, 0, record_discard, ACTION_EXECUTION_PAUSABLE},
};

void describe_voice_note_record(JsonObject& action) {
    action["group"] = "Voice notes";
    action["label"] = "Recording";
    action["note"] = "Toggle starts a recording when idle, or stops and saves a recording, including when paused. Commands are rejected during transitions.";
}

DEFINE_AND_REGISTER_ACTION_TYPE(kVoiceNoteRecordActionType, ACTION_TYPE_VOICE_NOTE_RECORD, describe_voice_note_record, nullptr, ACTION_EXECUTION_PAUSABLE,
    true, false, kRecordOperations, sizeof(kRecordOperations) / sizeof(kRecordOperations[0]));
}
#endif
