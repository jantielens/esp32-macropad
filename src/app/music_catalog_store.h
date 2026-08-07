#pragma once

#include "music_catalog.h"

#include <stdint.h>

struct MusicCatalogStatus {
    bool initialized;
    bool available;
    bool stale;
    uint8_t count;
    uint32_t generation;
    MusicCatalogResult last_refresh_result;
};

// Two PSRAM catalog slots. The audio worker exclusively builds the inactive
// slot. Readers copy the active slot under a normal mutex, never a portMUX.
bool music_catalog_store_init();
MusicCatalogSnapshot* music_catalog_store_begin_build();
void music_catalog_store_publish(MusicCatalogResult result);
void music_catalog_store_abort(MusicCatalogResult result);
// Only the audio worker may retain this pointer. Other tasks must use copy().
const MusicCatalogSnapshot* music_catalog_store_active_for_audio();
bool music_catalog_store_copy(MusicCatalogSnapshot* out, MusicCatalogStatus* status);
bool music_catalog_store_status(MusicCatalogStatus* out);