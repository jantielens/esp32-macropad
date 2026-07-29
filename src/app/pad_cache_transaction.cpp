#include "pad_cache_transaction.h"
#include "pad_config.h"

#include <stdlib.h>

PadCacheRefreshResult pad_cache_refresh(PadConfig** slot,
                                        uint8_t page,
                                        PadCacheAllocator allocate_primary,
                                        PadCacheAllocator allocate_fallback,
                                        PadCacheLoader load,
                                        PadCacheEligibilityPublisher publish) {
    PadConfig* replacement = allocate_primary();
    if (!replacement) replacement = allocate_fallback();
    if (!replacement) return PadCacheRefreshResult::AllocationFailed;

    if (load(page, replacement)) {
        PadConfig* old = *slot;
        *slot = replacement;
        publish(page, replacement->button_count > 0);
        free(old);
        return PadCacheRefreshResult::Replaced;
    }

    free(replacement);
    free(*slot);
    *slot = nullptr;
    publish(page, false);
    return PadCacheRefreshResult::Cleared;
}