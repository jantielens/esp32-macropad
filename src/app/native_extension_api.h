#pragma once

#include <stddef.h>
#include <stdint.h>

// The extension ABI is intentionally C-shaped. Native packages are built
// separately from the firmware, so this remains the compatibility boundary.
#define NATIVE_EXTENSION_ABI_VERSION 9u
#define NATIVE_EXTENSION_TARGET_ABI "rv32imafc-ilp32f"
#define NATIVE_EXTENSION_DESCRIPTOR_MAGIC 0x3744584Eu

enum NativeExtensionEventResult : uint8_t {
    NATIVE_EXTENSION_PASS_THROUGH = 0,
    NATIVE_EXTENSION_HANDLED = 1,
};

enum NativeExtensionLogLevel : uint8_t {
    NATIVE_EXTENSION_LOG_INFO = 0,
    NATIVE_EXTENSION_LOG_WARN = 1,
    NATIVE_EXTENSION_LOG_ERROR = 2,
};

enum NativeExtensionRuntimeState : uint8_t {
    NATIVE_EXTENSION_RUNTIME_IDLE = 0,
    NATIVE_EXTENSION_RUNTIME_RUNNING,
    NATIVE_EXTENSION_RUNTIME_STOPPING,
    NATIVE_EXTENSION_RUNTIME_ERROR,
};

enum NativeExtensionAlign : uint8_t {
    NATIVE_EXTENSION_ALIGN_TOP_LEFT = 0,
    NATIVE_EXTENSION_ALIGN_TOP_MID,
    NATIVE_EXTENSION_ALIGN_TOP_RIGHT,
    NATIVE_EXTENSION_ALIGN_LEFT_MID,
    NATIVE_EXTENSION_ALIGN_CENTER,
    NATIVE_EXTENSION_ALIGN_RIGHT_MID,
    NATIVE_EXTENSION_ALIGN_BOTTOM_LEFT,
    NATIVE_EXTENSION_ALIGN_BOTTOM_MID,
    NATIVE_EXTENSION_ALIGN_BOTTOM_RIGHT,
};

enum NativeExtensionLvglEventCode : uint8_t {
    NATIVE_EXTENSION_EVENT_PRESSED = 0,
    NATIVE_EXTENSION_EVENT_PRESSING,
    NATIVE_EXTENSION_EVENT_RELEASED,
    NATIVE_EXTENSION_EVENT_CLICKED,
    NATIVE_EXTENSION_EVENT_LONG_PRESSED,
    NATIVE_EXTENSION_EVENT_VALUE_CHANGED,
    NATIVE_EXTENSION_EVENT_DELETE,
};

struct NativeExtensionHttpResult {
    int32_t status_code;
    size_t body_length;
    uint8_t truncated;
};

// Required ABI 8 data export. It is fixed-layout and pointer-free so the host
// can inspect it before executing a package.
struct NativeExtensionDescriptor {
    uint32_t magic;
    uint32_t abi_version;
    char target_abi[24];
    char id[32];
    char version[16];
    char title[40];
};

typedef void (*NativeExtensionTaskFn)(void* context);
typedef void (*NativeExtensionLvglEventFn)(void* event);

// Grouped source-level service views. New extensions should prefer these over
// the direct fields in NativeExtensionHostApi; direct fields remain during the
// ABI 8 transition so early sample source stays readable.
struct NativeExtensionCoreApi {
    uint32_t (*millis)();
    void (*delay_ms)(uint32_t delay_ms);
    void (*log)(NativeExtensionLogLevel level, const char* message);
    void* (*alloc)(size_t size);
    void (*free)(void* memory);
    void* (*context_get_data)(void* extension_context);
    void (*context_set_data)(void* extension_context, void* data);
    float (*math_sin)(float radians);
    float (*math_cos)(float radians);
    float (*math_sqrt)(float value);
    float (*math_atan2)(float y, float x);
    void* (*mutex_create)();
    bool (*mutex_lock)(void* mutex, uint32_t timeout_ms);
    void (*mutex_unlock)(void* mutex);
    void (*mutex_destroy)(void* mutex);
    void (*notify)(const char* message);
    void (*status_set)(void* extension_context, NativeExtensionRuntimeState state,
                       const char* detail);
};

struct NativeExtensionTaskApi {
    bool (*task_create)(void* extension_context, NativeExtensionTaskFn entry, const char* name,
                        uint32_t stack_bytes, void* context,
                        uint32_t priority, void** out_handle);
    bool (*task_cancel_requested)(void* extension_context);
    bool (*task_wait_or_cancel)(void* extension_context, uint32_t timeout_ms);
};

struct NativeExtensionHttpApi {
    bool (*http_get)(const char* url, uint8_t* response, size_t capacity,
                     uint32_t timeout_ms, NativeExtensionHttpResult* result);
};

struct NativeExtensionUiApi {
    void* (*label_create)(void* parent);
    void (*label_set_text)(void* label, const char* text);
    void (*obj_center)(void* obj);
    void* (*obj_create)(void* parent);
    void (*obj_delete)(void* obj);
    void (*obj_set_size)(void* obj, int32_t width, int32_t height);
    void (*obj_set_pos)(void* obj, int32_t x, int32_t y);
    int32_t (*obj_get_width)(void* obj);
    int32_t (*obj_get_height)(void* obj);
    void (*obj_align)(void* obj, NativeExtensionAlign align, int32_t x, int32_t y);
    void (*obj_set_hidden)(void* obj, bool hidden);
    void (*obj_set_clickable)(void* obj, bool clickable);
    void (*obj_invalidate)(void* obj);
    void (*obj_set_bg_color)(void* obj, uint32_t rgb, uint8_t opacity);
    void (*obj_set_border)(void* obj, uint32_t rgb, int32_t width, int32_t radius);
    void (*obj_set_text_color)(void* obj, uint32_t rgb);
    void (*obj_set_padding)(void* obj, int32_t all);
};

struct NativeExtensionCanvasApi {
    void* (*canvas_create)(void* parent);
    size_t (*canvas_buffer_size)(uint32_t width, uint32_t height);
    void (*canvas_set_buffer)(void* canvas, void* buffer, uint32_t width, uint32_t height);
    void (*canvas_clear)(void* canvas, uint32_t rgb);
    void (*canvas_set_pixel)(void* canvas, int32_t x, int32_t y, uint32_t rgb);
    void (*canvas_draw_line)(void* canvas, int32_t x1, int32_t y1,
                             int32_t x2, int32_t y2, uint32_t rgb, uint8_t width);
    void (*canvas_draw_circle)(void* canvas, int32_t x, int32_t y,
                               int32_t radius, uint32_t rgb, uint8_t width);
};

// Resolve an existing binding template on demand. Available only while a
// package callback runs on the LVGL task. `instance_id` selects that widget's
// pad context, so [pad:...] tokens resolve against its owning pad. The caller
// owns `out`; a true return means arguments and context were valid, regardless
// of whether the binding output is a placeholder such as "---".
struct NativeExtensionBindingApi {
    bool (*resolve)(void* extension_context, uint32_t instance_id,
                    const char* template_text, char* out, size_t out_size);
};

struct NativeExtensionHostApi {
    uint32_t abi_version;
    void (*set_text)(char* out, size_t out_len, const char* text);

    // Core helpers.
    uint32_t (*millis)();
    void (*delay_ms)(uint32_t delay_ms);
    void (*log)(NativeExtensionLogLevel level, const char* message);
    void* (*alloc)(size_t size);
    void (*free)(void* memory);
    void* (*context_get_data)(void* extension_context);
    void (*context_set_data)(void* extension_context, void* data);
    float (*math_sin)(float radians);
    float (*math_cos)(float radians);
    float (*math_sqrt)(float value);
    float (*math_atan2)(float y, float x);
    void* (*mutex_create)();
    bool (*mutex_lock)(void* mutex, uint32_t timeout_ms);
    void (*mutex_unlock)(void* mutex);
    void* (*label_create)(void* parent);
    void (*label_set_text)(void* label, const char* text);
    void (*obj_center)(void* obj);
    void (*log_info)(const char* message);
    void (*notify)(const char* message);

    // Worker tasks and bounded, deliberately insecure HTTP GET. These may be
    // called only from an extension worker, never an LVGL callback.
    bool (*task_create)(void* extension_context, NativeExtensionTaskFn entry, const char* name,
                        uint32_t stack_bytes, void* context,
                        uint32_t priority, void** out_handle);
    bool (*http_get)(const char* url, uint8_t* response, size_t capacity,
                     uint32_t timeout_ms, NativeExtensionHttpResult* result);

    // Common LVGL object and style operations. Object pointers are opaque and
    // may only be used from extension lifecycle, event, and tick callbacks.
    void* (*obj_create)(void* parent);
    void (*obj_delete)(void* obj);
    void (*obj_set_size)(void* obj, int32_t width, int32_t height);
    void (*obj_set_pos)(void* obj, int32_t x, int32_t y);
    int32_t (*obj_get_width)(void* obj);
    int32_t (*obj_get_height)(void* obj);
    void (*obj_align)(void* obj, NativeExtensionAlign align, int32_t x, int32_t y);
    void (*obj_set_hidden)(void* obj, bool hidden);
    void (*obj_set_clickable)(void* obj, bool clickable);
    void (*obj_invalidate)(void* obj);
    void (*obj_set_bg_color)(void* obj, uint32_t rgb, uint8_t opacity);
    void (*obj_set_border)(void* obj, uint32_t rgb, int32_t width, int32_t radius);
    void (*obj_set_text_color)(void* obj, uint32_t rgb);
    void (*obj_set_padding)(void* obj, int32_t all);
    void (*obj_add_event_cb)(void* obj, NativeExtensionLvglEventFn callback,
                             NativeExtensionLvglEventCode event_code, void* user_data);
    NativeExtensionLvglEventCode (*event_get_code)(void* event);
    void* (*event_get_target)(void* event);
    void* (*event_get_user_data)(void* event);

    // Frequently useful LVGL controls.
    void* (*line_create)(void* parent);
    void (*line_set)(void* line, int32_t x1, int32_t y1,
                     int32_t x2, int32_t y2, uint32_t rgb, uint8_t width);
    void* (*arc_create)(void* parent);
    void (*arc_set_value)(void* arc, int32_t value);
    void* (*spinner_create)(void* parent, uint32_t duration_ms, uint32_t arc_degrees);
    void* (*table_create)(void* parent);
    void (*table_set_size)(void* table, uint16_t rows, uint16_t columns);
    void (*table_set_cell_text)(void* table, uint16_t row, uint16_t column, const char* text);

    // Canvas operations use RGB565 buffers allocated by the extension. Text
    // is normally represented by child labels, keeping this draw API compact.
    void* (*canvas_create)(void* parent);
    size_t (*canvas_buffer_size)(uint32_t width, uint32_t height);
    void (*canvas_set_buffer)(void* canvas, void* buffer, uint32_t width, uint32_t height);
    void (*canvas_clear)(void* canvas, uint32_t rgb);
    void (*canvas_set_pixel)(void* canvas, int32_t x, int32_t y, uint32_t rgb);
    void (*canvas_draw_line)(void* canvas, int32_t x1, int32_t y1,
                             int32_t x2, int32_t y2, uint32_t rgb, uint8_t width);
    void (*canvas_draw_circle)(void* canvas, int32_t x, int32_t y,
                               int32_t radius, uint32_t rgb, uint8_t width);

    // ABI 8 grouped service views. New package code should use these.
    const NativeExtensionCoreApi* core;
    const NativeExtensionTaskApi* task;
    const NativeExtensionHttpApi* http;
    const NativeExtensionUiApi* ui;
    const NativeExtensionCanvasApi* canvas;
    const NativeExtensionBindingApi* binding;
};

typedef void (*NativeExtensionCreateFn)(const NativeExtensionHostApi* host,
                                        void* extension_context, uint32_t instance_id, void* root,
                                        const char* config_json);
typedef void (*NativeExtensionDestroyFn)(const NativeExtensionHostApi* host,
                                         void* extension_context, uint32_t instance_id);
typedef void (*NativeExtensionShutdownFn)(const NativeExtensionHostApi* host,
                                          void* extension_context);
typedef NativeExtensionEventResult (*NativeExtensionEventFn)(const NativeExtensionHostApi* host,
                                                             void* extension_context, uint32_t instance_id);
typedef void (*NativeExtensionTickFn)(const NativeExtensionHostApi* host,
                                      void* extension_context, uint32_t instance_id);