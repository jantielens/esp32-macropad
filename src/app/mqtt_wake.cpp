#include "mqtt_wake.h"

#include "board_config.h"

#if HAS_MQTT && HAS_DISPLAY

#include "binding_template.h"
#include "screen_saver_manager.h"
#include "log_manager.h"

#include <string.h>

static const char* TAG = "MqttWake";

static const DeviceConfig* g_config = nullptr;
static bool g_was_on = false;        // previous resolved state (for edge detection)
static bool g_active = false;        // feature enabled (non-empty binding)
static uint32_t g_last_keepalive = 0; // throttle keep-alive to ~1 s

static constexpr uint32_t KEEPALIVE_INTERVAL_MS = 1000;

void mqtt_wake_init(const DeviceConfig* config) {
    g_config = config;
    g_was_on = false;
    g_last_keepalive = 0;
    g_active = config && strlen(config->screen_saver_wake_binding) > 0
               && binding_template_has_bindings(config->screen_saver_wake_binding);

    if (g_active) {
        LOGI(TAG, "Init: %s", config->screen_saver_wake_binding);
    }
}

void mqtt_wake_loop() {
    if (!g_active) return;

    char resolved[64];
    binding_template_resolve(g_config->screen_saver_wake_binding, resolved, sizeof(resolved));

    const bool is_on = (strcmp(resolved, "ON") == 0);

    if (is_on && !g_was_on) {
        // Rising edge: wake the screen (even from sleep).
        LOGI(TAG, "Wake triggered");
        screen_saver_manager_notify_activity(true);
        g_last_keepalive = millis();
    } else if (is_on) {
        // Sustained ON: reset idle timer periodically so screensaver stays off.
        const uint32_t now = millis();
        if (now - g_last_keepalive >= KEEPALIVE_INTERVAL_MS) {
            screen_saver_manager_notify_activity(false);
            g_last_keepalive = now;
        }
    }

    g_was_on = is_on;
}

#endif // HAS_MQTT && HAS_DISPLAY
