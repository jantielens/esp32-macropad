#include "pad_cache_transaction.h"
#include "pad_config.h"

#include <stdlib.h>

PadCacheRefreshResult pad_cache_refresh(PadConfig** slot,
                                        uint8_t page,
                                        PadCacheAllocator allocate_primary,
                                        PadCacheAllocator allocate_fallback,
                                        PadCacheLoader load,
                                        PadCacheEligibilityPublisher publish,
                                        PadCacheLock lock,
                                        PadCacheLock unlock) {
    PadConfig* replacement = allocate_primary();
    if (!replacement) replacement = allocate_fallback();
    if (!replacement) return PadCacheRefreshResult::AllocationFailed;

    if (load(page, replacement)) {
        lock();
        PadConfig* old = *slot;
        *slot = replacement;
        unlock();
        publish(page, replacement->button_count > 0 ||
                  replacement->pad_action_count > 0);
        free(old);
        return PadCacheRefreshResult::Replaced;
    }

    free(replacement);
    lock();
    PadConfig* old = *slot;
    *slot = nullptr;
    unlock();
    free(old);
    publish(page, false);
    return PadCacheRefreshResult::Cleared;
}