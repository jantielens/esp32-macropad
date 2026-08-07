#include <assert.h>

#include "fs_health.h"

int main() {
    fs_health_init();

    FSHealthStats stats{};
    fs_health_get(&stats);
#if USE_SD_STORAGE
    assert(stats.backend == FS_BACKEND_SDMMC);
    assert(stats.card_type == FS_CARD_TYPE_NONE);
#else
    assert(stats.backend == FS_BACKEND_LITTLEFS);
    assert(stats.card_type == FS_CARD_TYPE_NOT_APPLICABLE);
#endif
    assert(!stats.storage_mounted);
    assert(stats.storage_used_bytes == 0);
    assert(stats.storage_total_bytes == 0);

#if USE_SD_STORAGE
    fs_health_set_sd_card_type(FS_CARD_TYPE_SDHC);
#endif
    fs_health_set_storage_usage(123, 456);
    fs_health_get(&stats);
    assert(stats.storage_mounted);
    assert(stats.storage_used_bytes == 123);
    assert(stats.storage_total_bytes == 456);
#if USE_SD_STORAGE
    assert(stats.card_type == FS_CARD_TYPE_SDHC);
#else
    assert(stats.card_type == FS_CARD_TYPE_NOT_APPLICABLE);
#endif
    return 0;
}