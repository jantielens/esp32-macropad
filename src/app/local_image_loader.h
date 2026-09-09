#pragma once

#include "board_config.h"

#if HAS_IMAGE_LIBRARY

#include <stdbool.h>
#include <stdint.h>

#include "image_decoder.h"
#include "image_library.h"

typedef int8_t local_image_slot_t;
#define LOCAL_IMAGE_SLOT_INVALID (-1)
#define LOCAL_IMAGE_SLOT_MAX 16

// Requests one decode of a local image into a separate slot. The path is copied.
// The returned slot has task-owned decode buffers and an LVGL-owned render buffer.
local_image_slot_t local_image_loader_request(
    const char* path, uint16_t target_w, uint16_t target_h,
    ImageScaleMode scale_mode = IMAGE_SCALE_COVER, uint16_t letterbox_color = 0);

// Queues another decode for an active slot, useful when its local path changes.
bool local_image_loader_reload(local_image_slot_t slot);

// Cancels a slot and releases all of its pixel buffers.
void local_image_loader_cancel(local_image_slot_t slot);
void local_image_loader_cancel_all();

bool local_image_loader_has_new_frame(local_image_slot_t slot);

// Hands the newest frame to LVGL. The returned pointer remains LVGL-owned and
// stable until the next call for this slot.
const uint16_t* local_image_loader_get_frame(
    local_image_slot_t slot, uint16_t* out_w, uint16_t* out_h);

uint32_t local_image_loader_get_drops(local_image_slot_t slot);

#endif // HAS_IMAGE_LIBRARY