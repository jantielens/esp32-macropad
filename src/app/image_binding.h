#pragma once

#include "board_config.h"

#if HAS_IMAGE_LIBRARY
#include <stddef.h>

void image_binding_init();
void image_binding_set_error(const char* stage, const char* detail);
#endif