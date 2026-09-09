#include "local_image_loader.h"

#if HAS_IMAGE_LIBRARY

#include "image_binding.h"
#include "log_manager.h"
#include "ota_activity.h"
#include "rtos_task_utils.h"
#include "storage.h"

#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <string.h>

#define TAG "ImgLocal"

namespace {

constexpr size_t MAX_SOURCE_SIZE = 4 * 1024 * 1024;
constexpr uint32_t LOADER_TASK_STACK_BYTES = 12288;
constexpr UBaseType_t LOADER_TASK_PRIORITY = 1;
constexpr uint32_t IDLE_DELAY_MS = 100;

struct LocalImageSlot {
    bool active;
    bool pending;
    uint32_t generation;
    char path[IMAGE_LIBRARY_PATH_MAX_LEN];
    uint16_t target_w;
    uint16_t target_h;
    ImageScaleMode scale_mode;
    uint16_t letterbox_color;
    uint16_t* front_buf;
    uint16_t* back_buf;
    uint16_t* lvgl_buf;
    size_t buf_size;
    volatile bool new_frame;
    uint32_t frame_drops;
};

LocalImageSlot* g_slots = nullptr;
SemaphoreHandle_t g_mutex = nullptr;
TaskHandle_t g_task = nullptr;
RtosTaskPsramAlloc g_task_alloc;

bool is_valid_slot(local_image_slot_t slot) {
    return g_slots && slot >= 0 && slot < LOCAL_IMAGE_SLOT_MAX;
}

void free_slot_buffers(LocalImageSlot& slot) {
    if (slot.front_buf) heap_caps_free(slot.front_buf);
    if (slot.back_buf) heap_caps_free(slot.back_buf);
    if (slot.lvgl_buf) heap_caps_free(slot.lvgl_buf);
    slot.front_buf = nullptr;
    slot.back_buf = nullptr;
    slot.lvgl_buf = nullptr;
    slot.buf_size = 0;
}

bool read_source_file(const char* path, uint8_t** out_data, size_t* out_len) {
    *out_data = nullptr;
    *out_len = 0;
    if (ota_activity_is_active() || !path || !ImageLibraryCatalog::is_image_path(path)) return false;

    File source = Storage.open(path, "r");
    if (!source || source.isDirectory()) {
        if (source) source.close();
        LOGW(TAG, "Cannot open %.96s", path);
        return false;
    }

    const size_t length = source.size();
    if (length == 0 || length > MAX_SOURCE_SIZE) {
        LOGW(TAG, "Invalid source size %u for %.64s", (unsigned)length, path);
        source.close();
        return false;
    }

    uint8_t* data = static_cast<uint8_t*>(
        heap_caps_malloc(length, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!data) data = static_cast<uint8_t*>(malloc(length));
    if (!data) {
        source.close();
        LOGE(TAG, "OOM for %u-byte source", (unsigned)length);
        return false;
    }

    size_t received = 0;
    while (received < length && !ota_activity_is_active()) {
        const size_t remaining = length - received;
        const size_t chunk = remaining < 4096 ? remaining : 4096;
        const int count = source.read(data + received, chunk);
        if (count <= 0) break;
        received += static_cast<size_t>(count);
        taskYIELD();
    }
    source.close();

    if (received != length || ota_activity_is_active()) {
        heap_caps_free(data);
        return false;
    }
    *out_data = data;
    *out_len = length;
    return true;
}

void loader_task(void*) {
    LOGI(TAG, "Local image task started");
    for (;;) {
        local_image_slot_t selected = LOCAL_IMAGE_SLOT_INVALID;
        char path[IMAGE_LIBRARY_PATH_MAX_LEN] = {};
        uint16_t target_w = 0;
        uint16_t target_h = 0;
        uint32_t generation = 0;
        ImageScaleMode scale_mode = IMAGE_SCALE_COVER;
        uint16_t letterbox_color = 0;

        xSemaphoreTake(g_mutex, portMAX_DELAY);
        if (!ota_activity_is_active()) {
            for (local_image_slot_t index = 0; index < LOCAL_IMAGE_SLOT_MAX; ++index) {
                LocalImageSlot& slot = g_slots[index];
                if (!slot.active || !slot.pending) continue;
                slot.pending = false;
                selected = index;
                strlcpy(path, slot.path, sizeof(path));
                target_w = slot.target_w;
                target_h = slot.target_h;
                generation = slot.generation;
                scale_mode = slot.scale_mode;
                letterbox_color = slot.letterbox_color;
                break;
            }
        }
        xSemaphoreGive(g_mutex);

        if (selected == LOCAL_IMAGE_SLOT_INVALID) {
            vTaskDelay(pdMS_TO_TICKS(IDLE_DELAY_MS));
            continue;
        }

        uint8_t* source_data = nullptr;
        size_t source_len = 0;
        if (!read_source_file(path, &source_data, &source_len)) continue;

        if (ota_activity_is_active()) {
            heap_caps_free(source_data);
            continue;
        }

        uint16_t* pixels = nullptr;
        size_t pixel_size = 0;
        const bool decoded = image_decode_to_rgb565(
            source_data, source_len, target_w, target_h, scale_mode, &pixels, &pixel_size, letterbox_color);
        heap_caps_free(source_data);
        if (!decoded || !pixels || ota_activity_is_active()) {
            if (pixels) heap_caps_free(pixels);
            if (!ota_activity_is_active()) {
                LOGW(TAG, "Decode failed for %.96s", path);
                image_binding_set_error("show", path);
            }
            continue;
        }

        xSemaphoreTake(g_mutex, portMAX_DELAY);
        LocalImageSlot& slot = g_slots[selected];
        if (slot.active && slot.generation == generation) {
            if (slot.back_buf) heap_caps_free(slot.back_buf);
            slot.back_buf = pixels;
            slot.buf_size = pixel_size;
            uint16_t* old_front = slot.front_buf;
            slot.front_buf = slot.back_buf;
            slot.back_buf = old_front;
            if (slot.new_frame) ++slot.frame_drops;
            slot.new_frame = true;
        } else {
            heap_caps_free(pixels);
        }
        xSemaphoreGive(g_mutex);
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

} // namespace

static void local_image_loader_init() {
    if (g_task) return;
    g_slots = static_cast<LocalImageSlot*>(heap_caps_calloc(
        LOCAL_IMAGE_SLOT_MAX, sizeof(LocalImageSlot), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!g_slots) {
        LOGE(TAG, "OOM for slots");
        return;
    }
    g_mutex = xSemaphoreCreateMutex();
    if (!g_mutex) {
        heap_caps_free(g_slots);
        g_slots = nullptr;
        LOGE(TAG, "Failed to create mutex");
        return;
    }
    // Storage can resolve to LittleFS, so its task stack must remain internal
    // while the source and frame buffers use PSRAM.
    if (!rtos_create_task_internal_stack_pinned(
            loader_task, "img_local", LOADER_TASK_STACK_BYTES, nullptr,
            LOADER_TASK_PRIORITY, &g_task, &g_task_alloc, 0)) {
        vSemaphoreDelete(g_mutex);
        g_mutex = nullptr;
        heap_caps_free(g_slots);
        g_slots = nullptr;
        LOGE(TAG, "Failed to create local image task");
    }
}

local_image_slot_t local_image_loader_request(
    const char* path, uint16_t target_w, uint16_t target_h, ImageScaleMode scale_mode,
    uint16_t letterbox_color) {
    if (!path || !ImageLibraryCatalog::is_image_path(path) || !target_w || !target_h) {
        return LOCAL_IMAGE_SLOT_INVALID;
    }
    if (!g_mutex) local_image_loader_init();
    if (!g_mutex) return LOCAL_IMAGE_SLOT_INVALID;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    local_image_slot_t slot_id = LOCAL_IMAGE_SLOT_INVALID;
    for (local_image_slot_t index = 0; index < LOCAL_IMAGE_SLOT_MAX; ++index) {
        if (!g_slots[index].active) {
            slot_id = index;
            break;
        }
    }
    if (slot_id != LOCAL_IMAGE_SLOT_INVALID) {
        LocalImageSlot& slot = g_slots[slot_id];
        const uint32_t next_generation = slot.generation + 1;
        memset(&slot, 0, sizeof(slot));
        slot.active = true;
        slot.pending = true;
        slot.generation = next_generation;
        strlcpy(slot.path, path, sizeof(slot.path));
        slot.target_w = target_w;
        slot.target_h = target_h;
        slot.scale_mode = scale_mode;
        slot.letterbox_color = letterbox_color;
    }
    xSemaphoreGive(g_mutex);
    return slot_id;
}

bool local_image_loader_reload(local_image_slot_t slot) {
    if (!is_valid_slot(slot) || !g_mutex) return false;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    const bool active = g_slots[slot].active;
    if (active) g_slots[slot].pending = true;
    xSemaphoreGive(g_mutex);
    return active;
}

void local_image_loader_cancel(local_image_slot_t slot) {
    if (!is_valid_slot(slot) || !g_mutex) return;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    LocalImageSlot& target = g_slots[slot];
    if (target.active) {
        free_slot_buffers(target);
        target.active = false;
        target.pending = false;
        ++target.generation;
    }
    xSemaphoreGive(g_mutex);
}

void local_image_loader_cancel_all() {
    if (!g_slots || !g_mutex) return;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    for (local_image_slot_t slot = 0; slot < LOCAL_IMAGE_SLOT_MAX; ++slot) {
        free_slot_buffers(g_slots[slot]);
        g_slots[slot].active = false;
        g_slots[slot].pending = false;
        ++g_slots[slot].generation;
    }
    xSemaphoreGive(g_mutex);
}

bool local_image_loader_has_new_frame(local_image_slot_t slot) {
    if (!is_valid_slot(slot)) return false;
    return g_slots[slot].active && g_slots[slot].new_frame;
}

const uint16_t* local_image_loader_get_frame(
    local_image_slot_t slot, uint16_t* out_w, uint16_t* out_h) {
    if (!is_valid_slot(slot) || !g_mutex) return nullptr;
    xSemaphoreTake(g_mutex, portMAX_DELAY);
    LocalImageSlot& target = g_slots[slot];
    if (!target.active) {
        xSemaphoreGive(g_mutex);
        return nullptr;
    }
    if (target.new_frame && target.front_buf) {
        uint16_t* previous_lvgl = target.lvgl_buf;
        target.lvgl_buf = target.front_buf;
        target.front_buf = previous_lvgl;
        target.new_frame = false;
    }
    if (out_w) *out_w = target.target_w;
    if (out_h) *out_h = target.target_h;
    const uint16_t* result = target.lvgl_buf;
    xSemaphoreGive(g_mutex);
    return result;
}

uint32_t local_image_loader_get_drops(local_image_slot_t slot) {
    if (!is_valid_slot(slot)) return 0;
    return g_slots[slot].frame_drops;
}

#endif // HAS_IMAGE_LIBRARY