#pragma once

#ifndef EPAPER_WAKE_BUDGET_H
#define EPAPER_WAKE_BUDGET_H

#include "board_config.h"

#if HAS_EPAPER

#include <stdint.h>

// Tune these limits from field telemetry. Timer wakes use the overall limit;
// interactive cold-boot and button wakes keep their existing best-effort flow.
constexpr uint32_t EPAPER_TIMER_WAKE_BUDGET_MS = 15000;
constexpr uint32_t EPAPER_WIFI_TARGET_MS = 4000;
constexpr uint32_t EPAPER_WIFI_BUDGET_MS = 5500;
constexpr uint32_t EPAPER_FETCH_TARGET_MS = 2000;
constexpr uint32_t EPAPER_FETCH_BUDGET_MS = 3500;
constexpr uint32_t EPAPER_MQTT_TARGET_MS = 750;
constexpr uint32_t EPAPER_MQTT_BUDGET_MS = 1500;
constexpr uint32_t EPAPER_BUDGET_SHUTDOWN_RESERVE_MS = 300;

enum class EpaperWakeBudgetCut : uint8_t {
        None,
        Wifi,
        Fetch,
        Mqtt,
        Overall,
};

struct EpaperWakeBudget {
        uint32_t started_ms;
        uint32_t overall_budget_ms;
        uint32_t shutdown_reserve_ms;

        uint32_t elapsed_ms(uint32_t now_ms) const;
        uint32_t remaining_ms(uint32_t now_ms) const;
        uint32_t stage_limit_ms(uint32_t now_ms, uint32_t stage_budget_ms) const;
        bool expired(uint32_t now_ms) const;
};

EpaperWakeBudget epaper_wake_budget_begin(bool enforce, uint32_t overall_budget_ms,
                uint32_t shutdown_reserve_ms = EPAPER_BUDGET_SHUTDOWN_RESERVE_MS);

#endif // HAS_EPAPER

#endif // EPAPER_WAKE_BUDGET_H