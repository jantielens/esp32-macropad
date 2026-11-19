#include "action_registry.h"
#include "voice_notes_processing.h"
#include "log_manager.h"

#if HAS_VOICE_NOTES
namespace {
const char* processing_rejection(VoiceNotesProcessingStart result) {
    switch (result) {
        case VoiceNotesProcessingStart::NotConfigured: return "processing settings incomplete";
        case VoiceNotesProcessingStart::Busy: return "device busy";
        case VoiceNotesProcessingStart::Offline: return "Wi-Fi disconnected";
        case VoiceNotesProcessingStart::Cooldown: return "service cooldown";
        case VoiceNotesProcessingStart::Unavailable: return "processing resources unavailable";
        case VoiceNotesProcessingStart::InvalidId: return "invalid note ID";
        case VoiceNotesProcessingStart::NotFound: return "note not found";
        case VoiceNotesProcessingStart::MissingResult: return "saved result missing";
        case VoiceNotesProcessingStart::Unchanged: return "nothing to process";
        case VoiceNotesProcessingStart::Accepted: return "accepted";
    }
    return "unknown rejection";
}

ActionResult submit_processing(const char* label) {
    const VoiceNotesProcessingStart result = voice_notes_processing_start();
    if (result == VoiceNotesProcessingStart::Accepted) return ACTION_COMPLETE;
    LOGW("Action", "%s processing request rejected: %s", label, processing_rejection(result));
    return ACTION_FAILED;
}

ActionResult dispatch_process_operation(const ActionResolvedArgument*, const char* label, uint32_t) {
    return submit_processing(label);
}

const ActionOperationDef kProcessOperations[] = {
    {"voice_note.process", "Process saved notes", nullptr, 0, dispatch_process_operation},
};

void describe_voice_note_process(JsonObject& action) {
    action["group"] = "Voice notes";
    action["label"] = "Process saved notes";
    action["note"] = "Starts a background pass for missing results without waiting for completion. Offline or busy requests schedule nothing.";
}

DEFINE_AND_REGISTER_ACTION_TYPE(kVoiceNoteProcessActionType, ACTION_TYPE_VOICE_NOTE_PROCESS, describe_voice_note_process, nullptr, ACTION_EXECUTION_SYNC, true, false,
    kProcessOperations, sizeof(kProcessOperations) / sizeof(kProcessOperations[0]));
}
#endif
