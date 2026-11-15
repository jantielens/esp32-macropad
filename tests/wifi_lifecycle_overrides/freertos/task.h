#pragma once
#include "../../freertos/task.h"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

#define pdPASS 1
#define portMAX_DELAY UINT32_MAX
inline void (*test_wifi_worker)(void*) = nullptr;
inline void* test_wifi_worker_arg = nullptr;
inline bool test_wifi_task_allocation_fails = false;
inline unsigned test_wifi_task_creations = 0, test_wifi_task_deletions = 0;
inline unsigned test_wifi_task_allocations = 0, test_wifi_task_notifications = 0;
inline uint32_t test_wifi_task_stack = 0;
inline std::thread test_wifi_thread;
inline std::mutex test_wifi_task_mutex;
inline std::condition_variable test_wifi_task_cv;
inline bool test_wifi_task_waiting = false, test_wifi_task_deleted = false;
inline bool test_wifi_task_resume = false;
inline uint32_t test_wifi_notification_count = 0;

inline BaseType_t xTaskCreatePinnedToCore(void (*entry)(void*), const char*,
        uint32_t stack, void* arg, unsigned, TaskHandle_t* handle, int) {
    ++test_wifi_task_creations;
    test_wifi_task_stack = stack;
    if (test_wifi_task_allocation_fails) return pdFALSE;
    ++test_wifi_task_allocations;
    test_wifi_worker = entry;
    test_wifi_worker_arg = arg;
    test_wifi_task_waiting = test_wifi_task_deleted = test_wifi_task_resume = false;
    test_wifi_notification_count = 0;
    *handle = &test_wifi_thread;
    return pdPASS;
}
inline uint32_t ulTaskNotifyTake(BaseType_t clear, TickType_t) {
    std::unique_lock<std::mutex> lock(test_wifi_task_mutex);
    test_wifi_task_waiting = true;
    test_wifi_task_cv.notify_all();
    test_wifi_task_cv.wait(lock, [] {
        return test_wifi_notification_count && test_wifi_task_resume;
    });
    const uint32_t count = test_wifi_notification_count;
    if (clear) test_wifi_notification_count = 0;
    else --test_wifi_notification_count;
    test_wifi_task_waiting = false;
    return count;
}
inline BaseType_t xTaskNotifyGive(TaskHandle_t handle) {
    if (handle != &test_wifi_thread) return pdFALSE;
    std::lock_guard<std::mutex> lock(test_wifi_task_mutex);
    ++test_wifi_task_notifications;
    ++test_wifi_notification_count;
    test_wifi_task_cv.notify_all();
    return pdPASS;
}
inline void vTaskDelete(TaskHandle_t) {
    std::lock_guard<std::mutex> lock(test_wifi_task_mutex);
    ++test_wifi_task_deletions;
    test_wifi_task_deleted = true;
    test_wifi_task_cv.notify_all();
}

// Run the intact task until its notification wait or self-deletion. Notifications
// stay queued until the next call, so watchdog-only work is independently visible.
inline bool test_wifi_run_worker() {
    if (!test_wifi_worker) return true;
    std::unique_lock<std::mutex> lock(test_wifi_task_mutex);
    if (!test_wifi_thread.joinable()) {
        test_wifi_thread = std::thread([] { test_wifi_worker(test_wifi_worker_arg); });
    } else if (test_wifi_task_waiting && test_wifi_notification_count) {
        test_wifi_task_resume = true;
        test_wifi_task_cv.notify_all();
    } else {
        return true;
    }
    const bool completed = test_wifi_task_cv.wait_for(lock, std::chrono::seconds(2), [] {
        return test_wifi_task_deleted || (test_wifi_task_waiting && !test_wifi_task_resume);
    });
    if (test_wifi_task_deleted) {
        lock.unlock();
        test_wifi_thread.join();
        test_wifi_worker = nullptr;
    }
    return completed;
}
