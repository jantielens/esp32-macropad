#include "music_catalog.h"

#include <string.h>

#if __has_include(<LittleFS.h>) || __has_include(<SD_MMC.h>)
#include "storage.h"
#define MUSIC_CATALOG_HAS_STORAGE 1
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

void sort_paths(MusicCatalogSnapshot* snapshot) {
    for (uint8_t index = 1; index < snapshot->count; ++index) {
        char value[MUSIC_PATH_MAX_LEN];
        strlcpy(value, snapshot->paths[index], sizeof(value));
        uint8_t cursor = index;
        while (cursor > 0 && strcmp(value, snapshot->paths[cursor - 1]) < 0) {
            strlcpy(snapshot->paths[cursor], snapshot->paths[cursor - 1],
                    sizeof(snapshot->paths[cursor]));
            --cursor;
        }
        strlcpy(snapshot->paths[cursor], value, sizeof(snapshot->paths[cursor]));
    }
}

} // namespace

MusicCatalog::MusicCatalog() : candidate_{}, snapshot_{}, result_(MUSIC_CATALOG_UNAVAILABLE) {}

void MusicCatalog::begin() {
    candidate_ = {};
    result_ = MUSIC_CATALOG_OK;
}

MusicCatalogResult MusicCatalog::add(const char* path) {
    if (result_ != MUSIC_CATALOG_OK) return result_;
    if (!is_canonical_path(path)) return result_ = MUSIC_CATALOG_INVALID_PATH;
    if (candidate_.count >= MUSIC_TRACK_LIMIT) return result_ = MUSIC_CATALOG_OVERFLOW;
    strlcpy(candidate_.paths[candidate_.count], path, sizeof(candidate_.paths[candidate_.count]));
    ++candidate_.count;
    return MUSIC_CATALOG_OK;
}

MusicCatalogResult MusicCatalog::publish() {
    if (result_ != MUSIC_CATALOG_OK) {
        snapshot_ = {};
        return result_;
    }
    sort_paths(&candidate_);
    candidate_.available = true;
    snapshot_ = candidate_;
    return MUSIC_CATALOG_OK;
}

void MusicCatalog::fail(MusicCatalogResult result) {
    result_ = result == MUSIC_CATALOG_OK ? MUSIC_CATALOG_UNAVAILABLE : result;
    candidate_ = {};
    snapshot_ = {};
}

bool MusicCatalog::contains(const char* path) const {
    if (!path || !snapshot_.available) return false;
    for (uint8_t index = 0; index < snapshot_.count; ++index) {
        if (strcmp(snapshot_.paths[index], path) == 0) return true;
    }
    return false;
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

bool discover_directory(File directory, const char* directory_path, MusicCatalog* catalog) {
    for (File entry = directory.openNextFile(); entry; entry = directory.openNextFile()) {
        char path[MUSIC_PATH_MAX_LEN] = {};
        if (!child_path(directory_path, entry.name(), path, sizeof(path))) {
            entry.close();
            return false;
        }
        if (entry.isDirectory()) {
            if (!discover_directory(entry, path, catalog)) {
                entry.close();
                return false;
            }
        } else if (MusicCatalog::is_canonical_path(path) &&
                   catalog->add(path) != MUSIC_CATALOG_OK) {
            entry.close();
            return false;
        }
        entry.close();
    }
    return true;
}

} // namespace

bool music_catalog_discover(MusicCatalog* catalog) {
    if (!catalog) return false;
    catalog->begin();
    File media = Storage.open("/media", "r");
    if (!media || !media.isDirectory()) {
        if (media) media.close();
        catalog->fail(MUSIC_CATALOG_UNAVAILABLE);
        return false;
    }
    const bool complete = discover_directory(media, "/media", catalog);
    media.close();
    if (!complete || catalog->publish() != MUSIC_CATALOG_OK) {
        catalog->fail(catalog->result());
        return false;
    }
    return true;
}
#endif // MUSIC_CATALOG_HAS_STORAGE