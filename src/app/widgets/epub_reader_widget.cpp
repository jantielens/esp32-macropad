#include "../board_config.h"

#if HAS_DISPLAY && HAS_EPUB_READER

#include "widget.h"
#include "epub_pagination.h"
#include "../display_manager.h"
#include "../log_manager.h"
#include "../ota_activity.h"
#include "../rtos_task_utils.h"
#include "../storage.h"
#include <FreeInkBook.h>
#include <epub/XmlSax.h>
#include <esp_heap_caps.h>
#include <esp_timer.h>
#include <atomic>
#include <new>

namespace epub_reader_impl {
using namespace freeink::book;

constexpr size_t path_capacity = CONFIG_LABEL_MAX_LEN;
constexpr size_t page_capacity = 4096;
constexpr size_t book_arena_size = 512 * 1024;
constexpr size_t scratch_arena_size = 128 * 1024;
constexpr size_t max_chapter_size = 2 * 1024 * 1024;
constexpr size_t max_pages = 8192;
constexpr uint32_t worker_stack_bytes = 12 * 1024;

struct Config { char path[path_capacity]; };
static_assert(sizeof(Config) <= WIDGET_CONFIG_MAX_BYTES, "EPUB reader config too large");

struct Service {
    SemaphoreHandle_t mutex = nullptr;
    TaskHandle_t task = nullptr;
    RtosTaskInternalAlloc task_alloc{};
    std::atomic<uint32_t> generation{0};
    std::atomic<bool> active{false};
    char path[path_capacity]{};
    char page[page_capacity]{};
    char message[128]{};
    uint32_t revision = 0;
    int8_t command = 0;
    EpubPageMetrics metrics{};
    EpubGlyphCache* glyph_request = nullptr;
    uint32_t glyph_generation = 0;
    uint32_t glyph_ota_epoch = 0;
    size_t glyph_cursor = 0;
    bool glyph_done = false;
    bool glyph_interrupted = false;
    size_t min_internal_largest = SIZE_MAX;
    size_t min_psram_largest = SIZE_MAX;
    bool ready = false;
};

struct State {
    lv_obj_t* parent = nullptr;
    lv_obj_t* label = nullptr;
    lv_obj_t* message = nullptr;
    const lv_font_t* font = nullptr;
    lv_timer_t* glyph_timer = nullptr;
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

uint32_t publish(uint32_t generation, const char* page, const char* message, bool ready) {
    if (xSemaphoreTake(service->mutex, portMAX_DELAY) != pdTRUE) return 0;
    uint32_t revision = 0;
    if (current(generation)) {
        strlcpy(service->page, page ? page : "", sizeof(service->page));
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
        if (ota_activity_is_active()) { busy_ = true; return true; }
        return !current(generation_);
    }
    bool busy() const { return busy_; }
    void clearBusy() { busy_ = false; }

private:
    const char* path_;
    uint32_t generation_;
    uint64_t size_ = 0;
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

enum class PositionLoad { Missing, Valid, Invalid, Busy };

PositionLoad load_position(const char* path, uint32_t size, Position* position) {
    const StorageMutationGuard guard;
    if (!guard) return PositionLoad::Busy;
    char name[48];
    position_path(path, name, sizeof(name));
    if (!Storage.exists(name)) return PositionLoad::Missing;
    File file = Storage.open(name, "r");
    if (!file) return PositionLoad::Invalid;
    const size_t count = file.read(reinterpret_cast<uint8_t*>(position), sizeof(*position));
    file.close();
    if (count != sizeof(*position) || position->magic != position_magic ||
        position->size != size || !memchr(position->path, 0, sizeof(position->path)) ||
        strcmp(position->path, path)) return PositionLoad::Invalid;
    return PositionLoad::Valid;
}

bool save_position(const char* path, uint32_t size, uint32_t spine, uint32_t offset) {
    Position position{};
    position.magic = position_magic;
    position.size = size;
    position.spine = spine;
    position.offset = offset;
    strlcpy(position.path, path, sizeof(position.path));
    char name[48];
    position_path(path, name, sizeof(name));
    return storage_write_atomic(name, reinterpret_cast<const uint8_t*>(&position), sizeof(position));
}

bool block_element(const char* name) {
    const char* colon = strrchr(name, ':');
    if (colon) name = colon + 1;
    return !strcmp(name, "p") || !strcmp(name, "div") || !strcmp(name, "br") ||
           !strcmp(name, "li") || !strcmp(name, "h1") || !strcmp(name, "h2") ||
           !strcmp(name, "h3") || !strcmp(name, "h4") || !strcmp(name, "blockquote");
}

class TextHandler : public XmlHandler {
public:
    TextHandler(char* text, size_t capacity) : text_(text), capacity_(capacity) {}
    void onStartElement(const char* name, const char**) override {
        const char* local = strrchr(name, ':');
        local = local ? local + 1 : name;
        if (!strcmp(local, "body")) body_ = true;
        if (!strcmp(local, "script") || !strcmp(local, "style")) ++suppressed_;
        if (body_ && !suppressed_ && block_element(local)) newline();
    }
    void onEndElement(const char* name) override {
        const char* local = strrchr(name, ':');
        local = local ? local + 1 : name;
        if ((!strcmp(local, "script") || !strcmp(local, "style")) && suppressed_) --suppressed_;
        if (body_ && !suppressed_ && block_element(local)) newline();
        if (!strcmp(local, "body")) body_ = false;
    }
    void onText(const char* text, int length) override {
        if (!body_ || suppressed_) return;
        for (int i = 0; i < length && !overflow_; ++i) {
            const char c = text[i];
            if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
                space_ = used_ && text_[used_ - 1] != '\n';
            } else {
                if (space_) append(' ');
                space_ = false;
                append(c);
            }
        }
    }
    bool overflow() const { return overflow_; }
    size_t size() const { return used_; }
    void finish() { text_[used_] = 0; }
private:
    void append(char c) {
        if (used_ + 1 >= capacity_) { overflow_ = true; stopParse = true; return; }
        text_[used_++] = c;
    }
    void newline() {
        space_ = false;
        if (used_ && text_[used_ - 1] != '\n') append('\n');
    }
    char* text_;
    size_t capacity_;
    size_t used_ = 0;
    unsigned suppressed_ = 0;
    bool body_ = false;
    bool space_ = false;
    bool overflow_ = false;
};

struct Chapter {
    char* text = nullptr;
    uint32_t* pages = nullptr;
    char* output = nullptr;
    EpubGlyphCache* glyphs = nullptr;
    size_t length = 0;
    uint32_t count = 0;
    ~Chapter() { heap_caps_free(text); heap_caps_free(pages); heap_caps_free(output); heap_caps_free(glyphs); }
    void clear() {
        heap_caps_free(text); text = nullptr;
        heap_caps_free(pages); pages = nullptr;
        heap_caps_free(glyphs); glyphs = nullptr;
        length = 0; count = 0;
    }
};

const char* measure_chapter_glyphs(Source& source, uint32_t generation, Chapter& chapter) {
    chapter.glyphs = static_cast<EpubGlyphCache*>(
        heap_caps_calloc(1, sizeof(EpubGlyphCache), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!chapter.glyphs) return "Not enough PSRAM for glyph metrics";
    size_t offset = 0;
    size_t checkpoint = 1024;
    uint32_t previous = 0;
    while (offset < chapter.length) {
        const uint32_t code = epub_decode_utf8(chapter.text, chapter.length, offset);
        if (code == '\n') { previous = 0; continue; }
        if (!epub_glyph_entry(*chapter.glyphs, code, 0, true) ||
            (previous && !epub_glyph_entry(*chapter.glyphs, previous, code, true)))
            return "Chapter exceeds glyph-pair limit";
        previous = code;
        if (offset >= checkpoint) {
            if (source.shouldStop()) return "Reader interrupted";
            taskYIELD();
            checkpoint = offset + 1024;
        }
    }

    if (source.shouldStop()) return "Reader interrupted";
    if (xSemaphoreTake(service->mutex, portMAX_DELAY) != pdTRUE) return "Glyph metrics unavailable";
    if (!current(generation)) {
        xSemaphoreGive(service->mutex);
        return "Reader closed";
    }
    service->glyph_request = chapter.glyphs;
    service->glyph_generation = generation;
    const uint32_t ota_epoch = ota_activity_epoch();
    service->glyph_ota_epoch = ota_epoch;
    service->glyph_cursor = 0;
    service->glyph_done = false;
    service->glyph_interrupted = false;
    xSemaphoreGive(service->mutex);

    bool done = false, interrupted = false;
    while (current(generation) && !source.shouldStop() && ota_activity_epoch() == ota_epoch && !done && !interrupted) {
        if (xSemaphoreTake(service->mutex, portMAX_DELAY) == pdTRUE) {
            done = service->glyph_request == chapter.glyphs && service->glyph_done;
            interrupted = service->glyph_interrupted;
            xSemaphoreGive(service->mutex);
        }
        if (!done) vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (xSemaphoreTake(service->mutex, portMAX_DELAY) == pdTRUE) {
        if (service->glyph_request == chapter.glyphs) service->glyph_request = nullptr;
        xSemaphoreGive(service->mutex);
    }
    return done && !interrupted && ota_activity_epoch() == ota_epoch ? nullptr : "Reader interrupted";
}

const char* load_chapter(Source& source, Book& book, Arena& scratch, uint32_t index,
                         EpubPageMetrics& metrics, uint32_t generation, Chapter& chapter) {
    chapter.clear();
    const ManifestItem* item = book.spineItem(index);
    if (!item || !item->href || !item->mediaType ||
        strcmp(item->mediaType, "application/xhtml+xml")) return "Unsupported chapter format";
    const ZipEntry* entry = book.zip().find(item->href);
    if (!entry) return "Chapter not found in EPUB";
    LOGI("EpubReader", "chapter spine0=%lu href-hash=%08lx zip=%lu/%lu method=%u",
         static_cast<unsigned long>(index), static_cast<unsigned long>(path_hash(item->href)),
         static_cast<unsigned long>(entry->compressedSize), static_cast<unsigned long>(entry->uncompressedSize),
         static_cast<unsigned>(entry->method));
    if (entry->uncompressedSize > max_chapter_size) return "Chapter exceeds 2 MiB limit";
    const size_t capacity = static_cast<size_t>(entry->uncompressedSize) + 1024;
    chapter.text = static_cast<char*>(heap_caps_malloc(capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    chapter.pages = static_cast<uint32_t*>(heap_caps_malloc(max_pages * sizeof(uint32_t),
                                                             MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!chapter.text || !chapter.pages) return "Not enough PSRAM for chapter";
    TextHandler handler(chapter.text, capacity);
    const size_t mark = scratch.mark();
    XmlSaxSession session;
    BookStatus status = session.open(source, *entry, scratch, handler, true);
    bool end = false;
    uint32_t chunks = 0;
    const int64_t parse_started = esp_timer_get_time();
    while (status == BookStatus::Ok && !end && !handler.stopParse && current(generation)) {
        status = session.feedChunk(&end);
        ++chunks;
        taskYIELD();
    }
    const uint64_t parsed_bytes = session.bytesConsumed();
    LOGI("EpubReader", "parse spine0=%lu status=%s end=%u chunks=%lu filtered-bytes=%lu text-bytes=%u ms=%lu",
         static_cast<unsigned long>(index), bookStatusName(status), end ? 1u : 0u,
         static_cast<unsigned long>(chunks), static_cast<unsigned long>(parsed_bytes),
         static_cast<unsigned>(handler.size()),
         static_cast<unsigned long>((esp_timer_get_time() - parse_started) / 1000));
    session.close();
    scratch.release(mark);
    if (!current(generation)) return "Reader closed";
    if (handler.overflow()) return "Chapter text exceeds memory limit";
    if (status != BookStatus::Ok) return bookStatusName(status);
    handler.finish();
    chapter.length = handler.size();
    if (chapter.length) {
        const int64_t glyph_started = esp_timer_get_time();
        const char* error = measure_chapter_glyphs(source, generation, chapter);
        if (error) return error;
        metrics.glyphs = chapter.glyphs;
        LOGI("EpubReader", "glyphs spine0=%lu pairs=%u ms=%lu",
             static_cast<unsigned long>(index), static_cast<unsigned>(chapter.glyphs->count),
             static_cast<unsigned long>((esp_timer_get_time() - glyph_started) / 1000));
    }
    size_t offset = 0;
    const int64_t pagination_started = esp_timer_get_time();
    while (offset < chapter.length) {
        if (source.shouldStop()) return "Reader interrupted";
        if (chapter.count == max_pages) return "Chapter exceeds page limit";
        chapter.pages[chapter.count++] = static_cast<uint32_t>(offset);
        bool overflow;
        const size_t next = epub_make_page(chapter.text, chapter.length, offset, metrics, nullptr, 0, &overflow);
        if (next <= offset) return "Cannot paginate chapter";
        offset = next;
        if ((chapter.count & 31u) == 0) taskYIELD();
    }
    LOGI("EpubReader", "paginate spine0=%lu pages=%lu text-bytes=%u width=%u rows=%u ms=%lu",
         static_cast<unsigned long>(index), static_cast<unsigned long>(chapter.count),
         static_cast<unsigned>(chapter.length), static_cast<unsigned>(metrics.line_width),
         static_cast<unsigned>(metrics.rows),
         static_cast<unsigned long>((esp_timer_get_time() - pagination_started) / 1000));
    return nullptr;
}

void run_book(uint32_t generation, const char* path, EpubPageMetrics metrics) {
    const int64_t started = esp_timer_get_time();
    log_memory("open start", started);
    Source source(path, generation);
    if (!source.open()) {
        LOGW("EpubReader", "book open failed gen=%lu busy=%u", static_cast<unsigned long>(generation),
             source.busy() ? 1u : 0u);
        publish(generation, nullptr, source.busy() ? "Storage busy; reopen reader" : "Cannot open EPUB", false);
        return;
    }
    LOGI("EpubReader", "book gen=%lu path-hash=%08lx bytes=%lu",
         static_cast<unsigned long>(generation), static_cast<unsigned long>(path_hash(path)),
         static_cast<unsigned long>(source.size()));
    void* book_memory = heap_caps_malloc(book_arena_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    void* scratch_memory = heap_caps_malloc(scratch_arena_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!book_memory || !scratch_memory) {
        heap_caps_free(book_memory); heap_caps_free(scratch_memory);
        publish(generation, nullptr, "Not enough PSRAM to open EPUB", false);
        return;
    }
    {
        Arena book_arena(book_memory, book_arena_size), scratch(scratch_memory, scratch_arena_size);
        Book book;
        const BookStatus status = book.open(source, book_arena, scratch);
        LOGI("EpubReader", "book parse gen=%lu status=%s spine-count=%u ms=%lu",
             static_cast<unsigned long>(generation), bookStatusName(status), static_cast<unsigned>(book.spineCount()),
             static_cast<unsigned long>((esp_timer_get_time() - started) / 1000));
        if (status != BookStatus::Ok) {
            publish(generation, nullptr, source.busy() ? "Storage busy; reopen reader" : bookStatusName(status), false);
        } else if (!book.spineCount()) {
            publish(generation, nullptr, "EPUB has no chapters", false);
        } else {
            Position position{};
            const PositionLoad position_load = load_position(path, static_cast<uint32_t>(source.size()), &position);
            bool resumed = position_load == PositionLoad::Valid && position.spine < book.spineCount();
            const char* position_state = position_load == PositionLoad::Missing ? "missing" :
                                         position_load == PositionLoad::Valid ? "valid" :
                                         position_load == PositionLoad::Invalid ? "invalid" : "busy";
            LOGI("EpubReader", "resume gen=%lu state=%s use=%u spine0=%lu offset=%lu",
                 static_cast<unsigned long>(generation), position_state, resumed ? 1u : 0u,
                 static_cast<unsigned long>(position.spine), static_cast<unsigned long>(position.offset));
            const char* position_warning = (position_load == PositionLoad::Invalid ||
                                            (position_load == PositionLoad::Valid && !resumed))
                                               ? "Saved position invalid; restarted" : "";
            if (position_load == PositionLoad::Busy)
                publish(generation, nullptr, "Storage busy; reopen reader", false);
            uint32_t spine = resumed ? position.spine : 0;
            uint32_t previous_spine = spine;
            uint32_t page = 0;
            Chapter chapter;
            bool need_load = true, last_page = false, first = true;
            bool report_page = true;
            int64_t page_started = started;
            if (position_load != PositionLoad::Busy)
                chapter.output = static_cast<char*>(heap_caps_malloc(page_capacity, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (!chapter.output && position_load != PositionLoad::Busy)
                publish(generation, nullptr, "Not enough PSRAM for page", false);
            while (chapter.output && current(generation)) {
                if (ota_activity_is_active()) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
                if (need_load) {
                    const int64_t chapter_started = esp_timer_get_time();
                    if (!first) { page_started = chapter_started; report_page = true; }
                    publish(generation, nullptr, "Loading EPUB...", false);
                    source.clearBusy();
                    const char* error = load_chapter(source, book, scratch, spine, metrics, generation, chapter);
                    if (error) {
                        LOGW("EpubReader", "chapter failed gen=%lu spine0=%lu error=%s busy=%u",
                             static_cast<unsigned long>(generation), static_cast<unsigned long>(spine),
                             error, source.busy() ? 1u : 0u);
                        if (current(generation))
                            publish(generation, nullptr, source.busy() ? "Storage busy; reopen reader" : error, false);
                        break;
                    }
                    log_memory(first ? "first chapter" : "chapter change", chapter_started);
                    if (!chapter.count) {
                        if (first && resumed) {
                            resumed = false;
                            position_warning = "Saved position invalid; restarted";
                        }
                        const uint32_t empty_spine = spine;
                        const EpubEmptyChapter next = epub_skip_empty_chapter(
                            spine, book.spineCount(), last_page, first, previous_spine);
                        LOGI("EpubReader", "empty spine0=%lu direction=%s result=%s target-spine0=%lu",
                             static_cast<unsigned long>(empty_spine), last_page ? "previous" : "next",
                             next == EpubEmptyChapter::Continue ? "skip" :
                             next == EpubEmptyChapter::Restore ? "restore" : "no-text",
                             static_cast<unsigned long>(spine));
                        if (next == EpubEmptyChapter::Continue) continue;
                        if (next == EpubEmptyChapter::Restore) { last_page = !last_page; continue; }
                        publish(generation, nullptr, "No readable chapter text", false);
                        break;
                    }
                    page = last_page ? chapter.count - 1 : 0;
                    if (first && resumed) {
                        if (position.offset >= chapter.length)
                            position_warning = "Saved position invalid; restarted";
                        else
                            while (page + 1 < chapter.count && chapter.pages[page + 1] <= position.offset) ++page;
                    }
                    need_load = false;
                    first = false;
                }
                bool overflow;
                const size_t page_end = epub_make_page(chapter.text, chapter.length, chapter.pages[page], metrics,
                                                       chapter.output, page_capacity, &overflow);
                if (overflow) { publish(generation, nullptr, "Page exceeds text buffer", false); break; }
                const bool saved = save_position(path, static_cast<uint32_t>(source.size()), spine, chapter.pages[page]);
                if (!saved) LOGW("EpubReader", "Could not save reading position");
                const uint32_t revision = publish(generation, chapter.output, saved ? position_warning : "Position not saved", true);
                LOGI("EpubReader", "page gen=%lu rev=%lu spine0=%lu page1=%lu/%lu text=[%lu,%lu) index-end=%lu saved=%u",
                     static_cast<unsigned long>(generation), static_cast<unsigned long>(revision),
                     static_cast<unsigned long>(spine), static_cast<unsigned long>(page + 1),
                     static_cast<unsigned long>(chapter.count), static_cast<unsigned long>(chapter.pages[page]),
                     static_cast<unsigned long>(page_end),
                     static_cast<unsigned long>(page + 1 < chapter.count ? chapter.pages[page + 1] : chapter.length),
                     saved ? 1u : 0u);
                if (report_page) { log_memory("page ready", page_started); report_page = false; }
                int command = 0;
                while (current(generation) && !command) {
                    if (xSemaphoreTake(service->mutex, portMAX_DELAY) == pdTRUE) {
                        if (current(generation)) { command = service->command; service->command = 0; }
                        xSemaphoreGive(service->mutex);
                    }
                    if (!command) vTaskDelay(pdMS_TO_TICKS(40));
                }
                if (!current(generation)) break;
                LOGI("EpubReader", "turn gen=%lu direction=%s from-spine0=%lu page1=%lu/%lu",
                     static_cast<unsigned long>(generation), command > 0 ? "next" : "previous",
                     static_cast<unsigned long>(spine), static_cast<unsigned long>(page + 1),
                     static_cast<unsigned long>(chapter.count));
                if (command > 0 && page + 1 < chapter.count) ++page;
                else if (command < 0 && page > 0) --page;
                else if (command > 0 && spine + 1 < book.spineCount()) {
                    previous_spine = spine;
                    ++spine; need_load = true; last_page = false;
                } else if (command < 0 && spine > 0) {
                    previous_spine = spine;
                    --spine; need_load = true; last_page = true;
                } else LOGI("EpubReader", "turn at book boundary; page unchanged");
            }
            LOGI("EpubReader", "arena high-water: book=%u/%u scratch=%u/%u",
                 static_cast<unsigned>(book_arena.highWater()), static_cast<unsigned>(book_arena.capacity()),
                 static_cast<unsigned>(scratch.highWater()), static_cast<unsigned>(scratch.capacity()));
        }
    }
    heap_caps_free(scratch_memory);
    heap_caps_free(book_memory);
}

void worker(void*) {
    uint32_t processed = 0;
    for (;;) {
        const uint32_t generation = service->generation.load();
        if (!service->active.load() || generation == processed) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        char path[path_capacity];
        EpubPageMetrics metrics{};
        if (xSemaphoreTake(service->mutex, portMAX_DELAY) == pdTRUE) {
            strlcpy(path, service->path, sizeof(path));
            metrics = service->metrics;
            xSemaphoreGive(service->mutex);
        }
        processed = generation;
        if (current(generation)) run_book(generation, path, metrics);
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
    EpubGlyphCache* cache = service->glyph_request;
    if (cache && service->glyph_generation == state->generation && !service->glyph_done) {
        const int64_t started = esp_timer_get_time();
        unsigned measured = 0;
        while (service->glyph_cursor < epub_glyph_slots && measured < 32 &&
               esp_timer_get_time() - started < 3000 && !ota_activity_is_active() &&
               ota_activity_epoch() == service->glyph_ota_epoch) {
            EpubGlyphEntry& entry = cache->entries[service->glyph_cursor++];
            if (!entry.key) continue;
            const uint32_t code = static_cast<uint32_t>(entry.key >> 21);
            const uint32_t next = static_cast<uint32_t>(entry.key & 0x1fffffu);
            entry.width = lv_font_get_glyph_width(state->font, code, next);
            ++measured;
        }
        if (ota_activity_is_active() || ota_activity_epoch() != service->glyph_ota_epoch)
            service->glyph_interrupted = true;
        else if (service->glyph_cursor == epub_glyph_slots) service->glyph_done = true;
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
    strlcpy(state->path, reinterpret_cast<const Config*>(config->data)->path, sizeof(state->path));
    state->label = lv_label_create(parent);
    state->message = lv_label_create(parent);
    if (!state->label || !state->message) { LOGE("EpubReader", "Cannot create reader labels"); return; }
    state->font = lv_theme_get_font_normal(parent);
    lv_obj_set_style_text_font(state->label, state->font, 0);
    lv_obj_set_style_text_font(state->message, state->font, 0);
    lv_obj_set_style_text_align(state->label, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_text_letter_space(state->label, 0, 0);
    lv_obj_set_style_text_line_space(state->label, 0, 0);
    lv_label_set_long_mode(state->label, LV_LABEL_LONG_CLIP);
    lv_obj_clear_flag(state->label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(state->message, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(parent, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(parent, input, LV_EVENT_ALL, state);
}

void show(WidgetState* runtime) {
    State* state = get(runtime);
    if (!state || !state->label || !state->message) return;
    if (state->visible) return;
    state->visible = true;
    lv_obj_update_layout(state->parent);
    const int32_t width = lv_obj_get_content_width(state->parent);
    const int32_t height = lv_obj_get_content_height(state->parent);
    const int32_t line = lv_font_get_line_height(state->font);
    const int32_t rows = (height - line) / line;
    lv_obj_set_size(state->label, width, height - line);
    lv_obj_set_pos(state->label, 0, 0);
    lv_obj_set_pos(state->message, 0, height - line);
    if (width <= 2 || rows < 1) { lv_label_set_text(state->message, "Reader needs more space"); return; }
    if (!start_service()) { lv_label_set_text(state->message, "Reader task allocation failed"); return; }
    if (service->active.load()) { lv_label_set_text(state->message, "Only one EPUB reader can be active"); return; }
    state->glyph_timer = lv_timer_create(measure_glyphs, 30, state);
    if (!state->glyph_timer) { lv_label_set_text(state->message, "Glyph timer allocation failed"); return; }
    if (xSemaphoreTake(service->mutex, portMAX_DELAY) != pdTRUE) return;
    strlcpy(service->path, state->path, sizeof(service->path));
    service->metrics.line_width = static_cast<uint16_t>(width - 2);
    service->metrics.rows = static_cast<uint16_t>(rows);
    service->metrics.glyphs = nullptr;
    service->command = 0; service->ready = false;
    service->page[0] = 0;
    strlcpy(service->message, "Loading EPUB...", sizeof(service->message));
    ++service->revision;
    state->generation = service->generation.fetch_add(1) + 1;
    state->revision = 0;
    service->active.store(true);
    xSemaphoreGive(service->mutex);
    LOGI("EpubReader", "show gen=%lu content=%ldx%ld line-height=%ld rows=%ld",
         static_cast<unsigned long>(state->generation), static_cast<long>(width), static_cast<long>(height),
         static_cast<long>(line), static_cast<long>(rows));
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
    if (state->label) lv_obj_delete(state->label);
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
        lv_label_set_text(state->label, service->page);
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
    out["note"] = "Experimental text-only reader. One instance; tap left/right to turn pages.";
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
