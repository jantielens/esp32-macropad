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

const char* validate_voice_note_delete(JsonObjectConst action) {
    for (JsonPairConst field : action) {
        const char* name = field.key().c_str();
        if (strcmp(name, "type") && strcmp(name, "id")) return "vnote_delete accepts only id";
    }
    if (!action["id"].is<const char*>()) return "vnote_delete id must be a string";
    const JsonString id = action["id"].as<JsonString>();
    if (!id.c_str() || !id.size() || id.size() >= sizeof(VoiceNoteDeletePayload::id) ||
        memchr(id.c_str(), '\0', id.size())) return "vnote_delete id is empty or too long";
    return nullptr;
}

void parse_voice_note_delete(const JsonObject& json, ButtonAction& action) {
    if (validate_voice_note_delete(json)) return;
    strlcpy(action.payload.voice_note_delete.id, json["id"].as<const char*>(),
            sizeof(action.payload.voice_note_delete.id));
}

void serialize_voice_note_delete(const ButtonAction& action, JsonObject json) {
    json["id"] = action.payload.voice_note_delete.id;
}

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

ActionResult dispatch_voice_note_delete(const ButtonAction& action, const char* label, uint32_t token) {
    return queue_voice_note_delete(action.payload.voice_note_delete.id, label, token);
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

bool fields_voice_note_delete(ButtonAction& action, ActionFieldVisitor visitor, void* context) {
    return action_visit_field(visitor, context, "id", action.payload.voice_note_delete.id);
}

void describe_voice_note_delete(JsonObject& action) {
    action["group"] = "Voice notes";
    action["label"] = "Delete saved note";
    action["note"] = "Permanently deletes a saved note. Use [item:id] for a List item.";
    JsonArray fields = action["fields"].to<JsonArray>();
    JsonObject field = fields.add<JsonObject>();
    field["name"] = "id";
    field["description"] = "Saved note ID or binding resolving to one";
    JsonArray editor = action["editor_fields"].to<JsonArray>();
    JsonObject id = editor.add<JsonObject>();
    id["name"] = "id";
    id["label"] = "Note ID";
    id["type"] = "text";
    id["bindable"] = true;
}

DEFINE_AND_REGISTER_ACTION_TYPE(kVoiceNoteDeleteAction, ACTION_TYPE_VOICE_NOTE_DELETE,
    parse_voice_note_delete, serialize_voice_note_delete, dispatch_voice_note_delete,
    fields_voice_note_delete, describe_voice_note_delete, nullptr, validate_voice_note_delete,
    ACTION_EXECUTION_PAUSABLE, true, true, false, nullptr,
    kDeleteOperations, sizeof(kDeleteOperations) / sizeof(kDeleteOperations[0]));
}
#endif
