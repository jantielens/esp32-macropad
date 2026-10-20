#pragma once

#include "board_config.h"
#if HAS_HA_LIVE
#include <stddef.h>
#include <stdint.h>

constexpr size_t HA_LOOKUP_RESPONSE_BYTES = 12 * 1024;

struct HaLookupContext {
    uint32_t revision;
    uint32_t ota_epoch;
    bool current(uint32_t configured_revision, uint32_t epoch, bool ota_active) const {
        return !ota_active && revision == configured_revision && ota_epoch == epoch;
    }
};

struct HaLookupResult {
    unsigned status;
    uint32_t id;
    const char* error;
};

void ha_lookup_configure(const char* url, const char* token);
HaLookupResult ha_lookup_start(const char* query, bool entity);
HaLookupResult ha_lookup_poll(uint32_t id, char* response, size_t size);
#endif
