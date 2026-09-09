#include <gtest/gtest.h>

#include "device_classes/epaper/epaper_wake_budget.h"

extern "C" unsigned long millis() {
        return 0;
}

TEST(EpaperWakeBudget, LimitsStageToRemainingBudget) {
        const EpaperWakeBudget budget = {1000, 10000, 200};

        EXPECT_EQ(budget.remaining_ms(4000), 7000u);
        EXPECT_EQ(budget.stage_limit_ms(4000, 4500), 4500u);
                EXPECT_EQ(budget.stage_limit_ms(9000, 2000), 1800u);
}

TEST(EpaperWakeBudget, RefusesWorkInsideShutdownReserve) {
        const EpaperWakeBudget budget = {1000, 10000, 200};

        EXPECT_EQ(budget.stage_limit_ms(10800, 1000), 0u);
        EXPECT_TRUE(budget.expired(11000));
}

TEST(EpaperWakeBudget, UnlimitedBudgetKeepsStageCap) {
        const EpaperWakeBudget budget = {1000, 0, 200};

        EXPECT_EQ(budget.remaining_ms(999999), UINT32_MAX);
        EXPECT_EQ(budget.stage_limit_ms(999999, 1000), 1000u);
        EXPECT_FALSE(budget.expired(999999));
}

TEST(EpaperWakeBudget, BeginsWithProvidedTimerLimit) {
        const EpaperWakeBudget budget = epaper_wake_budget_begin(true, 15000, 300);

        EXPECT_EQ(budget.overall_budget_ms, 15000u);
        EXPECT_EQ(budget.shutdown_reserve_ms, 300u);
}