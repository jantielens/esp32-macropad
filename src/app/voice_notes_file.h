#pragma once

#include "board_config.h"

#if HAS_VOICE_NOTES
#include "storage.h"
#include <stdint.h>

enum class VoiceNoteFileStatus : uint8_t { Ready, NotFound, Busy, Error };

VoiceNoteFileStatus voice_notes_open(const char* id, File* file);
void voice_notes_close(File* file, const char* id);
VoiceNoteFileStatus voice_notes_remove(const char* id);
bool voice_notes_has_transcript(const char* id);
VoiceNoteFileStatus voice_notes_open_transcript(const char* id, File* file);
bool voice_notes_write_transcript(const char* id, const uint8_t* json, size_t length);
#endif
