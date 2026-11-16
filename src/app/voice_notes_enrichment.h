#pragma once

#include "board_config.h"

#if HAS_VOICE_NOTES
#include <ArduinoJson.h>
#include <stddef.h>
#include <stdint.h>

enum class VoiceNotesEnrichmentParse : uint8_t {
    Success, NoteFailure, ServiceFailure, ResourceFailure
};

bool voice_notes_enrichment_build_request(JsonDocument& request, const char* deployment,
                                          const char* transcript);
VoiceNotesEnrichmentParse voice_notes_enrichment_parse_response(
    const char* response, size_t length, JsonDocument& envelope, JsonDocument& result);
#endif
