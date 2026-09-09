#pragma once

#include "board_config.h"

#if HAS_IMAGE_LIBRARY

#include <stddef.h>
#include <stdint.h>

#include "image_library.h"

struct ImageLibraryConfig {
    char directory[IMAGE_LIBRARY_PATH_MAX_LEN];
    uint16_t interval_seconds;
};

void image_library_config_init();
bool image_library_config_get(ImageLibraryConfig* config);
bool image_library_config_save_raw(const uint8_t* json, size_t len);

#endif // HAS_IMAGE_LIBRARY