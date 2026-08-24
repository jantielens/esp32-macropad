#include "camera_feed.h"

#if HAS_CAMERA
#include "log_manager.h"
#include "ota_activity.h"

#include <esp_heap_caps.h>

namespace {
constexpr uint32_t kFrameIntervalMs = 250;
constexpr uint32_t kTimingLogIntervalMs = 1000;
constexpr uint8_t kSlotCount = 2;

struct FeedSlot {
    CameraJpegFrame jpeg = {};
    CameraRgb565Frame rgb565 = {};
    uint16_t leases = 0;
};

FeedSlot s_slots[kSlotCount];
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
uint8_t s_current_slot = kSlotCount;
uint16_t s_rgb565_demand = 0;
uint16_t s_jpeg_demand = 0;
uint32_t s_generation = 0;
uint32_t s_last_capture_ms = 0;
uint32_t s_last_timing_log_ms = 0;
CameraCaptureTiming s_timing = {};

bool camera_feed_ensure_cache() {
    if (s_slots[0].rgb565.data && s_slots[1].rgb565.data) return true;

    const CameraCaptureSettings settings = camera_get_capture_settings();
    const size_t bytes = (size_t)settings.output_width * settings.output_height * sizeof(uint16_t);
    for (FeedSlot& slot : s_slots) {
        if (slot.rgb565.data) continue;
        slot.rgb565.data = static_cast<uint16_t*>(
            heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        if (!slot.rgb565.data) {
            LOGE("Camera", "Feed RGB565 cache allocation failed: %u bytes", static_cast<unsigned>(bytes));
            for (FeedSlot& allocated_slot : s_slots) {
                if (allocated_slot.rgb565.data) free(allocated_slot.rgb565.data);
                allocated_slot.rgb565 = {};
            }
            return false;
        }
        slot.rgb565.size = bytes;
    }
    return true;
}

int8_t camera_feed_writable_slot() {
    int8_t result = -1;
    portENTER_CRITICAL(&s_mux);
    for (uint8_t slot = 0; slot < kSlotCount; ++slot) {
        if (slot != s_current_slot && s_slots[slot].leases == 0) {
            result = static_cast<int8_t>(slot);
            break;
        }
    }
    portEXIT_CRITICAL(&s_mux);
    return result;
}
}

void camera_feed_init() {
}

void camera_feed_deinit() {
    for (FeedSlot& slot : s_slots) {
        camera_release_jpeg(&slot.jpeg);
        if (slot.rgb565.data) free(slot.rgb565.data);
        slot = {};
    }
    portENTER_CRITICAL(&s_mux);
    s_current_slot = kSlotCount;
    s_rgb565_demand = 0;
    s_jpeg_demand = 0;
    s_generation = 0;
    s_last_capture_ms = 0;
    s_last_timing_log_ms = 0;
    portEXIT_CRITICAL(&s_mux);
}

void camera_feed_acquire_demand(CameraFeedOutput output) {
    portENTER_CRITICAL(&s_mux);
    uint16_t* demand = output == CAMERA_FEED_OUTPUT_JPEG ? &s_jpeg_demand : &s_rgb565_demand;
    if (*demand != UINT16_MAX) ++*demand;
    portEXIT_CRITICAL(&s_mux);
}

void camera_feed_release_demand(CameraFeedOutput output) {
    portENTER_CRITICAL(&s_mux);
    uint16_t* demand = output == CAMERA_FEED_OUTPUT_JPEG ? &s_jpeg_demand : &s_rgb565_demand;
    if (*demand) --*demand;
    portEXIT_CRITICAL(&s_mux);
}

CameraFeedState camera_feed_get_state() {
    const CameraCaptureSettings settings = camera_get_capture_settings();
    CameraFeedState state = {
        .width = settings.output_width,
        .height = settings.output_height,
        .jpeg_quality = settings.jpeg_quality,
        .interval_ms = kFrameIntervalMs,
    };
    portENTER_CRITICAL(&s_mux);
    state.rgb565_consumers = s_rgb565_demand;
    state.jpeg_consumers = s_jpeg_demand;
    state.active_consumers = s_rgb565_demand + s_jpeg_demand;
    state.generation = s_generation;
    state.timing = s_timing;
    portEXIT_CRITICAL(&s_mux);
    return state;
}

bool camera_feed_acquire_frame(CameraFeedFrame* frame, CameraFeedOutput output) {
    if (!frame) return false;
    portENTER_CRITICAL(&s_mux);
    const uint8_t slot = s_current_slot;
    if (slot >= kSlotCount || !s_slots[slot].rgb565.data ||
        (output == CAMERA_FEED_OUTPUT_JPEG && !s_slots[slot].jpeg.data) ||
        s_slots[slot].leases == UINT16_MAX) {
        portEXIT_CRITICAL(&s_mux);
        return false;
    }
    ++s_slots[slot].leases;
    *frame = {.jpeg = &s_slots[slot].jpeg, .rgb565 = &s_slots[slot].rgb565,
              .generation = s_generation, .slot = slot};
    portEXIT_CRITICAL(&s_mux);
    return true;
}

void camera_feed_release_frame(const CameraFeedFrame* frame) {
    if (!frame || frame->slot >= kSlotCount) return;
    portENTER_CRITICAL(&s_mux);
    if (s_slots[frame->slot].leases) --s_slots[frame->slot].leases;
    portEXIT_CRITICAL(&s_mux);
}

void camera_feed_loop() {
    uint16_t rgb565_demand = 0;
    uint16_t jpeg_demand = 0;
    portENTER_CRITICAL(&s_mux);
    rgb565_demand = s_rgb565_demand;
    jpeg_demand = s_jpeg_demand;
    portEXIT_CRITICAL(&s_mux);
    if ((!rgb565_demand && !jpeg_demand) || ota_activity_is_active()) return;
    if (!camera_feed_ensure_cache()) return;

    const uint32_t now = millis();
    if (now - s_last_capture_ms < kFrameIntervalMs) return;
    const int8_t writable_slot = camera_feed_writable_slot();
    if (writable_slot < 0) return;

    FeedSlot& slot = s_slots[writable_slot];
    CameraJpegFrame jpeg = {};
    CameraCaptureTiming timing = {};
    if (!camera_capture_rgb565(&slot.rgb565, jpeg_demand ? &jpeg : nullptr, &timing)) return;
    s_last_capture_ms = now;

    CameraJpegFrame previous = {};
    portENTER_CRITICAL(&s_mux);
    previous = slot.jpeg;
    slot.jpeg = jpeg;
    s_current_slot = static_cast<uint8_t>(writable_slot);
    ++s_generation;
    s_timing = timing;
    portEXIT_CRITICAL(&s_mux);
    camera_release_jpeg(&previous);
    if (now - s_last_timing_log_ms >= kTimingLogIntervalMs) {
        s_last_timing_log_ms = now;
        LOGD("Camera", "Feed timing: raw=%u us rgb565=%u us jpeg=%u us total=%u us",
             static_cast<unsigned>(timing.raw_capture_us), static_cast<unsigned>(timing.rgb565_convert_us),
             static_cast<unsigned>(timing.jpeg_encode_us), static_cast<unsigned>(timing.total_us));
    }
}

#else

void camera_feed_init() {}
void camera_feed_deinit() {}
void camera_feed_acquire_demand(CameraFeedOutput) {}
void camera_feed_release_demand(CameraFeedOutput) {}
CameraFeedState camera_feed_get_state() { return {}; }
bool camera_feed_acquire_frame(CameraFeedFrame*, CameraFeedOutput) { return false; }
void camera_feed_release_frame(const CameraFeedFrame*) {}
void camera_feed_loop() {}

#endif