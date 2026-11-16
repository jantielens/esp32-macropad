#pragma once

#include "board_config.h"

#if HAS_VOICE_NOTES
#include "voice_notes_transcription_request.h"
#include <ArduinoJson.h>

bool voice_notes_enrichment_request(const VoiceNotesServiceConfig& config, const char* transcript,
                                    uint32_t ota_epoch, uint64_t recording_generation,
                                    JsonDocument& content, VoiceNotesRequestResult& result);
#endif
