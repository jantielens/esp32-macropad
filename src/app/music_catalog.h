#pragma once

#include <stddef.h>
#include <stdint.h>

#define MUSIC_TRACK_LIMIT 32
#define MUSIC_PATH_MAX_LEN 192

enum MusicCatalogResult : uint8_t {
    MUSIC_CATALOG_OK,
    MUSIC_CATALOG_UNAVAILABLE,
    MUSIC_CATALOG_INVALID_PATH,
    MUSIC_CATALOG_OVERFLOW,
};

struct MusicCatalogSnapshot {
    uint8_t count;
    bool available;
    char paths[MUSIC_TRACK_LIMIT][MUSIC_PATH_MAX_LEN];
};

class MusicCatalog {
public:
    MusicCatalog();

    void begin();
    MusicCatalogResult add(const char* path);
    MusicCatalogResult publish();
    void fail(MusicCatalogResult result);

    const MusicCatalogSnapshot& snapshot() const { return snapshot_; }
    MusicCatalogResult result() const { return result_; }
    bool contains(const char* path) const;

    static bool is_canonical_path(const char* path);

private:
    MusicCatalogSnapshot candidate_;
    MusicCatalogSnapshot snapshot_;
    MusicCatalogResult result_;
};

// Rebuild the complete published snapshot from the selected Storage backend.
// Returns false without publishing a partial catalog on any traversal failure.
bool music_catalog_discover(MusicCatalog* catalog);