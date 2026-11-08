#include "icon_store.h"

#if HAS_DISPLAY

#include "log_manager.h"
#include "pad_config.h"
#include "pad_validate.h"
#include "ota_activity.h"
#include "widgets/widget_registry.h"

#include "storage.h"
#include "psram_json_allocator.h"
#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <atomic>
#include <new>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <stdio.h>
#include <string.h>

// LVGL's built-in lodepng for PNG decoding
#define LODEPNG_NO_COMPILE_CPP
#include <libs/lodepng/lodepng.h>

#define TAG "IconStore"

// ============================================================================
// Cache — growable list in PSRAM
// ============================================================================

struct IconEntry {
    char id[CONFIG_ICON_KEY_MAX_LEN];
    char source_id[CONFIG_ICON_SOURCE_ID_MAX_LEN];
    lv_draw_buf_t* draw_buf;
    IconKind kind;
};

static IconEntry* g_entries = nullptr;
static uint16_t g_count = 0;
static uint16_t g_capacity = 0;

struct RetiredIconBuffer {
    uint8_t page;
    lv_draw_buf_t* draw_buf;
};

static RetiredIconBuffer* g_retired = nullptr;
static uint16_t g_retired_count = 0;
static uint16_t g_retired_capacity = 0;

// Guards cache and retired-buffer state against concurrent access by the LVGL
// render task and the async web server task. Created in icon_store_init().
// Critical sections cover only in-memory state — never PNG decode or file I/O.
static SemaphoreHandle_t g_cache_mutex = nullptr;
static SemaphoreHandle_t g_file_mutex = nullptr;
static std::atomic<bool> g_ready{false};

struct ValidatedArtifact {
    char key[CONFIG_ICON_KEY_MAX_LEN];
    char source_id[CONFIG_ICON_SOURCE_ID_MAX_LEN];
    char catalog_id[129];
    IconKind kind;
    uint32_t render_key;
};

static ValidatedArtifact* g_validated = nullptr;
static size_t g_validated_count = 0;
static size_t g_validated_capacity = 0;

// These entries are protected by g_file_mutex and never fall back to internal RAM.
static void invalidate_validated_artifacts() {
    g_validated_count = 0;
}

static void invalidate_validated_artifact(const char* key) {
    for (size_t index = 0; index < g_validated_count; ++index) {
        if (!strcmp(g_validated[index].key, key)) {
            g_validated[index] = g_validated[--g_validated_count];
            return;
        }
    }
}

static void remember_validated_artifact(const char* key, const char* source_id,
                                        const char* catalog_id, IconKind kind,
                                        uint32_t render_key) {
    if (!psramFound()) return;
    size_t index = 0;
    while (index < g_validated_count && strcmp(g_validated[index].key, key)) ++index;
    if (index == g_validated_count) {
        if (g_validated_count == g_validated_capacity) {
            const size_t capacity = g_validated_capacity ? g_validated_capacity * 2 : 16;
            auto* entries = static_cast<ValidatedArtifact*>(heap_caps_realloc(
                g_validated, capacity * sizeof(ValidatedArtifact), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (!entries) {
                LOGW(TAG, "Unable to cache icon validation; save will recheck files");
                return;
            }
            g_validated = entries;
            g_validated_capacity = capacity;
        }
        ++g_validated_count;
    }
    ValidatedArtifact& entry = g_validated[index];
    strlcpy(entry.key, key, sizeof(entry.key));
    strlcpy(entry.source_id, source_id, sizeof(entry.source_id));
    strlcpy(entry.catalog_id, catalog_id, sizeof(entry.catalog_id));
    entry.kind = kind;
    entry.render_key = render_key;
}

static bool validated_artifact_matches(const char* key, const char* source_id,
                                        const char* catalog_id, IconKind kind,
                                        uint32_t render_key) {
    if (!key || !source_id) return false;
    for (size_t index = 0; index < g_validated_count; ++index) {
        const ValidatedArtifact& entry = g_validated[index];
        if (!strcmp(entry.key, key) && !strcmp(entry.source_id, source_id) &&
            (!catalog_id || (entry.catalog_id[0] && !strcmp(entry.catalog_id, catalog_id))) && entry.kind == kind &&
            (!render_key || entry.render_key == render_key)) return true;
    }
    return false;
}

static inline void cache_lock() {
    if (g_cache_mutex) xSemaphoreTake(g_cache_mutex, portMAX_DELAY);
}

static inline void cache_unlock() {
    if (g_cache_mutex) xSemaphoreGive(g_cache_mutex);
}

// ============================================================================
// Internal helpers
// ============================================================================

static bool ensure_capacity() {
    if (g_count < g_capacity) return true;

    uint16_t new_cap = g_capacity == 0 ? 16 : g_capacity * 2;
    size_t new_size = new_cap * sizeof(IconEntry);

    IconEntry* new_entries = nullptr;
    if (psramFound()) {
        new_entries = (IconEntry*)heap_caps_realloc(
            g_entries, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!new_entries) {
        new_entries = (IconEntry*)realloc(g_entries, new_size);
    }
    if (!new_entries) {
        LOGE(TAG, "Failed to grow cache to %u entries", new_cap);
        return false;
    }

    g_entries = new_entries;
    g_capacity = new_cap;
    return true;
}

static bool ensure_retired_capacity() {
    if (g_retired_count < g_retired_capacity) return true;

    uint16_t new_capacity = g_retired_capacity == 0 ? 16 : g_retired_capacity * 2;
    size_t new_size = (size_t)new_capacity * sizeof(RetiredIconBuffer);
    RetiredIconBuffer* new_retired = nullptr;
    if (psramFound()) {
        new_retired = (RetiredIconBuffer*)heap_caps_realloc(
            g_retired, new_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!new_retired) {
        new_retired = (RetiredIconBuffer*)realloc(g_retired, new_size);
    }
    if (!new_retired) {
        LOGE(TAG, "Failed to grow retired cache to %u buffers", new_capacity);
        return false;
    }

    g_retired = new_retired;
    g_retired_capacity = new_capacity;
    return true;
}

static uint8_t page_from_icon_id(const char* id) {
    unsigned page = 0;
    if (!id || sscanf(id, "pad_%u_", &page) != 1 || page >= MAX_PADS) {
        return UINT8_MAX;
    }
    return (uint8_t)page;
}

static bool retire_buffer(uint8_t page, lv_draw_buf_t* draw_buf) {
    if (!draw_buf) return true;
    if (!ensure_retired_capacity()) return false;

    g_retired[g_retired_count++] = {page, draw_buf};
    return true;
}

static int find_entry(const char* id) {
    for (uint16_t i = 0; i < g_count; i++) {
        if (strcmp(g_entries[i].id, id) == 0) return (int)i;
    }
    return -1;
}

static bool cache_entry_matches(const char* id, const char* source_id, IconKind kind) {
    cache_lock();
    const int index = find_entry(id);
    const bool matches = index >= 0 && g_entries[index].kind == kind &&
                         strcmp(g_entries[index].source_id, source_id) == 0;
    cache_unlock();
    return matches;
}

static bool invalidate_cache_entry(const char* id, uint8_t page) {
    cache_lock();
    const int index = find_entry(id);
    if (index < 0) {
        cache_unlock();
        return true;
    }
    if (!retire_buffer(page, g_entries[index].draw_buf)) {
        cache_unlock();
        return false;
    }
    if (index != g_count - 1) g_entries[index] = g_entries[g_count - 1];
    g_count--;
    cache_unlock();
    return true;
}

// Remove cache entries from the lookup table and retire their buffers until
// the corresponding LVGL pad screen deletes its image widgets.
static uint16_t invalidate_entries_with_prefix(const char* prefix, uint8_t page) {
    if (!prefix || !prefix[0]) return 0;

    const size_t prefix_len = strlen(prefix);
    uint16_t removed = 0;

    cache_lock();
    for (uint16_t i = 0; i < g_count; ) {
        if (strncmp(g_entries[i].id, prefix, prefix_len) == 0) {
            if (!retire_buffer(page, g_entries[i].draw_buf)) {
                break;
            }
            if (i != g_count - 1) {
                g_entries[i] = g_entries[g_count - 1];
            }
            g_count--;
            removed++;
            continue;
        }
        i++;
    }
    cache_unlock();

    return removed;
}

static bool validate_png(const uint8_t* data, size_t len) {
    // PNG signature: 89 50 4E 47 0D 0A 1A 0A
    if (len < 8) return false;
    return (data[0] == 0x89 && data[1] == 0x50 &&
            data[2] == 0x4E && data[3] == 0x47);
}

static uint32_t read_be32(const uint8_t* data) {
    return ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) |
           ((uint32_t)data[2] << 8) | data[3];
}

static bool png_dimensions_allowed(const uint8_t* data, size_t len,
                                   unsigned* width, unsigned* height) {
    if (!validate_png(data, len) || len < 24 ||
        read_be32(data + 8) != 13 || memcmp(data + 12, "IHDR", 4) != 0) {
        return false;
    }

    const uint32_t png_width = read_be32(data + 16);
    const uint32_t png_height = read_be32(data + 20);
    if (width) *width = png_width;
    if (height) *height = png_height;
    return png_width > 0 && png_height > 0 &&
           png_width <= ICON_MAX_DIMENSION && png_height <= ICON_MAX_DIMENSION;
}

// ArduinoJson reads streams byte by byte, so buffer filesystem reads locally.
class BufferedIconReader {
public:
    explicit BufferedIconReader(File& file) : file(file) {}
    int read() {
        if (position == length) {
            length = file.read(buffer, sizeof(buffer));
            position = 0;
            if (!length) return -1;
        }
        return buffer[position++];
    }
    size_t readBytes(char* out, size_t count) {
        size_t read_count = 0;
        while (read_count < count) {
            const int value = read();
            if (value < 0) break;
            out[read_count++] = (char)value;
        }
        return read_count;
    }
private:
    File& file;
    uint8_t buffer[512];
    size_t position = 0;
    size_t length = 0;
};

static bool read_source_metadata_unlocked(const char* source_id, char* catalog_id, size_t capacity,
                                          IconKind* kind) {
    if (!pad_icon_source_id_valid(source_id)) return false;
    char path[64];
    bool png = false;
    if (!icon_store_source_path(source_id, path, sizeof(path), &png)) return false;
    if (png) {
        catalog_id[0] = '\0';
        *kind = ICON_KIND_COLOR;
        return true;
    }
    File file = Storage.open(path, "r");
    if (!file || file.size() > ICON_MAX_SOURCE_JSON_SIZE) {
        if (file) file.close();
        return false;
    }
    BasicJsonDocument<PsramJsonAllocator> filter(128);
    filter["catalog_id"] = true;
    filter["kind"] = true;
    filter["media_type"] = true;
    BasicJsonDocument<PsramJsonAllocator> document(256);
    BufferedIconReader reader(file);
    const DeserializationError error = deserializeJson(
        document, reader, DeserializationOption::Filter(filter));
    file.close();
    if (error || document.overflowed()) return false;
    const JsonObjectConst source = document.as<JsonObjectConst>();
    const char* stored_catalog_id = source["catalog_id"] | "";
    const char* source_kind = source["kind"] | "";
    const char* media_type = source["media_type"] | "";
    const bool has_catalog = source.containsKey("catalog_id");
    const bool valid_metadata = strcmp(media_type, "image/png") == 0
        ? !has_catalog && strcmp(source_kind, "color") == 0
        : strcmp(media_type, "image/svg+xml") == 0 && has_catalog &&
            pad_icon_catalog_kind_valid(stored_catalog_id, source_kind);
    if (!valid_metadata || strlcpy(catalog_id, has_catalog ? stored_catalog_id : "", capacity) >= capacity)
        return false;
    *kind = strcmp(source_kind, "mono") == 0 ? ICON_KIND_MONO : ICON_KIND_COLOR;
    return true;
}

static bool source_matches_metadata_unlocked(const char* source_id,
                                            const char* catalog_id,
                                            const char* kind) {
    char stored_catalog[129];
    IconKind stored_kind;
    return read_source_metadata_unlocked(source_id, stored_catalog, sizeof(stored_catalog), &stored_kind) &&
        (!catalog_id || (stored_catalog[0] && !strcmp(catalog_id, stored_catalog))) &&
        !strcmp(kind, stored_kind == ICON_KIND_MONO ? "mono" : "color");
}

static bool source_matches_kind_unlocked(const char* source_id, IconKind kind) {
    return source_matches_metadata_unlocked(source_id, nullptr,
        kind == ICON_KIND_MONO ? "mono" : "color");
}

using ButtonIconVisitor = void (*)(uint8_t slot, const char* source, IconKind kind, void* context);

static void visit_button_icons(const ScreenButtonConfig& button, ButtonIconVisitor visitor, void* context) {
    if (button.icon_source_id[0])
        visitor(0, button.icon_source_id, static_cast<IconKind>(button.icon_kind), context);
    const WidgetType* type = widget_find(button.widget.type);
    if (!type || !type->visitResources) return;
    struct Context { ButtonIconVisitor visitor; void* context; } wrapper{visitor, context};
    type->visitResources(button.widget.data,
        [](WidgetResourceKind kind, const char*, uint8_t slot, const char* source, void* opaque) {
            if (kind == WidgetResourceKind::Binding || !source[0]) return;
            auto& wrapper = *static_cast<Context*>(opaque);
            wrapper.visitor(slot, source, kind == WidgetResourceKind::MonoIcon ? ICON_KIND_MONO : ICON_KIND_COLOR,
                            wrapper.context);
        }, &wrapper);
}

struct PadIconReferences {
    struct Reference {
        char key[CONFIG_ICON_KEY_MAX_LEN];
        char source[CONFIG_ICON_SOURCE_ID_MAX_LEN];
    };
    const PadConfig* configs[MAX_PADS] = {};
    Reference* entries = nullptr;
    size_t count = 0;
    bool readable = true;

    PadIconReferences() {
        size_t capacity = 0;
        for (uint8_t page = 0; page < MAX_PADS; ++page) {
            configs[page] = pad_config_acquire(page);
            if (!configs[page]) {
                if (pad_config_exists(page)) readable = false;
                continue;
            }
            for (uint8_t index = 0; index < configs[page]->button_count; ++index)
                visit_button_icons(configs[page]->buttons[index],
                    [](uint8_t, const char*, IconKind, void* opaque) {
                        ++*static_cast<size_t*>(opaque);
                    }, &capacity);
        }
        if (!readable || !capacity) return;
        PsramJsonAllocator allocator;
        entries = static_cast<Reference*>(allocator.allocate(capacity * sizeof(Reference)));
        if (!entries) {
            LOGW(TAG, "Skipping icon cleanup: unable to allocate reference snapshot");
            readable = false;
            return;
        }
        for (uint8_t page = 0; page < MAX_PADS; ++page) {
            const PadConfig* config = configs[page];
            if (!config) continue;
            for (uint8_t index = 0; index < config->button_count; ++index) {
                struct Context {
                    PadIconReferences* references;
                    uint8_t page;
                    const ScreenButtonConfig* button;
                    const char* layer;
                } context{this, page, &config->buttons[index],
                          index < config->main_button_count ? "main" : "overlay"};
                visit_button_icons(config->buttons[index],
                    [](uint8_t slot, const char* source, IconKind, void* opaque) {
                        auto& context = *static_cast<Context*>(opaque);
                        Reference& entry = context.references->entries[context.references->count++];
                        if (slot) icon_store_build_option_key(context.page, context.layer,
                            context.button->col, context.button->row, slot, entry.key, sizeof(entry.key));
                        else icon_store_build_key(context.page, context.layer,
                            context.button->col, context.button->row, entry.key, sizeof(entry.key));
                        strlcpy(entry.source, source, sizeof(entry.source));
                    }, &context);
            }
        }
    }

    ~PadIconReferences() {
        for (const PadConfig* config : configs)
            if (config) pad_config_release(config);
        free(entries);
    }

    bool uses_source(const char* source) const {
        for (size_t index = 0; index < count; ++index)
            if (!strcmp(entries[index].source, source)) return true;
        return false;
    }

    bool uses_key(const char* key) const {
        for (size_t index = 0; index < count; ++index)
            if (!strcmp(entries[index].key, key)) return true;
        return false;
    }
};

static bool read_artifact_metadata_unlocked(const char* key, IconArtifactInfo* out) {
    char metadata_path[72];
    snprintf(metadata_path, sizeof(metadata_path), "/icons/%s.meta", key);
    File metadata_file = Storage.open(metadata_path, "r");
    if (!metadata_file || metadata_file.size() > 256) {
        if (metadata_file) metadata_file.close();
        return false;
    }
    BasicJsonDocument<PsramJsonAllocator> metadata(512);
    char bytes[256];
    const size_t length = metadata_file.size();
    const size_t read = metadata_file.readBytes(bytes, length);
    metadata_file.close();
    if (read != length) return false;
    const DeserializationError error = deserializeJson(metadata, bytes, length);
    if (error || metadata.overflowed()) return false;
    const char* stored_source = metadata["source_id"] | "";
    const uint8_t stored_kind = metadata["kind"] | (uint8_t)255;
    const uint32_t stored_width = metadata["width"] | (uint32_t)0;
    const uint32_t stored_height = metadata["height"] | (uint32_t)0;
    if (!pad_icon_source_id_valid(stored_source) || stored_kind > ICON_KIND_MONO ||
        !stored_width || !stored_height || stored_width > ICON_MAX_DIMENSION ||
        stored_height > ICON_MAX_DIMENSION) return false;

    strlcpy(out->source_id, stored_source, sizeof(out->source_id));
    out->kind = (IconKind)stored_kind;
    out->width = stored_width;
    out->height = stored_height;
    out->render_key = metadata["render_key"] | (uint32_t)0;
    return true;
}

static bool artifact_dimensions_match(const uint8_t* data, size_t length,
                                      const IconArtifactInfo& info) {
    unsigned width = 0, height = 0;
    return png_dimensions_allowed(data, length, &width, &height) &&
           width == info.width && height == info.height;
}

static bool read_artifact_unlocked(const char* key, IconArtifactInfo* out) {
    IconArtifactInfo info = {};
    if (!read_artifact_metadata_unlocked(key, &info)) return false;
    char png_path[64];
    snprintf(png_path, sizeof(png_path), "/icons/%s.png", key);
    File png_file = Storage.open(png_path, "r");
    if (!png_file || png_file.size() < 24 || png_file.size() > ICON_MAX_PNG_SIZE) {
        if (png_file) png_file.close();
        return false;
    }
    uint8_t header[24];
    const size_t read = png_file.readBytes((char*)header, sizeof(header));
    png_file.close();
    if (read != sizeof(header) || !artifact_dimensions_match(header, sizeof(header), info)) return false;
    *out = info;
    return true;
}

static bool artifact_matches_unlocked(const char* key, const char* source_id,
                                      IconKind kind, uint32_t render_key = 0) {
    IconArtifactInfo info = {};
    return read_artifact_unlocked(key, &info) &&
        strcmp(info.source_id, source_id) == 0 && info.kind == kind &&
        (!render_key || info.render_key == render_key);
}

struct IconPreloadPass {
    struct Source {
        char id[CONFIG_ICON_SOURCE_ID_MAX_LEN];
        IconKind kind;
        bool valid;
    };
    Source* sources = nullptr;
    size_t count = 0;
    size_t capacity = 0;
    bool cache_disabled = false;
    uint32_t source_ms = 0;
    uint32_t metadata_ms = 0;
    uint32_t read_ms = 0;
    uint32_t decode_ms = 0;

    ~IconPreloadPass() { free(sources); }

    // The file mutex is held for the pass, so cached validation cannot go stale.
    bool source_matches(const char* id, IconKind kind) {
        for (size_t index = 0; index < count; ++index)
            if (sources[index].kind == kind && !strcmp(sources[index].id, id))
                return sources[index].valid;
        const uint32_t start = millis();
        const bool valid = source_matches_kind_unlocked(id, kind);
        source_ms += millis() - start;
        if (cache_disabled) return valid;
        if (count == capacity) {
            const size_t new_capacity = capacity ? capacity * 2 : 16;
            PsramJsonAllocator allocator;
            auto* entries = static_cast<Source*>(
                allocator.reallocate(sources, new_capacity * sizeof(Source)));
            if (!entries) {
                LOGW(TAG, "Unable to cache source validation; remaining icons will recheck sources");
                cache_disabled = true;
                return valid;
            }
            sources = entries;
            capacity = new_capacity;
        }
        Source& entry = sources[count++];
        strlcpy(entry.id, id, sizeof(entry.id));
        entry.kind = kind;
        entry.valid = valid;
        return valid;
    }
};

struct IconScanTiming {
    uint32_t passes = 0;
    uint32_t entries = 0;
    uint32_t open_ms = 0;
    uint32_t next_ms = 0;
    uint32_t check_ms = 0;
    uint32_t delete_ms = 0;
    uint32_t retire_ms = 0;
    uint32_t actions = 0;
    uint32_t total_ms = 0;
};

static File open_icon_directory(const char* path, IconScanTiming* timing) {
    const uint32_t start = timing ? millis() : 0;
    File dir = Storage.open(path);
    if (timing) {
        ++timing->passes;
        timing->open_ms += millis() - start;
    }
    return dir;
}

static bool next_icon_entry(File& dir, String& name, bool& is_directory, IconScanTiming* timing) {
    const uint32_t start = timing ? millis() : 0;
    name = dir.getNextFileName(&is_directory);
    if (!name.length()) {
        // Arduino's filename iterator also returns empty for unsupported entry
        // types; openNextFile skips those rather than treating them as EOF.
        File entry = dir.openNextFile();
        if (entry) {
            name = entry.name();
            is_directory = entry.isDirectory();
            entry.close();
            if (!name.length()) LOGW(TAG, "Could not retain icon directory entry name");
        }
    }
    if (timing) {
        timing->next_ms += millis() - start;
        if (name.length()) ++timing->entries;
    }
    return name.length() != 0;
}

static void log_icon_scan_timing(const char* phase, const IconScanTiming& timing) {
    LOGI(TAG, "%s scan: passes=%u entries=%u actions=%u open=%ums next=%ums "
              "check=%ums delete=%ums retire=%ums total=%ums",
         phase, (unsigned)timing.passes, (unsigned)timing.entries, (unsigned)timing.actions,
         (unsigned)timing.open_ms, (unsigned)timing.next_ms, (unsigned)timing.check_ms,
         (unsigned)timing.delete_ms, (unsigned)timing.retire_ms,
         (unsigned)timing.total_ms);
}

static bool recover_icon_directory_entry(const char* directory, IconScanTiming* timing) {
    File dir = open_icon_directory(directory, timing);
    if (!dir || !dir.isDirectory()) return false;
    String entry;
    bool is_directory = false;
    while (next_icon_entry(dir, entry, is_directory, timing)) {
        char name[64] = {};
        const char* raw_name = entry.c_str();
        const char* basename = raw_name ? strrchr(raw_name, '/') : nullptr;
        strlcpy(name, basename ? basename + 1 : (raw_name ? raw_name : ""), sizeof(name));
        if (!is_directory) {
            const size_t length = strlen(name);
            const bool temporary = length > 4 && strcmp(name + length - 4, ".tmp") == 0;
            const bool backup = length > 4 && strcmp(name + length - 4, ".bak") == 0;
            if (temporary || backup) {
                char path[112];
                snprintf(path, sizeof(path), "%s/%s", directory, name);
                dir.close();
                const uint32_t start = millis();
                bool recovered;
                if (temporary) {
                    recovered = Storage.remove(path);
                } else {
                    name[length - 4] = '\0';
                    char target[112];
                    snprintf(target, sizeof(target), "%s/%s", directory, name);
                    recovered = Storage.exists(target) ? Storage.remove(path) : Storage.rename(path, target);
                }
                timing->delete_ms += millis() - start;
                if (recovered) ++timing->actions;
                if (!recovered) LOGW(TAG, "Could not recover icon file %s", path);
                return recovered;
            }
        }
    }
    dir.close();
    return false;
}

static bool write_icon_pair(const char* png_path, const char* metadata_path,
                            const uint8_t* png_data, size_t png_length,
                            const uint8_t* metadata, size_t metadata_length) {
    char png_tmp[112], metadata_tmp[112], png_backup[112], metadata_backup[112];
    if (snprintf(png_tmp, sizeof(png_tmp), "%s.tmp", png_path) >= (int)sizeof(png_tmp) ||
        snprintf(metadata_tmp, sizeof(metadata_tmp), "%s.tmp", metadata_path) >= (int)sizeof(metadata_tmp) ||
        snprintf(png_backup, sizeof(png_backup), "%s.bak", png_path) >= (int)sizeof(png_backup) ||
        snprintf(metadata_backup, sizeof(metadata_backup), "%s.bak", metadata_path) >= (int)sizeof(metadata_backup)) return false;
    if ((Storage.exists(png_tmp) && !Storage.remove(png_tmp)) ||
        (Storage.exists(metadata_tmp) && !Storage.remove(metadata_tmp)) ||
        (Storage.exists(png_backup) && !Storage.remove(png_backup)) ||
        (Storage.exists(metadata_backup) && !Storage.remove(metadata_backup)) ||
        !storage_write_verified(png_tmp, png_data, png_length) ||
        !storage_write_verified(metadata_tmp, metadata, metadata_length)) {
        Storage.remove(png_tmp);
        Storage.remove(metadata_tmp);
        return false;
    }

    const bool had_png = Storage.exists(png_path);
    const bool had_metadata = Storage.exists(metadata_path);
    if ((had_png && !Storage.rename(png_path, png_backup)) ||
        (had_metadata && !Storage.rename(metadata_path, metadata_backup))) {
        if (had_png && Storage.exists(png_backup)) Storage.rename(png_backup, png_path);
        Storage.remove(png_tmp);
        Storage.remove(metadata_tmp);
        return false;
    }
    if (!Storage.rename(png_tmp, png_path) || !Storage.rename(metadata_tmp, metadata_path)) {
        Storage.remove(png_path);
        Storage.remove(metadata_path);
        const bool restored_png = !had_png || Storage.rename(png_backup, png_path);
        const bool restored_metadata = !had_metadata || Storage.rename(metadata_backup, metadata_path);
        if (!restored_png || !restored_metadata)
            LOGE(TAG, "Failed to restore generated icon pair %s", png_path);
        Storage.remove(png_tmp);
        Storage.remove(metadata_tmp);
        return false;
    }
    if (had_png) Storage.remove(png_backup);
    if (had_metadata) Storage.remove(metadata_backup);
    return true;
}

// lodepng outputs RGBA byte order; LVGL ARGB8888 expects BGRA. Swap R↔B.
static void swap_rb(lv_draw_buf_t* buf) {
    uint8_t* data = buf->data;
    uint32_t stride = buf->header.stride;
    uint32_t h = buf->header.h;
    uint32_t w = buf->header.w;
    for (uint32_t y = 0; y < h; y++) {
        uint8_t* row = data + y * stride;
        for (uint32_t x = 0; x < w; x++) {
            uint8_t tmp = row[x * 4 + 0];      // R
            row[x * 4 + 0] = row[x * 4 + 2];   // R ← B
            row[x * 4 + 2] = tmp;               // B ← R
        }
    }
}

// Decode PNG data into an LVGL draw buffer with R↔B swap applied.
// Returns nullptr on failure. Caller takes ownership of the returned buffer.
static lv_draw_buf_t* decode_png(const uint8_t* data, size_t len, const char* label) {
    unsigned char* out = nullptr;
    unsigned w = 0, h = 0;
    unsigned err = lodepng_decode32(&out, &w, &h, data, len);

    if (err || !out) {
        LOGW(TAG, "PNG decode failed for '%s': %s", label,
             err ? lodepng_error_text(err) : "null output");
        if (out) lv_draw_buf_destroy((lv_draw_buf_t*)out);
        return nullptr;
    }

    lv_draw_buf_t* draw_buf = (lv_draw_buf_t*)out;

    if (w == 0 || h == 0 || w > ICON_MAX_DIMENSION || h > ICON_MAX_DIMENSION) {
        LOGW(TAG, "PNG dims invalid for '%s': %ux%u", label, w, h);
        lv_draw_buf_destroy(draw_buf);
        return nullptr;
    }

    swap_rb(draw_buf);
    return draw_buf;
}

// Add or update a cache entry. Takes ownership of the lv_draw_buf_t.
static bool cache_entry_buf(const char* id, const char* source_id,
                            IconKind kind, lv_draw_buf_t* buf) {
    cache_lock();

    // Check if entry already exists (update in place)
    int idx = find_entry(id);
    if (idx >= 0) {
        IconEntry& e = g_entries[idx];
        if (!retire_buffer(page_from_icon_id(id), e.draw_buf)) {
            cache_unlock();
            lv_draw_buf_destroy(buf);
            return false;
        }
        e.draw_buf = buf;
        e.kind = kind;
        strlcpy(e.source_id, source_id ? source_id : "", sizeof(e.source_id));
        cache_unlock();
        LOGT(TAG, "Updated cache: '%s' %ux%u stride=%u", id,
             buf->header.w, buf->header.h, (unsigned)buf->header.stride);
        return true;
    }

    // New entry
    if (!ensure_capacity()) {
        cache_unlock();
        lv_draw_buf_destroy(buf);
        return false;
    }

    IconEntry& e = g_entries[g_count];
    strlcpy(e.id, id, CONFIG_ICON_KEY_MAX_LEN);
    strlcpy(e.source_id, source_id ? source_id : "", sizeof(e.source_id));
    e.draw_buf = buf;
    e.kind = kind;

    g_count++;
    uint16_t total = g_count;
    cache_unlock();
    LOGT(TAG, "Cached: '%s' %ux%u stride=%u kind=%u (%u total)",
         id, buf->header.w, buf->header.h,
         (unsigned)buf->header.stride, kind, total);
    return true;
}

// Load an icon PNG from LittleFS, decode, and cache.
// If cache_key is provided, the icon is cached under that key instead of fs_id.
enum class IconLoadResult { InvalidArtifact, LoadFailed, Loaded };

static IconLoadResult load_from_fs(const char* fs_id, IconKind kind, const char* source_id,
                                   IconPreloadPass& pass, const char* cache_key = nullptr) {
    IconArtifactInfo info = {};
    uint32_t start = millis();
    const bool matches = read_artifact_metadata_unlocked(fs_id, &info) &&
                         !strcmp(info.source_id, source_id) && info.kind == kind;
    pass.metadata_ms += millis() - start;
    if (!matches) return IconLoadResult::InvalidArtifact;
    start = millis();
    char path[64];
    snprintf(path, sizeof(path), "/icons/%s.png", fs_id);

    File f = Storage.open(path, "r");
    if (!f) {
        pass.read_ms += millis() - start;
        return IconLoadResult::InvalidArtifact;
    }

    size_t file_size = f.size();
    if (file_size < 24 || file_size > ICON_MAX_PNG_SIZE) {
        f.close();
        LOGW(TAG, "Invalid file size for '%s': %u", fs_id, (unsigned)file_size);
        pass.read_ms += millis() - start;
        return IconLoadResult::InvalidArtifact;
    }

    uint8_t* buf = nullptr;
    if (psramFound()) {
        buf = (uint8_t*)heap_caps_malloc(file_size,
                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    }
    if (!buf) buf = (uint8_t*)malloc(file_size);
    if (!buf) {
        f.close();
        LOGE(TAG, "OOM reading '%s' (%u bytes)", fs_id, (unsigned)file_size);
        pass.read_ms += millis() - start;
        return IconLoadResult::LoadFailed;
    }

    size_t read = f.readBytes((char*)buf, file_size);
    f.close();
    pass.read_ms += millis() - start;

    if (read != file_size) {
        free(buf);
        LOGW(TAG, "Short read for '%s': %u/%u", fs_id, (unsigned)read, (unsigned)file_size);
        return IconLoadResult::LoadFailed;
    }

    unsigned width = 0;
    unsigned height = 0;
    if (!png_dimensions_allowed(buf, file_size, &width, &height) ||
        width != info.width || height != info.height) {
        free(buf);
        LOGW(TAG, "PNG rejected for '%s': %ux%u invalid or differs from metadata %ux%u",
             fs_id, width, height, (unsigned)info.width, (unsigned)info.height);
        return IconLoadResult::InvalidArtifact;
    }

    // Decode PNG → LVGL draw buffer
    start = millis();
    lv_draw_buf_t* draw_buf = decode_png(buf, file_size, fs_id);
    pass.decode_ms += millis() - start;
    free(buf);

    if (!draw_buf) return IconLoadResult::LoadFailed;

    const char* key = cache_key ? cache_key : fs_id;
    LOGT(TAG, "Decoded '%s': %ux%u stride=%u cf=%u%s",
         fs_id, draw_buf->header.w, draw_buf->header.h,
         draw_buf->header.stride, draw_buf->header.cf,
         cache_key ? " (alias)" : "");

    return cache_entry_buf(key, source_id, kind, draw_buf)
        ? IconLoadResult::Loaded : IconLoadResult::LoadFailed;
}

// ============================================================================
// Public API
// ============================================================================

void icon_store_build_key(uint8_t page, const char* layer, uint8_t col, uint8_t row,
                          char* out, size_t out_size) {
    snprintf(out, out_size, "pad_%u_%s_%u_%u", page,
             layer && strcmp(layer, "overlay") == 0 ? "overlay" : "main", col, row);
}

void icon_store_build_option_key(uint8_t page, const char* layer, uint8_t col, uint8_t row,
                                 uint8_t slot, char* out, size_t out_size) {
    snprintf(out, out_size, "pad_%u_%s_%u_%u_opt_%u", page,
             layer && strcmp(layer, "overlay") == 0 ? "overlay" : "main", col, row, slot);
}

void icon_store_init() {
    if (!g_cache_mutex) {
        g_cache_mutex = xSemaphoreCreateMutex();
        if (!g_cache_mutex) {
            LOGE(TAG, "Failed to create cache mutex");
        }
    }
    if (!g_file_mutex) {
        g_file_mutex = xSemaphoreCreateMutex();
        if (!g_file_mutex) LOGE(TAG, "Failed to create file mutex");
    }

    if (!Storage.exists("/icons")) {
        Storage.mkdir("/icons");
        LOGI(TAG, "Created /icons directory");
    }
    if (!Storage.exists("/icon_sources")) {
        Storage.mkdir("/icon_sources");
        LOGI(TAG, "Created /icon_sources directory");
    }
    IconScanTiming icon_recovery;
    IconScanTiming source_recovery;
    uint32_t recovery_start = millis();
    while (recover_icon_directory_entry("/icons", &icon_recovery)) {}
    icon_recovery.total_ms = millis() - recovery_start;
    recovery_start = millis();
    while (recover_icon_directory_entry("/icon_sources", &source_recovery)) {}
    source_recovery.total_ms = millis() - recovery_start;
    log_icon_scan_timing("Icon recovery", icon_recovery);
    log_icon_scan_timing("Source recovery", source_recovery);
    LOGI(TAG, "Initialized");
}

bool icon_store_source_id_valid(const char* source_id) {
    return pad_icon_source_id_valid(source_id);
}

bool icon_store_key_valid(const char* key) {
    if (!key || !key[0] || strlen(key) >= CONFIG_ICON_KEY_MAX_LEN) return false;
    unsigned page = 0, col = 0, row = 0;
    char layer[9] = {};
    int consumed = 0;
    if (sscanf(key, "pad_%u_%8[^_]_%u_%u%n", &page, layer, &col, &row, &consumed) != 4 ||
        page >= MAX_PADS || col >= MAX_GRID_COLS || row >= MAX_GRID_ROWS ||
        (strcmp(layer, "main") != 0 && strcmp(layer, "overlay") != 0)) return false;
    char canonical[CONFIG_ICON_KEY_MAX_LEN];
    if (!key[consumed]) {
        icon_store_build_key((uint8_t)page, layer, (uint8_t)col, (uint8_t)row,
                             canonical, sizeof(canonical));
    } else {
        unsigned slot = 0;
        int suffix_length = 0;
        if (sscanf(key + consumed, "_opt_%u%n", &slot, &suffix_length) != 1 ||
            key[consumed + suffix_length] || slot < 1 || slot > 5) return false;
        icon_store_build_option_key((uint8_t)page, layer, (uint8_t)col, (uint8_t)row,
                                    (uint8_t)slot, canonical, sizeof(canonical));
    }
    return strcmp(key, canonical) == 0;
}

bool icon_store_source_exists(const char* source_id) {
    char path[64];
    bool png = false;
    return icon_store_source_path(source_id, path, sizeof(path), &png);
}

bool icon_store_source_path(const char* source_id, char* path, size_t capacity, bool* png) {
    if (!pad_icon_source_id_valid(source_id) || !path || !png) return false;
    for (const char* extension : {"png", "json"}) {
        if (snprintf(path, capacity, "/icon_sources/%s.%s", source_id, extension) >= (int)capacity)
            return false;
        if (Storage.exists(path)) {
            *png = strcmp(extension, "png") == 0;
            return true;
        }
    }
    return false;
}

bool icon_store_source_matches_kind(const char* source_id, IconKind kind) {
    if (!g_file_mutex || xSemaphoreTake(g_file_mutex, 0) != pdTRUE) return false;
    const bool matches = source_matches_kind_unlocked(source_id, kind);
    xSemaphoreGive(g_file_mutex);
    return matches;
}

bool icon_store_source_matches_metadata(const char* source_id,
                                       const char* catalog_id,
                                       const char* kind) {
    if (!kind || !g_file_mutex ||
        xSemaphoreTake(g_file_mutex, 0) != pdTRUE) return false;
    const bool matches = source_matches_metadata_unlocked(source_id, catalog_id, kind);
    xSemaphoreGive(g_file_mutex);
    return matches;
}

IconArtifactValidation icon_store_validate_artifact(const char* key, const char* fallback_key,
                                                    const char* source_id, const char* catalog_id,
                                                    const char* kind, uint32_t render_key) {
    if (!kind || !g_file_mutex || xSemaphoreTake(g_file_mutex, 0) != pdTRUE)
        return ICON_ARTIFACT_INVALID_SOURCE;
    IconArtifactValidation result = ICON_ARTIFACT_INVALID_SOURCE;
    const IconKind icon_kind = strcmp(kind, "mono") == 0 ? ICON_KIND_MONO : ICON_KIND_COLOR;
    if ((!strcmp(kind, "mono") || !strcmp(kind, "color")) &&
        (validated_artifact_matches(key, source_id, catalog_id, icon_kind, render_key) ||
         validated_artifact_matches(fallback_key, source_id, catalog_id, icon_kind, render_key))) {
        result = ICON_ARTIFACT_VALID;
    } else if (source_matches_metadata_unlocked(source_id, catalog_id, kind)) {
        result = ((icon_store_key_valid(key) &&
                   artifact_matches_unlocked(key, source_id, icon_kind, render_key)) ||
                  (fallback_key && icon_store_key_valid(fallback_key) &&
                   artifact_matches_unlocked(fallback_key, source_id, icon_kind, render_key)))
            ? ICON_ARTIFACT_VALID : ICON_ARTIFACT_INVALID_PNG;
    }
    xSemaphoreGive(g_file_mutex);
    return result;
}

static bool read_artifact(const char* key, IconArtifactInfo* out, bool prepare) {
    if (!out || !icon_store_key_valid(key) || !g_file_mutex ||
        xSemaphoreTake(g_file_mutex, 0) != pdTRUE) return false;
    if (prepare) invalidate_validated_artifact(key);
    const bool valid = read_artifact_unlocked(key, out);
    if (valid && prepare && psramFound()) {
        char catalog_id[129];
        IconKind source_kind;
        if (read_source_metadata_unlocked(out->source_id, catalog_id, sizeof(catalog_id), &source_kind) &&
            source_kind == out->kind)
            remember_validated_artifact(key, out->source_id, catalog_id, source_kind, out->render_key);
    }
    xSemaphoreGive(g_file_mutex);
    return valid;
}

bool icon_store_read_artifact(const char* key, IconArtifactInfo* out) {
    return read_artifact(key, out, false);
}

bool icon_store_prepare_artifact(const char* key, IconArtifactInfo* out) {
    return read_artifact(key, out, true);
}

static bool delete_unreferenced_source(const char* source_id, const PadIconReferences& references,
                                       IconScanTiming* timing = nullptr) {
    if (!references.readable) return false;
    const uint32_t check_start = timing ? millis() : 0;
    const bool referenced = references.uses_source(source_id);
    if (timing) timing->check_ms += millis() - check_start;
    if (referenced) return false;
    const uint32_t delete_start = timing ? millis() : 0;
    invalidate_validated_artifacts();
    char path[64];
    bool removed = true;
    for (const char* extension : {"png", "json"}) {
        snprintf(path, sizeof(path), "/icon_sources/%s.%s", source_id, extension);
        if (Storage.exists(path) && !Storage.remove(path)) {
            LOGW(TAG, "Could not remove orphaned icon source %s", path);
            removed = false;
        }
    }
    if (timing) {
        timing->delete_ms += millis() - delete_start;
        if (removed) ++timing->actions;
    }
    return removed;
}

bool icon_store_source_delete_if_unreferenced(const char* source_id) {
    if (!pad_icon_source_id_valid(source_id)) return false;
    const PadConfigMutationGuard guard;
    if (!guard || !g_file_mutex ||
        xSemaphoreTake(g_file_mutex, 0) != pdTRUE) return false;
    const PadIconReferences references;
    if (!references.readable)
        LOGW(TAG, "Skipping source deletion: pad reference snapshot unavailable");
    const bool removed = delete_unreferenced_source(source_id, references);
    xSemaphoreGive(g_file_mutex);
    return removed;
}

static void cleanup_unreferenced_sources(const PadIconReferences& references,
                                         IconScanTiming* timing = nullptr) {
    uint32_t removed_files = 0;
    size_t reclaimed = 0;
    for (;;) {
        if (ota_activity_is_active()) break;
        File dir = open_icon_directory("/icon_sources", timing);
        if (!dir || !dir.isDirectory()) break;
        bool removed = false;
        String entry;
        bool is_directory = false;
        while (next_icon_entry(dir, entry, is_directory, timing)) {
            char name[48] = {};
            const char* raw_name = entry.c_str();
            const char* basename = raw_name ? strrchr(raw_name, '/') : nullptr;
            strlcpy(name, basename ? basename + 1 : (raw_name ? raw_name : ""), sizeof(name));
            const size_t length = strlen(name);
            if (!is_directory && ((length == 31 && strcmp(name + 26, ".json") == 0) ||
                                  (length == 30 && strcmp(name + 26, ".png") == 0))) {
                char path[80];
                snprintf(path, sizeof(path), "/icon_sources/%s", name);
                name[26] = '\0';
                const uint32_t check_start = timing ? millis() : 0;
                const bool valid_unreferenced = pad_icon_source_id_valid(name) && !references.uses_source(name);
                if (timing) timing->check_ms += millis() - check_start;
                if (valid_unreferenced) {
                    File file = Storage.open(path, "r");
                    if (!file) {
                        LOGW(TAG, "Could not open orphaned icon source %s", path);
                        break;
                    }
                    const size_t file_size = file.size();
                    file.close();
                    if (delete_unreferenced_source(name, references, timing)) {
                        ++removed_files;
                        reclaimed += file_size;
                        removed = true;
                        break;
                    }
                }
            }
        }
        dir.close();
        if (!removed) break;
    }
    if (removed_files) LOGI(TAG, "Source cleanup: removed %u files, reclaimed %u bytes",
                           (unsigned)removed_files, (unsigned)reclaimed);
}

void icon_store_cleanup_unreferenced_sources() {
    const PadConfigMutationGuard guard;
    if (!guard || !g_file_mutex || xSemaphoreTake(g_file_mutex, 0) != pdTRUE) return;
    const PadIconReferences references;
    if (references.readable) cleanup_unreferenced_sources(references);
    else LOGW(TAG, "Skipping source cleanup: pad reference snapshot unavailable");
    xSemaphoreGive(g_file_mutex);
}

struct IconSourceUpload {
    File file;
    char id[CONFIG_ICON_SOURCE_ID_MAX_LEN];
    char temporary[72];
    size_t total;
    size_t received;
};

static bool source_png_file_valid(File& file) {
    const size_t length = file.size();
    uint8_t header[33];
    if (length < sizeof(header) || !file.seek(0) ||
        file.readBytes((char*)header, sizeof(header)) != sizeof(header) ||
        memcmp(header, "\x89PNG\r\n\x1a\n", 8) != 0 ||
        !png_dimensions_allowed(header, sizeof(header), nullptr, nullptr)) return false;
    size_t offset = sizeof(header);
    bool image_data = false;
    while (length - offset >= 12) {
        if (ota_activity_is_active()) return false;
        uint8_t chunk[8];
        if (!file.seek(offset) || file.readBytes((char*)chunk, sizeof(chunk)) != sizeof(chunk))
            return false;
        const uint32_t size = read_be32(chunk);
        if (size > length - offset - 12 || memcmp(chunk + 4, "IHDR", 4) == 0 ||
            memcmp(chunk + 4, "acTL", 4) == 0 || memcmp(chunk + 4, "fcTL", 4) == 0 ||
            memcmp(chunk + 4, "fdAT", 4) == 0) return false;
        offset += size + 12;
        if (memcmp(chunk + 4, "IDAT", 4) == 0 && size) image_data = true;
        if (memcmp(chunk + 4, "IEND", 4) == 0)
            return image_data && size == 0 && offset == length;
    }
    return false;
}

void icon_store_source_png_abort(IconSourceUpload* upload) {
    if (!upload) return;
    upload->file.close();
    if (upload->temporary[0]) Storage.remove(upload->temporary);
    delete upload;
}

IconStoreResult icon_store_source_png_begin(size_t total, IconSourceUpload** upload) {
    if (!upload || !total) return ICON_STORE_INVALID;
    *upload = nullptr;
    if (!g_ready.load()) return ICON_STORE_UNAVAILABLE;
    if (ota_activity_is_active()) return ICON_STORE_BUSY;
    if (total > ICON_MAX_PNG_SIZE) return ICON_STORE_TOO_LARGE;
    if (!g_file_mutex || xSemaphoreTake(g_file_mutex, 0) != pdTRUE) return ICON_STORE_BUSY;
    auto* state = new (std::nothrow) IconSourceUpload{};
    IconStoreResult result = state ? ICON_STORE_CONFLICT : ICON_STORE_UNAVAILABLE;
    if (state) {
        for (unsigned attempt = 0; attempt < 8; ++attempt) {
            snprintf(state->id, sizeof(state->id), "s_%08x%08x%08x",
                     (unsigned)esp_random(), (unsigned)esp_random(), (unsigned)esp_random());
            char path[64];
            bool png = false;
            snprintf(state->temporary, sizeof(state->temporary), "/icon_sources/%s.png.tmp", state->id);
            if (icon_store_source_path(state->id, path, sizeof(path), &png) ||
                Storage.exists(state->temporary)) continue;
            state->file = Storage.open(state->temporary, "w");
            result = state->file ? ICON_STORE_OK : ICON_STORE_IO_ERROR;
            break;
        }
        if (result == ICON_STORE_OK) {
            state->total = total;
            *upload = state;
        } else {
            if (result == ICON_STORE_IO_ERROR) Storage.remove(state->temporary);
            state->temporary[0] = '\0';
            delete state;
        }
    }
    xSemaphoreGive(g_file_mutex);
    return result;
}

IconStoreResult icon_store_source_png_write(IconSourceUpload* upload, const uint8_t* data, size_t length) {
    if (ota_activity_is_active()) return ICON_STORE_BUSY;
    if (!upload || (length && !data) || length > upload->total - upload->received)
        return ICON_STORE_INVALID;
    if (upload->file.write(data, length) != length) return ICON_STORE_IO_ERROR;
    upload->received += length;
    return ICON_STORE_OK;
}

IconStoreResult icon_store_source_png_finish(IconSourceUpload* upload, char* source_id, size_t source_id_len) {
    if (!upload || !source_id || source_id_len < CONFIG_ICON_SOURCE_ID_MAX_LEN ||
        upload->received != upload->total) return ICON_STORE_INVALID;
    upload->file.close();
    upload->file = Storage.open(upload->temporary, "r");
    if (!upload->file || upload->file.size() != upload->total) return ICON_STORE_IO_ERROR;
    const bool valid = source_png_file_valid(upload->file);
    upload->file.close();
    if (ota_activity_is_active()) return ICON_STORE_BUSY;
    if (!valid) return ICON_STORE_INVALID;
    if (!g_file_mutex || xSemaphoreTake(g_file_mutex, 0) != pdTRUE) return ICON_STORE_BUSY;
    char path[64];
    snprintf(path, sizeof(path), "/icon_sources/%s.png", upload->id);
    const bool saved = !Storage.exists(path) && Storage.rename(upload->temporary, path);
    xSemaphoreGive(g_file_mutex);
    if (!saved) return ICON_STORE_IO_ERROR;
    upload->temporary[0] = '\0';
    strlcpy(source_id, upload->id, source_id_len);
    return ICON_STORE_OK;
}

IconStoreResult icon_store_source_install(const uint8_t* json, size_t json_len,
                                         char* source_id, size_t source_id_len) {
    if (!g_ready.load()) return ICON_STORE_UNAVAILABLE;
    if (!json || !json_len || !source_id || source_id_len < CONFIG_ICON_SOURCE_ID_MAX_LEN)
        return ICON_STORE_INVALID;
    if (json_len > ICON_MAX_CATALOG_SOURCE_JSON_SIZE) return ICON_STORE_TOO_LARGE;
    if (!g_file_mutex || xSemaphoreTake(g_file_mutex, 0) != pdTRUE)
        return ICON_STORE_BUSY;

    BasicJsonDocument<PsramJsonAllocator> document(json_len * 2 + 512);
    if (!document.capacity()) {
        xSemaphoreGive(g_file_mutex);
        return ICON_STORE_UNAVAILABLE;
    }
    const DeserializationError error = deserializeJson(document, json, json_len);
    if (error || document.overflowed()) {
        xSemaphoreGive(g_file_mutex);
        return document.overflowed() ? ICON_STORE_UNAVAILABLE : ICON_STORE_INVALID;
    }
    const JsonObjectConst source = document.as<JsonObjectConst>();
    if (source.isNull()) {
        xSemaphoreGive(g_file_mutex);
        return ICON_STORE_INVALID;
    }
    const bool has_catalog = source.containsKey("catalog_id");
    const char* catalog_id = source["catalog_id"] | "";
    const char* kind = source["kind"] | "";
    const char* media_type = source["media_type"] | "";
    const char* data = source["data"] | "";
    const bool valid_source = strcmp(media_type, "image/svg+xml") == 0 &&
        source.size() == 4 && has_catalog && data[0] && pad_icon_catalog_kind_valid(catalog_id, kind);
    if (!valid_source) {
        xSemaphoreGive(g_file_mutex);
        return ICON_STORE_INVALID;
    }

    char id[CONFIG_ICON_SOURCE_ID_MAX_LEN] = {};
    char path[64];
    bool generated = false;
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        snprintf(id, sizeof(id), "s_%08x%08x%08x",
                 (unsigned)esp_random(), (unsigned)esp_random(), (unsigned)esp_random());
        snprintf(path, sizeof(path), "/icon_sources/%s.json", id);
        char existing_path[64], temporary[72];
        bool png = false;
        snprintf(temporary, sizeof(temporary), "/icon_sources/%s.png.tmp", id);
        if (!icon_store_source_path(id, existing_path, sizeof(existing_path), &png) &&
            !Storage.exists(temporary)) {
            generated = true;
            break;
        }
    }
    if (!generated) {
        xSemaphoreGive(g_file_mutex);
        return ICON_STORE_CONFLICT;
    }
    if (!storage_write_atomic(path, json, json_len)) {
        xSemaphoreGive(g_file_mutex);
        return ICON_STORE_IO_ERROR;
    }
    strlcpy(source_id, id, source_id_len);
    xSemaphoreGive(g_file_mutex);
    return ICON_STORE_OK;
}

IconStoreResult icon_store_install(const char* key, IconKind kind,
                                   const char* source_id, uint32_t render_key,
                                   const uint8_t* png_data, size_t png_len) {
    if (!g_ready.load()) return ICON_STORE_UNAVAILABLE;
    if (!icon_store_key_valid(key) || !pad_icon_source_id_valid(source_id) ||
        !render_key || !png_data || png_len < 8) return ICON_STORE_INVALID;
    if (png_len > ICON_MAX_PNG_SIZE) return ICON_STORE_TOO_LARGE;
    if (!g_file_mutex || xSemaphoreTake(g_file_mutex, 0) != pdTRUE)
        return ICON_STORE_BUSY;
    char catalog_id[129];
    IconKind source_kind;
    if (!read_source_metadata_unlocked(source_id, catalog_id, sizeof(catalog_id), &source_kind) ||
        source_kind != kind) {
        xSemaphoreGive(g_file_mutex);
        return ICON_STORE_INVALID;
    }

    unsigned width = 0;
    unsigned height = 0;
    if (!png_dimensions_allowed(png_data, png_len, &width, &height)) {
        LOGW(TAG, "Install rejected for '%s': %ux%u exceeds %u px limit or has invalid IHDR",
             key, width, height, ICON_MAX_DIMENSION);
        xSemaphoreGive(g_file_mutex);
        return ICON_STORE_INVALID;
    }

    lv_draw_buf_t* draw_buf = decode_png(png_data, png_len, key);
    if (!draw_buf) {
        xSemaphoreGive(g_file_mutex);
        return ICON_STORE_INVALID;
    }

    char png_path[64];
    char metadata_path[72];
    char metadata[160];
    snprintf(png_path, sizeof(png_path), "/icons/%s.png", key);
    snprintf(metadata_path, sizeof(metadata_path), "/icons/%s.meta", key);
    const int metadata_length = snprintf(metadata, sizeof(metadata),
        "{\"source_id\":\"%s\",\"kind\":%u,\"width\":%u,\"height\":%u,\"render_key\":%u}",
        source_id, (unsigned)kind, width, height, (unsigned)render_key);
    invalidate_validated_artifact(key);
    if (metadata_length <= 0 || metadata_length >= (int)sizeof(metadata) ||
        !write_icon_pair(png_path, metadata_path, png_data, png_len,
                         (const uint8_t*)metadata, (size_t)metadata_length)) {
        lv_draw_buf_destroy(draw_buf);
        xSemaphoreGive(g_file_mutex);
        return ICON_STORE_IO_ERROR;
    }
    remember_validated_artifact(key, source_id, catalog_id, kind, render_key);

    if (!cache_entry_buf(key, source_id, kind, draw_buf)) {
        xSemaphoreGive(g_file_mutex);
        LOGW(TAG, "Cache failed for '%s'; generated icon is saved to disk", key);
        return ICON_STORE_OK;
    }
    xSemaphoreGive(g_file_mutex);
    LOGI(TAG, "Installed: '%s' %ux%u (PNG %u bytes)",
         key, width, height, (unsigned)png_len);
    return ICON_STORE_OK;
}

bool icon_store_lookup(const char* id, IconRef* out) {
    if (!id || !id[0] || !out) return false;

    cache_lock();
    int idx = find_entry(id);
    if (idx < 0) {
        cache_unlock();
        return false;
    }

    const lv_draw_buf_t* buf = g_entries[idx].draw_buf;
    out->dsc = (const lv_image_dsc_t*)buf;
    out->kind = g_entries[idx].kind;
    cache_unlock();
    return true;
}

// Preload icons for a single pad (with template fallback).
static uint16_t preload_pad_icons(uint8_t page, const PadConfig* cfg, IconPreloadPass& pass) {
    uint16_t loaded = 0;
    for (uint8_t i = 0; i < cfg->button_count; i++) {
        struct Context {
            uint8_t page;
            const PadConfig* config;
            const ScreenButtonConfig* button;
            const char* layer;
            uint16_t* loaded;
            IconPreloadPass* pass;
        } context{page, cfg, &cfg->buttons[i], i >= cfg->main_button_count ? "overlay" : "main",
                  &loaded, &pass};
        visit_button_icons(cfg->buttons[i],
            [](uint8_t slot, const char* source, IconKind kind, void* opaque) {
                auto& context = *static_cast<Context*>(opaque);
                char key[CONFIG_ICON_KEY_MAX_LEN];
                if (slot) icon_store_build_option_key(context.page, context.layer, context.button->col,
                                                      context.button->row, slot, key, sizeof(key));
                else icon_store_build_key(context.page, context.layer, context.button->col,
                                          context.button->row, key, sizeof(key));
                if (cache_entry_matches(key, source, kind) || !invalidate_cache_entry(key, context.page)) return;
                if (!context.pass->source_matches(source, kind)) return;
                const IconLoadResult result = load_from_fs(key, kind, source, *context.pass);
                if (result != IconLoadResult::InvalidArtifact) {
                    if (result == IconLoadResult::Loaded) ++*context.loaded;
                    return;
                }
                const int8_t parent = context.config->template_pad;
                if (parent < 0 || parent == (int8_t)context.page) return;
                char parent_key[CONFIG_ICON_KEY_MAX_LEN];
                if (slot) icon_store_build_option_key((uint8_t)parent, "main", context.button->col,
                                                      context.button->row, slot, parent_key, sizeof(parent_key));
                else icon_store_build_key((uint8_t)parent, "main", context.button->col,
                                          context.button->row, parent_key, sizeof(parent_key));
                if (load_from_fs(parent_key, kind, source, *context.pass, key) == IconLoadResult::Loaded)
                    ++*context.loaded;
            }, &context);
    }
    return loaded;
}

static void cleanup_icon_files(uint8_t page, const PadIconReferences* references,
                               IconScanTiming* timing = nullptr);

void icon_store_preload_pad_pages() {
    if (g_ready.load()) return;
    const PadConfigMutationGuard guard;
    if (!guard) {
        LOGE(TAG, "Cannot preload icons without pad configuration lock");
        return;
    }
    if (g_ready.load()) return;
    if (!g_file_mutex) {
        LOGE(TAG, "Cannot preload icons without file mutex");
        return;
    }
    const uint32_t start = millis();
    xSemaphoreTake(g_file_mutex, portMAX_DELAY);
    const PadIconReferences references;
    IconPreloadPass pass;
    uint16_t loaded = 0;

    for (uint8_t page = 0; page < MAX_PADS; page++) {
        const PadConfig* cfg = references.configs[page];
        if (cfg) loaded += preload_pad_icons(page, cfg, pass);
    }

    const uint32_t load_ms = millis() - start;
    uint32_t cleanup_start = millis();
    IconScanTiming source_cleanup;
    IconScanTiming artifact_cleanup;
    if (references.readable) {
        cleanup_unreferenced_sources(references, &source_cleanup);
    } else {
        LOGW(TAG, "Skipping boot icon cleanup: pad reference snapshot unavailable");
    }
    const uint32_t source_cleanup_ms = millis() - cleanup_start;
    source_cleanup.total_ms = source_cleanup_ms;
    xSemaphoreGive(g_file_mutex);
    cleanup_start = millis();
    if (references.readable) cleanup_icon_files(MAX_PADS, &references, &artifact_cleanup);
    const uint32_t artifact_cleanup_ms = millis() - cleanup_start;
    artifact_cleanup.total_ms = artifact_cleanup_ms;
    g_ready.store(true);

    if (references.readable) {
        log_icon_scan_timing("Source cleanup", source_cleanup);
        log_icon_scan_timing("Artifact cleanup", artifact_cleanup);
    }
    LOGI(TAG, "Preload timing: source=%ums metadata=%ums read=%ums decode=%ums load=%ums "
              "source_cleanup=%ums artifact_cleanup=%ums total=%ums",
         (unsigned)pass.source_ms, (unsigned)pass.metadata_ms, (unsigned)pass.read_ms,
         (unsigned)pass.decode_ms, (unsigned)load_ms, (unsigned)source_cleanup_ms,
         (unsigned)artifact_cleanup_ms, (unsigned)(millis() - start));
    LOGI(TAG, "Preload complete: %u icons loaded, %u total cached",
         loaded, g_count);
}

void icon_store_preload_pad(uint8_t page) {
    if (page >= MAX_PADS) return;

    const PadConfig* cfg = pad_config_acquire(page);
    if (!cfg) return;

    if (!g_file_mutex) {
        LOGE(TAG, "Cannot preload pad icons without file mutex");
        pad_config_release(cfg);
        return;
    }
    xSemaphoreTake(g_file_mutex, portMAX_DELAY);
    IconPreloadPass pass;
    uint16_t loaded = preload_pad_icons(page, cfg, pass);
    xSemaphoreGive(g_file_mutex);
    pad_config_release(cfg);

    if (loaded > 0) {
        LOGI(TAG, "Preload pad %u: %u icons loaded", page, loaded);
    }
}

static bool pad_uses_icon_key(uint8_t page, const PadConfig* config, const char* key) {
    if (!config) return false;
    for (uint8_t index = 0; index < config->button_count; ++index) {
        const ScreenButtonConfig& button = config->buttons[index];
        struct Context {
            uint8_t page;
            const ScreenButtonConfig* button;
            const char* layer;
            const char* key;
            bool found;
        } context{page, &button, index < config->main_button_count ? "main" : "overlay", key, false};
        visit_button_icons(button,
            [](uint8_t slot, const char*, IconKind, void* opaque) {
                auto& context = *static_cast<Context*>(opaque);
                char expected[CONFIG_ICON_KEY_MAX_LEN];
                if (slot) icon_store_build_option_key(context.page, context.layer, context.button->col,
                                                      context.button->row, slot, expected, sizeof(expected));
                else icon_store_build_key(context.page, context.layer, context.button->col,
                                          context.button->row, expected, sizeof(expected));
                if (!strcmp(context.key, expected)) context.found = true;
            }, &context);
        if (context.found) return true;
    }
    return false;
}

static void retire_unused_icon_cache(uint8_t page, const PadConfig* const* configs) {
    cache_lock();
    for (uint16_t index = 0; index < g_count; ) {
        const uint8_t entry_page = page_from_icon_id(g_entries[index].id);
        if (entry_page < MAX_PADS && (page == MAX_PADS || page == entry_page) &&
            !pad_uses_icon_key(entry_page, configs[entry_page], g_entries[index].id)) {
            if (!retire_buffer(entry_page, g_entries[index].draw_buf)) break;
            g_entries[index] = g_entries[--g_count];
        } else {
            ++index;
        }
    }
    cache_unlock();
}

static void cleanup_icon_files(uint8_t page, const PadIconReferences* references,
                               IconScanTiming* timing) {
    if (ota_activity_is_active()) return;
    if (!g_file_mutex || xSemaphoreTake(g_file_mutex, 0) != pdTRUE) return;
    invalidate_validated_artifacts();

    uint32_t removed = 0;
    size_t reclaimed = 0;
    for (;;) {
        if (ota_activity_is_active()) break;
        File dir = open_icon_directory("/icons", timing);
        if (!dir || !dir.isDirectory()) break;
        char candidate[64] = {};
        size_t candidate_size = 0;
        String entry;
        bool is_directory = false;
        while (next_icon_entry(dir, entry, is_directory, timing)) {
            const char* raw_name = entry.c_str();
            const char* basename = raw_name ? strrchr(raw_name, '/') : nullptr;
            const char* name = basename ? basename + 1 : raw_name;
            const uint8_t file_page = page_from_icon_id(name);
            char prefix[16];
            snprintf(prefix, sizeof(prefix), "pad_%u_", file_page);
            if (!is_directory && name && strlen(name) < sizeof(candidate) &&
                file_page < MAX_PADS && (page == MAX_PADS || page == file_page) &&
                strncmp(name, prefix, strlen(prefix)) == 0) {
                bool keep = false;
                const char* extension = strrchr(name, '.');
                if (extension && (strcmp(extension, ".png") == 0 || strcmp(extension, ".meta") == 0) &&
                    (size_t)(extension - name) < CONFIG_ICON_KEY_MAX_LEN) {
                    char key[CONFIG_ICON_KEY_MAX_LEN] = {};
                    memcpy(key, name, extension - name);
                    const uint32_t check_start = timing ? millis() : 0;
                    keep = references && references->uses_key(key);
                    if (timing) timing->check_ms += millis() - check_start;
                }
                if (!keep) {
                    strlcpy(candidate, name, sizeof(candidate));
                    break;
                }
            }
        }
        dir.close();
        if (!candidate[0]) break;
        char path[80];
        snprintf(path, sizeof(path), "/icons/%s", candidate);
        File file = Storage.open(path, "r");
        if (!file) {
            LOGW(TAG, "Could not open orphaned icon file %s", path);
            break;
        }
        candidate_size = file.size();
        file.close();
        const uint32_t delete_start = timing ? millis() : 0;
        const bool deleted = Storage.remove(path);
        if (timing) timing->delete_ms += millis() - delete_start;
        if (!deleted) {
            LOGW(TAG, "Could not remove orphaned icon file %s", path);
            break;
        }
        ++removed;
        if (timing) ++timing->actions;
        reclaimed += candidate_size;
    }
    if (!ota_activity_is_active()) {
        const uint32_t retire_start = timing ? millis() : 0;
        const PadConfig* empty_configs[MAX_PADS] = {};
        retire_unused_icon_cache(page, references ? references->configs : empty_configs);
        if (timing) timing->retire_ms += millis() - retire_start;
    }
    xSemaphoreGive(g_file_mutex);
    if (removed) LOGI(TAG, "Icon cleanup: removed %u files, reclaimed %u bytes",
                     (unsigned)removed, (unsigned)reclaimed);
}

void icon_store_retire_unused_pad_icons(uint8_t page) {
    if (page >= MAX_PADS) return;
    const PadConfig* config = pad_config_acquire(page);
    if (!config) {
        LOGW(TAG, "Skipping icon cache retirement: pad %u config unavailable", page);
        return;
    }
    const PadConfig* configs[MAX_PADS] = {};
    configs[page] = config;
    retire_unused_icon_cache(page, configs);
    pad_config_release(config);
}

void icon_store_delete_page_icons(uint8_t page) {
    if (page >= MAX_PADS) return;
    cleanup_icon_files(page, nullptr);
    char prefix[16];
    snprintf(prefix, sizeof(prefix), "pad_%u_", page);
    invalidate_entries_with_prefix(prefix, page);
}

bool icon_store_delete_file(const char* filename) {
    if (!filename) return false;
    const size_t filename_len = strlen(filename);
    if (filename_len <= 4 || strcmp(filename + filename_len - 4, ".png") != 0 ||
        filename_len - 4 >= CONFIG_ICON_KEY_MAX_LEN) {
        return false;
    }

    char key[CONFIG_ICON_KEY_MAX_LEN];
    memcpy(key, filename, filename_len - 4);
    key[filename_len - 4] = '\0';
    if (!icon_store_key_valid(key)) return false;

    if (!g_file_mutex || xSemaphoreTake(g_file_mutex, 0) != pdTRUE) return false;
    invalidate_validated_artifact(key);
    char path[64];
    snprintf(path, sizeof(path), "/icons/%s", filename);
    if (!Storage.exists(path) || !Storage.remove(path)) {
        xSemaphoreGive(g_file_mutex);
        return false;
    }

    char metadata_path[72];
    snprintf(metadata_path, sizeof(metadata_path), "/icons/%s.meta", key);
    Storage.remove(metadata_path);

    const uint8_t page = page_from_icon_id(key);
    cache_lock();
    const int index = find_entry(key);
    if (index >= 0 && retire_buffer(page, g_entries[index].draw_buf)) {
        if (index != g_count - 1) g_entries[index] = g_entries[g_count - 1];
        g_count--;
    }
    cache_unlock();
    xSemaphoreGive(g_file_mutex);
    return true;
}

void icon_store_collect_retired(uint8_t page) {
    cache_lock();
    for (uint16_t i = 0; i < g_retired_count; ) {
        if (g_retired[i].page != page) {
            i++;
            continue;
        }

        lv_draw_buf_destroy(g_retired[i].draw_buf);
        if (i != g_retired_count - 1) {
            g_retired[i] = g_retired[g_retired_count - 1];
        }
        g_retired_count--;
    }
    cache_unlock();
}

uint16_t icon_store_cache_count() {
    cache_lock();
    uint16_t count = g_count;
    cache_unlock();
    return count;
}

void icon_store_enumerate_cache(bool (*cb)(const IconCacheInfo* info, void* ctx), void* ctx) {
    cache_lock();
    for (uint16_t i = 0; i < g_count; i++) {
        const lv_draw_buf_t* buf = g_entries[i].draw_buf;
        IconCacheInfo info;
        info.id = g_entries[i].id;
        info.kind = g_entries[i].kind;
        info.w = buf->header.w;
        info.h = buf->header.h;
        info.data_size = buf->data_size;
        if (!cb(&info, ctx)) break;
    }
    cache_unlock();
}

#endif // HAS_DISPLAY
