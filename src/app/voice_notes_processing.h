#pragma once

#include "board_config.h"

#if HAS_VOICE_NOTES
#include <stddef.h>
#include <stdint.h>

struct VoiceNotesServiceConfig {
    char endpoint[512];
    char api_key[256];
    char deployment[128];
};

struct VoiceNotesConfig {
    VoiceNotesServiceConfig transcription = {};
    VoiceNotesServiceConfig llm = {};
    bool auto_process = false;
};

enum class VoiceNotesTranscriptionState : uint8_t {
    Idle, Preparing, Transcribing, Saving, Ready, Error
};

struct VoiceNotesTranscriptionSnapshot {
    VoiceNotesTranscriptionState state;
    char note_id[17];
    char error[112];
    char retry_status[20];
};

enum class VoiceNotesProcessingStart : uint8_t {
    Accepted, Unchanged, InvalidId, NotFound, NotConfigured, Busy, Offline, Unavailable, Cooldown, MissingResult
};

enum class VoiceNotesProcessingState : uint8_t { Idle, Waiting, Running, Blocked };
enum class VoiceNotesProcessingReason : uint8_t { None, Offline, Recording, Update, Retry, Configuration, RetriesExhausted };
enum class VoiceNotesProcessingService : uint8_t { None, Transcription, Enrichment, Both };
enum class VoiceNotesProcessingMode : uint8_t { Missing, Retranscribe, Regenerate };
struct VoiceNotesProcessingSnapshot {
    VoiceNotesProcessingState state;
    VoiceNotesProcessingReason reason;
    VoiceNotesProcessingService service;
};

enum class VoiceNotesEnrichmentState : uint8_t { Idle, Preparing, Generating, Saving, Ready, Error };
struct VoiceNotesEnrichmentSnapshot {
    VoiceNotesEnrichmentState state;
    char note_id[17];
    char error[112];
    char retry_status[20];
};

bool voice_notes_normalize_endpoint(const char* input, char* output, size_t capacity);
bool voice_notes_processing_configure(const VoiceNotesConfig& config);
bool voice_notes_transcription_configured();
bool voice_notes_enrichment_configured();
bool voice_notes_processing_init();
VoiceNotesProcessingStart voice_notes_processing_start();
VoiceNotesProcessingStart voice_notes_processing_start(const char* id, VoiceNotesProcessingMode mode);
void voice_notes_processing_snapshot(VoiceNotesProcessingSnapshot* out);
const char* voice_notes_processing_state_name(VoiceNotesProcessingState state);
const char* voice_notes_processing_reason_name(VoiceNotesProcessingReason reason);
const char* voice_notes_processing_service_name(VoiceNotesProcessingService service);
void voice_notes_transcription_snapshot(VoiceNotesTranscriptionSnapshot* out);
const char* voice_notes_transcription_state_name(VoiceNotesTranscriptionState state);
void voice_notes_enrichment_snapshot(VoiceNotesEnrichmentSnapshot* out);
const char* voice_notes_enrichment_state_name(VoiceNotesEnrichmentState state);
#if HAS_DISPLAY && HAS_PSRAM
#include "binding_template.h"
BindingResolverStatus voice_notes_latest_transcript_resolve(const char* key, char* out, size_t size);
BindingResolverStatus voice_notes_latest_transcript_acquire_text(BindingTextView* out);
#endif
#endif
