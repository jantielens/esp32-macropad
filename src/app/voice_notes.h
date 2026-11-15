#pragma once

#include "board_config.h"

#if HAS_VOICE_NOTES
#include <stdint.h>
#include <Arduino.h>
#include <ArduinoJson.h>

enum class VoiceNoteState : uint8_t {
    Idle, Starting, Recording, Paused, Saving, Discarding, Error
};
enum class VoiceNoteCommand : uint8_t { Start, Pause, Resume, Save, Discard };
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

bool voice_notes_init();
bool voice_notes_command(VoiceNoteCommand command, uint32_t continuation_token);
void voice_notes_snapshot(VoiceNoteSnapshot* out);
const char* voice_notes_state_name(VoiceNoteState state);
VoiceNoteCancelStatus voice_notes_cancel();
bool voice_notes_list(JsonDocument& output);
bool voice_notes_read_metadata(const char* id, JsonObject output);
bool voice_notes_exists(const char* id);
VoiceNoteCandidate voice_notes_next_candidate(const VoiceNoteCursor& after, VoiceNoteCursor* next);
String voice_notes_path(const char* id);
#endif
