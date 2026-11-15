#pragma once

#include "board_config.h"

#if HAS_VOICE_NOTES
#include "voice_notes_transcription.h"
#include "storage.h"

enum class VoiceNotesRequestOutcome : uint8_t { SharedFailure, NoteFailure, SetupFailure, RateLimited, Interrupted, Success };
struct VoiceNotesRequestResult {
    char* text;
    int status;
    const char* error;
    VoiceNotesRequestOutcome outcome;
    uint64_t cooldown_ms;
};

bool voice_notes_transcription_checkpoint(uint32_t ota_epoch, uint64_t recording_generation);
bool voice_notes_transcription_request(const VoiceNotesTranscriptionConfig& config, File& recording,
                                       uint32_t ota_epoch, uint64_t recording_generation,
                                       VoiceNotesRequestResult& result);
#endif
