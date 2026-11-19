#include "action_registry.h"

#if HAS_VOICE_NOTES
#include "action_continuation.h"
#include "log_manager.h"
#include "main_loop_bridge.h"
#include "voice_notes.h"
#include "voice_notes_file.h"
#if HAS_DISPLAY && HAS_PSRAM
#include "voice_notes_loaded.h"
#endif
#include <string.h>

namespace {
struct VoiceNoteDeleteJob {
    uint32_t token;
    char id[17];
};

void execute_voice_note_delete(const void* opaque, bool* ok, char*, size_t) {
    const auto* job = static_cast<const VoiceNoteDeleteJob*>(opaque);
    const VoiceNoteFileStatus result = voice_notes_remove(job->id);
    *ok = result == VoiceNoteFileStatus::Ready;
#if HAS_DISPLAY && HAS_PSRAM
    if (*ok) voice_notes_loaded_clear_if_matches(job->id);
#endif
    if (!*ok) LOGW("Action", "Delete voice note %s failed (status %u)", job->id, unsigned(result));
    if (!action_continuation_complete(job->token, *ok))
        LOGW("Action", "Delete voice note %s could not complete its action sequence", job->id);
}

ActionResult queue_voice_note_delete(const char* id, const char* label, uint32_t token) {
    // A queued deletion must not run after its action sequence has timed out.
    if (!token || !voice_notes_id_valid(id) || !action_continuation_set_timeout(token, 0)) {
        LOGW("Action", "%s delete voice note rejected", label);
        return ACTION_FAILED;
    }
    VoiceNoteDeleteJob job = {token, {}};
    strlcpy(job.id, id, sizeof(job.id));
    if (loop_bridge_enqueue(execute_voice_note_delete, &job, sizeof(job)) != LOOP_BRIDGE_OK) {
        LOGW("Action", "%s delete voice note: main loop unavailable or busy", label);
        return ACTION_FAILED;
    }
    return ACTION_PENDING;
}

const char* validate_delete_note_id(const char* id) {
    return voice_notes_id_valid(id) ? nullptr : "note ID must be 16 lowercase hexadecimal characters";
}

const ActionArgumentDef kDeleteArguments[] = {
    {"id", "Note ID", ACTION_ARGUMENT_STRING, 0, 0, 16, true, "[item:id]", "text",
     nullptr, 0, false, validate_delete_note_id},
};

ActionResult dispatch_delete_operation(const ActionResolvedArgument* arguments, const char* label,
                                       uint32_t token) {
    return queue_voice_note_delete(arguments[0].text, label, token);
}

const ActionOperationDef kDeleteOperations[] = {
    {"voice_note.delete", "Delete saved note", kDeleteArguments, 1, dispatch_delete_operation,
     ACTION_EXECUTION_PAUSABLE},
};

void describe_voice_note_delete(JsonObject& action) {
    action["group"] = "Voice notes";
    action["label"] = "Delete saved note";
    action["note"] = "Permanently deletes a saved note. Use [item:id] for a List item.";
}

DEFINE_AND_REGISTER_ACTION_TYPE(kVoiceNoteDeleteAction, ACTION_TYPE_VOICE_NOTE_DELETE, describe_voice_note_delete, nullptr,
    ACTION_EXECUTION_PAUSABLE, true, false,
    kDeleteOperations, sizeof(kDeleteOperations) / sizeof(kDeleteOperations[0]));
}
#endif
