#include "voice_notes_binding.h"

#if HAS_VOICE_NOTES && HAS_DISPLAY
#include "binding_template.h"
#include "log_manager.h"
#include "voice_notes.h"
#include "voice_notes_processing.h"
#if HAS_PSRAM
#include "voice_notes_loaded.h"
#endif
#include <stdio.h>
#include <string.h>

namespace {
const char* const kKeys[] = {"recording.state", "recording.duration_s", "recording.scenario", "recording.error",
    "last_saved.id", "transcription.state", "transcription.note_id", "transcription.error",
    "enrichment.state", "enrichment.note_id", "enrichment.error", "processing.state",
    "transcription.retry_status", "enrichment.retry_status"
#if HAS_PSRAM
    , "latest_transcript.text", "latest_transcript.id",
    "loaded.status", "loaded.error", "loaded.id", "loaded.title", "loaded.summary",
    "loaded.transcript", "loaded.created_at", "loaded.duration_ms", "loaded.has_transcript",
    "loaded.has_enrichment"
#endif
};
#if HAS_PSRAM
BindingResolverStatus acquire_text(const char* key, BindingTextView* out) {
    if (!strcmp(key, "latest_transcript.text"))
        return voice_notes_latest_transcript_acquire_text(out);
    if (strncmp(key, "loaded.", 7)) return BINDING_RESOLVER_UNKNOWN;
    return voice_notes_loaded_acquire_text(key + 7, out);
}
#endif
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
    {"transcription.retry_status", "Service retry cycle: empty, Retry 1/2/3 of 3, or Retries halted; not per-note", nullptr},
    {"enrichment.state", "Current title and summary generation state", nullptr},
    {"enrichment.note_id", "Note ID of the latest enrichment attempt", nullptr},
    {"enrichment.error", "Most recent enrichment error", nullptr},
    {"enrichment.retry_status", "Service retry cycle: empty, Retry 1/2/3 of 3, or Retries halted; not per-note", nullptr},
    {"processing.state", "Background processing state", nullptr},
#if HAS_PSRAM
    {"latest_transcript.text", "Latest successful transcript since boot, in completion order: short scalar preview, or full text in a Text reader", nullptr},
    {"latest_transcript.id", "Voice note ID of the latest successful transcript since boot", nullptr},
    {"loaded.status", "Explicitly loaded note: idle, loading, ready, or error", nullptr},
    {"loaded.error", "Most recent load failure", nullptr},
    {"loaded.id", "ID of the loaded note", nullptr},
    {"loaded.title", "Saved title: short scalar preview, or full text in a Text reader", nullptr},
    {"loaded.summary", "Saved summary: short scalar preview, or full text in a Text reader", nullptr},
    {"loaded.transcript", "Saved transcript: short scalar preview, or full text in a Text reader", nullptr},
    {"loaded.created_at", "Note creation timestamp when known", nullptr},
    {"loaded.duration_ms", "Note duration in milliseconds", nullptr},
    {"loaded.has_transcript", "Whether the loaded note has a transcript", nullptr},
    {"loaded.has_enrichment", "Whether the loaded note has title and summary data", nullptr},
#endif
};
const BindingExampleDoc kExamples[] = {
    {"[voice_note:recording.state]", "Show the recorder state."},
    {"[voice_note:recording.duration_s]", "Show the captured duration."},
    {"[voice_note:processing.state]", "Show background processing state."},
    {"[voice_note:enrichment.state]", "Show title and summary generation state."},
    {"[voice_note:transcription.retry_status]", "Show the transcription service retry cycle."},
#if HAS_PSRAM
    {"[voice_note:latest_transcript.text|No transcript]", "Show the latest successful transcript."},
    {"[voice_note:latest_transcript.id]", "Identify the note that produced the latest transcript."},
    {"[voice_note:loaded.status]", "Show the explicit load state."},
    {"[voice_note:loaded.title|Untitled]", "Show a preview of the loaded note title."},
    {"[voice_note:loaded.transcript|No transcript]", "Show a preview of the loaded transcript."},
#endif
};
const BindingSchemeDoc kRecorderDoc = {
#if HAS_PSRAM
    "Device", "Local voice note recorder status, latest transcript, and explicitly loaded note data.",
#else
    "Device", "Local voice note recorder status.",
#endif
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
    binding_authoring_text("transcription.retry_status", "Retry 1 of 3"),
    binding_authoring_text("enrichment.state", "idle"),
    binding_authoring_text("enrichment.note_id", "0000000100000002"),
    binding_authoring_text("enrichment.error", "Enrichment request failed"),
    binding_authoring_text("enrichment.retry_status", "Retries halted"),
    binding_authoring_text("processing.state", "idle"),
#if HAS_PSRAM
    binding_authoring_text("latest_transcript.text", "A recorded conversation"),
    binding_authoring_text("latest_transcript.id", "0000000100000002"),
    binding_authoring_text("loaded.status", "ready"),
    binding_authoring_text("loaded.error", "note not found"),
    binding_authoring_text("loaded.id", "0000000100000002"),
    binding_authoring_text("loaded.title", "Meeting notes"),
    binding_authoring_text("loaded.summary", "A short summary"),
    binding_authoring_text("loaded.transcript", "A recorded conversation"),
    binding_authoring_integer("loaded.created_at", "s", "1700000000"),
    binding_authoring_integer("loaded.duration_ms", "ms", "12500"),
    binding_authoring_text("loaded.has_transcript", "true"),
    binding_authoring_text("loaded.has_enrichment", "true"),
#endif
};
const BindingAuthoringSpec kAuthoring = {BINDING_DOC_LIST(kAuthoringKeys)};

uint8_t key_count() { return sizeof(kKeys) / sizeof(kKeys[0]); }
const char* key_at(uint8_t index) { return index < key_count() ? kKeys[index] : nullptr; }
BindingResolverStatus resolve(const char* key, char* out, size_t size) {
    if (!key || !out || !size) return BINDING_RESOLVER_UNAVAILABLE;
#if HAS_PSRAM
    if (strncmp(key, "latest_transcript.", 18) == 0)
        return voice_notes_latest_transcript_resolve(key + 18, out, size);
    if (strncmp(key, "loaded.", 7) == 0) {
        bool known = false;
        for (const char* candidate : kKeys) known |= strcmp(candidate, key) == 0;
        return known ? voice_notes_loaded_resolve(key + 7, out, size) : BINDING_RESOLVER_UNKNOWN;
    }
#endif
    VoiceNoteSnapshot snapshot = {};
    voice_notes_snapshot(&snapshot);
    if (strcmp(key, "processing.state") == 0) {
        VoiceNotesProcessingSnapshot processing = {};
        voice_notes_processing_snapshot(&processing);
        strlcpy(out, voice_notes_processing_state_name(processing.state), size);
    } else if (strncmp(key, "enrichment.", 11) == 0) {
        VoiceNotesEnrichmentSnapshot enrichment = {};
        voice_notes_enrichment_snapshot(&enrichment);
        if (strcmp(key, "enrichment.state") == 0)
            strlcpy(out, voice_notes_enrichment_state_name(enrichment.state), size);
        else if (strcmp(key, "enrichment.retry_status") == 0)
            strlcpy(out, enrichment.retry_status, size);
        else if (strcmp(key, "enrichment.note_id") == 0) {
            if (!enrichment.note_id[0]) return BINDING_RESOLVER_UNAVAILABLE;
            strlcpy(out, enrichment.note_id, size);
        } else if (strcmp(key, "enrichment.error") == 0) {
            if (!enrichment.error[0]) return BINDING_RESOLVER_UNAVAILABLE;
            strlcpy(out, enrichment.error, size);
        } else return BINDING_RESOLVER_UNKNOWN;
    } else if (strncmp(key, "transcription.", 14) == 0) {
        VoiceNotesTranscriptionSnapshot transcription = {};
        voice_notes_transcription_snapshot(&transcription);
        if (strcmp(key, "transcription.state") == 0)
            strlcpy(out, voice_notes_transcription_state_name(transcription.state), size);
        else if (strcmp(key, "transcription.retry_status") == 0)
            strlcpy(out, transcription.retry_status, size);
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
         key_count, key_at, &kRecorderDoc, &kAuthoring, nullptr,
#if HAS_PSRAM
         acquire_text
#else
         nullptr
#endif
        })) {
        LOGE("RecorderBind", "Failed to register recorder binding scheme");
    }
}
#endif
