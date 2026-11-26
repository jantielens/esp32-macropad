#include "../board_config.h"

#if HAS_DISPLAY && HAS_EREADER

#include "widget.h"
#include "epub_pagination.h"
#include "../display_manager.h"
#include "../ereader.h"
#include "../font_store.h"
#include "../log_manager.h"
#include "../list_source.h"
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
#include <strings.h>

namespace epub_reader_impl {
using namespace freeink::book;

constexpr size_t path_capacity = CONFIG_LABEL_MAX_LEN;
constexpr size_t book_arena_size = 512 * 1024;
constexpr uint32_t worker_stack_bytes = 12 * 1024;

struct Config { char style[CONFIG_LABEL_STYLE_MAX_LEN]; };
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

struct State;

struct Service {
    SemaphoreHandle_t mutex = nullptr;
    TaskHandle_t task = nullptr;
    RtosTaskInternalAlloc task_alloc{};
    std::atomic<uint32_t> generation{0};
    std::atomic<uint32_t> book_generation{0};
    std::atomic<bool> active{false};
    char path[path_capacity]{};
    char selected[64]{};
    char working_book[64]{};
    char message[128]{};
    char chapter_error[128]{};
    char (*chapter_titles)[LIST_SOURCE_MAX_TEXT_BYTES + 1] = nullptr;
    uint32_t revision = 0;
    int8_t command = 0;
    bool busy = false;
    bool working = false;
    bool error = false;
    bool file_busy = false;
    bool chapters_available = false;
    bool page_available = false;
    bool chapter_pending = false;
    bool metadata_pending = false;
    EreaderMetadataState chapter_state = EreaderMetadataState::Idle;
    uint32_t metadata_book_key = 0;
    uint32_t chapter_count = 0;
    uint32_t chapter = 0;
    uint32_t page = 0;
    uint32_t page_count = 0;
    uint32_t requested_chapter = 0;
    std::atomic<uint32_t> chapters_revision{0};
    State* view = nullptr;
    uint32_t position_book_key = 0;
    uint32_t position_layout_key = 0;
    uint32_t position_spine = 0;
    uint32_t position_character = 0;
    uint32_t position_page = 0;
    bool position_valid = false;
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
    size_t min_internal_free = SIZE_MAX;
    size_t min_psram_free = SIZE_MAX;
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
    char font_name[CONFIG_FONT_ID_MAX_LEN]{};
    uint16_t font_size = 0;
    uint32_t generation = 0;
    uint32_t revision = 0;
    lv_point_t pressed_at{};
    bool pressed = false;
    bool visible = false;
};

Service* service = nullptr;
std::atomic<bool> service_ready{false};

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

uint32_t publish(uint32_t generation, const char* message, bool ready, bool error = true) {
    if (xSemaphoreTake(service->mutex, portMAX_DELAY) != pdTRUE) return 0;
    uint32_t revision = 0;
    if (current(generation)) {
        strlcpy(service->message, message ? message : "", sizeof(service->message));
        service->ready = ready;
        service->error = error;
        if (!ready) service->page_available = false;
        revision = ++service->revision;
    }
    xSemaphoreGive(service->mutex);
    if (revision) display_manager_request_binding_refresh();
    return revision;
}

void log_memory(const char* stage, int64_t start) {
    const uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const uint32_t external = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    const size_t internal_largest = heap_caps_get_largest_free_block(internal);
    const size_t psram_largest = heap_caps_get_largest_free_block(external);
    const size_t internal_free = heap_caps_get_free_size(internal);
    const size_t psram_free = heap_caps_get_free_size(external);
    if (internal_largest < service->min_internal_largest) service->min_internal_largest = internal_largest;
    if (psram_largest < service->min_psram_largest) service->min_psram_largest = psram_largest;
    if (internal_free < service->min_internal_free) service->min_internal_free = internal_free;
    if (psram_free < service->min_psram_free) service->min_psram_free = psram_free;
    LOGI("EpubReader", "%s elapsed=%lu ms task-lifetime-stack-low=%u",
         stage, static_cast<unsigned long>((esp_timer_get_time() - start) / 1000),
         static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    LOGI("EpubReader", "internal free=%u reader-sampled-min=%u largest=%u reader-sampled-min-largest=%u",
         static_cast<unsigned>(internal_free),
         static_cast<unsigned>(service->min_internal_free),
         static_cast<unsigned>(internal_largest), static_cast<unsigned>(service->min_internal_largest));
    LOGI("EpubReader", "PSRAM free=%u reader-sampled-min=%u largest=%u reader-sampled-min-largest=%u",
         static_cast<unsigned>(psram_free),
         static_cast<unsigned>(service->min_psram_free),
         static_cast<unsigned>(psram_largest), static_cast<unsigned>(service->min_psram_largest));
}

class Source : public BookSource {
public:
    Source(const char* path, uint32_t generation, bool metadata = false)
        : path_(path), generation_(generation), metadata_(metadata), ota_epoch_(ota_activity_epoch()) {}

    bool open() {
        const StorageMutationGuard guard;
        if (!guard) { busy_ = true; return false; }
        if (!valid()) return false;
        File file = Storage.open(path_, "r");
        if (!file || file.isDirectory()) return false;
        size_ = file.size();
        file.close();
        return size_ > 0 && size_ <= UINT32_MAX;
    }

    int32_t readAt(uint64_t offset, void* dst, uint32_t length) override {
        if (!valid() || offset > UINT32_MAX) return -1;
        size_t count = 0;
        {
            const StorageMutationGuard guard;
            if (!guard) { busy_ = true; return -1; }
            File file = Storage.open(path_, "r");
            if (!file || !file.seek(static_cast<uint32_t>(offset))) return -1;
            count = file.read(static_cast<uint8_t*>(dst), length);
            file.close();
        }
        vTaskDelay(1);
        return static_cast<int32_t>(count);
    }

    uint64_t size() const override { return size_; }
    bool shouldStop() override {
        if (!valid()) return true;
        const uint32_t now = millis();
        if (now - last_pause_ms_ >= 10) {
            vTaskDelay(1);
            last_pause_ms_ = millis();
            return !valid();
        }
        return false;
    }
    bool busy() const { return busy_; }

private:
    bool valid() {
        if (ota_activity_is_active() || (metadata_ && ota_activity_epoch() != ota_epoch_)) {
            busy_ = true;
            return false;
        }
        return metadata_ ? service->book_generation.load() == generation_ : current(generation_);
    }
    const char* path_;
    uint32_t generation_;
    bool metadata_;
    uint32_t ota_epoch_;
    uint64_t size_ = 0;
    uint32_t last_pause_ms_ = millis();
    bool busy_ = false;
};

uint32_t path_hash(const char* path) {
    uint32_t hash = 2166136261u;
    while (*path) { hash ^= static_cast<uint8_t>(*path++); hash *= 16777619u; }
    return hash;
}

void position_path(const char* path, char* out, size_t size) {
    snprintf(out, size, "/storage/.ereader-%08lx.pos", static_cast<unsigned long>(path_hash(path)));
}

}
#include "epub_sdk_worker.h"
namespace epub_reader_impl {

void worker(void*) {
    uint32_t processed = 0;
    for (;;) {
        char metadata_path[path_capacity];
        uint32_t book_generation = 0;
        bool metadata = false;
        if (xSemaphoreTake(service->mutex, portMAX_DELAY) == pdTRUE) {
            if (service->metadata_pending) {
                metadata = true;
                book_generation = service->book_generation.load();
                strlcpy(metadata_path, service->path, sizeof(metadata_path));
                strlcpy(service->working_book, service->selected, sizeof(service->working_book));
                service->working = true;
            }
            xSemaphoreGive(service->mutex);
        }
        if (metadata) {
            const uint32_t ota_epoch = ota_activity_epoch();
            ChapterMetadataResult result = load_chapter_metadata(book_generation, metadata_path);
            if (xSemaphoreTake(service->mutex, portMAX_DELAY) == pdTRUE) {
                if (ota_activity_is_active() || ota_activity_epoch() != ota_epoch)
                    result.error = "Chapter metadata interrupted";
                service->working = false;
                service->working_book[0] = '\0';
                if (service->book_generation.load() == book_generation) {
                    service->metadata_pending = false;
                    service->chapters_available = !result.error;
                    service->chapter_state = result.error ? EreaderMetadataState::Error : EreaderMetadataState::Ready;
                    strlcpy(service->chapter_error, result.error ? result.error : "", sizeof(service->chapter_error));
                    if (result.error) {
                        heap_caps_free(result.titles);
                        service->chapter_count = service->chapter = 0;
                        service->position_valid = false;
                        service->chapter_pending = false;
                        service->busy = false;
                        if (service->view) {
                            service->error = true;
                            strlcpy(service->message, result.error, sizeof(service->message));
                            ++service->revision;
                        }
                        processed = service->generation.load();
                        LOGE("EpubReader", "Chapter metadata: %s", result.error);
                    } else {
                        if (service->metadata_book_key && service->metadata_book_key != result.book_key) {
                            service->position_valid = false;
                            service->chapter_pending = false;
                            service->chapter = 0;
                        }
                        service->metadata_book_key = result.book_key;
                        service->chapter_count = result.count;
                        service->chapter_titles = result.titles;
                        service->busy = service->view != nullptr;
                    }
                    ++service->chapters_revision;
                } else {
                    heap_caps_free(result.titles);
                }
                xSemaphoreGive(service->mutex);
            }
            display_manager_request_binding_refresh();
            continue;
        }
        const uint32_t generation = service->generation.load();
        if (!service->active.load() || generation == processed) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        char path[path_capacity];
        char font_name[32];
        uint16_t font_size = 0, width = 0, height = 0;
        bool run = false;
        if (xSemaphoreTake(service->mutex, portMAX_DELAY) == pdTRUE) {
            strlcpy(path, service->path, sizeof(path));
            strlcpy(font_name, service->font_name, sizeof(font_name));
            font_size = service->font_size;
            width = service->width;
            height = service->height;
            run = current(generation) && path[0] && service->chapter_state != EreaderMetadataState::Error;
            if (run) {
                service->working = true;
                strlcpy(service->working_book, service->selected, sizeof(service->working_book));
            }
            xSemaphoreGive(service->mutex);
        }
        processed = generation;
        if (run) run_sdk_book(generation, path, font_name, font_size, width, height);
        if (run && xSemaphoreTake(service->mutex, portMAX_DELAY) == pdTRUE) {
            service->working = false;
            service->working_book[0] = '\0';
            if (!service->metadata_pending &&
                (!service->active.load() || service->generation.load() == generation)) {
                service->busy = false;
                service->command = 0;
            }
            xSemaphoreGive(service->mutex);
        }
    }
}

bool start_service() {
    if (service) return true;
    void* memory = heap_caps_malloc(sizeof(Service), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!memory) return false;
    service = new (memory) Service{};
    service->mutex = xSemaphoreCreateMutex();
    // Let reader checkpoints yield to the equally prioritized presentation task.
    if (!service->mutex || !rtos_create_task_internal_stack(worker, "EpubReader", worker_stack_bytes, nullptr, 1,
                                                             &service->task, &service->task_alloc)) {
        if (service->mutex) vSemaphoreDelete(service->mutex);
        service->~Service(); heap_caps_free(service); service = nullptr;
        return false;
    }
    service_ready.store(true, std::memory_order_release);
    return true;
}

void parse(const JsonObject& button, uint8_t* data) {
    auto* config = new (data) Config{};
    strlcpy(config->style, button["widget_ereader_style"] | "", sizeof(config->style));
}

const char* validate(JsonObjectConst button) {
    if (button.containsKey("widget_ereader_style")) {
        if (!button["widget_ereader_style"].is<const char*>()) return "E-reader style must be a string";
        const JsonString value = button["widget_ereader_style"].as<JsonString>();
        if (value.size() >= CONFIG_LABEL_STYLE_MAX_LEN || strlen(value.c_str()) != value.size())
            return "E-reader style is too long or contains a null character";
        char tokens[CONFIG_LABEL_STYLE_MAX_LEN];
        strlcpy(tokens, value.c_str(), sizeof(tokens));
        char* save = nullptr;
        for (char* token = strtok_r(tokens, ";", &save); token; token = strtok_r(nullptr, ";", &save)) {
            while (*token == ' ') ++token;
            LabelStyle style{};
            label_style_parse(token, &style);
            if ((!strncmp(token, "font:", 5) && style.font_name[0]) ||
                (!strncmp(token, "font_size:", 10) && style.font_size)) continue;
            return "E-reader style supports only literal font and font_size";
        }
    }
    if ((button["actions"].is<JsonArrayConst>() && button["actions"].size()) ||
        (button["lp_actions"].is<JsonArrayConst>() && button["lp_actions"].size()))
        return "E-reader owns touch input; use a separate button for actions";
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
        if (!state->visible || !ereader_available() ||
            LV_ABS(point.x - state->pressed_at.x) > 10 || LV_ABS(point.y - state->pressed_at.y) > 10 ||
            point.x < bounds.x1 || point.x > bounds.x2 || point.y < bounds.y1 || point.y > bounds.y2) {
            LOGI("EpubReader", "tap gen=%lu x=%ld y=%ld split-x=%ld result=%s",
                 static_cast<unsigned long>(state->generation), static_cast<long>(point.x),
                 static_cast<long>(point.y), static_cast<long>(split), result);
            return;
        }
        bool owner = false;
        if (xSemaphoreTake(service->mutex, portMAX_DELAY) == pdTRUE) {
            owner = service->view == state;
            xSemaphoreGive(service->mutex);
        }
        if (!owner) return;
        result = ereader_navigate(point.x < split ? EreaderCommand::PreviousPage : EreaderCommand::NextPage)
            ? "accepted" : "not-ready-or-pending";
        LOGI("EpubReader", "tap gen=%lu x=%ld y=%ld split-x=%ld result=%s",
             static_cast<unsigned long>(state->generation), static_cast<long>(point.x),
             static_cast<long>(point.y), static_cast<long>(split), result);
    }
}

void measure_glyphs(lv_timer_t* timer) {
    auto* state = static_cast<State*>(lv_timer_get_user_data(timer));
    if (!state->visible || !ereader_available() || ota_activity_is_active()) return;
    if (xSemaphoreTake(service->mutex, 0) != pdTRUE) return;
    if (service->view == state) {
        process_font_request();
    }
    xSemaphoreGive(service->mutex);
}

void create(lv_obj_t* parent, const WidgetConfig* config, const ScreenButtonConfig*,
            const PadRect*, const UIScaleInfo*, WidgetState* runtime) {
    void* memory = heap_caps_malloc(sizeof(State), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    State* state = memory ? new (memory) State{} : nullptr;
    memcpy(runtime->data, &state, sizeof(state));
    if (!state) { LOGE("EpubReader", "Cannot allocate widget state"); return; }
    state->parent = parent;
    LabelStyle style{};
    label_style_parse(reinterpret_cast<const Config*>(config->data)->style, &style);
    const DeviceTypography& defaults = device_typography_get();
    const uint32_t original = pad_font_pixel_size(LV_FONT_DEFAULT);
    const uint32_t medium = pad_font_pixel_size(pad_get_scale_info().font_medium);
    const uint32_t requested = style.font_size ? style.font_size : defaults.base_size
        ? (original * defaults.base_size + medium / 2) / medium : original;
    strlcpy(state->font_name, style.font_name[0] ? style.font_name : defaults.font_name,
            sizeof(state->font_name));
    state->font_size = static_cast<uint16_t>(requested ? requested : 1);
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
    const int32_t page_height = height;
    if (width <= 8 || page_height <= line || width > INT16_MAX || page_height > INT16_MAX) {
        lv_label_set_text(state->message, "Reader needs more space");
        return;
    }
    lv_obj_set_size(state->image, width, page_height);
    lv_obj_set_pos(state->image, 0, 0);
    lv_obj_set_pos(state->message, 0, 0);
    lv_obj_set_width(state->message, width);
    lv_label_set_long_mode(state->message, LV_LABEL_LONG_WRAP);
    if (!ereader_available()) { lv_label_set_text(state->message, "Reader task allocation failed"); return; }
    if (xSemaphoreTake(service->mutex, portMAX_DELAY) != pdTRUE) return;
    const bool occupied = service->view && service->view != state;
    const bool file_busy = service->file_busy;
    xSemaphoreGive(service->mutex);
    if (occupied) { lv_label_set_text(state->message, "Only one E-reader can be visible"); return; }
    if (file_busy) { lv_label_set_text(state->message, "E-book library is busy"); return; }
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
    state->glyph_timer = lv_timer_create(measure_glyphs, 5, state);
    if (!state->glyph_timer) {
        lv_image_set_src(state->image, nullptr);
        heap_caps_free(state->pixels);
        state->pixels = nullptr;
        state->pixel_count = 0;
        lv_label_set_text(state->message, "Glyph timer allocation failed");
        return;
    }
    if (xSemaphoreTake(service->mutex, portMAX_DELAY) != pdTRUE) return;
    if (service->file_busy) {
        xSemaphoreGive(service->mutex);
        lv_timer_delete(state->glyph_timer);
        state->glyph_timer = nullptr;
        lv_image_set_src(state->image, nullptr);
        heap_caps_free(state->pixels);
        state->pixels = nullptr;
        state->pixel_count = 0;
        lv_label_set_text(state->message, "E-book library is busy");
        return;
    }
    service->view = state;
    strlcpy(service->font_name, state->font_name, sizeof(service->font_name));
    service->font_size = state->font_size;
    service->width = static_cast<uint16_t>(width);
    service->height = static_cast<uint16_t>(page_height);
    service->frame = nullptr;
    service->frame_revision = 0;
    service->applied_revision = 0;
    service->font_request = {};
    service->command = 0; service->ready = false;
    service->error = service->chapter_state == EreaderMetadataState::Error;
    service->page_available = false;
    service->busy = service->working || service->metadata_pending ||
        (service->selected[0] && !service->error);
    strlcpy(service->message, service->error ? service->chapter_error :
            service->selected[0] ? "Loading EPUB..." : "No E-book selected",
            sizeof(service->message));
    ++service->revision;
    state->generation = service->generation.fetch_add(1) + 1;
    state->revision = 0;
    service->active.store(true);
    ++service->chapters_revision;
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
    if (ereader_available() && xSemaphoreTake(service->mutex, portMAX_DELAY) == pdTRUE) {
        if (service->view == state) {
            service->view = nullptr;
            service->active.store(false);
            service->generation.fetch_add(1);
            service->frame = nullptr;
            service->ready = false;
            service->page_available = false;
            service->busy = service->working || service->metadata_pending;
            ++service->chapters_revision;
        }
        xSemaphoreGive(service->mutex);
    }
    display_manager_request_binding_refresh();
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
    if (!state || !state->visible || !ereader_available()) return;
    if (xSemaphoreTake(service->mutex, 0) != pdTRUE) return;
    uint32_t applied = 0;
    bool ready = false;
    if (service->view == state && state->generation != service->generation.load()) {
        state->generation = service->generation.load();
        if (state->pixels) {
            memset(state->pixels, 0xff, state->pixel_count * sizeof(uint16_t));
            lv_obj_invalidate(state->image);
        }
    }
    if (service->view == state && current(state->generation) && state->revision != service->revision) {
        if (service->frame && service->frame_revision == service->revision && state->pixels) {
            for (size_t i = 0; i < state->pixel_count; ++i) {
                const uint8_t value = service->frame[i];
                state->pixels[i] = lv_color_to_u16(lv_color_make(value, value, value));
            }
            lv_obj_invalidate(state->image);
            service->applied_revision = service->frame_revision;
            service->ready = true;
            service->page_available = true;
            service->busy = false;
        } else if (service->error && state->pixels) {
            memset(state->pixels, 0xff, state->pixel_count * sizeof(uint16_t));
            lv_obj_invalidate(state->image);
        }
        lv_label_set_text(state->message,
            service->error || !service->selected[0] ? service->message : "");
        state->revision = service->revision;
        applied = state->revision;
        ready = service->ready;
        if (displayManager) displayManager->interactionChanged();
    }
    xSemaphoreGive(service->mutex);
    if (applied) {
        display_manager_request_binding_refresh();
        LOGI("EpubReader", "ui-applied gen=%lu rev=%lu ready=%u",
             static_cast<unsigned long>(state->generation), static_cast<unsigned long>(applied), ready ? 1u : 0u);
    }
}

void cancel(WidgetState* runtime) { State* state = get(runtime); if (state) state->pressed = false; }
static const WidgetPreview ereader_preview = {"E-reader", "menu_book"};

#if HAS_MCP
void describe(JsonObject& out) {
    JsonObject field = out.createNestedArray("config_fields").createNestedObject();
    field["name"] = "widget_ereader_style";
    field["type"] = "string";
    field["max_length"] = CONFIG_LABEL_STYLE_MAX_LEN - 1;
    field["description"] = "Text style: literal font and font_size; empty inherits device typography.";
    out["note"] = "Supports DRM-free, reflowable EPUB files. Open an E-book with an action; tap left/right to turn pages.";
}
#endif
}

bool ereader_valid_book_name(const char* name) {
    if (!name) return false;
    const size_t length = strlen(name);
    if (length < 6 || length > 63 || strcasecmp(name + length - 5, ".epub") ||
        name[length - 1] == ' ' || name[length - 1] == '.') return false;
    const auto* bytes = reinterpret_cast<const unsigned char*>(name);
    for (size_t i = 0; i < length;) {
        const unsigned char c = bytes[i++];
        if (c < 0x80) {
            if (c < 0x20 || c == 0x7f || strchr("/\\:*?\"<>|", c)) return false;
            continue;
        }
        unsigned remaining;
        uint32_t code, minimum;
        if (c >= 0xc2 && c <= 0xdf) { remaining = 1; code = c & 0x1f; minimum = 0x80; }
        else if (c >= 0xe0 && c <= 0xef) { remaining = 2; code = c & 0x0f; minimum = 0x800; }
        else if (c >= 0xf0 && c <= 0xf4) { remaining = 3; code = c & 0x07; minimum = 0x10000; }
        else return false;
        if (remaining > length - i) return false;
        while (remaining--) {
            const unsigned char next = bytes[i++];
            if ((next & 0xc0) != 0x80) return false;
            code = (code << 6) | (next & 0x3f);
        }
        if (code < minimum || code > 0x10ffff || (code >= 0xd800 && code <= 0xdfff) ||
            (code >= 0x80 && code <= 0x9f)) return false;
    }
    return true;
}

bool ereader_open(const char* name) {
    using namespace epub_reader_impl;
    if (!ereader_valid_book_name(name) || !ereader_available()) return false;
    if (xSemaphoreTake(service->mutex, portMAX_DELAY) != pdTRUE) return false;
    if (service->busy || service->file_busy || ota_activity_is_active()) {
        xSemaphoreGive(service->mutex);
        return false;
    }
    if (service->ready && !strcmp(service->selected, name)) {
        xSemaphoreGive(service->mutex);
        return true;
    }
    if (strcmp(service->selected, name)) {
        service->position_valid = false;
        service->chapter_pending = false;
        service->chapter = 0;
        service->metadata_book_key = 0;
    }
    service->chapters_available = false;
    heap_caps_free(service->chapter_titles);
    service->chapter_titles = nullptr;
    service->chapter_count = 0;
    ++service->chapters_revision;
    strlcpy(service->selected, name, sizeof(service->selected));
    snprintf(service->path, sizeof(service->path), "/books/%s", name);
    service->command = 0;
    service->ready = false;
    service->page_available = false;
    service->error = false;
    service->frame = nullptr;
    service->frame_revision = 0;
    service->applied_revision = 0;
    service->font_request = {};
    service->busy = true;
    service->metadata_pending = true;
    service->chapter_state = EreaderMetadataState::Loading;
    service->chapter_error[0] = '\0';
    service->book_generation.fetch_add(1);
    strlcpy(service->message, service->view ? "Loading EPUB..." : "", sizeof(service->message));
    ++service->revision;
    service->generation.fetch_add(1);
    xSemaphoreGive(service->mutex);
    display_manager_request_binding_refresh();
    return true;
}

bool ereader_navigate(EreaderCommand command) {
    using namespace epub_reader_impl;
    if (!ereader_available() || xSemaphoreTake(service->mutex, portMAX_DELAY) != pdTRUE) return false;
    if (!service->view || !service->ready || service->busy || service->file_busy || ota_activity_is_active()) {
        xSemaphoreGive(service->mutex);
        return false;
    }
    switch (command) {
        case EreaderCommand::PreviousPage: service->command = -1; break;
        case EreaderCommand::NextPage: service->command = 1; break;
        case EreaderCommand::PreviousChapter: service->command = -2; break;
        case EreaderCommand::NextChapter: service->command = 2; break;
        default: xSemaphoreGive(service->mutex); return false;
    }
    service->busy = true;
    service->ready = false;
    service->page_available = false;
    service->error = false;
    strlcpy(service->message, "Loading EPUB...", sizeof(service->message));
    ++service->revision;
    xSemaphoreGive(service->mutex);
    display_manager_request_binding_refresh();
    return true;
}

bool ereader_init() { return epub_reader_impl::start_service(); }
bool ereader_available() { return epub_reader_impl::service_ready.load(std::memory_order_acquire); }

bool ereader_open_chapter(uint32_t chapter, const char* expected_book) {
    using namespace epub_reader_impl;
    if (!ereader_available() || !chapter ||
        xSemaphoreTake(service->mutex, portMAX_DELAY) != pdTRUE) return false;
    const bool accepted = !service->busy && !service->file_busy && !ota_activity_is_active() &&
        service->chapters_available && (!expected_book || !strcmp(service->selected, expected_book)) &&
        chapter <= service->chapter_count && (!service->view || service->ready);
    if (accepted) {
        service->requested_chapter = chapter;
        service->chapter_pending = true;
        service->chapter = chapter;
        service->page_available = false;
        service->error = false;
        service->message[0] = '\0';
        if (service->view) {
            service->command = 3;
            service->busy = true;
            service->ready = false;
            strlcpy(service->message, "Loading EPUB...", sizeof(service->message));
        }
        ++service->revision;
    }
    xSemaphoreGive(service->mutex);
    if (accepted) display_manager_request_binding_refresh();
    return accepted;
}

bool ereader_chapter_title(const char* book, uint32_t chapter, char* out, size_t size) {
    using namespace epub_reader_impl;
    if (!book || !out || !size || !ereader_available() ||
        xSemaphoreTake(service->mutex, portMAX_DELAY) != pdTRUE) return false;
    const bool valid = service->chapters_available && !strcmp(service->selected, book) &&
        chapter > 0 && chapter <= service->chapter_count;
    if (valid) strlcpy(out, service->chapter_titles ? service->chapter_titles[chapter - 1] : "", size);
    xSemaphoreGive(service->mutex);
    return valid;
}

void ereader_snapshot(EreaderSnapshot* snapshot) {
    using namespace epub_reader_impl;
    if (!snapshot) return;
    *snapshot = {};
    if (!ereader_available() || xSemaphoreTake(service->mutex, portMAX_DELAY) != pdTRUE) return;
    strlcpy(snapshot->book, service->selected, sizeof(snapshot->book));
    snapshot->visible = service->view != nullptr;
    snapshot->failed = snapshot->visible && service->error;
    snapshot->loading = snapshot->visible && !snapshot->failed && service->busy;
    snapshot->chapters_available = service->chapters_available;
    snapshot->chapter = service->chapter;
    snapshot->chapter_count = service->chapter_count;
    snapshot->page_available = snapshot->visible && service->page_available;
    snapshot->page = service->page;
    snapshot->page_count = service->page_count;
    snapshot->chapter_state = service->chapter_state;
    strlcpy(snapshot->chapter_error, service->chapter_error, sizeof(snapshot->chapter_error));
    if (snapshot->failed) strlcpy(snapshot->error, service->message, sizeof(snapshot->error));
    else if (!snapshot->loading && service->message[0] && snapshot->visible &&
             service->selected[0])
        strlcpy(snapshot->warning, service->message, sizeof(snapshot->warning));
    xSemaphoreGive(service->mutex);
}

uint32_t ereader_chapters_revision() {
    return ereader_available() ? epub_reader_impl::service->chapters_revision.load() : 0;
}

bool ereader_file_begin(const char* name, bool deleting) {
    using namespace epub_reader_impl;
    if (!ereader_available() || !ereader_valid_book_name(name) ||
        xSemaphoreTake(service->mutex, portMAX_DELAY) != pdTRUE) return false;
#if USE_SD_STORAGE
    const bool same_book = !strcasecmp(service->selected, name);
    const bool owned_book = service->working && !strcasecmp(service->working_book, name);
#else
    const bool same_book = !strcmp(service->selected, name);
    const bool owned_book = service->working && !strcmp(service->working_book, name);
#endif
    const bool in_use = owned_book || (same_book && (service->view || service->busy));
    const bool accepted = !service->file_busy && !(deleting && in_use) && !ota_activity_is_active();
    if (accepted) service->file_busy = true;
    xSemaphoreGive(service->mutex);
    return accepted;
}

bool ereader_catalog_begin() {
    using namespace epub_reader_impl;
    if (!ereader_available() || xSemaphoreTake(service->mutex, portMAX_DELAY) != pdTRUE) return false;
    const bool accepted = !service->file_busy && !ota_activity_is_active();
    if (accepted) service->file_busy = true;
    xSemaphoreGive(service->mutex);
    return accepted;
}

void ereader_file_removed(const char* name) {
    using namespace epub_reader_impl;
    if (!ereader_available() || xSemaphoreTake(service->mutex, portMAX_DELAY) != pdTRUE) return;
#if USE_SD_STORAGE
    const bool same_book = !strcasecmp(service->selected, name);
#else
    const bool same_book = !strcmp(service->selected, name);
#endif
    if (same_book) {
        service->position_valid = false;
        service->chapter_pending = false;
        service->chapters_available = false;
        heap_caps_free(service->chapter_titles);
        service->chapter_titles = nullptr;
        service->chapter = service->chapter_count = 0;
        service->page_available = false;
        service->chapter_state = EreaderMetadataState::Idle;
        service->chapter_error[0] = '\0';
        service->book_generation.fetch_add(1);
        ++service->chapters_revision;
    }
    xSemaphoreGive(service->mutex);
    display_manager_request_binding_refresh();
}

void ereader_file_end() {
    using namespace epub_reader_impl;
    if (!ereader_available() || xSemaphoreTake(service->mutex, portMAX_DELAY) != pdTRUE) return;
    service->file_busy = false;
    xSemaphoreGive(service->mutex);
}

static const WidgetType ereader_widget_type{
    "ereader", epub_reader_impl::parse, epub_reader_impl::create, nullptr,
    epub_reader_impl::destroy, epub_reader_impl::tick, nullptr, false,
#if HAS_MCP
    epub_reader_impl::describe,
#else
    nullptr,
#endif
    epub_reader_impl::show, epub_reader_impl::hide, epub_reader_impl::validate,
    &epub_reader_impl::ereader_preview, epub_reader_impl::cancel, nullptr, nullptr, nullptr,
    nullptr, nullptr, nullptr, false
};
static struct EpubReaderRegistration {
    EpubReaderRegistration() { widget_register(&ereader_widget_type); }
} epub_reader_registration;

#endif
