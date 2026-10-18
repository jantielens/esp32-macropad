#include "ota_activity.h"

static uint32_t g_ota_activity_epoch = 0;
static bool g_ota_activity_active = false;
static uint32_t g_config_writers = 0;
static bool g_network_transition = false;

#if defined(OTA_ACTIVITY_HOST_TEST)
#include <mutex>

static std::mutex g_ota_activity_mutex;
using ActivityLock = std::lock_guard<std::mutex>;
#define ACTIVITY_MUTEX g_ota_activity_mutex
#else
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

static portMUX_TYPE g_ota_activity_mux = portMUX_INITIALIZER_UNLOCKED;
class ActivityLock {
public:
    explicit ActivityLock(portMUX_TYPE& mux) : mux_(mux) { portENTER_CRITICAL(&mux_); }
    ~ActivityLock() { portEXIT_CRITICAL(&mux_); }
    ActivityLock(const ActivityLock&) = delete;
    ActivityLock& operator=(const ActivityLock&) = delete;
private:
    portMUX_TYPE& mux_;
};
#define ACTIVITY_MUTEX g_ota_activity_mux
#endif

bool ota_activity_try_begin() {
    ActivityLock lock(ACTIVITY_MUTEX);
    if (g_ota_activity_active || g_config_writers || g_network_transition) return false;
    g_ota_activity_active = true;
    ++g_ota_activity_epoch;
    return true;
}

void ota_activity_finish() {
    ActivityLock lock(ACTIVITY_MUTEX);
    g_ota_activity_active = false;
}

bool ota_activity_is_active() {
    ActivityLock lock(ACTIVITY_MUTEX);
    return g_ota_activity_active;
}

uint32_t ota_activity_epoch() {
    ActivityLock lock(ACTIVITY_MUTEX);
    return g_ota_activity_epoch;
}

bool ota_activity_try_begin_config_write() {
    ActivityLock lock(ACTIVITY_MUTEX);
    if (g_ota_activity_active || g_network_transition) return false;
    ++g_config_writers;
    return true;
}

void ota_activity_finish_config_write() {
    ActivityLock lock(ACTIVITY_MUTEX);
    if (g_config_writers) --g_config_writers;
}

bool ota_activity_try_begin_network_transition() {
    ActivityLock lock(ACTIVITY_MUTEX);
    if (g_ota_activity_active || g_config_writers || g_network_transition) return false;
    g_network_transition = true;
    return true;
}

void ota_activity_finish_network_transition() {
    ActivityLock lock(ACTIVITY_MUTEX);
    g_network_transition = false;
}