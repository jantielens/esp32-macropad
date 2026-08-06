#include "music_catalog.h"

#include <string.h>

#if __has_include(<LittleFS.h>) || __has_include(<SD_MMC.h>)
#include "storage.h"
#define MUSIC_CATALOG_HAS_STORAGE 1
#endif

#if MUSIC_CATALOG_HAS_STORAGE
#include <esp_heap_caps.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#endif

namespace {

bool has_mp3_extension(const char* path) {
    const size_t length = strlen(path);
    if (length < 4) return false;
    const char* extension = path + length - 4;
    return extension[0] == '.' &&
           (extension[1] == 'm' || extension[1] == 'M') &&
           (extension[2] == 'p' || extension[2] == 'P') &&
           extension[3] == '3';
}

void insert_sorted_path(MusicCatalogSnapshot* snapshot, const char* path,
                        const Mp3Metadata* metadata) {
    uint8_t cursor = snapshot->count;
    while (cursor > 0 && strcmp(path, snapshot->paths[cursor - 1]) < 0) {
        if (cursor < MUSIC_TRACK_LIMIT) {
            strlcpy(snapshot->paths[cursor], snapshot->paths[cursor - 1],
                    sizeof(snapshot->paths[cursor]));
            snapshot->metadata[cursor] = snapshot->metadata[cursor - 1];
        }
        --cursor;
    }
    strlcpy(snapshot->paths[cursor], path, sizeof(snapshot->paths[cursor]));
    snapshot->metadata[cursor] = metadata ? *metadata : Mp3Metadata{};
}

} // namespace

void MusicCatalog::begin(MusicCatalogSnapshot* target) {
    target_ = target;
    if (target_) *target_ = {};
    result_ = MUSIC_CATALOG_OK;
}

MusicCatalogResult MusicCatalog::add(const char* path, const Mp3Metadata* metadata) {
    if (!target_ || result_ != MUSIC_CATALOG_OK) return result_;
    if (!is_canonical_path(path)) return result_ = MUSIC_CATALOG_INVALID_PATH;
    if (target_->total_found != UINT16_MAX) ++target_->total_found;
    if (target_->count < MUSIC_TRACK_LIMIT) {
        insert_sorted_path(target_, path, metadata);
        ++target_->count;
        return MUSIC_CATALOG_OK;
    }
    target_->overflow = true;
    if (strcmp(path, target_->paths[MUSIC_TRACK_LIMIT - 1]) < 0) {
        insert_sorted_path(target_, path, metadata);
    }
    return MUSIC_CATALOG_OK;
}

void MusicCatalog::skip() {
    if (target_ && target_->skipped != UINT16_MAX) ++target_->skipped;
}

MusicCatalogResult MusicCatalog::publish() {
    if (!target_ || result_ != MUSIC_CATALOG_OK) return result_;
    target_->available = true;
    return MUSIC_CATALOG_OK;
}

void MusicCatalog::fail(MusicCatalogResult result) {
    result_ = result == MUSIC_CATALOG_OK ? MUSIC_CATALOG_UNAVAILABLE : result;
    if (target_) *target_ = {};
}

bool MusicCatalog::is_canonical_path(const char* path) {
    if (!path || strncmp(path, "/media/", 7) != 0) return false;
    const size_t length = strlen(path);
    if (length >= MUSIC_PATH_MAX_LEN || !has_mp3_extension(path)) return false;

    const char* component = path + 7;
    if (!component[0]) return false;
    for (const char* cursor = component;; ++cursor) {
        const char value = *cursor;
        if (value == '\\' || (value != '\0' && static_cast<unsigned char>(value) < 0x20)) return false;
        if (value == '/' || value == '\0') {
            const size_t component_length = static_cast<size_t>(cursor - component);
            if (component_length == 0 ||
                (component_length == 1 && component[0] == '.') ||
                (component_length == 2 && component[0] == '.' && component[1] == '.')) {
                return false;
            }
            if (value == '\0') return true;
            component = cursor + 1;
        }
    }
}

#if MUSIC_CATALOG_HAS_STORAGE
namespace {

bool child_path(const char* directory_path, const char* entry_name,
                char* out, size_t out_len) {
    if (!directory_path || !entry_name || !out || out_len == 0) return false;
    const size_t directory_len = strlen(directory_path);
    if (strncmp(entry_name, directory_path, directory_len) == 0 &&
        entry_name[directory_len] == '/') {
        return strlcpy(out, entry_name, out_len) < out_len;
    }
    while (*entry_name == '/') ++entry_name;
    const int written = snprintf(out, out_len, "%s/%s", directory_path, entry_name);
    return written > 0 && static_cast<size_t>(written) < out_len;
}

constexpr size_t kMetadataPrefixBytes = 8192;
constexpr uint16_t kCatalogYieldInterval = 16;

bool discover_directory(File directory, const char* directory_path, MusicCatalog* catalog,
                        uint8_t* metadata_prefix, uint16_t* entries_since_yield) {
    for (File entry = directory.openNextFile(); entry; entry = directory.openNextFile()) {
        char path[MUSIC_PATH_MAX_LEN] = {};
        if (!child_path(directory_path, entry.name(), path, sizeof(path))) {
            entry.close();
            if (has_mp3_extension(entry.name())) catalog->skip();
            continue;
        }
        if (entry.isDirectory()) {
            if (!discover_directory(entry, path, catalog, metadata_prefix, entries_since_yield)) {
                entry.close();
                return false;
            }
        } else if (MusicCatalog::is_canonical_path(path)) {
            Mp3Metadata metadata = {};
            // Metadata is optional. One PSRAM prefix buffer is reused across
            // the entire scan so large libraries do not churn the PSRAM heap.
            if (metadata_prefix) {
                const size_t bytes = entry.read(metadata_prefix, kMetadataPrefixBytes);
                mp3_metadata_parse(metadata_prefix, bytes, entry.size(), &metadata);
            }
            if (catalog->add(path, &metadata) != MUSIC_CATALOG_OK) {
                entry.close();
                return false;
            }
        } else if (has_mp3_extension(path) && !MusicCatalog::is_canonical_path(path)) {
            catalog->skip();
        }
        entry.close();
        if (entries_since_yield && ++*entries_since_yield >= kCatalogYieldInterval) {
            *entries_since_yield = 0;
            taskYIELD();
        }
    }
    return true;
}

} // namespace

bool music_catalog_discover(MusicCatalog* catalog, MusicCatalogSnapshot* target) {
    if (!catalog || !target) return false;
    catalog->begin(target);
    File media = Storage.open("/media", "r");
    if (!media || !media.isDirectory()) {
        if (media) media.close();
        catalog->fail(MUSIC_CATALOG_UNAVAILABLE);
        return false;
    }
    uint8_t* metadata_prefix = static_cast<uint8_t*>(heap_caps_malloc(
        kMetadataPrefixBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    uint16_t entries_since_yield = 0;
    const bool complete = discover_directory(media, "/media", catalog,
                                             metadata_prefix, &entries_since_yield);
    media.close();
    if (metadata_prefix) heap_caps_free(metadata_prefix);
    if (!complete || catalog->publish() != MUSIC_CATALOG_OK) {
        catalog->fail(catalog->result());
        return false;
    }
    return true;
}
#endif // MUSIC_CATALOG_HAS_STORAGE