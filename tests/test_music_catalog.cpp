#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "music_catalog.h"

static void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

int main() {
    MusicCatalog catalog;
    MusicCatalogSnapshot snapshot = {};
    catalog.begin(&snapshot);
    check(catalog.add("/media/z.mp3") == MUSIC_CATALOG_OK, "must accept valid path");
    check(catalog.add("/media/folder/A.MP3") == MUSIC_CATALOG_OK, "extension must be case insensitive");
    check(catalog.add("/media/a.mp3") == MUSIC_CATALOG_OK, "must accept another valid path");
    check(catalog.publish() == MUSIC_CATALOG_OK, "complete valid catalog must publish");
    check(snapshot.available && snapshot.count == 3, "published catalog must be complete");
    check(std::strcmp(snapshot.paths[0], "/media/a.mp3") == 0 &&
              std::strcmp(snapshot.paths[1], "/media/folder/A.MP3") == 0 &&
              std::strcmp(snapshot.paths[2], "/media/z.mp3") == 0,
          "catalog order must be bytewise canonical-path order");

    const char* invalid_paths[] = {
        "/media/../outside.mp3", "/media/folder//bad.mp3",
        "/media/folder\\bad.mp3", "/sounds/track.mp3", "/media/track.wav",
    };
    for (const char* path : invalid_paths) {
        check(!MusicCatalog::is_canonical_path(path), "unsafe or non-MP3 path accepted");
    }

    char maximum_path[MUSIC_PATH_MAX_LEN + 1];
    memset(maximum_path, 'a', MUSIC_PATH_MAX_LEN);
    memcpy(maximum_path, "/media/", 7);
    memcpy(maximum_path + MUSIC_PATH_MAX_LEN - 4, ".mp3", 4);
    maximum_path[MUSIC_PATH_MAX_LEN] = '\0';
    check(!MusicCatalog::is_canonical_path(maximum_path),
          "path at the fixed catalog buffer limit must be rejected");

    catalog.begin(&snapshot);
    for (uint8_t index = 0; index < MUSIC_TRACK_LIMIT; ++index) {
        char path[MUSIC_PATH_MAX_LEN];
        std::snprintf(path, sizeof(path), "/media/%u.mp3", index);
        check(catalog.add(path) == MUSIC_CATALOG_OK, "limit item must fit");
    }
        check(catalog.add("/media/overflow.mp3") == MUSIC_CATALOG_OK,
            "overflow track must preserve a recoverable catalog");
        check(catalog.publish() == MUSIC_CATALOG_OK && snapshot.available,
            "overflow must publish a bounded catalog");
        check(snapshot.count == MUSIC_TRACK_LIMIT && snapshot.overflow &&
              snapshot.total_found == MUSIC_TRACK_LIMIT + 1,
            "overflow metadata must describe all discovered tracks");

        MusicCatalogSnapshot deterministic = {};
        catalog.begin(&deterministic);
        check(catalog.add("/media/z.mp3") == MUSIC_CATALOG_OK, "must accept sorted candidate");
        check(catalog.add("/media/a.mp3") == MUSIC_CATALOG_OK, "must accept sorted candidate");
        for (uint8_t index = 0; index < MUSIC_TRACK_LIMIT; ++index) {
          char path[MUSIC_PATH_MAX_LEN];
          std::snprintf(path, sizeof(path), "/media/m%u.mp3", index);
          check(catalog.add(path) == MUSIC_CATALOG_OK, "must accept bounded candidate");
        }
        check(catalog.publish() == MUSIC_CATALOG_OK, "deterministic overflow must publish");
        check(std::strcmp(deterministic.paths[0], "/media/a.mp3") == 0 &&
                  std::strcmp(deterministic.paths[MUSIC_TRACK_LIMIT - 1], "/media/m8.mp3") == 0,
            "overflow must retain lexicographically smallest paths");

    std::puts("music catalog checks passed");
    return 0;
}