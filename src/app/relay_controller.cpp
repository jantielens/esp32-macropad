#include "relay_controller.h"
#include "board_config.h"

#if IS_DARKROOM_TIMER

#include "config_manager.h"
#include "log_manager.h"
#include "rtos_task_utils.h"
#include "web_portal_state.h"

#include <HTTPClient.h>

#define TAG "Relay"

// ============================================================================
// Configuration
// ============================================================================

static constexpr uint32_t RELAY_TASK_STACK_WORDS = 4096;
static constexpr UBaseType_t RELAY_TASK_PRIORITY  = 2;

// ============================================================================
// State
// ============================================================================

static bool    g_relay_on      = false;
static portMUX_TYPE g_relay_lock = portMUX_INITIALIZER_UNLOCKED;

static TaskHandle_t       g_relay_task   = nullptr;
static RtosTaskPsramAlloc g_relay_alloc  = {};
static SemaphoreHandle_t  g_relay_sem    = nullptr;
// Pending command for the relay task: 0=none, 1=ON, 2=OFF
static volatile uint8_t   g_relay_cmd    = 0;

// ============================================================================
// Shelly HTTP backend — runs on the dedicated relay task
// ============================================================================

static void shelly_send(bool on) {
    DeviceConfig* cfg = web_portal_get_current_config();
    if (!cfg || cfg->shelly_ip[0] == '\0') {
        LOGW(TAG, "Shelly IP not configured");
        return;
    }

    char url[128];
    snprintf(url, sizeof(url), "http://%s/relay/0?turn=%s", cfg->shelly_ip, on ? "on" : "off");

    HTTPClient http;
    http.setConnectTimeout(2000);
    http.setTimeout(2000);
    http.begin(url);
    int code = http.GET();
    if (code > 0) {
        LOGI(TAG, "Shelly %s → HTTP %d", on ? "ON" : "OFF", code);
    } else {
        LOGW(TAG, "Shelly %s failed: %s", on ? "ON" : "OFF", http.errorToString(code).c_str());
    }
    http.end();
    vTaskDelay(pdMS_TO_TICKS(100));   // TCP close guard — protect WiFi MAC DMA
}

// ============================================================================
// Relay FreeRTOS task
// ============================================================================

static void relay_task_fn(void*) {
    for (;;) {
        xSemaphoreTake(g_relay_sem, portMAX_DELAY);

        // Drain: only the latest command matters
        portENTER_CRITICAL(&g_relay_lock);
        uint8_t cmd = g_relay_cmd;
        g_relay_cmd = 0;
        portEXIT_CRITICAL(&g_relay_lock);

        if (cmd) {
            shelly_send(cmd == 1);
        }
    }
}

// ============================================================================
// Public API
// ============================================================================

void relay_controller_init() {
    g_relay_on = false;
    g_relay_cmd = 0;

    g_relay_sem = xSemaphoreCreateBinary();

    bool ok = rtos_create_task_psram_stack_pinned(
        relay_task_fn, "relay",
        RELAY_TASK_STACK_WORDS, nullptr,
        RELAY_TASK_PRIORITY, &g_relay_task, &g_relay_alloc,
        tskNO_AFFINITY);

    if (!ok) {
        LOGE(TAG, "Failed to create relay task");
    } else {
        LOGI(TAG, "Relay task created (Shelly backend)");
    }
}

void relay_request(bool on) {
    portENTER_CRITICAL(&g_relay_lock);
    g_relay_on = on;
    g_relay_cmd = on ? 1 : 2;
    portEXIT_CRITICAL(&g_relay_lock);
    if (g_relay_sem) xSemaphoreGive(g_relay_sem);
}

bool relay_is_on() {
    portENTER_CRITICAL(&g_relay_lock);
    bool on = g_relay_on;
    portEXIT_CRITICAL(&g_relay_lock);
    return on;
}

void relay_loop() {
    // No-op: relay requests are now processed by the dedicated relay task.
}

#else // !IS_DARKROOM_TIMER

void relay_controller_init() {}
void relay_request(bool) {}
bool relay_is_on() { return false; }
void relay_loop() {}

#endif // IS_DARKROOM_TIMER
