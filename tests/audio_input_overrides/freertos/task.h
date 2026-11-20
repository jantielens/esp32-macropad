#pragma once
#include "FreeRTOS.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

struct AudioTestStop {};
inline thread_local TaskHandle_t audio_test_current_task = nullptr;
inline std::thread audio_test_worker;
inline std::thread audio_test_worker2;
inline std::thread audio_test_worker3;
inline std::mutex audio_test_mutex;
inline std::condition_variable audio_test_cv;
inline unsigned audio_test_notifications = 0;
inline std::atomic<bool> audio_test_stopping{false};
inline int audio_test_worker_handle;
inline int audio_test_worker2_handle;
inline int audio_test_worker3_handle;
inline std::mutex audio_test_mutex2;
inline std::condition_variable audio_test_cv2;
inline unsigned audio_test_notifications2 = 0;
inline std::mutex audio_test_mutex3;
inline std::condition_variable audio_test_cv3;
inline unsigned audio_test_notifications3 = 0;
inline unsigned audio_test_task_count = 0;

inline TaskHandle_t xTaskGetCurrentTaskHandle() { return audio_test_current_task; }
inline void vTaskDelay(TickType_t ticks) {
    if (audio_test_stopping) throw AudioTestStop{};
    std::this_thread::sleep_for(std::chrono::milliseconds(ticks));
}
inline BaseType_t xTaskCreatePinnedToCore(void (*entry)(void*), const char*, uint32_t,
                                          void* arg, unsigned, TaskHandle_t* handle, int) {
    const unsigned index = audio_test_task_count++;
    *handle = index == 0 ? &audio_test_worker_handle :
              index == 1 ? &audio_test_worker2_handle : &audio_test_worker3_handle;
    std::thread& worker = index == 0 ? audio_test_worker :
                          index == 1 ? audio_test_worker2 : audio_test_worker3;
    const TaskHandle_t task = *handle;
    worker = std::thread([entry, arg, task] {
        audio_test_current_task = task;
        try { entry(arg); } catch (const AudioTestStop&) {}
    });
    return pdPASS;
}
inline BaseType_t xTaskNotifyGive(TaskHandle_t task) {
    if (task == &audio_test_worker3_handle) {
        std::lock_guard<std::mutex> lock(audio_test_mutex3);
        ++audio_test_notifications3;
        audio_test_cv3.notify_one();
        return pdPASS;
    }
    if (task == &audio_test_worker2_handle) {
        std::lock_guard<std::mutex> lock(audio_test_mutex2);
        ++audio_test_notifications2;
        audio_test_cv2.notify_one();
        return pdPASS;
    }
    std::lock_guard<std::mutex> lock(audio_test_mutex);
    ++audio_test_notifications;
    audio_test_cv.notify_one();
    return pdPASS;
}
inline uint32_t ulTaskNotifyTake(BaseType_t, TickType_t timeout) {
    if (audio_test_current_task == &audio_test_worker3_handle) {
        std::unique_lock<std::mutex> lock(audio_test_mutex3);
        if (timeout == portMAX_DELAY)
            audio_test_cv3.wait(lock, [] { return audio_test_notifications3 || audio_test_stopping; });
        else audio_test_cv3.wait_for(lock, std::chrono::milliseconds(timeout), [] { return audio_test_notifications3 || audio_test_stopping; });
        if (audio_test_stopping) throw AudioTestStop{};
        const unsigned result = audio_test_notifications3;
        audio_test_notifications3 = 0;
        return result;
    }
    if (audio_test_current_task == &audio_test_worker2_handle) {
        std::unique_lock<std::mutex> lock(audio_test_mutex2);
        if (timeout == portMAX_DELAY)
            audio_test_cv2.wait(lock, [] { return audio_test_notifications2 || audio_test_stopping; });
        else audio_test_cv2.wait_for(lock, std::chrono::milliseconds(timeout), [] { return audio_test_notifications2 || audio_test_stopping; });
        if (audio_test_stopping) throw AudioTestStop{};
        const unsigned result = audio_test_notifications2;
        audio_test_notifications2 = 0;
        return result;
    }
    std::unique_lock<std::mutex> lock(audio_test_mutex);
    if (timeout == portMAX_DELAY)
        audio_test_cv.wait(lock, [] { return audio_test_notifications || audio_test_stopping; });
    else audio_test_cv.wait_for(lock, std::chrono::milliseconds(timeout), [] { return audio_test_notifications || audio_test_stopping; });
    if (audio_test_stopping) throw AudioTestStop{};
    const unsigned result = audio_test_notifications;
    audio_test_notifications = 0;
    return result;
}
inline void vTaskDelete(TaskHandle_t) {}
