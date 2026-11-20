#include "action_registry.h"

#if HAS_VOICE_NOTES && HAS_DISPLAY && HAS_PSRAM
#include "../binding_template.h"
#include "../voice_notes.h"
#include "../voice_notes_loaded.h"
#include "log_manager.h"
#include <string.h>

namespace {
ActionResult request_load(const char* id, const char* label) {
    if (voice_notes_load_request(id)) return ACTION_COMPLETE;
    LOGW("Action", "%s load voice note rejected", label);
    return ACTION_FAILED;
}

const char* validate_load_note_id(const char* id) {
    return voice_notes_id_valid(id) ? nullptr : "note ID must be 16 lowercase hexadecimal characters";
}

const ActionArgumentDef kLoadArguments[] = {
    {"id", "Note ID", ACTION_ARGUMENT_STRING, 0, 0, 16, true, "[item:id]", "text",
     nullptr, 0, false, validate_load_note_id},
};

ActionResult dispatch_load_operation(const ActionResolvedArgument* arguments, const char* label, uint32_t) {
    return request_load(arguments[0].text, label);
}

const ActionOperationDef kLoadOperations[] = {
    {"voice_note.load", "Load saved note", kLoadArguments, 1, dispatch_load_operation},
};

void describe_load(JsonObject& action) {
    action["group"] = "Voice notes";
    action["label"] = "Load saved note";
    action["note"] = "Loads one shared snapshot in the background. Use [item:id] for a List item.";
}

DEFINE_AND_REGISTER_ACTION_TYPE(kVoiceNoteLoadAction, ACTION_TYPE_VOICE_NOTE_LOAD, describe_load, nullptr, ACTION_EXECUTION_SYNC, true, false,
    kLoadOperations, sizeof(kLoadOperations) / sizeof(kLoadOperations[0]));
}
#endif
