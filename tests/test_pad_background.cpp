#include "pad_background.h"
#include "local_image_loader.h"
#include "image_library_runtime.h"

#include <cassert>
#include <string>
#include <vector>

namespace {
unsigned requests = 0;
unsigned cancellations = 0;
uint32_t generation = 0;
bool active = false;
bool ready = false;
bool fail_request = false;
uint16_t frame_width = 0, frame_height = 0;
std::vector<uint16_t> pixels;
std::string requested_path;
}

local_image_slot_t local_image_loader_request(const char* path, uint16_t width, uint16_t height,
                                              ImageScaleMode mode, uint16_t) {
    assert(!active); // A different background must release its predecessor first.
    assert(mode == IMAGE_SCALE_COVER);
    ++requests;
    requested_path = path;
    if (fail_request) return LOCAL_IMAGE_SLOT_INVALID;
    frame_width = width;
    frame_height = height;
    active = true;
    ready = false;
    return 0;
}

void local_image_loader_cancel(local_image_slot_t slot) {
    assert(slot == 0 && active);
    // No LVGL object may still reference the buffer when it is freed.
    assert(lv_obj_get_child_count(lv_screen_active()) == 0);
    active = ready = false;
    std::vector<uint16_t>().swap(pixels);
    ++cancellations;
}

bool local_image_loader_has_new_frame(local_image_slot_t) { return ready; }
const uint16_t* local_image_loader_get_frame(local_image_slot_t, uint16_t* width, uint16_t* height) {
    *width = frame_width;
    *height = frame_height;
    ready = false;
    return pixels.empty() ? nullptr : pixels.data();
}
uint32_t image_library_runtime_content_generation() { return generation; }

static void finish_decode() {
    pixels.assign(frame_width * frame_height, 0x1234);
    ready = true;
}

int main() {
    lv_init();
    lv_display_t* display = lv_display_create(4, 4);
    lv_obj_t* first = lv_screen_active();
    lv_obj_t* second = lv_obj_create(nullptr);

    pad_background_update(first, "/images/a.png", 4, 4);
    assert(requests == 1 && active);
    pad_background_detach(first);
    lv_screen_load(second);
    pad_background_update(second, "/images/a.png", 4, 4);
    assert(requests == 1 && cancellations == 0); // Pending requests are shared.
    finish_decode();
    pad_background_update(second, "/images/a.png", 4, 4);
    auto* image = lv_obj_get_child(second, 0);
    auto* descriptor = static_cast<const lv_image_dsc_t*>(lv_image_get_src(image));
    assert(descriptor && descriptor->data == reinterpret_cast<const uint8_t*>(pixels.data()));
    assert(!lv_obj_is_clickable(image));

    pad_background_detach(second);
    lv_screen_load(first);
    pad_background_update(first, "/images/a.png", 4, 4);
    assert(requests == 1 && cancellations == 0); // Ready buffers are shared too.
    assert(lv_image_get_src(lv_obj_get_child(first, 0)));
    pad_background_update(first, "/images/b.png", 4, 4);
    assert(requests == 2 && cancellations == 1 && requested_path == "/images/b.png");

    ++generation;
    pad_background_update(first, "/images/b.png", 4, 4);
    assert(requests == 3 && cancellations == 2); // File mutations invalidate the buffer.
    pad_background_update(first, "/images/b.png", 3, 4);
    assert(requests == 4 && cancellations == 3); // Orientation/geometry is part of the key.
    pad_background_update(first, "", 3, 4);
    assert(!active && requests == 4 && cancellations == 4);
    assert(lv_obj_get_child_count(first) == 0);

    fail_request = true;
    pad_background_update(first, "/images/fail.png", 4, 4);
    pad_background_update(first, "/images/fail.png", 4, 4);
    assert(requests == 5 && !active && lv_obj_get_child_count(first) == 0);
    fail_request = false;
    pad_background_update(first, "/images/a.png", 4, 4);
    pad_background_release();
    assert(!active && cancellations == 5);
    pad_background_release(); // Shutdown/empty navigation can release repeatedly.
    lv_obj_delete(second);
    lv_display_delete(display);
    return 0;
}
