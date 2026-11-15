#pragma once

#include "board_config.h"

#if HAS_VOICE_NOTES
#include <stddef.h>
#include <stdint.h>

struct VoiceNotesTranscriptionConfig {
    char base_url[512];
    char api_key[256];
    char model[128];
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
    Accepted, InvalidId, NotFound, NotConfigured, Busy, Offline, Unavailable
};

bool voice_notes_normalize_endpoint(const char* input, char* output, size_t capacity);
bool voice_notes_transcription_configure(const VoiceNotesTranscriptionConfig& config);
bool voice_notes_transcription_configured();
VoiceNotesTranscriptionStart voice_notes_transcription_start(const char* id);
void voice_notes_transcription_snapshot(VoiceNotesTranscriptionSnapshot* out);
const char* voice_notes_transcription_state_name(VoiceNotesTranscriptionState state);
#endif
