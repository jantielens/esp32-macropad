#pragma once

#include "board_config.h"

#if HAS_IMAGE_LIBRARY

#include <stddef.h>

void image_library_runtime_init();
bool image_library_runtime_current(char* out, size_t out_len);
bool image_library_runtime_next(char* out, size_t out_len);
bool image_library_runtime_previous(char* out, size_t out_len);
bool image_library_runtime_reload();
void image_library_runtime_tick(uint32_t now_ms);
uint32_t image_library_runtime_generation();

#endif // HAS_IMAGE_LIBRARY