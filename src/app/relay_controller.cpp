#include "relay_controller.h"
#include "board_config.h"

#if IS_DARKROOM_TIMER

#include "config_manager.h"
#include "log_manager.h"
#include "web_portal_state.h"

#include <HTTPClient.h>

#define TAG "Relay"

// ============================================================================
// State
// ============================================================================

static volatile uint8_t g_relay_pending = 0;   // 0=none, 1=ON, 2=OFF
static volatile bool    g_relay_on      = false;

// ============================================================================
// Shelly HTTP backend — fire-and-forget
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
}

// ============================================================================
// Public API
// ============================================================================

void relay_controller_init() {
    g_relay_pending = 0;
    g_relay_on = false;
    LOGI(TAG, "Relay controller initialized (Shelly backend)");
}

void relay_request(bool on) {
    g_relay_on = on;
    g_relay_pending = on ? 1 : 2;
}

bool relay_is_on() {
    return g_relay_on;
}

void relay_loop() {
    uint8_t pending = g_relay_pending;
    if (pending == 0) return;
    g_relay_pending = 0;
    shelly_send(pending == 1);
}

#else // !IS_DARKROOM_TIMER

void relay_controller_init() {}
void relay_request(bool) {}
bool relay_is_on() { return false; }
void relay_loop() {}

#endif // IS_DARKROOM_TIMER
