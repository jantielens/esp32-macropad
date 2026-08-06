#pragma once

#include <stddef.h>
#include <stdint.h>

#define MUSIC_TRACK_LIMIT 32
#define MUSIC_PATH_MAX_LEN 192

enum MusicCatalogResult : uint8_t {
    MUSIC_CATALOG_OK,
    MUSIC_CATALOG_UNAVAILABLE,
    MUSIC_CATALOG_INVALID_PATH,
    MUSIC_CATALOG_IO_ERROR,
    MUSIC_CATALOG_OUT_OF_MEMORY,
};

struct MusicCatalogSnapshot {
    uint8_t count;
    bool available;
    bool overflow;
    uint16_t total_found;
    uint16_t skipped;
    char paths[MUSIC_TRACK_LIMIT][MUSIC_PATH_MAX_LEN];
};

// Builds one catalog directly in caller-owned storage. It intentionally owns
// no MusicCatalogSnapshot so an audio task never carries catalog-sized stack
// frames. Paths remain sorted while being added; after the limit, the builder
// keeps the deterministic lexicographically smallest published paths.
class MusicCatalog {
public:
    void begin(MusicCatalogSnapshot* target);
    MusicCatalogResult add(const char* path);
    void skip();
    MusicCatalogResult publish();
    void fail(MusicCatalogResult result);

    const MusicCatalogSnapshot& snapshot() const { return *target_; }
    MusicCatalogResult result() const { return result_; }

    static bool is_canonical_path(const char* path);

private:
    MusicCatalogSnapshot* target_ = nullptr;
    MusicCatalogResult result_;
};

// Rebuild one caller-owned snapshot from the selected Storage backend. The
// caller decides whether a failed scan replaces its active publication.
bool music_catalog_discover(MusicCatalog* catalog, MusicCatalogSnapshot* target);