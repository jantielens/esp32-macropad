#pragma once

#include <stddef.h>

// Returns the formatted length, or a negative value when formatting fails.
int voice_transcription_build_multipart_prefix(char* out, size_t out_size,
                                               const char* boundary, const char* language);