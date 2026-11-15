#pragma once

#include "board_config.h"

#if HAS_VOICE_CLIPS
#include "storage.h"
#include <stdint.h>

enum class VoiceClipFileStatus : uint8_t { Ready, NotFound, Busy, Error };

VoiceClipFileStatus voice_clips_open(const char* id, File* file);
void voice_clips_close(File* file, const char* id);
VoiceClipFileStatus voice_clips_remove(const char* id);
#endif
