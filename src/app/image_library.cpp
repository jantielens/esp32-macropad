#include "image_library.h"

#include <stdio.h>
#include <string.h>

#if __has_include(<LittleFS.h>) || __has_include(<SD_MMC.h>)
#include "storage.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#define IMAGE_LIBRARY_HAS_STORAGE 1
#endif

namespace {

bool has_image_extension(const char* path) {
    if (!path) return false;
    const char* extension = strrchr(path, '.');
    if (!extension) return false;
    return strcasecmp(extension, ".jpg") == 0 ||
           strcasecmp(extension, ".jpeg") == 0 ||
           strcasecmp(extension, ".png") == 0;
}

bool is_clean_path(const char* path) {
    if (!path || path[0] != '/' || strlen(path) >= IMAGE_LIBRARY_PATH_MAX_LEN) return false;
    const char* component = path + 1;
    if (!component[0]) return false;
    for (const char* cursor = component;; ++cursor) {
        const char value = *cursor;
        if (value == '\\' || (value != '\0' && static_cast<unsigned char>(value) < 0x20)) return false;
        if (value == '/' || value == '\0') {
            const size_t length = static_cast<size_t>(cursor - component);
            if (length == 0 || (length == 1 && component[0] == '.') ||
                (length == 2 && component[0] == '.' && component[1] == '.')) return false;
            if (value == '\0') return true;
            component = cursor + 1;
        }
    }
}

void insert_sorted(ImageLibrarySnapshot* snapshot, const char* path) {
    uint8_t index = snapshot->count;
    while (index > 0 && strcmp(path, snapshot->paths[index - 1]) < 0) {
        if (index < IMAGE_LIBRARY_ENTRY_LIMIT) {
            strlcpy(snapshot->paths[index], snapshot->paths[index - 1],
                    sizeof(snapshot->paths[index]));
        }
        --index;
    }
    strlcpy(snapshot->paths[index], path, sizeof(snapshot->paths[index]));
}

} // namespace

void ImageLibraryCatalog::begin(ImageLibrarySnapshot* target) {
    target_ = target;
    if (target_) *target_ = {};
    result_ = IMAGE_LIBRARY_OK;
}

ImageLibraryResult ImageLibraryCatalog::add(const char* path) {
    if (!target_ || result_ != IMAGE_LIBRARY_OK) return result_;
    if (!is_image_path(path)) return result_ = IMAGE_LIBRARY_INVALID_PATH;
    if (target_->total_found != UINT16_MAX) ++target_->total_found;
    if (target_->count < IMAGE_LIBRARY_ENTRY_LIMIT) {
        insert_sorted(target_, path);
        ++target_->count;
        return IMAGE_LIBRARY_OK;
    }
    target_->overflow = true;
    if (strcmp(path, target_->paths[IMAGE_LIBRARY_ENTRY_LIMIT - 1]) < 0) {
        insert_sorted(target_, path);
    }
    return IMAGE_LIBRARY_OK;
}

ImageLibraryResult ImageLibraryCatalog::publish() {
    if (!target_ || result_ != IMAGE_LIBRARY_OK) return result_;
    target_->available = true;
    return IMAGE_LIBRARY_OK;
}

void ImageLibraryCatalog::fail(ImageLibraryResult result) {
    result_ = result == IMAGE_LIBRARY_OK ? IMAGE_LIBRARY_UNAVAILABLE : result;
    if (target_) *target_ = {};
}

bool image_library_child_path(const char* directory, const char* entry_name,
                              char* out, size_t out_len) {
    if (!directory || !entry_name || !out || out_len == 0) return false;
    const size_t directory_length = strlen(directory);
    if (strncmp(entry_name, directory, directory_length) == 0 &&
        entry_name[directory_length] == '/') {
        return strlcpy(out, entry_name, out_len) < out_len;
    }
    while (*entry_name == '/') ++entry_name;
    const int written = snprintf(out, out_len, "%s/%s", directory, entry_name);
    return written > 0 && static_cast<size_t>(written) < out_len;
}

bool ImageLibraryCatalog::is_directory_path(const char* path) {
    if (!is_clean_path(path)) return false;
    if (strcmp(path, IMAGE_LIBRARY_ROOT) == 0) return true;
    const size_t root_length = strlen(IMAGE_LIBRARY_ROOT);
    if (strncmp(path, IMAGE_LIBRARY_ROOT, root_length) != 0 || path[root_length] != '/') return false;
    return strchr(path + root_length + 1, '/') == nullptr;
}

bool ImageLibraryCatalog::is_image_path(const char* path) {
    if (!is_clean_path(path) || !has_image_extension(path)) return false;
    const char* slash = strrchr(path, '/');
    if (!slash || !slash[1]) return false;
    char parent[IMAGE_LIBRARY_PATH_MAX_LEN] = {};
    const size_t parent_length = static_cast<size_t>(slash - path);
    if (parent_length == 0 || parent_length >= sizeof(parent)) return false;
    memcpy(parent, path, parent_length);
    return is_directory_path(parent);
}

bool ImageLibraryCursor::configure(const char* directory) {
    if (!ImageLibraryCatalog::is_directory_path(directory)) return false;
    strlcpy(directory_, directory, sizeof(directory_));
    reset();
    return true;
}

void ImageLibraryCursor::reset() {
    index_ = 0;
}

const char* ImageLibraryCursor::current(const ImageLibrarySnapshot& snapshot) const {
    if (!snapshot.available || snapshot.count == 0 || !directory_[0]) return nullptr;
    if (index_ >= snapshot.count) return snapshot.paths[0];
    return snapshot.paths[index_];
}

const char* ImageLibraryCursor::next(const ImageLibrarySnapshot& snapshot) {
    if (!snapshot.available || snapshot.count == 0) return nullptr;
    index_ = static_cast<uint8_t>((index_ + 1) % snapshot.count);
    return current(snapshot);
}

const char* ImageLibraryCursor::previous(const ImageLibrarySnapshot& snapshot) {
    if (!snapshot.available || snapshot.count == 0) return nullptr;
    index_ = index_ == 0 ? static_cast<uint8_t>(snapshot.count - 1) : static_cast<uint8_t>(index_ - 1);
    return current(snapshot);
}

#if IMAGE_LIBRARY_HAS_STORAGE
bool image_library_discover(const char* directory, ImageLibraryCatalog* catalog,
                            ImageLibrarySnapshot* target) {
    if (!catalog || !target || !ImageLibraryCatalog::is_directory_path(directory)) return false;
    catalog->begin(target);
    File album = Storage.open(directory, "r");
    if (!album || !album.isDirectory()) {
        if (album) album.close();
        catalog->fail(IMAGE_LIBRARY_UNAVAILABLE);
        return false;
    }

    uint16_t entries_since_yield = 0;
    for (File entry = album.openNextFile(); entry; entry = album.openNextFile()) {
        char path[IMAGE_LIBRARY_PATH_MAX_LEN] = {};
        if (!entry.isDirectory() && image_library_child_path(directory, entry.name(), path, sizeof(path)) &&
            ImageLibraryCatalog::is_image_path(path)) {
            const char* slash = strrchr(path, '/');
            const size_t parent_length = slash ? static_cast<size_t>(slash - path) : 0;
            if (parent_length == strlen(directory) && strncmp(path, directory, parent_length) == 0) {
                catalog->add(path);
            }
        }
        entry.close();
        if (++entries_since_yield == 16) {
            entries_since_yield = 0;
            taskYIELD();
        }
    }
    album.close();
    return catalog->publish() == IMAGE_LIBRARY_OK;
}
#else
bool image_library_discover(const char*, ImageLibraryCatalog* catalog,
                            ImageLibrarySnapshot* target) {
    if (catalog) {
        catalog->begin(target);
        catalog->fail(IMAGE_LIBRARY_UNAVAILABLE);
    }
    return false;
}
#endif