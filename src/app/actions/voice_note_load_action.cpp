#include "action_registry.h"

#if HAS_VOICE_NOTES && HAS_DISPLAY && HAS_PSRAM
#include "../binding_template.h"
#include "../voice_notes.h"
#include "../voice_notes_loaded.h"
#include "log_manager.h"
#include <string.h>

namespace {
const char* validate_load(JsonObjectConst action) {
    for (JsonPairConst field : action) {
        const char* name = field.key().c_str();
        if (strcmp(name, "type") && strcmp(name, "id")) return "vnote_load accepts only id";
    }
    if (!action["id"].is<const char*>()) return "vnote_load id must be a string";
    const JsonString id = action["id"].as<JsonString>();
    if (!id.c_str() || !id.size() || id.size() >= sizeof(VoiceNoteLoadPayload::id) ||
        memchr(id.c_str(), '\0', id.size())) return "vnote_load id is empty or too long";
    return nullptr;
}

void parse_load(const JsonObject& json, ButtonAction& action) {
    if (validate_load(json)) return;
    strlcpy(action.payload.voice_note_load.id, json["id"].as<const char*>(),
            sizeof(action.payload.voice_note_load.id));
}

void serialize_load(const ButtonAction& action, JsonObject json) {
    json["id"] = action.payload.voice_note_load.id;
}

ActionResult request_load(const char* id, const char* label) {
    if (voice_notes_load_request(id)) return ACTION_COMPLETE;
    LOGW("Action", "%s load voice note rejected", label);
    return ACTION_FAILED;
}

ActionResult dispatch_load(const ButtonAction& action, const char* label, uint32_t) {
    return request_load(action.payload.voice_note_load.id, label);
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

bool fields_load(ButtonAction& action, ActionFieldVisitor visitor, void* context) {
    return action_visit_field(visitor, context, "id", action.payload.voice_note_load.id);
}

void describe_load(JsonObject& action) {
    action["group"] = "Voice notes";
    action["label"] = "Load saved note";
    action["note"] = "Loads one shared snapshot in the background. Use [item:id] for a List item.";
    JsonArray field_docs = action["fields"].to<JsonArray>();
    JsonObject field = field_docs.add<JsonObject>();
    field["name"] = "id";
    field["description"] = "Saved note ID or binding resolving to one";
    JsonArray fields = action["editor_fields"].to<JsonArray>();
    JsonObject id = fields.add<JsonObject>();
    id["name"] = "id";
    id["label"] = "Note ID";
    id["type"] = "text";
    id["bindable"] = true;
}

DEFINE_AND_REGISTER_ACTION_TYPE(kVoiceNoteLoadAction, ACTION_TYPE_VOICE_NOTE_LOAD,
    parse_load, serialize_load, dispatch_load, fields_load, describe_load,
    nullptr, validate_load, ACTION_EXECUTION_SYNC, true, true, false, nullptr,
    kLoadOperations, sizeof(kLoadOperations) / sizeof(kLoadOperations[0]));
}
#endif
