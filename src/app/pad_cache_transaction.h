#pragma once

#include <stdint.h>

struct PadConfig;

using PadCacheAllocator = PadConfig* (*)();
using PadCacheLoader = bool (*)(uint8_t page, PadConfig* config);
using PadCacheEligibilityPublisher = void (*)(uint8_t page, bool eligible);
using PadCacheLock = void (*)();

enum class PadCacheRefreshResult : uint8_t {
    Replaced,
    Cleared,
    AllocationFailed,
};

PadCacheRefreshResult pad_cache_refresh(PadConfig** slot,
                                        uint8_t page,
                                        PadCacheAllocator allocate_primary,
                                        PadCacheAllocator allocate_fallback,
                                        PadCacheLoader load,
                                        PadCacheEligibilityPublisher publish,
                                        PadCacheLock lock,
                                        PadCacheLock unlock);