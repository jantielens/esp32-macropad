#include "native_extension.h"

#if HAS_NATIVE_EXTENSIONS

#include "log_manager.h"
#include "message_bubble.h"
#include "pad_config.h"
#include "storage.h"
#if HAS_MQTT
#include "binding_template.h"
#include "pad_binding.h"
#endif

#include <esp_heap_caps.h>
#include <esp_partition.h>
#include <HTTPClient.h>
#include <lvgl.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <math.h>
#include <string.h>

#define TAG "Extensions"

namespace {

// New magics keep older, shorter records from being misread as valid
// signature-bearing headers.
constexpr uint32_t SLOT_MAGIC = 0x34545845u;
constexpr uint32_t STAGE_MAGIC = 0x33544753u;
constexpr uint32_t ELF_OFFSET = 0x2000;
constexpr uint32_t SLOT_OFFSET[NATIVE_EXTENSION_SLOT_COUNT] = {0x00000, 0x10000, 0x20000};
constexpr uint32_t SLOT_SIZE[NATIVE_EXTENSION_SLOT_COUNT] = {0x10000, 0x10000, 0x20000};
constexpr uint32_t SLOT_CAPACITY[NATIVE_EXTENSION_SLOT_COUNT] = {0xE000, 0xE000, 0x1E000};
constexpr uint16_t ELF_TYPE_DYN = 3;
constexpr uint16_t ELF_MACHINE_RISCV = 243;
constexpr uint32_t ELF_PT_LOAD = 1;
constexpr uint32_t ELF_PF_X = 1;
constexpr uint32_t ELF_SHT_DYNSYM = 11;
constexpr uint32_t ELF_SHT_RELA = 4;
constexpr uint32_t WORKER_JOIN_TIMEOUT_MS = 20000;
constexpr uint8_t MAX_EXTENSION_INSTANCE_BINDINGS = MAX_PAD_BUTTONS;

struct ElfHeader { uint8_t ident[16]; uint16_t type, machine; uint32_t version, entry, phoff, shoff, flags; uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx; };
struct ElfProgram { uint32_t type, offset, vaddr, paddr, filesz, memsz, flags, align; };
struct ElfSection { uint32_t name, type, flags, addr, offset, size, link, info, align, entsize; };
struct ElfSymbol { uint32_t name, value, size; uint8_t info, other; uint16_t shndx; };
struct StageHeader {
    uint32_t magic;
    NativeExtensionSlotHeader slot;
};
static_assert(sizeof(ElfHeader) == 52 && sizeof(ElfProgram) == 32 && sizeof(ElfSection) == 40 && sizeof(ElfSymbol) == 16, "Unexpected ELF layout");

struct LoadedSlot {
    void* mapping;
    esp_partition_mmap_handle_t mapping_handle;
    void* extension_data;
    uint8_t active_instances;
    TaskHandle_t worker_task;
    NativeExtensionTaskFn worker_entry;
    void* worker_context;
    volatile bool worker_cancel_requested;
    volatile bool worker_completed;
    bool worker_join_pending;
    bool worker_join_failed;
    uint32_t worker_cancelled_at_ms;
    struct InstanceBindingContext {
        uint32_t instance_id;
        const PadBinding* bindings;
        uint8_t binding_count;
    } instance_bindings[MAX_EXTENSION_INSTANCE_BINDINGS];
    NativeExtensionSlotInfo info;
    NativeExtensionCreateFn create;
    NativeExtensionDestroyFn destroy;
    NativeExtensionShutdownFn shutdown;
    NativeExtensionEventFn tap;
    NativeExtensionEventFn long_press;
    NativeExtensionTickFn tick;
};
LoadedSlot s_slots[NATIVE_EXTENSION_SLOT_COUNT] = {};
portMUX_TYPE s_worker_lock = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t s_lvgl_task = nullptr;

bool valid_slot(uint8_t slot) { return slot < NATIVE_EXTENSION_SLOT_COUNT; }
bool range_valid(size_t offset, size_t size, size_t total) { return offset <= total && size <= total - offset; }
void stage_path(uint8_t slot, char* path, size_t len) { snprintf(path, len, "/extensions/slot%u.stage", slot); }
void delete_path(uint8_t slot, char* path, size_t len) { snprintf(path, len, "/extensions/slot%u.delete", slot); }
void enabled_path(uint8_t slot, char* path, size_t len) { snprintf(path, len, "/extensions/slot%u.enabled", slot); }

bool delete_pending(uint8_t slot) {
    char path[32];
    delete_path(slot, path, sizeof(path));
    return Storage.exists(path);
}

bool set_delete_pending(uint8_t slot) {
    char path[32];
    delete_path(slot, path, sizeof(path));
    File marker = Storage.open(path, "w");
    if (!marker) return false;
    const bool written = marker.write(static_cast<uint8_t>(1)) == 1;
    marker.close();
    return written;
}

bool pending_enabled(uint8_t slot, bool* enabled) {
    char path[32];
    enabled_path(slot, path, sizeof(path));
    File marker = Storage.open(path, "r");
    char value = 0;
    const bool valid = marker && marker.read(reinterpret_cast<uint8_t*>(&value), 1) == 1 &&
                       (value == '0' || value == '1');
    if (marker) marker.close();
    if (valid && enabled) *enabled = value == '1';
    return valid;
}

bool set_pending_enabled(uint8_t slot, bool enabled) {
    char path[32];
    enabled_path(slot, path, sizeof(path));
    File marker = Storage.open(path, "w");
    if (!marker) return false;
    const char value = enabled ? '1' : '0';
    const bool written = marker.write(reinterpret_cast<const uint8_t*>(&value), 1) == 1;
    marker.close();
    return written;
}

lv_obj_t* as_obj(void* obj) { return static_cast<lv_obj_t*>(obj); }
lv_color_t host_color(uint32_t rgb) { return lv_color_hex(rgb & 0xFFFFFF); }

void host_set_text(char* out, size_t len, const char* text) { if (out && len) strlcpy(out, text ? text : "", len); }
uint32_t host_millis() { return millis(); }
void host_delay_ms(uint32_t delay_ms) { vTaskDelay(pdMS_TO_TICKS(delay_ms)); }
void host_log(NativeExtensionLogLevel level, const char* message) {
    if (level == NATIVE_EXTENSION_LOG_ERROR) LOGE(TAG, "Extension: %s", message ? message : "");
    else if (level == NATIVE_EXTENSION_LOG_WARN) LOGW(TAG, "Extension: %s", message ? message : "");
    else LOGI(TAG, "Extension: %s", message ? message : "");
}
void* host_alloc(size_t size) {
    if (size == 0) return nullptr;
    void* memory = heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    return memory ? memory : heap_caps_malloc(size, MALLOC_CAP_DEFAULT);
}
void host_free(void* memory) { if (memory) heap_caps_free(memory); }
void* host_context_get_data(void* extension_context) {
    return extension_context ? static_cast<LoadedSlot*>(extension_context)->extension_data : nullptr;
}
void host_context_set_data(void* extension_context, void* data) {
    if (extension_context) static_cast<LoadedSlot*>(extension_context)->extension_data = data;
}
void host_status_set(void* extension_context, NativeExtensionRuntimeState state, const char* detail) {
    if (!extension_context) return;
    LoadedSlot* slot = static_cast<LoadedSlot*>(extension_context);
    slot->info.runtime_state = state;
    strlcpy(slot->info.runtime_detail, detail ? detail : "", sizeof(slot->info.runtime_detail));
}

bool host_binding_resolve(void* extension_context, uint32_t instance_id,
                          const char* template_text, char* out, size_t out_size) {
    if (out && out_size) out[0] = '\0';
    LoadedSlot* slot = static_cast<LoadedSlot*>(extension_context);
    if (!slot || !template_text || !out || out_size == 0 ||
        !s_lvgl_task || xTaskGetCurrentTaskHandle() != s_lvgl_task) return false;
#if HAS_MQTT
    const PadBinding* bindings = nullptr;
    uint8_t binding_count = 0;
    for (const auto& context : slot->instance_bindings) {
        if (context.instance_id == instance_id && context.bindings) {
            bindings = context.bindings;
            binding_count = context.binding_count;
            break;
        }
    }
    const PadBinding* previous_bindings = nullptr;
    uint8_t previous_count = 0;
    pad_binding_get_bindings(&previous_bindings, &previous_count);
    pad_binding_set_bindings(bindings, binding_count);
    binding_template_resolve(template_text, out, out_size);
    pad_binding_set_bindings(previous_bindings, previous_count);
    return true;
#else
    return false;
#endif
}
float host_math_sin(float radians) { return sinf(radians); }
float host_math_cos(float radians) { return cosf(radians); }
float host_math_sqrt(float value) { return sqrtf(value); }
float host_math_atan2(float y, float x) { return atan2f(y, x); }
void* host_mutex_create() { return xSemaphoreCreateMutex(); }
bool host_mutex_lock(void* mutex, uint32_t timeout_ms) {
    return mutex && xSemaphoreTake(static_cast<SemaphoreHandle_t>(mutex), pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}
void host_mutex_unlock(void* mutex) { if (mutex) xSemaphoreGive(static_cast<SemaphoreHandle_t>(mutex)); }
void host_mutex_destroy(void* mutex) { if (mutex) vSemaphoreDelete(static_cast<SemaphoreHandle_t>(mutex)); }
void* host_label_create(void* parent) { return parent ? lv_label_create(as_obj(parent)) : nullptr; }
void host_label_set_text(void* label, const char* text) { if (label) lv_label_set_text(as_obj(label), text ? text : ""); }
void host_obj_center(void* obj) { if (obj) lv_obj_center(as_obj(obj)); }
void host_log_info(const char* message) { host_log(NATIVE_EXTENSION_LOG_INFO, message); }
void host_notify(const char* message) {
    if (!message || !message[0]) return;
    MessageBubbleParams params = {"", 1500, 0xFFFFFF, 0x00695C, 0, false, 100, 0, NOTIFY_LOC_CENTER};
    strlcpy(params.text, message, sizeof(params.text));
    message_bubble_show(&params);
}

void extension_worker_entry(void* raw_slot) {
    LoadedSlot* slot = static_cast<LoadedSlot*>(raw_slot);
    NativeExtensionTaskFn entry = nullptr;
    void* context = nullptr;
    portENTER_CRITICAL(&s_worker_lock);
    entry = slot->worker_entry;
    context = slot->worker_context;
    portEXIT_CRITICAL(&s_worker_lock);
    if (entry) entry(context);
    portENTER_CRITICAL(&s_worker_lock);
    slot->worker_completed = true;
    portEXIT_CRITICAL(&s_worker_lock);
    vTaskDelete(nullptr);
}

bool host_task_create(void* extension_context, NativeExtensionTaskFn entry, const char* name, uint32_t stack_bytes,
                      void* context, uint32_t priority, void** out_handle) {
    LoadedSlot* slot = static_cast<LoadedSlot*>(extension_context);
    if (!slot || !entry || !out_handle || stack_bytes == 0) return false;
    portENTER_CRITICAL(&s_worker_lock);
    const bool unavailable = slot->worker_task || slot->worker_join_pending || slot->worker_join_failed;
    if (!unavailable) {
        slot->worker_entry = entry;
        slot->worker_context = context;
        slot->worker_cancel_requested = false;
        slot->worker_completed = false;
    }
    portEXIT_CRITICAL(&s_worker_lock);
    if (unavailable) return false;
    TaskHandle_t handle = nullptr;
    const BaseType_t created = xTaskCreate(extension_worker_entry, name ? name : "Extension",
                                           stack_bytes, slot, priority, &handle);
    if (created != pdPASS) {
        portENTER_CRITICAL(&s_worker_lock);
        slot->worker_entry = nullptr;
        slot->worker_context = nullptr;
        portEXIT_CRITICAL(&s_worker_lock);
    } else {
        portENTER_CRITICAL(&s_worker_lock);
        slot->worker_task = handle;
        portEXIT_CRITICAL(&s_worker_lock);
    }
    *out_handle = handle;
    return created == pdPASS;
}

bool host_task_cancel_requested(void* extension_context) {
    LoadedSlot* slot = static_cast<LoadedSlot*>(extension_context);
    return slot && slot->worker_cancel_requested;
}

bool host_task_wait_or_cancel(void* extension_context, uint32_t timeout_ms) {
    LoadedSlot* slot = static_cast<LoadedSlot*>(extension_context);
    if (!slot || slot->worker_cancel_requested) return false;
    ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(timeout_ms));
    return !slot->worker_cancel_requested;
}

bool host_http_get(const char* url, uint8_t* response, size_t capacity,
                   uint32_t timeout_ms, NativeExtensionHttpResult* result) {
    if (result) *result = {0, 0, 0};
    if (!url || !response || capacity == 0 || timeout_ms == 0) return false;

    const bool secure_request = strncmp(url, "https://", 8) == 0;
    WiFiClient plain;
    WiFiClientSecure secure;
    HTTPClient http;
    if (secure_request) {
        secure.setInsecure();
        secure.setTimeout(timeout_ms);
    } else {
        plain.setTimeout(timeout_ms);
    }
    if (!(secure_request ? http.begin(secure, url) : http.begin(plain, url))) return false;

    http.setTimeout(timeout_ms);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    const int status = http.GET();
    if (result) result->status_code = status;
    if (status <= 0) {
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    const int content_length = http.getSize();
    size_t copied = 0;
    uint32_t last_data_at = millis();
    while (stream && (content_length < 0 || copied < static_cast<size_t>(content_length))) {
        const size_t available = stream->available();
        if (available > 0) {
            const size_t remaining = capacity - copied;
            if (remaining == 0) {
                if (result) result->truncated = 1;
                http.end();
                return false;
            }
            const size_t count = available < remaining ? available : remaining;
            const size_t read = stream->readBytes(response + copied, count);
            if (read == 0) break;
            copied += read;
            last_data_at = millis();
        } else if (!http.connected() || millis() - last_data_at >= timeout_ms) {
            break;
        } else {
            vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    const bool complete = content_length < 0 || copied == static_cast<size_t>(content_length);
    if (result) result->body_length = copied;
    http.end();
    return complete;
}

void* host_obj_create(void* parent) { return parent ? lv_obj_create(as_obj(parent)) : nullptr; }
void host_obj_delete(void* obj) { if (obj) lv_obj_delete(as_obj(obj)); }
void host_obj_set_size(void* obj, int32_t width, int32_t height) { if (obj) lv_obj_set_size(as_obj(obj), width, height); }
void host_obj_set_pos(void* obj, int32_t x, int32_t y) { if (obj) lv_obj_set_pos(as_obj(obj), x, y); }
int32_t host_obj_get_width(void* obj) { return obj ? lv_obj_get_width(as_obj(obj)) : 0; }
int32_t host_obj_get_height(void* obj) { return obj ? lv_obj_get_height(as_obj(obj)) : 0; }
void host_obj_align(void* obj, NativeExtensionAlign align, int32_t x, int32_t y) { if (obj) lv_obj_align(as_obj(obj), static_cast<lv_align_t>(align), x, y); }
void host_obj_set_hidden(void* obj, bool hidden) { if (obj) { if (hidden) lv_obj_add_flag(as_obj(obj), LV_OBJ_FLAG_HIDDEN); else lv_obj_remove_flag(as_obj(obj), LV_OBJ_FLAG_HIDDEN); } }
void host_obj_set_clickable(void* obj, bool clickable) { if (obj) { if (clickable) lv_obj_add_flag(as_obj(obj), LV_OBJ_FLAG_CLICKABLE); else lv_obj_remove_flag(as_obj(obj), LV_OBJ_FLAG_CLICKABLE); } }
void host_obj_invalidate(void* obj) { if (obj) lv_obj_invalidate(as_obj(obj)); }
void host_obj_set_bg_color(void* obj, uint32_t rgb, uint8_t opacity) { if (obj) { lv_obj_set_style_bg_color(as_obj(obj), host_color(rgb), 0); lv_obj_set_style_bg_opa(as_obj(obj), opacity, 0); } }
void host_obj_set_border(void* obj, uint32_t rgb, int32_t width, int32_t radius) { if (obj) { lv_obj_set_style_border_color(as_obj(obj), host_color(rgb), 0); lv_obj_set_style_border_width(as_obj(obj), width, 0); lv_obj_set_style_radius(as_obj(obj), radius, 0); } }
void host_obj_set_text_color(void* obj, uint32_t rgb) { if (obj) lv_obj_set_style_text_color(as_obj(obj), host_color(rgb), 0); }
void host_obj_set_padding(void* obj, int32_t all) { if (obj) lv_obj_set_style_pad_all(as_obj(obj), all, 0); }
lv_event_code_t host_event_code(NativeExtensionLvglEventCode code) {
    switch (code) {
        case NATIVE_EXTENSION_EVENT_PRESSED: return LV_EVENT_PRESSED;
        case NATIVE_EXTENSION_EVENT_PRESSING: return LV_EVENT_PRESSING;
        case NATIVE_EXTENSION_EVENT_RELEASED: return LV_EVENT_RELEASED;
        case NATIVE_EXTENSION_EVENT_CLICKED: return LV_EVENT_CLICKED;
        case NATIVE_EXTENSION_EVENT_LONG_PRESSED: return LV_EVENT_LONG_PRESSED;
        case NATIVE_EXTENSION_EVENT_VALUE_CHANGED: return LV_EVENT_VALUE_CHANGED;
        case NATIVE_EXTENSION_EVENT_DELETE: return LV_EVENT_DELETE;
    }
    return LV_EVENT_ALL;
}
void host_obj_add_event_cb(void* obj, NativeExtensionLvglEventFn callback, NativeExtensionLvglEventCode code, void* user_data) { if (obj && callback) lv_obj_add_event_cb(as_obj(obj), reinterpret_cast<lv_event_cb_t>(callback), host_event_code(code), user_data); }
NativeExtensionLvglEventCode host_event_get_code(void* event) {
    if (!event) return NATIVE_EXTENSION_EVENT_DELETE;
    switch (lv_event_get_code(static_cast<lv_event_t*>(event))) {
        case LV_EVENT_PRESSED: return NATIVE_EXTENSION_EVENT_PRESSED;
        case LV_EVENT_PRESSING: return NATIVE_EXTENSION_EVENT_PRESSING;
        case LV_EVENT_RELEASED: return NATIVE_EXTENSION_EVENT_RELEASED;
        case LV_EVENT_CLICKED: return NATIVE_EXTENSION_EVENT_CLICKED;
        case LV_EVENT_LONG_PRESSED: return NATIVE_EXTENSION_EVENT_LONG_PRESSED;
        case LV_EVENT_VALUE_CHANGED: return NATIVE_EXTENSION_EVENT_VALUE_CHANGED;
        case LV_EVENT_DELETE: return NATIVE_EXTENSION_EVENT_DELETE;
        default: return NATIVE_EXTENSION_EVENT_DELETE;
    }
}
void* host_event_get_target(void* event) { return event ? lv_event_get_target(static_cast<lv_event_t*>(event)) : nullptr; }
void* host_event_get_user_data(void* event) { return event ? lv_event_get_user_data(static_cast<lv_event_t*>(event)) : nullptr; }

void* host_line_create(void* parent) { return parent ? lv_line_create(as_obj(parent)) : nullptr; }
void host_line_set(void* line, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t rgb, uint8_t width) {
    if (!line || width == 0) return;
    lv_point_precise_t* points = static_cast<lv_point_precise_t*>(lv_obj_get_user_data(as_obj(line)));
    if (!points) {
        points = static_cast<lv_point_precise_t*>(lv_malloc(sizeof(lv_point_precise_t) * 2));
        if (!points) return;
        lv_obj_set_user_data(as_obj(line), points);
        lv_obj_add_event_cb(as_obj(line), [](lv_event_t* event) { lv_free(lv_event_get_user_data(event)); }, LV_EVENT_DELETE, points);
    }
    const int32_t min_x = x1 < x2 ? x1 : x2;
    const int32_t min_y = y1 < y2 ? y1 : y2;
    points[0] = {static_cast<lv_value_precise_t>(x1 - min_x), static_cast<lv_value_precise_t>(y1 - min_y)};
    points[1] = {static_cast<lv_value_precise_t>(x2 - min_x), static_cast<lv_value_precise_t>(y2 - min_y)};
    lv_line_set_points(as_obj(line), points, 2);
    lv_obj_set_pos(as_obj(line), min_x, min_y);
    lv_obj_set_style_line_color(as_obj(line), host_color(rgb), 0);
    lv_obj_set_style_line_width(as_obj(line), width, 0);
}
void* host_arc_create(void* parent) { return parent ? lv_arc_create(as_obj(parent)) : nullptr; }
void host_arc_set_value(void* arc, int32_t value) { if (arc) lv_arc_set_value(as_obj(arc), value); }
void* host_spinner_create(void* parent, uint32_t duration_ms, uint32_t arc_degrees) {
    lv_obj_t* spinner = parent ? lv_spinner_create(as_obj(parent)) : nullptr;
    if (spinner) lv_spinner_set_anim_params(spinner, duration_ms, arc_degrees);
    return spinner;
}
void* host_table_create(void* parent) { return parent ? lv_table_create(as_obj(parent)) : nullptr; }
void host_table_set_size(void* table, uint16_t rows, uint16_t columns) { if (table) { lv_table_set_row_count(as_obj(table), rows); lv_table_set_column_count(as_obj(table), columns); } }
void host_table_set_cell_text(void* table, uint16_t row, uint16_t column, const char* text) { if (table) lv_table_set_cell_value(as_obj(table), row, column, text ? text : ""); }

void* host_canvas_create(void* parent) { return parent ? lv_canvas_create(as_obj(parent)) : nullptr; }
size_t host_canvas_buffer_size(uint32_t width, uint32_t height) { return LV_CANVAS_BUF_SIZE(width, height, 16, 1); }
void host_canvas_set_buffer(void* canvas, void* buffer, uint32_t width, uint32_t height) { if (canvas && buffer) lv_canvas_set_buffer(as_obj(canvas), buffer, width, height, LV_COLOR_FORMAT_RGB565); }
void host_canvas_clear(void* canvas, uint32_t rgb) { if (canvas) lv_canvas_fill_bg(as_obj(canvas), host_color(rgb), LV_OPA_COVER); }
void host_canvas_set_pixel(void* canvas, int32_t x, int32_t y, uint32_t rgb) { if (canvas && x >= 0 && y >= 0) lv_canvas_set_px(as_obj(canvas), x, y, host_color(rgb), LV_OPA_COVER); }
void host_canvas_draw_line(void* canvas, int32_t x1, int32_t y1, int32_t x2, int32_t y2, uint32_t rgb, uint8_t width) {
    if (!canvas || width == 0) return;
    const int32_t delta_x = abs(x2 - x1), step_x = x1 < x2 ? 1 : -1;
    const int32_t delta_y = -abs(y2 - y1), step_y = y1 < y2 ? 1 : -1;
    int32_t error = delta_x + delta_y;
    while (true) {
        const int32_t half = width / 2;
        for (int32_t oy = -half; oy <= half; ++oy) for (int32_t ox = -half; ox <= half; ++ox) host_canvas_set_pixel(canvas, x1 + ox, y1 + oy, rgb);
        if (x1 == x2 && y1 == y2) break;
        const int32_t double_error = error * 2;
        if (double_error >= delta_y) { error += delta_y; x1 += step_x; }
        if (double_error <= delta_x) { error += delta_x; y1 += step_y; }
    }
}
void host_canvas_draw_circle(void* canvas, int32_t x, int32_t y, int32_t radius, uint32_t rgb, uint8_t width) {
    if (!canvas || radius < 0 || width == 0) return;
    int32_t dx = radius, dy = 0, error = 1 - radius;
    while (dx >= dy) {
        for (int32_t stroke = 0; stroke < width; ++stroke) {
            host_canvas_set_pixel(canvas, x + dx - stroke, y + dy, rgb); host_canvas_set_pixel(canvas, x + dy, y + dx - stroke, rgb);
            host_canvas_set_pixel(canvas, x - dy, y + dx - stroke, rgb); host_canvas_set_pixel(canvas, x - dx + stroke, y + dy, rgb);
            host_canvas_set_pixel(canvas, x - dx + stroke, y - dy, rgb); host_canvas_set_pixel(canvas, x - dy, y - dx + stroke, rgb);
            host_canvas_set_pixel(canvas, x + dy, y - dx + stroke, rgb); host_canvas_set_pixel(canvas, x + dx - stroke, y - dy, rgb);
        }
        ++dy;
        if (error < 0) error += 2 * dy + 1;
        else { --dx; error += 2 * (dy - dx + 1); }
    }
}

const NativeExtensionCoreApi CORE_API = {
    host_millis, host_delay_ms, host_log, host_alloc, host_free,
    host_context_get_data, host_context_set_data,
    host_math_sin, host_math_cos, host_math_sqrt, host_math_atan2,
    host_mutex_create, host_mutex_lock, host_mutex_unlock, host_mutex_destroy,
    host_notify, host_status_set,
};
const NativeExtensionTaskApi TASK_API = {host_task_create, host_task_cancel_requested, host_task_wait_or_cancel};
const NativeExtensionHttpApi HTTP_API = {host_http_get};
const NativeExtensionUiApi UI_API = {
    host_label_create, host_label_set_text, host_obj_center,
    host_obj_create, host_obj_delete, host_obj_set_size, host_obj_set_pos,
    host_obj_get_width, host_obj_get_height, host_obj_align, host_obj_set_hidden,
    host_obj_set_clickable, host_obj_invalidate, host_obj_set_bg_color,
    host_obj_set_border, host_obj_set_text_color, host_obj_set_padding,
};
const NativeExtensionCanvasApi CANVAS_API = {
    host_canvas_create, host_canvas_buffer_size, host_canvas_set_buffer,
    host_canvas_clear, host_canvas_set_pixel, host_canvas_draw_line,
    host_canvas_draw_circle,
};
const NativeExtensionBindingApi BINDING_API = {host_binding_resolve};

const NativeExtensionHostApi HOST_API = {
    NATIVE_EXTENSION_ABI_VERSION,
    host_set_text, host_millis, host_delay_ms, host_log, host_alloc, host_free,
    host_context_get_data, host_context_set_data,
    host_math_sin, host_math_cos, host_math_sqrt, host_math_atan2,
    host_mutex_create, host_mutex_lock, host_mutex_unlock,
    host_label_create, host_label_set_text, host_obj_center, host_log_info, host_notify,
    host_task_create, host_http_get,
    host_obj_create, host_obj_delete, host_obj_set_size, host_obj_set_pos, host_obj_get_width,
    host_obj_get_height, host_obj_align,
    host_obj_set_hidden, host_obj_set_clickable, host_obj_invalidate, host_obj_set_bg_color,
    host_obj_set_border, host_obj_set_text_color, host_obj_set_padding, host_obj_add_event_cb,
    host_event_get_code, host_event_get_target, host_event_get_user_data,
    host_line_create, host_line_set, host_arc_create, host_arc_set_value,
    host_spinner_create, host_table_create, host_table_set_size,
    host_table_set_cell_text, host_canvas_create, host_canvas_buffer_size, host_canvas_set_buffer, host_canvas_clear,
    host_canvas_set_pixel, host_canvas_draw_line, host_canvas_draw_circle,
    &CORE_API, &TASK_API, &HTTP_API, &UI_API, &CANVAS_API, &BINDING_API,
};

const esp_partition_t* extension_partition() {
    return esp_partition_find_first(ESP_PARTITION_TYPE_DATA, static_cast<esp_partition_subtype_t>(NATIVE_EXTENSION_PARTITION_SUBTYPE), NATIVE_EXTENSION_PARTITION_LABEL);
}

bool read_header(const esp_partition_t* partition, uint8_t slot, NativeExtensionSlotHeader* header) {
    if (!partition || !valid_slot(slot) || !header || esp_partition_read(partition, SLOT_OFFSET[slot], header, sizeof(*header)) != ESP_OK) return false;
    return header->magic == SLOT_MAGIC && header->abi_version == NATIVE_EXTENSION_ABI_VERSION && header->elf_size > 0 && header->elf_size <= SLOT_CAPACITY[slot] && header->enabled <= 1 && header->id[0];
}

bool read_raw_header(const esp_partition_t* partition, uint8_t slot, NativeExtensionSlotHeader* header) {
    return partition && valid_slot(slot) && header &&
           esp_partition_read(partition, SLOT_OFFSET[slot], header, sizeof(*header)) == ESP_OK &&
           header->magic == SLOT_MAGIC && header->elf_size > 0 &&
           header->elf_size <= SLOT_CAPACITY[slot] && header->enabled <= 1 && header->id[0];
}

bool read_stage(uint8_t slot, StageHeader* stage) {
    char path[32]; stage_path(slot, path, sizeof(path));
    File file = Storage.open(path, "r");
    const bool valid = file && file.read(reinterpret_cast<uint8_t*>(stage), sizeof(*stage)) == sizeof(*stage) &&
                       stage->magic == STAGE_MAGIC && stage->slot.elf_size > 0 &&
                       stage->slot.elf_size <= SLOT_CAPACITY[slot];
    if (file) file.close();
    return valid;
}

bool parse_filename(const char* filename, NativeExtensionSlotHeader* header) {
    const char* at = filename ? strrchr(filename, '@') : nullptr;
    const char* dot = filename ? strrchr(filename, '.') : nullptr;
    if (!at || !dot || at == filename || at >= dot || strcmp(dot, ".ext") != 0 ||
        static_cast<size_t>(at - filename) >= sizeof(header->id) ||
        static_cast<size_t>(dot - at - 1) >= sizeof(header->version)) return false;
    memset(header, 0, sizeof(*header));
    memcpy(header->id, filename, at - filename);
    memcpy(header->version, at + 1, dot - at - 1);
    for (size_t index = 0; header->id[index]; ++index) {
        const char c = header->id[index];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    }
    header->abi_version = NATIVE_EXTENSION_ABI_VERSION;
    header->enabled = 1;
    return header->version[0];
}

bool symbol_is_executable(const ElfProgram* programs, uint16_t count, uint32_t value) {
    for (uint16_t index = 0; index < count; ++index) {
        const ElfProgram& program = programs[index];
        if (program.type != ELF_PT_LOAD || (program.flags & ELF_PF_X) == 0 ||
            program.vaddr > UINT32_MAX - program.filesz) continue;
        if (value >= program.vaddr && value < program.vaddr + program.filesz) return true;
    }
    return false;
}

bool symbol_name_matches(const char* name, size_t available, const char* target) {
    const size_t target_len = strlen(target);
    return target_len < available && name[target_len] == '\0' &&
           memcmp(name, target, target_len) == 0;
}

bool valid_elf(const uint8_t* elf_data, size_t elf_size);

bool find_symbol(const uint8_t* file, size_t len, const ElfHeader* elf,
                 const ElfProgram* programs, const ElfSection* sections,
                 uintptr_t bias, const char* target, uintptr_t* result) {
    for (uint16_t section_index = 0; section_index < elf->shnum; ++section_index) {
        const ElfSection& section = sections[section_index];
        if (section.type != ELF_SHT_DYNSYM || section.entsize != sizeof(ElfSymbol) || section.link >= elf->shnum || !range_valid(section.offset, section.size, len)) continue;
        const ElfSection& strings = sections[section.link];
        if (!range_valid(strings.offset, strings.size, len)) continue;
        const ElfSymbol* symbols = reinterpret_cast<const ElfSymbol*>(file + section.offset);
        const char* names = reinterpret_cast<const char*>(file + strings.offset);
        for (size_t index = 0; index < section.size / sizeof(ElfSymbol); ++index) {
            if (!symbols[index].shndx || symbols[index].name >= strings.size ||
                !symbol_is_executable(programs, elf->phnum, symbols[index].value)) continue;
            const size_t available = strings.size - symbols[index].name;
            if (symbol_name_matches(names + symbols[index].name, available, target)) {
                *result = bias + symbols[index].value;
                return true;
            }
        }
    }
    return false;
}

bool vaddr_to_file_offset(const ElfProgram* programs, uint16_t count, uint32_t vaddr,
                          size_t size, size_t total, size_t* out_offset) {
    for (uint16_t index = 0; index < count; ++index) {
        const ElfProgram& program = programs[index];
        if (program.type != ELF_PT_LOAD || vaddr < program.vaddr ||
            vaddr > UINT32_MAX - size || vaddr + size > program.vaddr + program.filesz) continue;
        const size_t offset = program.offset + (vaddr - program.vaddr);
        if (!range_valid(offset, size, total)) continue;
        *out_offset = offset;
        return true;
    }
    return false;
}

bool read_descriptor(const uint8_t* file, size_t len, NativeExtensionDescriptor* out) {
    if (!file || !out || !valid_elf(file, len)) return false;
    const ElfHeader* elf = reinterpret_cast<const ElfHeader*>(file);
    const ElfProgram* programs = reinterpret_cast<const ElfProgram*>(file + elf->phoff);
    const ElfSection* sections = reinterpret_cast<const ElfSection*>(file + elf->shoff);
    for (uint16_t section_index = 0; section_index < elf->shnum; ++section_index) {
        const ElfSection& section = sections[section_index];
        if (section.type != ELF_SHT_DYNSYM || section.entsize != sizeof(ElfSymbol) || section.link >= elf->shnum || !range_valid(section.offset, section.size, len)) continue;
        const ElfSection& strings = sections[section.link];
        if (!range_valid(strings.offset, strings.size, len)) continue;
        const ElfSymbol* symbols = reinterpret_cast<const ElfSymbol*>(file + section.offset);
        const char* names = reinterpret_cast<const char*>(file + strings.offset);
        for (size_t index = 0; index < section.size / sizeof(ElfSymbol); ++index) {
            if (!symbols[index].shndx || symbols[index].name >= strings.size) continue;
            const size_t available = strings.size - symbols[index].name;
            if (!symbol_name_matches(names + symbols[index].name, available, "native_extension_descriptor")) continue;
            size_t offset = 0;
            if (!vaddr_to_file_offset(programs, elf->phnum, symbols[index].value, sizeof(*out), len, &offset)) return false;
            memcpy(out, file + offset, sizeof(*out));
            return out->magic == NATIVE_EXTENSION_DESCRIPTOR_MAGIC &&
                   out->abi_version == NATIVE_EXTENSION_ABI_VERSION &&
                   strcmp(out->target_abi, NATIVE_EXTENSION_TARGET_ABI) == 0 &&
                   out->id[0] && out->version[0] && out->title[0];
        }
    }
    return false;
}

bool valid_elf(const uint8_t* elf_data, size_t elf_size) {
    if (!elf_data || elf_size < sizeof(ElfHeader)) return false;
    const ElfHeader* elf = reinterpret_cast<const ElfHeader*>(elf_data);
    if (memcmp(elf->ident, "\x7f" "ELF", 4) || elf->type != ELF_TYPE_DYN ||
        elf->machine != ELF_MACHINE_RISCV || elf->phentsize != sizeof(ElfProgram) ||
        elf->shentsize != sizeof(ElfSection) ||
        !range_valid(elf->phoff, static_cast<size_t>(elf->phnum) * elf->phentsize, elf_size) ||
        !range_valid(elf->shoff, static_cast<size_t>(elf->shnum) * elf->shentsize, elf_size)) return false;
    const ElfSection* sections = reinterpret_cast<const ElfSection*>(elf_data + elf->shoff);
    const ElfProgram* programs = reinterpret_cast<const ElfProgram*>(elf_data + elf->phoff);
    for (uint16_t index = 0; index < elf->phnum; ++index) {
        if (programs[index].type == ELF_PT_LOAD &&
            (!range_valid(programs[index].offset, programs[index].filesz, elf_size) ||
             programs[index].vaddr > UINT32_MAX - programs[index].filesz)) return false;
    }
    for (uint16_t index = 0; index < elf->shnum; ++index) {
        if (sections[index].type == ELF_SHT_RELA && sections[index].size) return false;
    }
    return true;
}

bool load_slot(const esp_partition_t* partition, uint8_t slot, const NativeExtensionSlotHeader& header) {
    uint8_t* metadata = static_cast<uint8_t*>(heap_caps_malloc(header.elf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!metadata) metadata = static_cast<uint8_t*>(malloc(header.elf_size));
    if (!metadata || esp_partition_read(partition, SLOT_OFFSET[slot] + ELF_OFFSET, metadata, header.elf_size) != ESP_OK) { if (metadata) heap_caps_free(metadata); return false; }
    const ElfHeader* elf = reinterpret_cast<const ElfHeader*>(metadata);
    if (!valid_elf(metadata, header.elf_size) ||
        !native_extension_verify_signature(metadata, header.elf_size, header.signature)) {
        heap_caps_free(metadata);
        LOGW(TAG, "Skipped slot %u: invalid or unsigned package", slot);
        return false;
    }
    NativeExtensionDescriptor descriptor = {};
    if (!read_descriptor(metadata, header.elf_size, &descriptor) ||
        strcmp(descriptor.id, header.id) != 0 || strcmp(descriptor.version, header.version) != 0 ||
        strcmp(descriptor.target_abi, header.target_abi) != 0) { heap_caps_free(metadata); return false; }
    const ElfProgram* programs = reinterpret_cast<const ElfProgram*>(metadata + elf->phoff);
    const ElfSection* sections = reinterpret_cast<const ElfSection*>(metadata + elf->shoff);
    for (uint16_t index = 0; index < elf->shnum; ++index) if (sections[index].type == ELF_SHT_RELA && sections[index].size) { heap_caps_free(metadata); return false; }
    const void* mapping = nullptr;
    esp_partition_mmap_handle_t mapping_handle = 0;
    if (esp_partition_mmap(partition, SLOT_OFFSET[slot] + ELF_OFFSET, header.elf_size,
                           ESP_PARTITION_MMAP_INST, &mapping, &mapping_handle) != ESP_OK || !mapping) {
        heap_caps_free(metadata);
        return false;
    }
    uintptr_t create = 0, destroy = 0, shutdown = 0, tap = 0, long_press = 0, tick = 0;
    const uintptr_t bias = reinterpret_cast<uintptr_t>(mapping);
    const bool required = find_symbol(metadata, header.elf_size, elf, programs, sections, bias, "native_extension_create_instance", &create) &&
                          find_symbol(metadata, header.elf_size, elf, programs, sections, bias, "native_extension_destroy_instance", &destroy) &&
                          find_symbol(metadata, header.elf_size, elf, programs, sections, bias, "native_extension_shutdown", &shutdown);
    find_symbol(metadata, header.elf_size, elf, programs, sections, bias, "native_extension_on_tap", &tap);
    find_symbol(metadata, header.elf_size, elf, programs, sections, bias, "native_extension_on_long_press", &long_press);
    find_symbol(metadata, header.elf_size, elf, programs, sections, bias, "native_extension_tick", &tick);
    heap_caps_free(metadata);
    if (!required) { esp_partition_munmap(mapping_handle); return false; }
    LoadedSlot& loaded = s_slots[slot];
    loaded.mapping = const_cast<void*>(mapping); loaded.mapping_handle = mapping_handle;
    loaded.create = reinterpret_cast<NativeExtensionCreateFn>(create); loaded.destroy = reinterpret_cast<NativeExtensionDestroyFn>(destroy); loaded.shutdown = reinterpret_cast<NativeExtensionShutdownFn>(shutdown); loaded.tap = reinterpret_cast<NativeExtensionEventFn>(tap); loaded.long_press = reinterpret_cast<NativeExtensionEventFn>(long_press); loaded.tick = reinterpret_cast<NativeExtensionTickFn>(tick);
    loaded.info = {slot, true, false, false, false, true, true, SLOT_CAPACITY[slot], header.elf_size, 0, header.abi_version, {}, {}, {}, {}, NATIVE_EXTENSION_RUNTIME_IDLE, {}};
    strlcpy(loaded.info.id, header.id, sizeof(loaded.info.id)); strlcpy(loaded.info.version, header.version, sizeof(loaded.info.version));
    strlcpy(loaded.info.target_abi, header.target_abi, sizeof(loaded.info.target_abi));
    strlcpy(loaded.info.title, header.title, sizeof(loaded.info.title));
        LOGI(TAG, "Loaded slot %u: %s@%s map=%p create=%p", slot, header.id, header.version,
            mapping, reinterpret_cast<void*>(create));
    return true;
}

void install_stage(const esp_partition_t* partition, uint8_t slot) {
    char path[32]; stage_path(slot, path, sizeof(path));
    if (!Storage.exists(path)) return;
    File file = Storage.open(path, "r"); StageHeader stage = {};
    if (!file || file.read(reinterpret_cast<uint8_t*>(&stage), sizeof(stage)) != sizeof(stage) ||
        stage.magic != STAGE_MAGIC || stage.slot.elf_size == 0 ||
        stage.slot.elf_size > SLOT_CAPACITY[slot]) { if (file) file.close(); return; }
    uint8_t* elf = static_cast<uint8_t*>(heap_caps_malloc(stage.slot.elf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    const bool valid = elf && file.read(elf, stage.slot.elf_size) == stage.slot.elf_size &&
                       valid_elf(elf, stage.slot.elf_size) &&
                       native_extension_verify_signature(elf, stage.slot.elf_size, stage.slot.signature);
    if (!valid) {
        if (elf) heap_caps_free(elf);
        file.close();
        LOGW(TAG, "Rejected staged slot %u: invalid or unsigned package", slot);
        return;
    }
    if (esp_partition_erase_range(partition, SLOT_OFFSET[slot], SLOT_SIZE[slot]) != ESP_OK) {
        heap_caps_free(elf);
        file.close();
        return;
    }
    const bool written = esp_partition_write(partition, SLOT_OFFSET[slot] + ELF_OFFSET,
                                             elf, stage.slot.elf_size) == ESP_OK;
    heap_caps_free(elf);
    file.close();
    stage.slot.magic = SLOT_MAGIC;
    if (written && esp_partition_write(partition, SLOT_OFFSET[slot], &stage.slot, sizeof(stage.slot)) == ESP_OK) { Storage.remove(path); LOGI(TAG, "Installed slot %u: %s", slot, stage.slot.id); }
}

void process_pending_delete(const esp_partition_t* partition, uint8_t slot) {
    if (!delete_pending(slot)) return;
    if (esp_partition_erase_range(partition, SLOT_OFFSET[slot], SLOT_SIZE[slot]) != ESP_OK) return;
    char path[32];
    delete_path(slot, path, sizeof(path));
    Storage.remove(path);
    LOGI(TAG, "Erased pending-delete slot %u", slot);
}

void process_pending_enabled(const esp_partition_t* partition, uint8_t slot) {
    bool enabled = false;
    if (!pending_enabled(slot, &enabled)) return;
    NativeExtensionSlotHeader header = {};
    if (!read_raw_header(partition, slot, &header)) return;
    header.enabled = enabled ? 1 : 0;
    if (esp_partition_erase_range(partition, SLOT_OFFSET[slot], ELF_OFFSET) != ESP_OK ||
        esp_partition_write(partition, SLOT_OFFSET[slot], &header, sizeof(header)) != ESP_OK) return;
    char path[32];
    enabled_path(slot, path, sizeof(path));
    Storage.remove(path);
    LOGI(TAG, "Applied pending enabled=%u for slot %u", enabled, slot);
}

LoadedSlot* loaded_by_id(const char* id) { for (auto& slot : s_slots) if (slot.mapping && strcmp(slot.info.id, id) == 0) return &slot; return nullptr; }

void request_worker_stop(LoadedSlot* slot) {
    if (!slot) return;
    TaskHandle_t worker = nullptr;
    portENTER_CRITICAL(&s_worker_lock);
    slot->worker_cancel_requested = true;
    slot->worker_join_pending = true;
    slot->worker_cancelled_at_ms = millis();
    worker = slot->worker_task;
    portEXIT_CRITICAL(&s_worker_lock);
    if (worker) xTaskNotifyGive(worker);
    host_status_set(slot, NATIVE_EXTENSION_RUNTIME_STOPPING, worker ? "Stopping worker" : "Stopping package");
}

} // namespace

bool native_extension_init() {
    const esp_partition_t* partition = extension_partition();
    if (!partition) return false;
    LOGI(TAG, "Native extension ABI %u", NATIVE_EXTENSION_ABI_VERSION);
    for (uint8_t slot = 0; slot < NATIVE_EXTENSION_SLOT_COUNT; ++slot) process_pending_delete(partition, slot);
    for (uint8_t slot = 0; slot < NATIVE_EXTENSION_SLOT_COUNT; ++slot) install_stage(partition, slot);
    for (uint8_t slot = 0; slot < NATIVE_EXTENSION_SLOT_COUNT; ++slot) process_pending_enabled(partition, slot);
    for (uint8_t slot = 0; slot < NATIVE_EXTENSION_SLOT_COUNT; ++slot) {
        NativeExtensionSlotHeader header = {};
        if (!read_raw_header(partition, slot, &header)) continue;
        if (header.abi_version != NATIVE_EXTENSION_ABI_VERSION) {
            LOGW(TAG, "Skipped slot %u: ABI %u requires ABI %u", slot, header.abi_version,
                 NATIVE_EXTENSION_ABI_VERSION);
            continue;
        }
        if (header.enabled) load_slot(partition, slot, header);
    }
    return true;
}

uint8_t native_extension_slot_count() { return NATIVE_EXTENSION_SLOT_COUNT; }
bool native_extension_get_slot(uint8_t slot, NativeExtensionSlotInfo* out) {
    if (!out || !valid_slot(slot)) return false;
    *out = s_slots[slot].info; out->slot = slot; out->capacity = SLOT_CAPACITY[slot];
    out->pending_delete = delete_pending(slot);
    NativeExtensionSlotHeader header = {};
    const esp_partition_t* partition = extension_partition();
    if (read_raw_header(partition, slot, &header)) {
        out->installed = true; out->enabled = header.enabled; out->elf_size = header.elf_size;
        out->abi_version = header.abi_version;
        out->incompatible_abi = header.abi_version != NATIVE_EXTENSION_ABI_VERSION;
        strlcpy(out->id, header.id, sizeof(out->id)); strlcpy(out->version, header.version, sizeof(out->version));
        strlcpy(out->target_abi, header.target_abi, sizeof(out->target_abi));
        strlcpy(out->title, header.title, sizeof(out->title));
    }
    bool desired_enabled = false;
    if (pending_enabled(slot, &desired_enabled)) out->enabled = desired_enabled;
    StageHeader stage = {};
    if (read_stage(slot, &stage)) {
        out->staged = true;
        out->staged_size = stage.slot.elf_size;
        if (!out->installed) {
            strlcpy(out->id, stage.slot.id, sizeof(out->id));
            strlcpy(out->version, stage.slot.version, sizeof(out->version));
            strlcpy(out->target_abi, stage.slot.target_abi, sizeof(out->target_abi));
            strlcpy(out->title, stage.slot.title, sizeof(out->title));
        }
    }
    return true;
}
bool native_extension_stage_file(uint8_t slot, const char* filename, const char* source_path) {
    if (!valid_slot(slot) || !source_path) return false;
    File source = Storage.open(source_path, "r");
    const size_t package_size = source ? source.size() : 0;
    if (!source || package_size <= NATIVE_EXTENSION_SIGNATURE_SIZE ||
        package_size - NATIVE_EXTENSION_SIGNATURE_SIZE > SLOT_CAPACITY[slot]) {
        if (source) source.close();
        return false;
    }
    const size_t elf_size = package_size - NATIVE_EXTENSION_SIGNATURE_SIZE;
    uint8_t* elf = static_cast<uint8_t*>(heap_caps_malloc(elf_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    uint8_t signature[NATIVE_EXTENSION_SIGNATURE_SIZE] = {};
    if (!elf || source.read(elf, elf_size) != elf_size ||
        source.read(signature, sizeof(signature)) != sizeof(signature) || !valid_elf(elf, elf_size)) {
        if (elf) heap_caps_free(elf);
        source.close();
        return false;
    }
    source.close();
    NativeExtensionDescriptor descriptor = {};
    NativeExtensionSlotHeader slot_header = {};
    if (!native_extension_verify_signature(elf, elf_size, signature) ||
        !read_descriptor(elf, elf_size, &descriptor) || !parse_filename(filename, &slot_header) ||
        strcmp(slot_header.id, descriptor.id) != 0 || strcmp(slot_header.version, descriptor.version) != 0) {
        heap_caps_free(elf);
        return false;
    }
    slot_header.abi_version = descriptor.abi_version;
    strlcpy(slot_header.target_abi, descriptor.target_abi, sizeof(slot_header.target_abi));
    strlcpy(slot_header.title, descriptor.title, sizeof(slot_header.title));
    slot_header.elf_size = elf_size;
    if (!Storage.exists("/extensions") && !Storage.mkdir("/extensions")) {
        heap_caps_free(elf);
        return false;
    }
    char path[32]; stage_path(slot, path, sizeof(path));
    File file = Storage.open(path, "w");
    memcpy(slot_header.signature, signature, sizeof(slot_header.signature));
    const StageHeader stage = {STAGE_MAGIC, slot_header};
    const bool written = file && file.write(reinterpret_cast<const uint8_t*>(&stage), sizeof(stage)) == sizeof(stage) &&
                         file.write(elf, elf_size) == elf_size;
    if (file) file.close();
    heap_caps_free(elf);
    if (!written) Storage.remove(path);
    if (written) {
        char delete_marker[32];
        delete_path(slot, delete_marker, sizeof(delete_marker));
        Storage.remove(delete_marker);
        char enabled_marker[32];
        enabled_path(slot, enabled_marker, sizeof(enabled_marker));
        Storage.remove(enabled_marker);
    }
    if (written) LOGI(TAG, "Staged slot %u: %s@%s (%u bytes)", slot, slot_header.id,
                      slot_header.version, static_cast<unsigned>(elf_size));
    return written;
}
bool native_extension_set_enabled(uint8_t slot, bool enabled) {
    const esp_partition_t* partition = extension_partition();
    NativeExtensionSlotHeader header = {};
    return read_raw_header(partition, slot, &header) && set_pending_enabled(slot, enabled);
}
bool native_extension_delete(uint8_t slot) {
    if (!valid_slot(slot) || !extension_partition() || !set_delete_pending(slot)) return false;
    LOGI(TAG, "Delete pending for slot %u; reboot required", slot);
    return true;
}
bool native_extension_create_instance(const char* id, uint32_t instance_id, void* root, const char* config) {
    LoadedSlot* slot = loaded_by_id(id);
    if (!slot || !root) { LOGW(TAG, "Create unavailable: %s", id ? id : ""); return false; }
    if (slot->worker_join_pending || slot->worker_join_failed) {
        LOGW(TAG, "Create unavailable while worker stops: %s", id ? id : "");
        return false;
    }
    s_lvgl_task = xTaskGetCurrentTaskHandle();
    LOGI(TAG, "Create %s instance=%08lx", id, static_cast<unsigned long>(instance_id));
    slot->create(&HOST_API, slot, instance_id, root, config ? config : "");
    ++slot->active_instances;
    host_status_set(slot, NATIVE_EXTENSION_RUNTIME_RUNNING, "Widget instance active");
    return true;
}
void native_extension_set_instance_binding_context(const char* id, uint32_t instance_id,
                                                   const PadBinding* bindings, uint8_t binding_count) {
    LoadedSlot* slot = loaded_by_id(id);
    if (!slot) return;
    for (auto& context : slot->instance_bindings) {
        if (context.instance_id == instance_id || context.instance_id == 0) {
            context.instance_id = instance_id;
            context.bindings = bindings;
            context.binding_count = bindings ? binding_count : 0;
            return;
        }
    }
    LOGW(TAG, "Binding context limit reached: %s", id ? id : "");
}
void native_extension_clear_instance_binding_context(const char* id, uint32_t instance_id) {
    LoadedSlot* slot = loaded_by_id(id);
    if (!slot) return;
    for (auto& context : slot->instance_bindings) {
        if (context.instance_id == instance_id) {
            context = {};
            return;
        }
    }
}
bool native_extension_is_stopping(const char* id) {
    LoadedSlot* slot = loaded_by_id(id);
    return slot && slot->worker_join_pending && !slot->worker_join_failed;
}
void native_extension_destroy_instance(const char* id, uint32_t instance_id) {
    if (LoadedSlot* slot = loaded_by_id(id)) {
        slot->destroy(&HOST_API, slot, instance_id);
        if (slot->active_instances) --slot->active_instances;
        if (!slot->active_instances) request_worker_stop(slot);
    }
}
NativeExtensionEventResult native_extension_on_tap(const char* id, uint32_t instance_id) {
    LoadedSlot* slot = loaded_by_id(id);
    s_lvgl_task = xTaskGetCurrentTaskHandle();
    const NativeExtensionEventResult result = (slot && slot->tap) ? slot->tap(&HOST_API, slot, instance_id) : NATIVE_EXTENSION_PASS_THROUGH;
    LOGI(TAG, "Tap %s instance=%08lx: %s", id ? id : "", static_cast<unsigned long>(instance_id), result == NATIVE_EXTENSION_HANDLED ? "handled" : "pass-through");
    return result;
}
NativeExtensionEventResult native_extension_on_long_press(const char* id, uint32_t instance_id) {
    LoadedSlot* slot = loaded_by_id(id);
    s_lvgl_task = xTaskGetCurrentTaskHandle();
    const NativeExtensionEventResult result = (slot && slot->long_press) ? slot->long_press(&HOST_API, slot, instance_id) : NATIVE_EXTENSION_PASS_THROUGH;
    LOGI(TAG, "Long press %s instance=%08lx: %s", id ? id : "", static_cast<unsigned long>(instance_id), result == NATIVE_EXTENSION_HANDLED ? "handled" : "pass-through");
    return result;
}
void native_extension_tick_instance(const char* id, uint32_t instance_id) {
    if (LoadedSlot* slot = loaded_by_id(id); slot && slot->tick) {
        s_lvgl_task = xTaskGetCurrentTaskHandle();
        slot->tick(&HOST_API, slot, instance_id);
    }
}

void native_extension_loop() {
    for (auto& slot : s_slots) {
        if (!slot.mapping || !slot.worker_join_pending) continue;
        bool completed = false;
        uint32_t cancelled_at = 0;
        portENTER_CRITICAL(&s_worker_lock);
        completed = slot.worker_completed || !slot.worker_task;
        cancelled_at = slot.worker_cancelled_at_ms;
        portEXIT_CRITICAL(&s_worker_lock);
        if (completed) {
            slot.shutdown(&HOST_API, &slot);
            portENTER_CRITICAL(&s_worker_lock);
            slot.worker_task = nullptr;
            slot.worker_entry = nullptr;
            slot.worker_context = nullptr;
            slot.worker_cancel_requested = false;
            slot.worker_completed = false;
            slot.worker_join_pending = false;
            portEXIT_CRITICAL(&s_worker_lock);
            host_status_set(&slot, NATIVE_EXTENSION_RUNTIME_IDLE, "Worker stopped");
        } else if (millis() - cancelled_at >= WORKER_JOIN_TIMEOUT_MS) {
            slot.worker_join_pending = false;
            slot.worker_join_failed = true;
            host_status_set(&slot, NATIVE_EXTENSION_RUNTIME_ERROR, "Worker stop timed out");
            LOGW(TAG, "Worker stop timed out: %s", slot.info.id);
        }
    }
}

#endif