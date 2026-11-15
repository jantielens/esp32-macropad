#pragma once

#include "board_config.h"

#if HAS_VOICE_NOTES
#include <stdint.h>

// Parse one HTTP byte range against a nonempty file. End is inclusive.
bool voice_note_parse_range(const char* header, uint64_t size,
                            uint64_t* start, uint64_t* end);
#endif
