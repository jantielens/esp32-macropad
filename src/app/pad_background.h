#pragma once

#include "board_config.h"

#if HAS_DISPLAY && HAS_IMAGE_LIBRARY
#include <lvgl.h>

// Called only by the LVGL owner. Detaching preserves the single shared slot
// until the destination pad selects its background or navigation releases it.
void pad_background_update(lv_obj_t* screen, const char* path, uint16_t width, uint16_t height);
void pad_background_detach(lv_obj_t* screen);
void pad_background_release();
#endif
