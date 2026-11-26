#pragma once

#include <render/ImageRenderer.h>
#include "epub_chapter_labels.h"

namespace epub_reader_impl {

class PsramBuffer {
public:
    explicit PsramBuffer(size_t size) : data_(heap_caps_malloc(size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) {}
    ~PsramBuffer() { heap_caps_free(data_); }
    PsramBuffer(const PsramBuffer&) = delete;
    PsramBuffer& operator=(const PsramBuffer&) = delete;
    void* data() const { return data_; }
private:
    void* data_;
};

struct SdkPosition {
    uint32_t magic;
    uint32_t size;
    uint32_t book_key;
    uint32_t layout_key;
    uint32_t spine;
    uint32_t character;
    uint32_t page;
    char path[path_capacity];
};
constexpr uint32_t sdk_position_magic = 0x45524431;

enum class SdkPositionLoad { Missing, Valid, Invalid, Busy };

SdkPositionLoad load_sdk_position(const char* path, uint32_t size, uint32_t book_key, SdkPosition& position) {
    const StorageMutationGuard guard;
    if (!guard) return SdkPositionLoad::Busy;
    char name[48];
    position_path(path, name, sizeof(name));
    if (!Storage.exists(name)) return SdkPositionLoad::Missing;
    File file = Storage.open(name, "r");
    if (!file) return SdkPositionLoad::Invalid;
    const size_t length = file.size();
    const size_t count = file.read(reinterpret_cast<uint8_t*>(&position), sizeof(position));
    file.close();
    if (length != sizeof(position) || count != sizeof(position) || position.magic != sdk_position_magic ||
        position.size != size || position.book_key != book_key ||
        !memchr(position.path, 0, sizeof(position.path)) || strcmp(position.path, path))
        return SdkPositionLoad::Invalid;
    return SdkPositionLoad::Valid;
}

bool save_sdk_position(const char* path, uint32_t size, uint32_t book_key, uint32_t layout_key,
                       uint32_t spine, uint32_t character, uint32_t page) {
    SdkPosition position{sdk_position_magic, size, book_key, layout_key, spine, character, page, {}};
    strlcpy(position.path, path, sizeof(position.path));
    char name[48];
    position_path(path, name, sizeof(name));
    return storage_write_atomic(name, reinterpret_cast<const uint8_t*>(&position), sizeof(position));
}

uint32_t hash_bytes(uint32_t hash, const uint8_t* bytes, size_t count) {
    for (size_t i = 0; i < count; ++i) { hash ^= bytes[i]; hash *= 16777619u; }
    return hash;
}

bool book_fingerprint(Source& source, Arena& scratch, const char* path, uint32_t& result) {
    uint32_t offset = 0, size = 0;
    uint16_t entries = 0;
    const BookStatus status = ZipCatalog::locateCentralDirectory(source, scratch, &offset, &size, &entries);
    if (status != BookStatus::Ok) return false;
    const size_t mark = scratch.mark();
    auto* buffer = static_cast<uint8_t*>(scratch.alloc(4096, 1));
    if (!buffer) return false;
    uint32_t hash = path_hash(path);
    uint32_t done = 0;
    while (done < size) {
        const uint32_t count = size - done < 4096 ? size - done : 4096;
        if (source.shouldStop() || source.readAt(static_cast<uint64_t>(offset) + done, buffer, count) !=
                                       static_cast<int32_t>(count)) {
            scratch.release(mark);
            return false;
        }
        hash = hash_bytes(hash, buffer, count);
        done += count;
    }
    scratch.release(mark);
    result = hash;
    return true;
}

struct ChapterMetadataResult {
    uint32_t count = 0;
    uint32_t book_key = 0;
    const char* error = nullptr;
    char (*titles)[LIST_SOURCE_MAX_TEXT_BYTES + 1] = nullptr;
};

ChapterMetadataResult load_chapter_metadata(uint32_t generation, const char* path) {
    Source source(path, generation, true);
    if (!source.open())
        return {0, 0, source.busy() ? "Storage busy or update interrupted" : "Cannot open EPUB"};
    PsramBuffer book_memory(book_arena_size), scratch_memory(384 * 1024);
    if (!book_memory.data() || !scratch_memory.data())
        return {0, 0, "Not enough PSRAM for chapter metadata"};
    Arena book_arena(book_memory.data(), book_arena_size), scratch(scratch_memory.data(), 384 * 1024);
    Book book;
    const BookStatus status = book.open(source, book_arena, scratch);
    if (source.shouldStop()) return {0, 0, "Chapter metadata interrupted"};
    if (status != BookStatus::Ok) return {0, 0, bookStatusName(status)};
    if (!book.spineCount()) return {0, 0, "EPUB has no chapters"};
    uint32_t book_key = 0;
    if (!book_fingerprint(source, scratch, path, book_key))
        return {0, 0, source.busy() ? "Chapter metadata interrupted" : "Cannot fingerprint EPUB"};
    if (source.shouldStop()) return {0, 0, "Chapter metadata interrupted"};
    ChapterMetadataResult result{static_cast<uint32_t>(book.spineCount()), book_key, nullptr};
    if (book.spineCount() > LIST_SOURCE_MAX_ITEMS || !book.tocCount()) return result;
    result.titles = static_cast<char (*)[LIST_SOURCE_MAX_TEXT_BYTES + 1]>(
        heap_caps_malloc(book.spineCount() * sizeof(*result.titles), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!result.titles) return {0, 0, "Not enough PSRAM for chapter labels"};
    memset(result.titles, 0, book.spineCount() * sizeof(*result.titles));
    bool has_labels = false;
    for (size_t i = 0; i < book.spineCount(); ++i) {
        if (source.shouldStop()) { result.error = "Chapter metadata interrupted"; return result; }
        const ManifestItem* spine = book.spineItem(i);
        if (!spine || !spine->href) continue;
        bool duplicate_spine = false;
        for (size_t j = 0; j < book.spineCount(); ++j) {
            const ManifestItem* other = book.spineItem(j);
            if (j != i && other && other->href && !strcmp(spine->href, other->href)) {
                duplicate_spine = true;
                break;
            }
        }
        if (duplicate_spine) continue;
        bool interrupted = false;
        const char* label = chapter_navigation_label(source, book.tocEntry(0), book.tocCount(),
                                                       spine->href, interrupted);
        if (interrupted) { result.error = "Chapter metadata interrupted"; return result; }
        if (!label) continue;
        size_t length = 0;
        if (!list_source_preview_length(label, strlen(label), LIST_SOURCE_MAX_TEXT_BYTES, &length))
            continue;
        memcpy(result.titles[i], label, length);
        result.titles[i][length] = '\0';
        has_labels = has_labels || length > 0;
    }
    if (!has_labels) {
        heap_caps_free(result.titles);
        result.titles = nullptr;
    }
    return result;
}

struct SdkImageTarget {
    Source* source;
    uint8_t* pixels;
    uint16_t width;
    uint16_t height;
    int16_t x;
    int16_t y;
    bool interrupted;
};

bool sdk_image_row(void* context, uint16_t row, const uint8_t* gray, uint16_t width) {
    auto& target = *static_cast<SdkImageTarget*>(context);
    if (target.source->shouldStop()) { target.interrupted = true; return false; }
    const int32_t y = target.y + row;
    if (y >= 0 && y < target.height) {
        for (uint32_t x = 0; x < width; ++x) {
            const int32_t dx = target.x + static_cast<int32_t>(x);
            if (dx >= 0 && dx < target.width) target.pixels[static_cast<size_t>(y) * target.width + dx] = gray[x];
        }
    }
    taskYIELD();
    return true;
}

bool wait_frame_applied(uint32_t generation) {
    while (current(generation) && !ota_activity_is_active()) {
        bool done = false;
        if (xSemaphoreTake(service->mutex, portMAX_DELAY) == pdTRUE) {
            done = service->applied_revision >= service->frame_revision;
            xSemaphoreGive(service->mutex);
        }
        if (done) return true;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return false;
}

uint32_t publish_frame(uint32_t generation, uint8_t* frame, const char* message) {
    if (xSemaphoreTake(service->mutex, portMAX_DELAY) != pdTRUE) return 0;
    uint32_t revision = 0;
    if (current(generation)) {
        service->frame = frame;
        strlcpy(service->message, message ? message : "", sizeof(service->message));
        service->ready = false;
        service->error = false;
        revision = ++service->revision;
        service->frame_revision = revision;
    }
    xSemaphoreGive(service->mutex);
    if (revision) display_manager_request_binding_refresh();
    return revision;
}

void run_sdk_book(uint32_t generation, const char* path, const char* font_name,
                  uint16_t font_size, uint16_t width, uint16_t height) {
    constexpr size_t sdk_scratch_size = 384 * 1024;
    constexpr size_t index_size = 128 * 1024;
    constexpr size_t page_size = 192 * 1024;
    constexpr size_t glyph_size = 128 * 1024;
    constexpr size_t cache_buffer_size = 8192;
    const int64_t started = esp_timer_get_time();
    service->min_internal_free = service->min_psram_free = SIZE_MAX;
    service->min_internal_largest = service->min_psram_largest = SIZE_MAX;
    log_memory("open start", started);
    Source source(path, generation);
    if (!source.open()) {
        if (!source.busy() && xSemaphoreTake(service->mutex, portMAX_DELAY) == pdTRUE) {
            if (current(generation)) {
                service->chapters_available = false;
                heap_caps_free(service->chapter_titles);
                service->chapter_titles = nullptr;
                service->chapter_pending = false;
                service->position_valid = false;
                service->chapter = service->chapter_count = 0;
                service->chapter_state = EreaderMetadataState::Error;
                strlcpy(service->chapter_error, "Cannot open EPUB", sizeof(service->chapter_error));
                ++service->chapters_revision;
            }
            xSemaphoreGive(service->mutex);
        }
        publish(generation, source.busy() ? "Storage busy; reopen reader" : "Cannot open EPUB", false);
        return;
    }
    PsramBuffer book_memory(book_arena_size), scratch_memory(sdk_scratch_size), index_memory(index_size),
        page_memory(page_size), glyph_memory(glyph_size), cache_memory(cache_buffer_size),
        frame_memory(static_cast<size_t>(width) * height);
    log_memory("buffers allocated", started);
    if (!book_memory.data() || !scratch_memory.data() || !index_memory.data() || !page_memory.data() ||
        !glyph_memory.data() || !cache_memory.data() || !frame_memory.data()) {
        publish(generation, "Not enough PSRAM for SDK reader", false);
        return;
    }
    Arena book_arena(book_memory.data(), book_arena_size), scratch(scratch_memory.data(), sdk_scratch_size),
        index_arena(index_memory.data(), index_size), page_arena(page_memory.data(), page_size);
    freeink::font::Arena glyph_arena(glyph_memory.data(), glyph_size);
    Book book;
    BookStatus status = book.open(source, book_arena, scratch);
    if (status != BookStatus::Ok || !book.spineCount()) {
        if (!source.shouldStop() && xSemaphoreTake(service->mutex, portMAX_DELAY) == pdTRUE) {
            if (current(generation)) {
                service->chapters_available = false;
                heap_caps_free(service->chapter_titles);
                service->chapter_titles = nullptr;
                service->chapter_pending = false;
                service->position_valid = false;
                service->chapter = service->chapter_count = 0;
                service->chapter_state = EreaderMetadataState::Error;
                strlcpy(service->chapter_error,
                        status == BookStatus::Ok ? "EPUB has no chapters" : bookStatusName(status),
                        sizeof(service->chapter_error));
                ++service->chapters_revision;
            }
            xSemaphoreGive(service->mutex);
        }
        publish(generation, status == BookStatus::Ok ? "EPUB has no chapters" : bookStatusName(status), false);
        return;
    }
    uint32_t book_key = 0;
    if (!book_fingerprint(source, scratch, path, book_key)) {
        publish(generation, "Cannot fingerprint EPUB", false);
        return;
    }
    if (xSemaphoreTake(service->mutex, portMAX_DELAY) == pdTRUE) {
        if (current(generation)) {
            if (service->chapters_available && service->metadata_book_key != book_key) {
                service->position_valid = false;
                service->chapter_pending = false;
                service->chapter = 0;
                heap_caps_free(service->chapter_titles);
                service->chapter_titles = nullptr;
            }
            service->metadata_book_key = book_key;
            service->chapter_count = static_cast<uint32_t>(book.spineCount());
            service->chapters_available = true;
            ++service->chapters_revision;
        }
        xSemaphoreGive(service->mutex);
    }

    size_t font_length = 0;
    uint8_t* font_data = nullptr;
#if HAS_TTF_FONTS
    if (font_name[0] && strcmp(font_name, "@builtin")) {
        font_data = font_store_load(font_name, font_length, [](void* context) {
            return static_cast<Source*>(context)->shouldStop();
        }, &source);
        if (source.shouldStop()) {
            heap_caps_free(font_data);
            if (current(generation)) publish(generation, "Reader interrupted; reopen", false);
            return;
        }
    }
#endif
    struct FontDataOwner {
        uint8_t* data;
        ~FontDataOwner() { heap_caps_free(data); }
    } font_owner{font_data};
    TtfFont ttf;
    BuiltinFont builtin(generation, glyph_arena);
    FontChain fonts;
    bool custom = false;
    uint32_t font_key = 0x4c564731;
    if (font_data && font_length <= UINT32_MAX && ttf.init(font_data, font_length, glyph_arena)) {
        ttf.setEmSizing(true);
        font_key = 2166136261u;
        for (size_t offset = 0; offset < font_length;) {
            if (source.shouldStop()) return;
            const size_t count = font_length - offset < 4096 ? font_length - offset : 4096;
            font_key = hash_bytes(font_key, font_data + offset, count);
            offset += count;
            taskYIELD();
        }
        custom = fonts.add(&ttf);
    }
    if (!custom) glyph_arena.reset();
    if (!custom && (!builtin.prepare() || !fonts.add(&builtin))) {
        publish(generation, "Cannot initialize reader font", false);
        return;
    }
    const bool font_fallback = font_name[0] && strcmp(font_name, "@builtin") && !custom;
    if (font_fallback) LOGW("EpubReader", "Configured font unavailable; using built-in font");
    const int16_t body_height = fonts.lineHeight(font_size);
    const int16_t body_width = fonts.advance('M', font_size, StyleNone);
    if (source.shouldStop() || (!custom && builtin.failed())) {
        if (current(generation)) publish(generation, "Reader interrupted; reopen", false);
        return;
    }
    if (body_height <= 0) {
        publish(generation, "Cannot initialize reader font", false);
        return;
    }
    if (body_height > height || body_width > width) {
        publish(generation, "Reader needs more space", false);
        return;
    }

    CssStylesheetBuilder builder;
    if (!builder.begin(book_arena)) {
        publish(generation, "Not enough PSRAM for CSS", false);
        return;
    }
    unsigned sheets = 0;
    for (size_t i = 0; i < book.manifestCount(); ++i) {
        const ManifestItem* item = book.manifestItem(i);
        if (!item || !item->href || !item->mediaType || strcmp(item->mediaType, "text/css")) continue;
        const ZipEntry* entry = book.zip().find(item->href);
        if (!entry) {
            LOGW("EpubReader", "Stylesheet missing from EPUB");
            continue;
        }
        status = builder.addSheet(source, *entry, scratch);
        if (status != BookStatus::Ok) {
            LOGW("EpubReader", "Stylesheet parse failed: %s", bookStatusName(status));
            if (source.shouldStop()) return;
        } else ++sheets;
    }
    if (sheets > 1) LOGW("EpubReader", "Stylesheets are merged globally, not selected per chapter");
    CssStylesheet stylesheet = builder.finish();
    if (builder.skippedSheets()) LOGW("EpubReader", "Skipped %u oversized stylesheets", builder.skippedSheets());
    LayoutParams params;
    params.pageWidth = width;
    params.pageHeight = height;
    params.marginLeft = 0;
    params.marginRight = 0;
    params.marginTop = 0;
    params.marginBottom = 0;
    params.baseSizePx = font_size;
    params.font = &fonts;
    params.stylesheet = &stylesheet;
    const uint32_t layout_key = layoutGenerationHash(params, font_key);
    CacheStorageAdapter cache(generation, static_cast<uint8_t*>(cache_memory.data()), cache_buffer_size);
    SdkPosition position{};
    const SdkPositionLoad load = load_sdk_position(path, static_cast<uint32_t>(source.size()), book_key, position);
    if (load == SdkPositionLoad::Busy) {
        publish(generation, "Storage busy; reopen reader", false);
        return;
    }
    bool retained = false;
    bool explicit_chapter = false;
    uint32_t target_chapter = 0;
    if (xSemaphoreTake(service->mutex, portMAX_DELAY) == pdTRUE) {
        if (current(generation) && service->chapter_pending &&
            service->requested_chapter <= book.spineCount()) {
            explicit_chapter = true;
            target_chapter = service->requested_chapter;
            service->chapter_pending = false;
        } else if (current(generation) && service->position_valid && service->position_book_key == book_key) {
            position.book_key = service->position_book_key;
            position.layout_key = service->position_layout_key;
            position.spine = service->position_spine;
            position.character = service->position_character;
            position.page = service->position_page;
            retained = true;
        } else if (current(generation) && service->position_valid) {
            service->position_valid = false;
        }
        xSemaphoreGive(service->mutex);
    }
    bool resumed = !explicit_chapter && (load == SdkPositionLoad::Valid || retained) &&
        position.spine < book.spineCount();
    const char* warning = (!explicit_chapter && !retained && load == SdkPositionLoad::Invalid) ||
                          (!explicit_chapter && load == SdkPositionLoad::Valid && !resumed)
                              ? "Saved position invalid; restarted" :
                          font_fallback ? "Default font unavailable; using built-in font" : "";
    uint32_t spine = explicit_chapter ? target_chapter - 1 : resumed ? position.spine : 0;
    uint32_t previous_spine = spine;
    uint32_t page_number = 0;
    uint32_t previous_page = 0;
    bool need_load = true, last_page = false, backwards = false, first = true;
    bool restoring_boundary = false;
    bool rebuilt_stale_page = false;
    const char* boundary_message = nullptr;
    char name[40]{};
    PageCacheReader reader;
    auto* frame = static_cast<uint8_t*>(frame_memory.data());
    int64_t page_started = started;

    while (current(generation)) {
        if (ota_activity_is_active()) {
            publish(generation, "Reader interrupted; reopen", false);
            break;
        }
        if (need_load) {
            const int64_t chapter_started = esp_timer_get_time();
            if (!first) page_started = chapter_started;
            publish(generation, "Loading EPUB...", false, false);
            index_arena.reset();
            if (spine > UINT16_MAX || !pageCacheName(static_cast<uint16_t>(spine), layout_key, name, sizeof(name))) {
                publish(generation, "Chapter index exceeds cache limit", false);
                break;
            }
            uint32_t cache_key = hash_bytes(layout_key, reinterpret_cast<const uint8_t*>(&book_key), sizeof(book_key));
            cache_key = hash_bytes(cache_key, reinterpret_cast<const uint8_t*>(&spine), sizeof(spine));
            status = reader.open(cache, name, cache_key, index_arena);
            const bool hit = status == BookStatus::Ok && !reader.isPartial();
            bool chapter_empty = hit && !reader.pageCount();
            LOGI("EpubReader", "cache spine0=%lu %s status=%s",
                 static_cast<unsigned long>(spine), hit ? "hit" : "miss", bookStatusName(status));
            if (!hit) {
                if (source.shouldStop()) {
                    if (current(generation)) publish(generation, "Reader interrupted; reopen", false);
                    break;
                }
                if (status != BookStatus::Ok && status != BookStatus::NotFound && status != BookStatus::Stale) {
                    publish(generation, bookStatusName(status), false);
                    break;
                }
                const ManifestItem* item = book.spineItem(spine);
                const ZipEntry* entry = item && item->href ? book.zip().find(item->href) : nullptr;
                if (!item || !item->mediaType || strcmp(item->mediaType, "application/xhtml+xml") || !entry) {
                    publish(generation, "Unsupported or missing chapter", false);
                    break;
                }
                index_arena.reset();
                PageCacheWriter writer;
                if (!writer.begin(cache, name, cache_key, index_arena)) {
                    publish(generation, "Cannot create page cache", false);
                    break;
                }
                uint32_t pages = 0, characters = 0;
                status = ChapterLayout::layout(source, book.zip(), *entry, item->href, params, scratch,
                                               writer, &pages, &characters);
                if (status != BookStatus::Ok || writer.failed() || source.shouldStop() || (!custom && builtin.failed())) {
                    cache.abort();
                    LOGW("EpubReader", "Layout failed: %s", bookStatusName(status));
                    if (current(generation))
                        publish(generation, status == BookStatus::Ok ? "Page cache or layout interrupted" :
                                                   bookStatusName(status), false);
                    break;
                }
                chapter_empty = pages == 0;
                if (chapter_empty) {
                    cache.abort();
                } else {
                    writer.setTotalChars(characters);
                    if (!writer.finish()) {
                        publish(generation, "Cannot save page cache", false);
                        break;
                    }
                    index_arena.reset();
                    status = reader.open(cache, name, cache_key, index_arena);
                    if (status != BookStatus::Ok || reader.isPartial()) {
                        publish(generation, "Cannot read page cache", false);
                        break;
                    }
                }
            }
            log_memory(first ? "first chapter" : "chapter change", chapter_started);
            if (chapter_empty) {
                if (first && resumed) {
                    resumed = false;
                    warning = "Saved position invalid; restarted";
                    spine = 0;
                    continue;
                }
                const EpubEmptyChapter next = epub_skip_empty_chapter(spine, book.spineCount(),
                                                                      backwards, first, previous_spine);
                if (next == EpubEmptyChapter::Continue) continue;
                if (next == EpubEmptyChapter::Restore) {
                    restoring_boundary = true;
                    boundary_message = backwards ? "Start of book" : "End of book";
                    continue;
                }
                publish(generation, "No readable chapters", false);
                break;
            }
            page_number = restoring_boundary ? previous_page : last_page ? reader.pageCount() - 1 : 0;
            if (page_number >= reader.pageCount()) {
                publish(generation, "Reading position invalid", false);
                break;
            }
            if (first && resumed) {
                if (position.layout_key == layout_key && position.page < reader.pageCount() &&
                    reader.charStart(position.page) == position.character)
                    page_number = position.page;
                else if (position.character <= reader.totalChars())
                    page_number = reader.pageForChar(position.character);
                else warning = "Saved position invalid; restarted";
            }
            need_load = false;
            first = false;
        }
        if (!restoring_boundary) {
        if (!wait_frame_applied(generation)) continue;
        page_arena.reset();
        Page page{};
        status = reader.readPage(page_number, page_arena, &page);
        if (status != BookStatus::Ok) {
            if (status == BookStatus::Stale && !rebuilt_stale_page) {
                if (!cache.remove(name)) {
                    publish(generation, "Cannot discard invalid page cache", false);
                    break;
                }
                rebuilt_stale_page = true;
                need_load = true;
                last_page = false;
                warning = "Invalid cache; restarted chapter";
                continue;
            }
            publish(generation, bookStatusName(status), false);
            break;
        }
        memset(frame, 0xff, static_cast<size_t>(width) * height);
        bool image_warning = false;
        for (uint16_t i = 0; i < page.imageCount; ++i) {
            const PageImage& image = page.images[i];
            SdkImageTarget target{&source, frame, width, height, image.x, image.y, false};
            status = ImageRenderer::render(source, book.zip(), image, scratch, sdk_image_row, &target);
            if (target.interrupted || source.shouldStop()) break;
            if (status != BookStatus::Ok) {
                image_warning = true;
                LOGW("EpubReader", "Image unavailable: %s", bookStatusName(status));
            }
        }
        if (source.shouldStop()) {
            if (current(generation)) publish(generation, "Reader interrupted; reopen", false);
            break;
        }
        FrameTarget target{frame, static_cast<int16_t>(width), static_cast<int16_t>(height),
                           static_cast<int16_t>(width), FrameFormat::Gray8};
        uint32_t missing_code = 0;
        const uint32_t missing = PageRenderer::renderText(page, fonts, target, &missing_code, &source);
        if (source.shouldStop() || (!custom && builtin.failed())) {
            if (current(generation)) publish(generation, "Reader interrupted; reopen", false);
            break;
        }
        if (missing) LOGW("EpubReader", "Missing %lu glyphs, first U+%04lx",
                          static_cast<unsigned long>(missing), static_cast<unsigned long>(missing_code));
        const bool saved = save_sdk_position(path, static_cast<uint32_t>(source.size()), book_key,
                                             layout_key, spine, reader.charStart(page_number), page_number);
        if (!saved) LOGW("EpubReader", "Could not save reading position");
        if (xSemaphoreTake(service->mutex, portMAX_DELAY) == pdTRUE) {
            if (current(generation)) {
                service->position_book_key = book_key;
                service->position_layout_key = layout_key;
                service->position_spine = spine;
                service->position_character = reader.charStart(page_number);
                service->position_page = page_number;
                service->position_valid = true;
                service->chapter = spine + 1;
                service->page = page_number + 1;
                service->page_count = reader.pageCount();
            }
            xSemaphoreGive(service->mutex);
        }
        const char* message = !saved ? "Position not saved" : image_warning ? "Some images unavailable" :
                              missing ? "Some glyphs unavailable" : warning;
        const uint32_t revision = publish_frame(generation, frame, message);
        LOGI("EpubReader", "page rev=%lu spine0=%lu page1=%lu/%lu char=%lu cache=%08lx",
             static_cast<unsigned long>(revision), static_cast<unsigned long>(spine),
             static_cast<unsigned long>(page_number + 1), static_cast<unsigned long>(reader.pageCount()),
             static_cast<unsigned long>(reader.charStart(page_number)), static_cast<unsigned long>(layout_key));
        log_memory("page ready", page_started);
        } else {
            bool updated = false;
            if (xSemaphoreTake(service->mutex, portMAX_DELAY) == pdTRUE) {
                if (current(generation)) {
                    service->ready = true;
                    service->page_available = true;
                    service->busy = false;
                    strlcpy(service->message, boundary_message, sizeof(service->message));
                    ++service->revision;
                    updated = true;
                }
                xSemaphoreGive(service->mutex);
            }
            if (updated) display_manager_request_binding_refresh();
            restoring_boundary = false;
        }
        while (current(generation) && !ota_activity_is_active()) {
            int command = 0;
            if (xSemaphoreTake(service->mutex, portMAX_DELAY) == pdTRUE) {
                if (current(generation)) {
                    command = service->command;
                    service->command = 0;
                }
                xSemaphoreGive(service->mutex);
            }
            if (!command) { vTaskDelay(pdMS_TO_TICKS(40)); continue; }
            page_started = esp_timer_get_time();
            warning = font_fallback ? "Default font unavailable; using built-in font" : "";
            if (command == 3) {
                if (xSemaphoreTake(service->mutex, portMAX_DELAY) == pdTRUE) {
                    if (current(generation) && service->chapter_pending) {
                        previous_page = page_number;
                        previous_spine = spine;
                        spine = service->requested_chapter - 1;
                        service->chapter_pending = false;
                        need_load = true;
                        last_page = false;
                        backwards = false;
                    }
                    xSemaphoreGive(service->mutex);
                }
                break;
            }
            if (command == 1 && page_number + 1 < reader.pageCount()) {
                ++page_number;
                break;
            }
            if (command == -1 && page_number > 0) {
                --page_number;
                break;
            }
            if (command > 0 && spine + 1 < book.spineCount()) {
                previous_page = page_number;
                previous_spine = spine++;
                need_load = true;
                last_page = false;
                backwards = false;
                break;
            }
            if (command < 0 && spine > 0) {
                previous_page = page_number;
                previous_spine = spine--;
                need_load = true;
                last_page = command == -1;
                backwards = true;
                break;
            }
            bool updated = false;
            if (xSemaphoreTake(service->mutex, portMAX_DELAY) == pdTRUE) {
                if (current(generation)) {
                    service->ready = true;
                    service->page_available = true;
                    service->busy = false;
                    strlcpy(service->message, command < 0 ? "Start of book" : "End of book",
                            sizeof(service->message));
                    ++service->revision;
                    updated = true;
                }
                xSemaphoreGive(service->mutex);
            }
            if (updated) display_manager_request_binding_refresh();
        }
        if (!current(generation)) break;
        if (ota_activity_is_active()) {
            publish(generation, "Reader interrupted; reopen", false);
            break;
        }
    }
    cache.abort();
    if (xSemaphoreTake(service->mutex, portMAX_DELAY) == pdTRUE) {
        if (service->generation.load() == generation) service->frame = nullptr;
        xSemaphoreGive(service->mutex);
    }
    LOGI("EpubReader", "arenas book=%u scratch=%u index=%u page=%u glyph=%u",
         static_cast<unsigned>(book_arena.highWater()), static_cast<unsigned>(scratch.highWater()),
         static_cast<unsigned>(index_arena.highWater()), static_cast<unsigned>(page_arena.highWater()),
         static_cast<unsigned>(glyph_arena.highWater()));
}

}  // namespace epub_reader_impl
