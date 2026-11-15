#include "voice_notes_binding.h"

#if HAS_VOICE_NOTES && HAS_DISPLAY
#include "binding_template.h"
#include "log_manager.h"
#include "voice_notes.h"
#include "voice_notes_transcription.h"
#include <stdio.h>
#include <string.h>

namespace {
const char* const kKeys[] = {"recording.state", "recording.duration_s", "recording.scenario", "recording.error",
    "last_saved.id", "transcription.state", "transcription.note_id", "transcription.error"};
const BindingParamDoc kParams[] = {{"key", "Recorder snapshot key, see below."}};
const BindingKeyDoc kKeyDocs[] = {
    {"recording.state", "Current recording state", nullptr},
    {"recording.duration_s", "Captured duration in seconds, excluding pauses", nullptr},
    {"recording.scenario", "Active recording scenario", nullptr},
    {"recording.error", "Most recent recorder error", nullptr},
    {"last_saved.id", "ID of the last successfully saved note since boot", nullptr},
    {"transcription.state", "Current transcription state", nullptr},
    {"transcription.note_id", "Note ID of the latest transcription attempt", nullptr},
    {"transcription.error", "Most recent transcription error", nullptr},
};
const BindingExampleDoc kExamples[] = {
    {"[voice_note:recording.state]", "Show the recorder state."},
    {"[voice_note:recording.duration_s]", "Show the captured duration."},
};
const BindingSchemeDoc kRecorderDoc = {
    "Device", "Local voice note recorder status.",
    BINDING_DOC_LIST(kParams), BINDING_DOC_LIST(kKeyDocs), BINDING_DOC_LIST(kExamples),
    nullptr, false, BINDING_DOC_NONE, nullptr, nullptr,
};
const BindingAuthoringKey kAuthoringKeys[] = {
    binding_authoring_text("recording.state", "idle"),
    binding_authoring_integer("recording.duration_s", "s", "0"),
    binding_authoring_text("recording.scenario", "general"),
    binding_authoring_text("recording.error", "No audio captured"),
    binding_authoring_text("last_saved.id", "0000000100000002"),
    binding_authoring_text("transcription.state", "idle"),
    binding_authoring_text("transcription.note_id", "0000000100000002"),
    binding_authoring_text("transcription.error", "Transcription request failed"),
};
const BindingAuthoringSpec kAuthoring = {BINDING_DOC_LIST(kAuthoringKeys)};

uint8_t key_count() { return sizeof(kKeys) / sizeof(kKeys[0]); }
const char* key_at(uint8_t index) { return index < key_count() ? kKeys[index] : nullptr; }
BindingResolverStatus resolve(const char* key, char* out, size_t size) {
    if (!key || !out || !size) return BINDING_RESOLVER_UNAVAILABLE;
    VoiceNoteSnapshot snapshot = {};
    voice_notes_snapshot(&snapshot);
    if (strncmp(key, "transcription.", 14) == 0) {
        VoiceNotesTranscriptionSnapshot transcription = {};
        voice_notes_transcription_snapshot(&transcription);
        if (strcmp(key, "transcription.state") == 0)
            strlcpy(out, voice_notes_transcription_state_name(transcription.state), size);
        else if (strcmp(key, "transcription.note_id") == 0) {
            if (!transcription.note_id[0]) return BINDING_RESOLVER_UNAVAILABLE;
            strlcpy(out, transcription.note_id, size);
        } else if (strcmp(key, "transcription.error") == 0) {
            if (!transcription.error[0]) return BINDING_RESOLVER_UNAVAILABLE;
            strlcpy(out, transcription.error, size);
        } else return BINDING_RESOLVER_UNKNOWN;
    }
    else if (strcmp(key, "recording.state") == 0) strlcpy(out, voice_notes_state_name(snapshot.state), size);
    else if (strcmp(key, "recording.duration_s") == 0)
        snprintf(out, size, "%llu", static_cast<unsigned long long>(snapshot.duration_ms / 1000));
    else if (strcmp(key, "recording.scenario") == 0) {
        if (!snapshot.session_active) return BINDING_RESOLVER_UNAVAILABLE;
        strlcpy(out, snapshot.scenario, size);
    }
    else if (strcmp(key, "last_saved.id") == 0) {
        if (!snapshot.last_clip[0]) return BINDING_RESOLVER_UNAVAILABLE;
        strlcpy(out, snapshot.last_clip, size);
    }
    else if (strcmp(key, "recording.error") == 0) {
        if (!snapshot.error[0]) return BINDING_RESOLVER_UNAVAILABLE;
        strlcpy(out, snapshot.error, size);
    }
    else return BINDING_RESOLVER_UNKNOWN;
    return BINDING_RESOLVER_RESOLVED;
}
}

void voice_notes_binding_init() {
    if (!binding_template_register("voice_note", resolve, nullptr,
        {1, 1, 1, -1, BINDING_VALIDATION_STANDARD, false,
         key_count, key_at, &kRecorderDoc, &kAuthoring})) {
        LOGE("RecorderBind", "Failed to register recorder binding scheme");
    }
}
#endif
