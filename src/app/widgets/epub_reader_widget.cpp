#include "../board_config.h"

#if HAS_DISPLAY && HAS_EPUB_READER

#include "widget.h"
#include "epub_pagination.h"
#include "../display_manager.h"
#include "../font_store.h"
#include "../log_manager.h"
#include "../ota_activity.h"
#include "../rtos_task_utils.h"
#include "../storage.h"
#include <FreeInkBook.h>
#include <cache/PageCache.h>
#include <css/Css.h>
#include <epub/XmlSax.h>
#include <layout/ChapterLayout.h>
#include <render/PageRenderer.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <atomic>
#include <new>

namespace epub_reader_impl {
using namespace freeink::book;

constexpr size_t path_capacity = CONFIG_LABEL_MAX_LEN;
constexpr size_t book_arena_size = 512 * 1024;
constexpr uint32_t worker_stack_bytes = 12 * 1024;

struct Config { char path[path_capacity]; };
static_assert(sizeof(Config) <= WIDGET_CONFIG_MAX_BYTES, "EPUB reader config too large");

enum class FontRequestKind : uint8_t { None, Width, Metrics, Bitmap };

struct FontRequest {
    FontRequestKind kind = FontRequestKind::None;
    uint32_t generation = 0;
    uint32_t code = 0;
    uint32_t next = 0;
    uint16_t size = 0;
    int16_t width = 0;
    int16_t height = 0;
    int16_t ascent = 0;
    bool available = false;
    bool done = false;
    freeink::font::GlyphBitmap bitmap{};
    uint8_t* pixels = nullptr;
    size_t pixel_capacity = 0;
};

struct Service {
    SemaphoreHandle_t mutex = nullptr;
    TaskHandle_t task = nullptr;
    RtosTaskInternalAlloc task_alloc{};
    std::atomic<uint32_t> generation{0};
    std::atomic<bool> active{false};
    char path[path_capacity]{};
    char message[128]{};
    uint32_t revision = 0;
    int8_t command = 0;
    FontRequest font_request{};
    char font_name[32]{};
    uint16_t font_size = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    uint8_t* frame = nullptr;
    uint32_t frame_revision = 0;
    uint32_t applied_revision = 0;
    size_t min_internal_largest = SIZE_MAX;
    size_t min_psram_largest = SIZE_MAX;
    bool ready = false;
};

struct State {
    lv_obj_t* parent = nullptr;
    lv_obj_t* image = nullptr;
    lv_obj_t* message = nullptr;
    const lv_font_t* font = nullptr;
    lv_timer_t* glyph_timer = nullptr;
    lv_image_dsc_t descriptor{};
    uint16_t* pixels = nullptr;
    size_t pixel_count = 0;
    char path[path_capacity]{};
    uint32_t generation = 0;
    uint32_t revision = 0;
    lv_point_t pressed_at{};
    bool pressed = false;
    bool visible = false;
};

Service* service = nullptr;

State* get(WidgetState* runtime) {
    State* state = nullptr;
    memcpy(&state, runtime->data, sizeof(state));
    return state;
}

bool current(uint32_t generation) {
    return service->active.load() && service->generation.load() == generation;
}

}
#include "epub_sdk_font.h"
#include "epub_cache_storage.h"
namespace epub_reader_impl {

uint32_t publish(uint32_t generation, const char* message, bool ready) {
    if (xSemaphoreTake(service->mutex, portMAX_DELAY) != pdTRUE) return 0;
    uint32_t revision = 0;
    if (current(generation)) {
        strlcpy(service->message, message ? message : "", sizeof(service->message));
        service->ready = ready;
        revision = ++service->revision;
    }
    xSemaphoreGive(service->mutex);
    return revision;
}

void log_memory(const char* stage, int64_t start) {
    const uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const uint32_t external = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    const size_t internal_largest = heap_caps_get_largest_free_block(internal);
    const size_t psram_largest = heap_caps_get_largest_free_block(external);
    if (internal_largest < service->min_internal_largest) service->min_internal_largest = internal_largest;
    if (psram_largest < service->min_psram_largest) service->min_psram_largest = psram_largest;
    LOGI("EpubReader", "%s elapsed=%lu ms stack-low=%u",
         stage, static_cast<unsigned long>((esp_timer_get_time() - start) / 1000),
         static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    LOGI("EpubReader", "internal free=%u boot-min=%u largest=%u sampled-min-largest=%u",
         static_cast<unsigned>(heap_caps_get_free_size(internal)),
         static_cast<unsigned>(heap_caps_get_minimum_free_size(internal)),
         static_cast<unsigned>(internal_largest), static_cast<unsigned>(service->min_internal_largest));
    LOGI("EpubReader", "PSRAM free=%u boot-min=%u largest=%u sampled-min-largest=%u",
         static_cast<unsigned>(heap_caps_get_free_size(external)),
         static_cast<unsigned>(heap_caps_get_minimum_free_size(external)),
         static_cast<unsigned>(psram_largest), static_cast<unsigned>(service->min_psram_largest));
}

class Source : public BookSource {
public:
    Source(const char* path, uint32_t generation) : path_(path), generation_(generation) {}

    bool open() {
        const StorageMutationGuard guard;
        if (!guard) { busy_ = true; return false; }
        if (!current(generation_)) return false;
        File file = Storage.open(path_, "r");
        if (!file || file.isDirectory()) return false;
        size_ = file.size();
        file.close();
        return size_ > 0 && size_ <= UINT32_MAX;
    }

    int32_t readAt(uint64_t offset, void* dst, uint32_t length) override {
        if (!current(generation_) || offset > UINT32_MAX) return -1;
        const StorageMutationGuard guard;
        if (!guard) { busy_ = true; return -1; }
        File file = Storage.open(path_, "r");
        if (!file || !file.seek(static_cast<uint32_t>(offset))) return -1;
        const size_t count = file.read(static_cast<uint8_t*>(dst), length);
        file.close();
        taskYIELD();
        return static_cast<int32_t>(count);
    }

    uint64_t size() const override { return size_; }
    bool shouldStop() override {
        if ((++checkpoints_ & 255u) == 0) taskYIELD();
        if (ota_activity_is_active()) { busy_ = true; return true; }
        return !current(generation_);
    }
    bool busy() const { return busy_; }

private:
    const char* path_;
    uint32_t generation_;
    uint64_t size_ = 0;
    uint32_t checkpoints_ = 0;
    bool busy_ = false;
};

struct Position {
    uint32_t magic;
    uint32_t size;
    uint32_t spine;
    uint32_t offset;
    char path[path_capacity];
};
constexpr uint32_t position_magic = 0x45504231;

uint32_t path_hash(const char* path) {
    uint32_t hash = 2166136261u;
    while (*path) { hash ^= static_cast<uint8_t>(*path++); hash *= 16777619u; }
    return hash;
}

void position_path(const char* path, char* out, size_t size) {
    snprintf(out, size, "/storage/.epub-%08lx.pos", static_cast<unsigned long>(path_hash(path)));
}

}
#include "epub_sdk_worker.h"
namespace epub_reader_impl {

void worker(void*) {
    uint32_t processed = 0;
    for (;;) {
        const uint32_t generation = service->generation.load();
        if (!service->active.load() || generation == processed) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        char path[path_capacity];
        char font_name[32];
        uint16_t font_size = 0, width = 0, height = 0;
        if (xSemaphoreTake(service->mutex, portMAX_DELAY) == pdTRUE) {
            strlcpy(path, service->path, sizeof(path));
            strlcpy(font_name, service->font_name, sizeof(font_name));
            font_size = service->font_size;
            width = service->width;
            height = service->height;
            xSemaphoreGive(service->mutex);
        }
        processed = generation;
        if (current(generation)) run_sdk_book(generation, path, font_name, font_size, width, height);
    }
}

bool start_service() {
    if (service) return true;
    void* memory = heap_caps_malloc(sizeof(Service), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) return false;
    service = new (memory) Service{};
    service->mutex = xSemaphoreCreateMutex();
    if (!service->mutex || !rtos_create_task_internal_stack(worker, "EpubReader", worker_stack_bytes, nullptr, 2,
                                                             &service->task, &service->task_alloc)) {
        if (service->mutex) vSemaphoreDelete(service->mutex);
        service->~Service(); heap_caps_free(service); service = nullptr;
        return false;
    }
    return true;
}

void parse(const JsonObject& button, uint8_t* data) {
    auto* config = new (data) Config{};
    strlcpy(config->path, button["widget_epub_path"] | "", sizeof(config->path));
}

const char* validate(JsonObjectConst button) {
    if (!button["widget_epub_path"].is<const char*>()) return "EPUB path must be a string";
    JsonString path = button["widget_epub_path"].as<JsonString>();
    if (!path.size() || path.size() >= path_capacity || strlen(path.c_str()) != path.size() || path.c_str()[0] != '/')
        return "EPUB path must be an absolute path shorter than 192 bytes";
    if ((button["actions"].is<JsonArrayConst>() && button["actions"].size()) ||
        (button["lp_actions"].is<JsonArrayConst>() && button["lp_actions"].size()))
        return "EPUB reader owns touch input; use a separate button for actions";
    return nullptr;
}

void input(lv_event_t* event) {
    auto* state = static_cast<State*>(lv_event_get_user_data(event));
    lv_event_stop_bubbling(event);
    const auto code = lv_event_get_code(event);
    lv_indev_t* input = lv_event_get_indev(event);
    if (code == LV_EVENT_PRESSED && input) {
        state->pressed = true;
        lv_indev_get_point(input, &state->pressed_at);
    } else if (code == LV_EVENT_PRESS_LOST || code == LV_EVENT_INDEV_RESET) {
        state->pressed = false;
    } else if (code == LV_EVENT_RELEASED && state->pressed && input) {
        state->pressed = false;
        lv_point_t point;
        lv_indev_get_point(input, &point);
        lv_area_t bounds;
        lv_obj_get_coords(state->parent, &bounds);
        const int32_t split = (bounds.x1 + bounds.x2) / 2;
        const char* result = "rejected-gesture";
        if (!state->visible || !service || !current(state->generation) ||
            LV_ABS(point.x - state->pressed_at.x) > 10 || LV_ABS(point.y - state->pressed_at.y) > 10 ||
            point.x < bounds.x1 || point.x > bounds.x2 || point.y < bounds.y1 || point.y > bounds.y2) {
            LOGI("EpubReader", "tap gen=%lu x=%ld y=%ld split-x=%ld result=%s",
                 static_cast<unsigned long>(state->generation), static_cast<long>(point.x),
                 static_cast<long>(point.y), static_cast<long>(split), result);
            return;
        }
        if (xSemaphoreTake(service->mutex, 0) == pdTRUE) {
            if (current(state->generation) && service->ready && !service->command) {
                service->command = point.x < split ? -1 : 1;
                result = service->command > 0 ? "queued-next" : "queued-previous";
            } else {
                result = "not-ready-or-pending";
            }
            xSemaphoreGive(service->mutex);
        } else result = "mutex-busy";
        LOGI("EpubReader", "tap gen=%lu x=%ld y=%ld split-x=%ld result=%s",
             static_cast<unsigned long>(state->generation), static_cast<long>(point.x),
             static_cast<long>(point.y), static_cast<long>(split), result);
    }
}

void measure_glyphs(lv_timer_t* timer) {
    auto* state = static_cast<State*>(lv_timer_get_user_data(timer));
    if (!state->visible || !service || !current(state->generation) || ota_activity_is_active()) return;
    if (xSemaphoreTake(service->mutex, 0) != pdTRUE) return;
    process_font_request();
    xSemaphoreGive(service->mutex);
}

void create(lv_obj_t* parent, const WidgetConfig* config, const ScreenButtonConfig*,
            const PadRect*, const UIScaleInfo*, WidgetState* runtime) {
    void* memory = heap_caps_malloc(sizeof(State), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    State* state = memory ? new (memory) State{} : nullptr;
    memcpy(runtime->data, &state, sizeof(state));
    if (!state) { LOGE("EpubReader", "Cannot allocate widget state"); return; }
    state->parent = parent;
    strlcpy(state->path, reinterpret_cast<const Config*>(config->data)->path, sizeof(state->path));
    state->image = lv_image_create(parent);
    state->message = lv_label_create(parent);
    if (!state->image || !state->message) { LOGE("EpubReader", "Cannot create reader display"); return; }
    state->font = lv_theme_get_font_normal(parent);
    lv_obj_set_style_text_font(state->message, state->font, 0);
    lv_obj_clear_flag(state->image, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(state->message, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(parent, input, LV_EVENT_ALL, state);
}

void show(WidgetState* runtime) {
    State* state = get(runtime);
    if (!state || !state->image || !state->message) return;
    if (state->visible) return;
    state->visible = true;
    lv_obj_update_layout(state->parent);
    const int32_t width = lv_obj_get_content_width(state->parent);
    const int32_t height = lv_obj_get_content_height(state->parent);
    const int32_t line = lv_font_get_line_height(state->font);
    const int32_t page_height = height - line;
    lv_obj_set_size(state->image, width, page_height);
    lv_obj_set_pos(state->image, 0, 0);
    lv_obj_set_pos(state->message, 0, height - line);
    if (width <= 8 || page_height <= line || width > INT16_MAX || page_height > INT16_MAX) {
        lv_label_set_text(state->message, "Reader needs more space");
        return;
    }
    if (!start_service()) { lv_label_set_text(state->message, "Reader task allocation failed"); return; }
    if (service->active.load()) { lv_label_set_text(state->message, "Only one EPUB reader can be active"); return; }
    state->pixel_count = static_cast<size_t>(width) * page_height;
    state->pixels = static_cast<uint16_t*>(
        heap_caps_malloc(state->pixel_count * sizeof(uint16_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!state->pixels) { lv_label_set_text(state->message, "Not enough PSRAM for display"); return; }
    memset(state->pixels, 0xff, state->pixel_count * sizeof(uint16_t));
    state->descriptor = {};
    state->descriptor.header.magic = LV_IMAGE_HEADER_MAGIC;
    state->descriptor.header.cf = LV_COLOR_FORMAT_RGB565;
    state->descriptor.header.w = width;
    state->descriptor.header.h = page_height;
    state->descriptor.header.stride = width * sizeof(uint16_t);
    state->descriptor.data_size = state->pixel_count * sizeof(uint16_t);
    state->descriptor.data = reinterpret_cast<const uint8_t*>(state->pixels);
    lv_image_set_src(state->image, &state->descriptor);
    state->glyph_timer = lv_timer_create(measure_glyphs, 1, state);
    if (!state->glyph_timer) {
        lv_image_set_src(state->image, nullptr);
        heap_caps_free(state->pixels);
        state->pixels = nullptr;
        state->pixel_count = 0;
        lv_label_set_text(state->message, "Glyph timer allocation failed");
        return;
    }
    if (xSemaphoreTake(service->mutex, portMAX_DELAY) != pdTRUE) return;
    strlcpy(service->path, state->path, sizeof(service->path));
    const DeviceTypography& defaults = device_typography_get();
    const uint32_t original = pad_font_pixel_size(LV_FONT_DEFAULT);
    const uint32_t medium = pad_font_pixel_size(pad_get_scale_info().font_medium);
    const uint32_t requested_size = defaults.base_size
        ? (original * defaults.base_size + medium / 2) / medium : original;
    strlcpy(service->font_name, defaults.font_name, sizeof(service->font_name));
    service->font_size = static_cast<uint16_t>(requested_size ? requested_size : 1);
    service->width = static_cast<uint16_t>(width);
    service->height = static_cast<uint16_t>(page_height);
    service->frame = nullptr;
    service->frame_revision = 0;
    service->applied_revision = 0;
    service->font_request = {};
    service->command = 0; service->ready = false;
    strlcpy(service->message, "Loading EPUB...", sizeof(service->message));
    ++service->revision;
    state->generation = service->generation.fetch_add(1) + 1;
    state->revision = 0;
    service->active.store(true);
    xSemaphoreGive(service->mutex);
    LOGI("EpubReader", "show gen=%lu content=%ldx%ld font-size=%u",
         static_cast<unsigned long>(state->generation), static_cast<long>(width), static_cast<long>(height),
         static_cast<unsigned>(service->font_size));
}

void hide(WidgetState* runtime) {
    State* state = get(runtime);
    if (!state) return;
    state->visible = false;
    state->pressed = false;
    if (state->glyph_timer) {
        lv_timer_delete(state->glyph_timer);
        state->glyph_timer = nullptr;
    }
    if (state->image) lv_image_set_src(state->image, nullptr);
    heap_caps_free(state->pixels);
    state->pixels = nullptr;
    state->pixel_count = 0;
    if (service && state->generation && current(state->generation)) {
        LOGI("EpubReader", "hide gen=%lu", static_cast<unsigned long>(state->generation));
        service->active.store(false);
        service->generation.fetch_add(1);
    }
    state->generation = 0;
}

void destroy(WidgetState* runtime) {
    State* state = get(runtime);
    if (!state) return;
    hide(runtime);
    lv_obj_remove_event_cb_with_user_data(state->parent, input, state);
    if (state->image) lv_obj_delete(state->image);
    if (state->message) lv_obj_delete(state->message);
    state->~State(); heap_caps_free(state);
    state = nullptr;
    memcpy(runtime->data, &state, sizeof(state));
}

void tick(lv_obj_t*, const WidgetConfig*, WidgetState* runtime) {
    State* state = get(runtime);
    if (!state || !state->visible || !service || !state->generation) return;
    if (xSemaphoreTake(service->mutex, 0) != pdTRUE) return;
    uint32_t applied = 0;
    bool ready = false;
    if (current(state->generation) && state->revision != service->revision) {
        if (service->frame && service->frame_revision == service->revision && state->pixels) {
            for (size_t i = 0; i < state->pixel_count; ++i) {
                const uint8_t value = service->frame[i];
                state->pixels[i] = lv_color_to_u16(lv_color_make(value, value, value));
            }
            lv_obj_invalidate(state->image);
            service->applied_revision = service->frame_revision;
        }
        lv_label_set_text(state->message, service->message);
        state->revision = service->revision;
        applied = state->revision;
        ready = service->ready;
        if (displayManager) displayManager->interactionChanged();
    }
    xSemaphoreGive(service->mutex);
    if (applied)
        LOGI("EpubReader", "ui-applied gen=%lu rev=%lu ready=%u",
             static_cast<unsigned long>(state->generation), static_cast<unsigned long>(applied), ready ? 1u : 0u);
}

void cancel(WidgetState* runtime) { State* state = get(runtime); if (state) state->pressed = false; }
static const WidgetPreview preview{"EPUB reader (experimental)", "menu_book"};

#if HAS_MCP
void describe(JsonObject& out) {
    JsonObject field = out.createNestedArray("config_fields").createNestedObject();
    field["name"] = "widget_epub_path";
    field["type"] = "string";
    field["max_length"] = path_capacity - 1;
    field["description"] = "Absolute EPUB path on device storage.";
    out["note"] = "Experimental EPUB reader. One instance; tap left/right to turn pages.";
}
#endif
}

static const WidgetType epub_reader_widget_type{
    "epub_reader", epub_reader_impl::parse, epub_reader_impl::create, nullptr,
    epub_reader_impl::destroy, epub_reader_impl::tick, nullptr, false,
#if HAS_MCP
    epub_reader_impl::describe,
#else
    nullptr,
#endif
    epub_reader_impl::show, epub_reader_impl::hide, epub_reader_impl::validate,
    &epub_reader_impl::preview, epub_reader_impl::cancel, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, false
};
static struct EpubReaderRegistration {
    EpubReaderRegistration() { widget_register(&epub_reader_widget_type); }
} epub_reader_registration;

#endif
