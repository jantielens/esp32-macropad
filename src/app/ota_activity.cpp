#include "ota_activity.h"

static uint32_t g_ota_activity_epoch = 0;

#if defined(OTA_ACTIVITY_HOST_TEST)
#include <mutex>

static std::mutex g_ota_activity_mutex;
static bool g_ota_activity_active = false;

bool ota_activity_try_begin() {
    std::lock_guard<std::mutex> lock(g_ota_activity_mutex);
    if (g_ota_activity_active) return false;
    g_ota_activity_active = true;
    ++g_ota_activity_epoch;
    return true;
}

void ota_activity_finish() {
    std::lock_guard<std::mutex> lock(g_ota_activity_mutex);
    g_ota_activity_active = false;
}

bool ota_activity_is_active() {
    std::lock_guard<std::mutex> lock(g_ota_activity_mutex);
    return g_ota_activity_active;
}

uint32_t ota_activity_epoch() {
    std::lock_guard<std::mutex> lock(g_ota_activity_mutex);
    return g_ota_activity_epoch;
}
#else
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

static portMUX_TYPE g_ota_activity_mux = portMUX_INITIALIZER_UNLOCKED;
static bool g_ota_activity_active = false;

bool ota_activity_try_begin() {
    bool acquired = false;
    portENTER_CRITICAL(&g_ota_activity_mux);
    if (!g_ota_activity_active) {
        g_ota_activity_active = true;
        ++g_ota_activity_epoch;
        acquired = true;
    }
    portEXIT_CRITICAL(&g_ota_activity_mux);
    return acquired;
}

void ota_activity_finish() {
    portENTER_CRITICAL(&g_ota_activity_mux);
    g_ota_activity_active = false;
    portEXIT_CRITICAL(&g_ota_activity_mux);
}

bool ota_activity_is_active() {
    portENTER_CRITICAL(&g_ota_activity_mux);
    const bool active = g_ota_activity_active;
    portEXIT_CRITICAL(&g_ota_activity_mux);
    return active;
}

uint32_t ota_activity_epoch() {
    portENTER_CRITICAL(&g_ota_activity_mux);
    const uint32_t epoch = g_ota_activity_epoch;
    portEXIT_CRITICAL(&g_ota_activity_mux);
    return epoch;
}
#endif