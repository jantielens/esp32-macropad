#pragma once

#include "board_config.h"

#if HAS_VOICE_CLIPS
#include <stdint.h>
#include <Arduino.h>

enum class VoiceClipState : uint8_t {
    Idle, Starting, Recording, Paused, Saving, Discarding, Error
};
enum class VoiceClipCommand : uint8_t { Start, Pause, Resume, Save, Discard };
enum class VoiceClipCancelStatus : uint8_t { Ready, Pending, Error };

struct VoiceClipSnapshot {
    VoiceClipState state;
    uint64_t duration_ms;
    char last_clip[32];
    char error[112];
    bool session_active;
};

bool voice_clips_init();
bool voice_clips_command(VoiceClipCommand command, uint32_t continuation_token);
void voice_clips_snapshot(VoiceClipSnapshot* out);
const char* voice_clips_state_name(VoiceClipState state);
VoiceClipCancelStatus voice_clips_cancel();
String voice_clips_list();
bool voice_clips_exists(const char* id);
String voice_clips_path(const char* id);
#endif
