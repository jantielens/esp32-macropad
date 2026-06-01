#include "relay_controller.h"
#include "board_config.h"

#if IS_DARKROOM_TIMER

#include "action_dispatch.h"
#include "action_parse.h"
#include "darkroom_timer_payload.h"
#include "log_manager.h"
#include "psram_json_allocator.h"
#include "rtos_task_utils.h"
#include "storage.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <string.h>

#define TAG "Relay"

static const char* RELAY_CONFIG_PATH = "/config/relay_actions.json";

// Slot JSON keys (order matches slot index)
static const char* SLOT_KEYS[RELAY_SLOT_COUNT] = {
    "enlarger_on", "enlarger_off", "safelight_on", "safelight_off"
};

// ============================================================================
// Configuration
// ============================================================================

static constexpr uint32_t RELAY_TASK_STACK_WORDS = 8192;
static constexpr UBaseType_t RELAY_TASK_PRIORITY  = 2;

// ============================================================================
// State
// ============================================================================

static bool         g_relay_on   = false;
static portMUX_TYPE g_relay_lock = portMUX_INITIALIZER_UNLOCKED;

static TaskHandle_t       g_relay_task  = nullptr;
static RtosTaskPsramAlloc g_relay_alloc = {};
static SemaphoreHandle_t  g_relay_sem   = nullptr;

// Cached relay action configs (one per slot)
static ButtonAction s_relay_actions[RELAY_SLOT_COUNT];

// Pending slots bitmask — latest-state semantics (replaced, not accumulated)
static volatile uint8_t s_pending_slots = 0;

// Queued ad-hoc shelly request (from action_dispatch via relay_queue_shelly)
static volatile bool s_adhoc_shelly_pending = false;
static char          s_adhoc_shelly_host[SHELLY_HOST_MAX_LEN] = {};
static uint8_t       s_adhoc_shelly_relay = 0;
static bool          s_adhoc_shelly_on    = true;

// Deferred action for non-shelly types (dispatched on main loop)
static volatile bool s_deferred_pending = false;
static ButtonAction  s_deferred_action;

// ============================================================================
// Persistent Shelly HTTP connection — reused across calls to avoid TCP
// handshake overhead and DNS lookups on every relay toggle.
// ============================================================================

static constexpr uint16_t SHELLY_CONNECT_TIMEOUT_MS = 500;
static constexpr uint16_t SHELLY_READ_TIMEOUT_MS    = 250;

static WiFiClient  s_shelly_client;
static HTTPClient  s_shelly_http;
static bool        s_shelly_active = false;
static char        s_shelly_host[SHELLY_HOST_MAX_LEN] = {};

// Full TCP teardown — next shelly_http_send() call will reconnect.
static void shelly_conn_teardown() {
    s_shelly_http.end();
    vTaskDelay(pdMS_TO_TICKS(100));  // TCP close guard — protect WiFi MAC DMA
    s_shelly_client.stop();
    s_shelly_active = false;
    s_shelly_host[0] = '\0';
}

// Ensure persistent connection state is ready for the given host.
static void shelly_conn_ensure(const char* host) {
    // Tear down if the target host changed
    if (s_shelly_active && strcmp(s_shelly_host, host) != 0) {
        shelly_conn_teardown();
    }

    if (!s_shelly_active) {
        s_shelly_client.setTimeout(SHELLY_READ_TIMEOUT_MS);
        s_shelly_http.setReuse(true);   // HTTP keep-alive
        s_shelly_http.setConnectTimeout(SHELLY_CONNECT_TIMEOUT_MS);
        s_shelly_http.setTimeout(SHELLY_READ_TIMEOUT_MS);
        strncpy(s_shelly_host, host, sizeof(s_shelly_host) - 1);
        s_shelly_host[sizeof(s_shelly_host) - 1] = '\0';
        s_shelly_active = true;
    }
}

// Single HTTP attempt.  Returns the HTTP status code (>0) or an error (<= 0).
static int shelly_http_attempt(const char* url) {
    if (!s_shelly_http.begin(s_shelly_client, url)) return -1;
    int code = s_shelly_http.GET();
    s_shelly_http.end();   // with setReuse: drains body + keeps TCP on success
    return code;
}

static void shelly_http_send(const char* host, uint8_t relay_index, bool on) {
    if (!host || !host[0]) return;

    shelly_conn_ensure(host);

    char url[128];
    snprintf(url, sizeof(url), "http://%s/relay/%u?turn=%s",
             host, relay_index, on ? "on" : "off");

    int code = shelly_http_attempt(url);

    // Retry once on connection-level errors where a fresh TCP socket helps:
    // refused (-1), not connected (-4), connection lost (-5).
    // Do NOT retry on read timeout (-11) — the Shelly received our request
    // but is too busy; retrying just adds another 250ms+ of blocking.
    if (code <= 0 && code != HTTPC_ERROR_READ_TIMEOUT) {
        LOGD(TAG, "Shelly %s:%u %s retry after: %s", host, relay_index,
             on ? "on" : "off", s_shelly_http.errorToString(code).c_str());
        shelly_conn_teardown();
        shelly_conn_ensure(host);
        code = shelly_http_attempt(url);
    }

    if (code > 0) {
        LOGI(TAG, "Shelly %s:%u %s → HTTP %d", host, relay_index,
             on ? "on" : "off", code);
    } else {
        LOGW(TAG, "Shelly %s:%u %s failed: %s", host, relay_index,
             on ? "on" : "off", s_shelly_http.errorToString(code).c_str());
        shelly_conn_teardown();
    }
}

void relay_queue_shelly(const char* host, uint8_t relay_index, bool on) {
    if (!host || !host[0]) return;
    portENTER_CRITICAL(&g_relay_lock);
    strlcpy(s_adhoc_shelly_host, host, sizeof(s_adhoc_shelly_host));
    s_adhoc_shelly_relay = relay_index;
    s_adhoc_shelly_on    = on;
    s_adhoc_shelly_pending = true;
    portEXIT_CRITICAL(&g_relay_lock);
    if (g_relay_sem) xSemaphoreGive(g_relay_sem);
}

// ============================================================================
// Config load / save / clear
// ============================================================================

void relay_load_config() {
    portENTER_CRITICAL(&g_relay_lock);
    memset(s_relay_actions, 0, sizeof(s_relay_actions));
    portEXIT_CRITICAL(&g_relay_lock);

    if (!Storage.exists(RELAY_CONFIG_PATH)) {
        LOGD(TAG, "No relay config file, all slots empty");
        return;
    }

    File f = Storage.open(RELAY_CONFIG_PATH, "r");
    if (!f) {
        LOGW(TAG, "Failed to open relay config");
        return;
    }

    size_t file_size = f.size();
    if (file_size == 0 || file_size > 4096) {
        LOGW(TAG, "Invalid relay config size: %u", (unsigned)file_size);
        f.close();
        return;
    }

    BasicJsonDocument<PsramJsonAllocator> doc(2048);
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        LOGE(TAG, "Relay config parse error: %s", err.c_str());
        return;
    }

    // Parse one slot at a time to avoid a large stack allocation, then copy
    // each parsed action into the cache under lock.
    for (int i = 0; i < RELAY_SLOT_COUNT; i++) {
        ButtonAction parsed;
        memset(&parsed, 0, sizeof(parsed));
        if (doc[SLOT_KEYS[i]].is<JsonObject>()) {
            action_parse(doc[SLOT_KEYS[i]].as<JsonObject>(), parsed);
        }
        portENTER_CRITICAL(&g_relay_lock);
        s_relay_actions[i] = parsed;
        portEXIT_CRITICAL(&g_relay_lock);
    }

    LOGI(TAG, "Loaded relay config (%u bytes)", (unsigned)file_size);
}

static bool relay_save_to_flash(const uint8_t* json, size_t len) {
    if (!json || len == 0) return false;

    File f = Storage.open(RELAY_CONFIG_PATH, "w");
    if (!f) {
        LOGE(TAG, "Failed to open relay config for write");
        return false;
    }

    size_t written = f.write(json, len);
    f.close();
    storage_publish_usage(false);

    if (written != len) {
        LOGE(TAG, "Relay config write failed (%u of %u)",
             (unsigned)written, (unsigned)len);
        return false;
    }

    LOGI(TAG, "Relay config saved (%u bytes)", (unsigned)len);
    return true;
}

void relay_controller_clear_config() {
    portENTER_CRITICAL(&g_relay_lock);
    memset(s_relay_actions, 0, sizeof(s_relay_actions));
    portEXIT_CRITICAL(&g_relay_lock);

    if (Storage.exists(RELAY_CONFIG_PATH)) {
        Storage.remove(RELAY_CONFIG_PATH);
        storage_publish_usage(false);
        LOGI(TAG, "Relay config deleted");
    }
}

// ============================================================================
// Deferred action dispatch (non-shelly types, runs on main loop)
// ============================================================================

static void relay_defer_action(const ButtonAction& act) {
    portENTER_CRITICAL(&g_relay_lock);
    s_deferred_action = act;
    s_deferred_pending = true;
    portEXIT_CRITICAL(&g_relay_lock);
}

// ============================================================================
// Relay FreeRTOS task
// ============================================================================

static void relay_task_fn(void*) {
    for (;;) {
        xSemaphoreTake(g_relay_sem, portMAX_DELAY);

        // Snapshot pending state under lock
        portENTER_CRITICAL(&g_relay_lock);
        uint8_t slots = s_pending_slots;
        s_pending_slots = 0;
        bool adhoc = s_adhoc_shelly_pending;
        char adhoc_host[SHELLY_HOST_MAX_LEN] = {};
        uint8_t adhoc_relay = 0;
        bool adhoc_on = true;
        if (adhoc) {
            strlcpy(adhoc_host, s_adhoc_shelly_host, sizeof(adhoc_host));
            adhoc_relay = s_adhoc_shelly_relay;
            adhoc_on    = s_adhoc_shelly_on;
            s_adhoc_shelly_pending = false;
        }
        portEXIT_CRITICAL(&g_relay_lock);

        // Process ad-hoc shelly request (from action_dispatch)
        if (adhoc && adhoc_host[0]) {
            shelly_http_send(adhoc_host, adhoc_relay, adhoc_on);
        }

        // Process relay slot actions
        for (int i = 0; i < RELAY_SLOT_COUNT; i++) {
            if (!(slots & (1 << i))) continue;

            portENTER_CRITICAL(&g_relay_lock);
            ButtonAction act = s_relay_actions[i];
            portEXIT_CRITICAL(&g_relay_lock);

            if (!act.type[0]) continue;  // empty slot

            if (strcmp(act.type, ACTION_TYPE_SHELLY) == 0) {
                bool on = (i == 0 || i == 2);  // slots 0,2 = ON
                const ShellyPayload& p = shelly_payload(act);
                shelly_http_send(p.host, p.relay, on);
            } else {
                // Defer all non-shelly types to main loop
                relay_defer_action(act);
            }
        }
    }
}

// ============================================================================
// Public API
// ============================================================================

void relay_controller_init() {
    g_relay_on = false;
    s_pending_slots = 0;
    memset(s_relay_actions, 0, sizeof(s_relay_actions));

    g_relay_sem = xSemaphoreCreateBinary();

    bool ok = rtos_create_task_psram_stack_pinned(
        relay_task_fn, "relay",
        RELAY_TASK_STACK_WORDS, nullptr,
        RELAY_TASK_PRIORITY, &g_relay_task, &g_relay_alloc,
        tskNO_AFFINITY);

    if (!ok) {
        LOGE(TAG, "Failed to create relay task");
    } else {
        LOGI(TAG, "Relay task created");
    }
}

void relay_request(bool on) {
    portENTER_CRITICAL(&g_relay_lock);
    g_relay_on = on;
    // Fire enlarger + safelight slots (safelight is inverse of enlarger):
    //   on=true  → enlarger ON (slot 0) + safelight OFF (slot 3)
    //   on=false → enlarger OFF (slot 1) + safelight ON (slot 2)
    s_pending_slots = on ? ((1 << 0) | (1 << 3))
                         : ((1 << 1) | (1 << 2));
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
    if (!s_deferred_pending) return;
    portENTER_CRITICAL(&g_relay_lock);
    ButtonAction act = s_deferred_action;
    s_deferred_pending = false;
    portEXIT_CRITICAL(&g_relay_lock);
    action_dispatch(act, "Relay");
}

// ============================================================================
// REST API helpers (called from web_portal_relay.cpp)
// ============================================================================

bool relay_get_config_json(String& out) {
    // Reference s_relay_actions directly — ArduinoJson stores pointers to
    // char arrays (zero-copy), so the source must outlive serializeJson().
    // No spinlock needed: GET/PUT handlers are serialized by AsyncWebServer,
    // and the relay task only reads s_relay_actions (never writes).
    BasicJsonDocument<PsramJsonAllocator> doc(2048);
    for (int i = 0; i < RELAY_SLOT_COUNT; i++) {
        JsonObject obj = doc[SLOT_KEYS[i]].to<JsonObject>();
        action_to_json(s_relay_actions[i], obj);
    }
    serializeJson(doc, out);
    return true;
}

bool relay_save_config_from_json(const uint8_t* json, size_t len) {
    if (!relay_save_to_flash(json, len)) return false;
    relay_load_config();
    return true;
}

#endif // IS_DARKROOM_TIMER
