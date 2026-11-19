#include "relay_controller.h"
#include "board_config.h"

#if IS_DARKROOM_TIMER

#include "action_dispatch.h"
#include "action_list.h"
#include "action_parse.h"
#include "action_registry.h"
#include "action_storage.h"
#include "action_validate.h"
#include "json_key_validation.h"
#include "config_psram.h"
#include "darkroom_timer_payload.h"
#include "log_manager.h"
#include "psram_json_allocator.h"
#include "rtos_task_utils.h"
#include "storage.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFiClient.h>
#include <freertos/semphr.h>
#include <stdlib.h>
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

static constexpr uint32_t RELAY_TASK_STACK_BYTES = 8192;
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
static ActionStorage s_relay_storage = {};
static SemaphoreHandle_t s_config_mutex = nullptr;
static bool s_config_valid = false;

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
static ActionStorage s_deferred_storage = {};
static SemaphoreHandle_t s_deferred_mutex = nullptr;
static bool s_slot_shelly_active = false;
static bool s_slot_shelly_on = false;

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
    if (g_relay_task && xTaskGetCurrentTaskHandle() == g_relay_task && s_slot_shelly_active) {
        shelly_http_send(host, relay_index, s_slot_shelly_on);
        return;
    }
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

static bool relay_parse_config(JsonDocument& doc, ButtonAction* actions,
                               ActionStorage& storage, bool validate_bindings) {
    if (!doc.is<JsonObject>() || !doc["action_format_version"].is<int>() ||
        doc["action_format_version"].as<int>() != 1) {
        LOGW(TAG, "Incompatible relay actions document; recreate it");
        return false;
    }
    ActionStorageScope scope(&storage);
    for (int i = 0; i < RELAY_SLOT_COUNT; ++i) {
        if (!doc[SLOT_KEYS[i]].is<JsonObject>()) {
            LOGW(TAG, "Relay slot %s must be an object", SLOT_KEYS[i]);
            return false;
        }
        JsonObject action = doc[SLOT_KEYS[i]].as<JsonObject>();
        if (action.size() == 0) continue;
        const char* operation = action["op"] | "";
        const char* error = validate_bindings ? action_validate_json(action)
                                              : action_operation_validate_json(action, false);
        if (!error && (action.containsKey("type") || !operation[0]))
            error = "legacy action is not supported";
        if (error) {
            LOGW(TAG, "Invalid relay slot %s: %s", SLOT_KEYS[i], error);
            return false;
        }
        action_parse(action, actions[i]);
        if (storage.failed || !actions[i].type[0]) {
            LOGW(TAG, "Cannot parse relay slot %s", SLOT_KEYS[i]);
            return false;
        }
    }
    return true;
}

void relay_load_config() {
    if (!s_config_mutex || xSemaphoreTake(s_config_mutex, portMAX_DELAY) != pdTRUE) {
        LOGE(TAG, "Cannot lock relay config");
        return;
    }
    if (!Storage.exists(RELAY_CONFIG_PATH)) {
        s_config_valid = true;
        xSemaphoreGive(s_config_mutex);
        LOGD(TAG, "No relay config file, all slots empty");
        return;
    }
    File f = Storage.open(RELAY_CONFIG_PATH, "r");
    if (!f) {
        LOGW(TAG, "Failed to open relay config");
        xSemaphoreGive(s_config_mutex);
        return;
    }
    const size_t file_size = f.size();
    if (file_size == 0 || file_size > 4096) {
        LOGW(TAG, "Invalid relay config size: %u", (unsigned)file_size);
        f.close();
        xSemaphoreGive(s_config_mutex);
        return;
    }
    const JsonKeyScanResult scan = json_stream_scan_keys(f);
    if (scan != JsonKeyScanResult::Valid) {
        LOGW(TAG, "Relay config %s", scan == JsonKeyScanResult::InvalidNul
             ? "contains an invalid JSON NUL" : "could not be read");
        f.close();
        xSemaphoreGive(s_config_mutex);
        return;
    }
    BasicJsonDocument<PsramJsonAllocator> doc(4096);
    const DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err || doc.overflowed()) {
        LOGE(TAG, "Relay config parse error: %s", err.c_str());
        xSemaphoreGive(s_config_mutex);
        return;
    }
    auto* actions = static_cast<ButtonAction*>(
        config_psram_alloc(sizeof(s_relay_actions), "relay_actions"));
    ActionStorage storage = {};
    if (actions && relay_parse_config(doc, actions, storage, false)) {
        action_storage_clear(s_relay_storage);
        memcpy(s_relay_actions, actions, sizeof(s_relay_actions));
        action_storage_move(s_relay_storage, storage);
        s_config_valid = true;
        LOGI(TAG, "Loaded relay config (%u bytes)", (unsigned)file_size);
    } else {
        LOGW(TAG, "Relay config unavailable or incompatible");
    }
    action_storage_clear(storage);
    free(actions);
    xSemaphoreGive(s_config_mutex);
}

void relay_controller_clear_config() {
    if (!s_config_mutex || xSemaphoreTake(s_config_mutex, portMAX_DELAY) != pdTRUE) {
        LOGE(TAG, "Cannot lock relay config for deletion");
        return;
    }
    StorageMutationGuard guard;
    if (!guard || (Storage.exists(RELAY_CONFIG_PATH) && !Storage.remove(RELAY_CONFIG_PATH))) {
        LOGE(TAG, "Cannot delete relay config");
        xSemaphoreGive(s_config_mutex);
        return;
    }
    action_storage_clear(s_relay_storage);
    memset(s_relay_actions, 0, sizeof(s_relay_actions));
    s_config_valid = true;
    xSemaphoreGive(s_config_mutex);
    storage_publish_usage(false);
    LOGI(TAG, "Relay config deleted");
}

// ============================================================================
// Deferred action dispatch (non-shelly types, runs on main loop)
// ============================================================================

static void relay_defer_action(const ButtonAction& act) {
    ActionStorage storage = {};
    ButtonAction copy = {};
    if (!action_storage_copy_actions(storage, &copy, &act, 1)) {
        LOGE(TAG, "Cannot copy deferred relay action");
        action_storage_clear(storage);
        return;
    }
    if (!s_deferred_mutex || xSemaphoreTake(s_deferred_mutex, portMAX_DELAY) != pdTRUE) {
        LOGE(TAG, "Cannot lock deferred relay action");
        action_storage_clear(storage);
        return;
    }
    action_storage_clear(s_deferred_storage);
    s_deferred_action = copy;
    action_storage_move(s_deferred_storage, storage);
    s_deferred_pending = true;
    xSemaphoreGive(s_deferred_mutex);
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

            ButtonAction act = {};
            ActionStorage storage = {};
            if (!s_config_mutex || xSemaphoreTake(s_config_mutex, portMAX_DELAY) != pdTRUE) {
                LOGE(TAG, "Cannot lock relay config for dispatch");
                continue;
            }
            const bool copied = s_config_valid &&
                action_storage_copy_actions(storage, &act, &s_relay_actions[i], 1);
            xSemaphoreGive(s_config_mutex);
            if (!copied) {
                LOGW(TAG, "Cannot copy relay slot %s", SLOT_KEYS[i]);
                action_storage_clear(storage);
                continue;
            }

            const ActionOperationDef* operation = action_operation_for_action(act);
            if (operation && strcmp(operation->id, "shelly.relay.set") == 0) {
                s_slot_shelly_on = (i == 0 || i == 2);
                s_slot_shelly_active = true;
                if (action_dispatch(act, "Relay") != ACTION_COMPLETE)
                    LOGW(TAG, "Relay slot %s failed", SLOT_KEYS[i]);
                s_slot_shelly_active = false;
            } else if (act.type[0]) {
                relay_defer_action(act);
            }
            action_storage_clear(storage);
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

    s_config_mutex = xSemaphoreCreateMutex();
    s_deferred_mutex = xSemaphoreCreateMutex();
    g_relay_sem = xSemaphoreCreateBinary();
    if (!s_config_mutex || !s_deferred_mutex || !g_relay_sem) {
        LOGE(TAG, "Failed to create relay synchronization objects");
        return;
    }

    bool ok = rtos_create_task_psram_stack_pinned(
        relay_task_fn, "relay",
        RELAY_TASK_STACK_BYTES, nullptr,
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
    if (!s_deferred_mutex || xSemaphoreTake(s_deferred_mutex, portMAX_DELAY) != pdTRUE) return;
    ButtonAction act = {};
    ActionStorage storage = {};
    const bool pending = s_deferred_pending;
    if (pending) {
        act = s_deferred_action;
        action_storage_move(storage, s_deferred_storage);
        s_deferred_pending = false;
    }
    xSemaphoreGive(s_deferred_mutex);
    if (pending) {
        action_list_dispatch(&act, 1, "Relay", ACTION_CONTINUATION_OWNER_LOOP);
        action_storage_clear(storage);
    }
}

// ============================================================================
// REST API helpers (called from web_portal_relay.cpp)
// ============================================================================

bool relay_get_config_json(std::string& out) {
    if (!s_config_mutex || xSemaphoreTake(s_config_mutex, portMAX_DELAY) != pdTRUE)
        return false;
    if (!s_config_valid) {
        xSemaphoreGive(s_config_mutex);
        return false;
    }
    BasicJsonDocument<PsramJsonAllocator> doc(4096);
    doc["action_format_version"] = 1;
    for (int i = 0; i < RELAY_SLOT_COUNT; i++) {
        JsonObject obj = doc[SLOT_KEYS[i]].to<JsonObject>();
        if (s_relay_actions[i].type[0]) action_to_json(s_relay_actions[i], obj);
    }
    out = "";
    const bool ok = !doc.overflowed() && serializeJson(doc, out) > 0;
    xSemaphoreGive(s_config_mutex);
    return ok;
}

bool relay_save_config_from_json(const uint8_t* json, size_t len) {
    if (!json || !len || len > 4096 || !s_config_mutex) {
        LOGW(TAG, "Invalid relay config save");
        return false;
    }
    if (!json_keys_valid(json, len)) {
        LOGW(TAG, "Relay config JSON contains an invalid NUL");
        return false;
    }
    BasicJsonDocument<PsramJsonAllocator> doc(4096);
    const DeserializationError err = deserializeJson(doc, json, len);
    if (err || doc.overflowed()) {
        LOGW(TAG, "Invalid relay config JSON: %s", err.c_str());
        return false;
    }
    auto* actions = static_cast<ButtonAction*>(
        config_psram_alloc(sizeof(s_relay_actions), "relay_actions_save"));
    if (!actions) {
        LOGE(TAG, "Cannot allocate relay config");
        return false;
    }
    ActionStorage storage = {};
    if (!relay_parse_config(doc, actions, storage, true)) {
        action_storage_clear(storage);
        free(actions);
        return false;
    }
    if (xSemaphoreTake(s_config_mutex, portMAX_DELAY) != pdTRUE) {
        LOGE(TAG, "Cannot lock relay config for save");
        action_storage_clear(storage);
        free(actions);
        return false;
    }
    const bool saved = storage_write_atomic(RELAY_CONFIG_PATH, json, len);
    if (saved) {
        action_storage_clear(s_relay_storage);
        memcpy(s_relay_actions, actions, sizeof(s_relay_actions));
        action_storage_move(s_relay_storage, storage);
        s_config_valid = true;
    }
    xSemaphoreGive(s_config_mutex);
    action_storage_clear(storage);
    free(actions);
    if (!saved) {
        LOGE(TAG, "Atomic relay config write failed");
        return false;
    }
    storage_publish_usage(false);
    return true;
}

#endif // IS_DARKROOM_TIMER
