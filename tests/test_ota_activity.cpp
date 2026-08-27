#include "ota_activity.h"

#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

static int failures = 0;

static void check(bool condition, const char* message) {
    if (condition) return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++failures;
}

int main() {
    check(!ota_activity_is_active(), "initially inactive");
    check(ota_activity_try_begin(), "first acquisition succeeds");
    check(ota_activity_is_active(), "active after acquisition");
    check(!ota_activity_try_begin(), "second acquisition fails");
    ota_activity_finish();
    ota_activity_finish();
    check(!ota_activity_is_active(), "finish is idempotent");

    std::atomic<unsigned> acquisitions = 0;
    std::vector<std::thread> contenders;
    for (unsigned index = 0; index < 16; ++index) {
        contenders.emplace_back([&acquisitions] {
            if (ota_activity_try_begin()) acquisitions.fetch_add(1);
        });
    }
    for (std::thread& contender : contenders) contender.join();
    check(acquisitions.load() == 1, "exactly one concurrent acquisition succeeds");
    ota_activity_finish();

    if (failures == 0) std::puts("OTA activity tests passed");
    return failures == 0 ? 0 : 1;
}