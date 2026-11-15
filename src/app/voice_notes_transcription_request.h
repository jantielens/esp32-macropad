#pragma once

#include "board_config.h"

#if HAS_VOICE_NOTES
#include "voice_notes_transcription.h"
#include "storage.h"

struct VoiceNotesRequestResult {
    char* text;
    int status;
    const char* error;
};

bool voice_notes_transcription_checkpoint(uint32_t ota_epoch);
bool voice_notes_transcription_request(const VoiceNotesTranscriptionConfig& config, File& recording,
                                       uint32_t ota_epoch, VoiceNotesRequestResult& result);
#endif
