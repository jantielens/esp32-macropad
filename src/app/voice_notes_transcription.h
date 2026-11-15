#pragma once

#include "board_config.h"

#if HAS_VOICE_NOTES
#include <stddef.h>
#include <stdint.h>

struct VoiceNotesTranscriptionConfig {
    char base_url[512];
    char api_key[256];
    char model[128];
    bool auto_transcribe = false;
};

enum class VoiceNotesTranscriptionState : uint8_t {
    Idle, Preparing, Transcribing, Saving, Ready, Error
};

struct VoiceNotesTranscriptionSnapshot {
    VoiceNotesTranscriptionState state;
    char note_id[17];
    char error[112];
};

enum class VoiceNotesTranscriptionStart : uint8_t {
    Accepted, InvalidId, NotFound, NotConfigured, Busy, Offline, Unavailable, Cooldown
};

enum class VoiceNotesProcessingState : uint8_t { Idle, Waiting, Running, Blocked };
enum class VoiceNotesProcessingReason : uint8_t { None, Offline, Recording, Update, Retry, Configuration };
struct VoiceNotesProcessingSnapshot {
    VoiceNotesProcessingState state;
    VoiceNotesProcessingReason reason;
};

bool voice_notes_normalize_endpoint(const char* input, char* output, size_t capacity);
bool voice_notes_transcription_configure(const VoiceNotesTranscriptionConfig& config);
bool voice_notes_transcription_configured();
VoiceNotesTranscriptionStart voice_notes_transcription_start(const char* id);
bool voice_notes_processing_init();
VoiceNotesTranscriptionStart voice_notes_processing_start();
void voice_notes_processing_snapshot(VoiceNotesProcessingSnapshot* out);
const char* voice_notes_processing_state_name(VoiceNotesProcessingState state);
const char* voice_notes_processing_reason_name(VoiceNotesProcessingReason reason);
void voice_notes_transcription_snapshot(VoiceNotesTranscriptionSnapshot* out);
const char* voice_notes_transcription_state_name(VoiceNotesTranscriptionState state);
#endif
