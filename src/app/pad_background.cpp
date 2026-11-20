#include "pad_background.h"

#if HAS_DISPLAY && HAS_IMAGE_LIBRARY
#include "image_library_runtime.h"
#include "local_image_loader.h"
#include "log_manager.h"
#include "screens/pad_image_frame.h"

#include <string.h>

namespace {
local_image_slot_t slot = LOCAL_IMAGE_SLOT_INVALID;
char source[IMAGE_LIBRARY_PATH_MAX_LEN] = {};
uint16_t targetWidth = 0;
uint16_t targetHeight = 0;
uint32_t contentGeneration = 0;
lv_obj_t* image = nullptr;
lv_image_dsc_t descriptor = {};
}

void pad_background_detach(lv_obj_t* screen) {
    if (image && lv_obj_get_parent(image) == screen) {
        lv_image_set_src(image, nullptr);
        lv_obj_delete(image);
        image = nullptr;
    }
}

void pad_background_release() {
    if (image) pad_background_detach(lv_obj_get_parent(image));
    if (slot != LOCAL_IMAGE_SLOT_INVALID) local_image_loader_cancel(slot);
    slot = LOCAL_IMAGE_SLOT_INVALID;
    source[0] = '\0';
    targetWidth = targetHeight = 0;
}

void pad_background_update(lv_obj_t* screen, const char* path, uint16_t width, uint16_t height) {
    const uint32_t generation = image_library_runtime_content_generation();
    if (strcmp(path, source) != 0 || width != targetWidth || height != targetHeight ||
        generation != contentGeneration) {
        pad_background_release();
        strlcpy(source, path, sizeof(source));
        targetWidth = width;
        targetHeight = height;
        contentGeneration = generation;
        if (path[0]) {
            slot = local_image_loader_request(path, width, height);
            if (slot == LOCAL_IMAGE_SLOT_INVALID)
                LOGE("PadBg", "Unable to load background %.96s", path);
        }
    }
    if (slot == LOCAL_IMAGE_SLOT_INVALID) return;
    if (!image) {
        image = lv_image_create(screen);
        lv_obj_set_clickable(image, false);
        lv_obj_move_to_index(image, 0);
    }
    const bool fresh = local_image_loader_has_new_frame(slot);
    if (!fresh && lv_image_get_src(image)) return;
    uint16_t frameWidth = 0, frameHeight = 0;
    const uint16_t* pixels = local_image_loader_get_frame(slot, &frameWidth, &frameHeight);
    if (pixels)
        pad_set_image_frame(image, descriptor, pixels, frameWidth, frameHeight,
                            {0, 0, frameWidth, frameHeight}, false);
}
#endif
