#pragma once

#include <stddef.h>
#include <stdint.h>

#define IMAGE_LIBRARY_ROOT "/images"
#define IMAGE_LIBRARY_PATH_MAX_LEN 192
#define IMAGE_LIBRARY_ENTRY_LIMIT 128

enum ImageLibraryResult : uint8_t {
    IMAGE_LIBRARY_OK,
    IMAGE_LIBRARY_INVALID_PATH,
    IMAGE_LIBRARY_UNAVAILABLE,
};

struct ImageLibrarySnapshot {
    uint8_t count;
    bool available;
    bool overflow;
    uint16_t total_found;
    char paths[IMAGE_LIBRARY_ENTRY_LIMIT][IMAGE_LIBRARY_PATH_MAX_LEN];
};

class ImageLibraryCatalog {
public:
    void begin(ImageLibrarySnapshot* target);
    ImageLibraryResult add(const char* path);
    ImageLibraryResult publish();
    void fail(ImageLibraryResult result);

    static bool is_directory_path(const char* path);
    static bool is_image_path(const char* path);

private:
    ImageLibrarySnapshot* target_ = nullptr;
    ImageLibraryResult result_ = IMAGE_LIBRARY_OK;
};

// Normalize a filesystem entry name into its canonical path below directory.
// Backends may return either a basename or a fully-qualified path.
bool image_library_child_path(const char* directory, const char* entry_name,
                              char* out, size_t out_len);

// One global slideshow cursor. The caller owns its snapshot and reloads it
// after storage mutations; this type deliberately has no filesystem or LVGL
// dependency.
class ImageLibraryCursor {
public:
    bool configure(const char* directory);
    void reset();
    const char* current(const ImageLibrarySnapshot& snapshot) const;
    const char* next(const ImageLibrarySnapshot& snapshot);
    const char* previous(const ImageLibrarySnapshot& snapshot);
    const char* directory() const { return directory_; }

private:
    char directory_[IMAGE_LIBRARY_PATH_MAX_LEN] = {};
    uint8_t index_ = 0;
};

// Rebuild a caller-owned, bounded snapshot from one configured album.
// The scan is non-recursive and accepts /images itself as the default album.
bool image_library_discover(const char* directory, ImageLibraryCatalog* catalog,
                            ImageLibrarySnapshot* target);