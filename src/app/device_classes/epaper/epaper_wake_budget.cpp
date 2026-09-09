#include "device_classes/epaper/epaper_wake_budget.h"

#if HAS_EPAPER

#include <Arduino.h>

uint32_t EpaperWakeBudget::elapsed_ms(uint32_t now_ms) const {
        return now_ms - started_ms;
}

uint32_t EpaperWakeBudget::remaining_ms(uint32_t now_ms) const {
        if (overall_budget_ms == 0) return UINT32_MAX;
        const uint32_t elapsed = elapsed_ms(now_ms);
        return elapsed >= overall_budget_ms ? 0 : overall_budget_ms - elapsed;
}

uint32_t EpaperWakeBudget::stage_limit_ms(uint32_t now_ms, uint32_t stage_budget_ms) const {
        const uint32_t remaining = remaining_ms(now_ms);
        if (remaining == UINT32_MAX) return stage_budget_ms;
        if (remaining <= shutdown_reserve_ms) return 0;
        const uint32_t available = remaining - shutdown_reserve_ms;
        return available < stage_budget_ms ? available : stage_budget_ms;
}

bool EpaperWakeBudget::expired(uint32_t now_ms) const {
        return remaining_ms(now_ms) == 0;
}

EpaperWakeBudget epaper_wake_budget_begin(bool enforce, uint32_t overall_budget_ms,
                uint32_t shutdown_reserve_ms) {
        return {
                                (uint32_t)millis(),
                                enforce ? overall_budget_ms : 0,
                                shutdown_reserve_ms,
        };
}

#endif // HAS_EPAPER