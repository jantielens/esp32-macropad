#pragma once

#include "board_config.h"

#if HAS_VOICE_NOTES
#include <stdint.h>
#include <Arduino.h>
#include <ArduinoJson.h>
#include <string.h>

inline bool voice_notes_id_valid(const char* id) {
    if (!id || strlen(id) != 16) return false;
    for (size_t i = 0; i < 16; ++i)
        if (!((id[i] >= '0' && id[i] <= '9') || (id[i] >= 'a' && id[i] <= 'f'))) return false;
    return true;
}

enum class VoiceNoteState : uint8_t {
    Idle, Starting, Recording, Paused, Saving, Discarding, Error
};
enum class VoiceNoteCommand : uint8_t { Toggle, Start, Pause, Resume, Save, Discard };
enum class VoiceNoteCancelStatus : uint8_t { Ready, Pending, Error };

struct VoiceNoteSnapshot {
    VoiceNoteState state;
    uint64_t duration_ms;
    char last_clip[32];
    char error[112];
    char scenario[8];
    bool session_active;
    uint64_t recording_generation;
};

struct VoiceNoteCursor {
    uint32_t created_at;
    char id[17];
};
enum class VoiceNoteCandidate : uint8_t { Found, None, Error, Interrupted };
enum class VoiceNoteRecentResult : uint8_t { Ready, Deferred, Error };

bool voice_notes_init();
bool voice_notes_command(VoiceNoteCommand command, uint32_t continuation_token,
                         bool type_after_transcription = false);
bool voice_notes_take_typing_request(const char* id);
void voice_notes_snapshot(VoiceNoteSnapshot* out);
const char* voice_notes_state_name(VoiceNoteState state);
VoiceNoteCancelStatus voice_notes_cancel();
bool voice_notes_list(JsonDocument& output);
uint32_t voice_notes_catalog_revision();
VoiceNoteRecentResult voice_notes_select_recent(JsonDocument& output, size_t limit);
VoiceNoteRecentResult voice_notes_enrich_recent(JsonObject entry);
bool voice_notes_read_metadata(const char* id, JsonObject output);
bool voice_notes_exists(const char* id);
VoiceNoteCandidate voice_notes_next_candidate(const VoiceNoteCursor& after, VoiceNoteCursor* next,
                                              bool transcription = true, bool enrichment = false);
String voice_notes_path(const char* id);
#endif
