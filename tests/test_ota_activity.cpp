#include "ota_activity.h"
#include "storage.h"

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
    const uint32_t initial_epoch = ota_activity_epoch();
    check(!ota_activity_is_active(), "initially inactive");
    check(!ota_activity_is_busy(), "no operation blocks shutdown initially");
    check(ota_activity_try_begin(), "first acquisition succeeds");
    check(ota_activity_is_active(), "active after acquisition");
    check(ota_activity_is_busy(), "OTA blocks automatic shutdown");
    check(ota_activity_epoch() == initial_epoch + 1, "successful begin increments epoch");
    check(!ota_activity_try_begin(), "second acquisition fails");
    check(ota_activity_epoch() == initial_epoch + 1, "failed begin preserves epoch");
    ota_activity_finish();
    ota_activity_finish();
    check(!ota_activity_is_active(), "finish is idempotent");
    check(ota_activity_epoch() == initial_epoch + 1, "finished OTA remains detectable through epoch");

    std::atomic<unsigned> acquisitions = 0;
    std::vector<std::thread> contenders;
    for (unsigned index = 0; index < 16; ++index) {
        contenders.emplace_back([&acquisitions] {
            if (ota_activity_try_begin()) acquisitions.fetch_add(1);
        });
    }
    for (std::thread& contender : contenders) contender.join();
    check(acquisitions.load() == 1, "exactly one concurrent acquisition succeeds");
    check(ota_activity_epoch() == initial_epoch + 2, "concurrent begins increment epoch only once");
    ota_activity_finish();

    check(ota_activity_try_begin_config_write(), "config update admitted");
    check(ota_activity_try_begin_config_write(), "nested config save admitted");
    check(ota_activity_is_busy(), "existing config writes block automatic shutdown");
    check(!ota_activity_try_begin(), "config update excludes OTA");
    check(!ota_activity_try_begin_network_transition(), "config update excludes teardown");
    ota_activity_finish_config_write();
    check(!ota_activity_try_begin_network_transition(), "outer update still excludes teardown");
    check(ota_activity_is_busy(), "nested save completion leaves the outer config write protected");
    ota_activity_finish_config_write();
    check(!ota_activity_is_busy(), "config write completion restores shutdown eligibility");

    check(ota_activity_try_begin_network_transition(), "idle network transition admitted");
    check(!ota_activity_is_active(), "transition does not masquerade as OTA");
    check(ota_activity_is_busy(), "network transitions block automatic shutdown");
    check(!ota_activity_try_begin(), "transition excludes new OTA");
    check(!ota_activity_try_begin_config_write(), "transition excludes new config writes");
    check(!ota_activity_try_begin_network_transition(), "transition has one owner");
    ota_activity_finish();
    check(!ota_activity_try_begin(), "finishing OTA cannot release transition ownership");
    ota_activity_finish_network_transition();
    check(ota_activity_try_begin(), "OTA resumes after transition");
    check(!ota_activity_try_begin_config_write(), "OTA excludes config writes");
    check(!ota_activity_try_begin_network_transition(), "OTA excludes transition");
    ota_activity_finish();

    {
        StorageMutationGuard upload;
        check(static_cast<bool>(upload), "storage upload admitted");
        check(!ota_activity_try_begin(), "active storage upload blocks shutdown");
        {
            const StorageMutationGuard nested;
            check(static_cast<bool>(nested), "nested storage mutation admitted");
        }
        upload.release();
        check(!ota_activity_try_begin(), "transferred upload remains protected");
        storage_mutation_finish();
    }
    check(ota_activity_try_begin(), "shutdown admitted after storage upload closes");
    {
        const StorageMutationGuard denied;
        check(!denied, "shutdown blocks new storage mutations");
    }
    check(ota_activity_is_active(), "rejected mutation does not release shutdown");
    ota_activity_finish();

    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        std::atomic<bool> start{false};
        std::atomic<unsigned> winners{0};
        auto contend = [&](bool (*begin)()) {
            while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
            if (begin()) winners.fetch_add(1);
        };
        std::thread update(contend, ota_activity_try_begin);
        std::thread config(contend, ota_activity_try_begin_config_write);
        std::thread transition(contend, ota_activity_try_begin_network_transition);
        start.store(true, std::memory_order_release);
        update.join();
        config.join();
        transition.join();
        check(winners.load() == 1, "OTA/config/teardown race admits exactly one activity");
        ota_activity_finish();
        ota_activity_finish_config_write();
        ota_activity_finish_network_transition();
    }

    if (failures == 0) std::puts("OTA activity tests passed");
    return failures == 0 ? 0 : 1;
}