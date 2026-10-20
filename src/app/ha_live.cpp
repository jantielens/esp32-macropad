#include "ha_live.h"
#if HAS_HA_LIVE

#include "ha_live_state.h"
#include "ha_lookup.h"
#include "binding_template.h"
#include "binding_sources.h"
#include "pad_binding.h"
#include "pad_config.h"
#include "config_manager.h"
#include "log_manager.h"
#include "ota_activity.h"
#include "rtos_task_utils.h"
#include "bounded_psram_json_allocator.h"

#include <WebSocketsClient.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <freertos/semphr.h>
#include <atomic>
#include <algorithm>
#include <new>
#include <string.h>

static constexpr const char* TAG = "HALive";
static constexpr size_t MESSAGE_BYTES = 15 * 1024;
static constexpr size_t JSON_BYTES = 64 * 1024;
static constexpr uint32_t STACK_BYTES = 8192;
static constexpr uint32_t PREVIEW_MS = 60000;
static_assert(WEBSOCKETS_MAX_DATA_SIZE == MESSAGE_BYTES, "Review WebSockets frame limit");

struct HaEntry {
    char entity[HA_LIVE_ENTITY_BYTES] = {};
    char snapshot[HA_LIVE_SNAPSHOT_BYTES] = {};
    uint32_t preview_until = 0;
    bool saved = false;
    bool available = false;
};

struct HaRuntime {
    HaEntry entries[HA_LIVE_MAX_ENTITIES];
    char entities[HA_LIVE_MAX_ENTITIES][HA_LIVE_ENTITY_BYTES] = {};
    char message[MESSAGE_BYTES + 1] = {};
    HaLiveMessage frame{message, MESSAGE_BYTES};
    char snapshot[HA_LIVE_SNAPSHOT_BYTES] = {};
    char host[CONFIG_HA_URL_MAX_LEN] = {};
    char token[CONFIG_HA_TOKEN_MAX_LEN] = {};
    uint16_t port = 80;
    uint32_t revision = 0, epoch = 0;
    size_t count = 0, largest_message = 0;
    uint32_t fragment_ms = 0;
    BoundedPsramJsonAllocator allocator{JSON_BYTES};
    WebSocketsClient* socket = nullptr;
    const char* failure = nullptr;
    bool terminal = false;
    unsigned phase = 0;
    bool subscribed[HA_LIVE_MAX_ENTITIES] = {};
};

static StaticSemaphore_t g_mutex_storage;
static SemaphoreHandle_t g_mutex = nullptr;
static std::atomic<bool> g_ready{false};
static HaRuntime* g_runtime = nullptr; // Published before the worker starts; never freed while running.
static TaskHandle_t g_task = nullptr;
static char g_host[CONFIG_HA_URL_MAX_LEN] = {};
static char g_token[CONFIG_HA_TOKEN_MAX_LEN] = {};
static uint16_t g_port = 80;
static uint32_t g_revision = 1;
static HaLiveStatus g_status{false, false, 0, 0, 0, "unconfigured", ""};

class HaLock {
public:
    HaLock() { xSemaphoreTake(g_mutex, portMAX_DELAY); }
    ~HaLock() { xSemaphoreGive(g_mutex); }
};

static void set_status(const char* state, const char* error = "", uint32_t revision = 0) {
    HaLock lock;
    if (revision && revision != g_revision) return;
    g_status.state = state;
    g_status.last_error = error;
    g_status.connected = strcmp(state, "connected") == 0;
}

void ha_live_status(HaLiveStatus& status) {
    if (!g_ready.load(std::memory_order_acquire)) {
        status = {false, false, 0, 0, 0, "disabled", ""};
        return;
    }
    HaLock lock;
    status = g_status;
}

bool ha_live_valid_entity(const char* entity) {
    if (!entity || !entity[0] || strlen(entity) >= HA_LIVE_ENTITY_BYTES) return false;
    const char* dot = strchr(entity, '.');
    if (!dot || dot == entity || !dot[1] || strchr(dot + 1, '.')) return false;
    for (const char* p = entity; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= '0' && *p <= '9') || *p == '_' || *p == '.'))
            return false;
    return true;
}

static const char* parse_url(const char* url, char* host, uint16_t& port) {
    if (strncmp(url, "http://", 7)) return "Live HA requires local HTTP; HTTPS is not downgraded";
    if (strlen(url) >= CONFIG_HA_URL_MAX_LEN) return "HA URL too long";
    strlcpy(host, url + 7, CONFIG_HA_URL_MAX_LEN);
    size_t length = strlen(host);
    if (length && host[length - 1] == '/') host[length - 1] = '\0';
    uint32_t value = 80;
    if (char* colon = strchr(host, ':')) {
        *colon++ = '\0';
        value = 0;
        if (!*colon) return "Empty HA port";
        for (const char* p = colon; *p; ++p) {
            if (*p < '0' || *p > '9' || value > 6553) return "Invalid HA port";
            value = value * 10 + unsigned(*p - '0');
        }
    }
    if (!host[0] || !value || value > 65535) return "Invalid HA host/port";
    for (const char* p = host; *p; ++p)
        if (!((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
              (*p >= '0' && *p <= '9') || *p == '-' || *p == '.'))
            return "Use a hostname/IPv4 URL without path, credentials or query";
    port = uint16_t(value);
    return nullptr;
}

void ha_live_configure(const char* url, const char* token) {
    if (!g_ready.load(std::memory_order_acquire)) return;
    ha_lookup_configure(url, token);
    char host[CONFIG_HA_URL_MAX_LEN] = {};
    uint16_t port = 80;
    const bool configured = url && url[0] && token && token[0];
    const char* error = configured ? parse_url(url, host, port) : nullptr;
    if (configured && strlen(token) >= CONFIG_HA_TOKEN_MAX_LEN) error = "HA token too long";
    {
        HaLock lock;
        strlcpy(g_host, error ? "" : host, sizeof(g_host));
        strlcpy(g_token, configured && !error ? token : "", sizeof(g_token));
        g_port = port;
        ++g_revision;
        g_status.configured = configured;
        g_status.connected = false;
        g_status.state = error ? "error" : configured ? "idle" : "unconfigured";
        g_status.last_error = error ? error : "";
        if (g_runtime)
            for (auto& entry : g_runtime->entries) entry.available = false;
    }
    if (error) LOGE(TAG, "%s", error);
    else if (configured) LOGW(TAG, "Live configuration applied; HA token travels over LAN unencrypted");
}

void ha_live_init(const char* url, const char* token) {
    if (g_ready.load(std::memory_order_acquire)) return;
    g_mutex = xSemaphoreCreateMutexStatic(&g_mutex_storage);
    if (!g_mutex) { LOGE(TAG, "Mutex initialization failed"); return; }
    g_ready.store(true, std::memory_order_release);
    ha_live_configure(url, token);
}

static void ensure_entity(const char* entity, bool saved) {
    if (!ha_live_valid_entity(entity)) {
        LOGW(TAG, "Invalid HA entity reference");
        return;
    }
    HaLock lock;
    if (!g_host[0] || !g_token[0]) return;
    if (!g_runtime) {
        void* memory = heap_caps_malloc(sizeof(HaRuntime), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!memory) {
            g_status.state = "error";
            g_status.last_error = "PSRAM runtime allocation failed";
            LOGE(TAG, "%s", g_status.last_error);
            return;
        }
        g_runtime = new (memory) HaRuntime;
        LOGT(TAG, "PSRAM runtime=%u bytes; cache=%u x %u, JSON cap=%u stack=%u",
             unsigned(sizeof(HaRuntime)), unsigned(HA_LIVE_MAX_ENTITIES),
             unsigned(HA_LIVE_SNAPSHOT_BYTES), unsigned(JSON_BYTES), unsigned(STACK_BYTES));
    }
    HaEntry* empty = nullptr;
    for (auto& entry : g_runtime->entries) {
        if (strcmp(entry.entity, entity) == 0) {
            if (saved) { entry.saved = true; entry.preview_until = 0; }
            else if (!entry.saved) entry.preview_until = millis() + PREVIEW_MS;
            return;
        }
        if (!entry.entity[0] && !empty) empty = &entry;
    }
    if (!empty) {
        if (!strcmp(g_status.last_error, "HA entity capacity exceeded (64)")) return;
        g_status.last_error = "HA entity capacity exceeded (64)";
        LOGW(TAG, "%s", g_status.last_error);
        return;
    }
    strlcpy(empty->entity, entity, sizeof(empty->entity));
    empty->saved = saved;
    empty->preview_until = saved ? 0 : millis() + PREVIEW_MS;
    empty->available = false;
    empty->snapshot[0] = '\0';
    ++g_status.entity_count;
    ++g_revision;
}

bool ha_live_read(const char* entity, char* snapshot, size_t size) {
    if (!g_ready.load(std::memory_order_acquire) || !snapshot || size == 0) return false;
    ensure_entity(entity, false);
    HaLock lock;
    if (!g_runtime || !g_status.connected) return false;
    for (const auto& entry : g_runtime->entries) {
        if (strcmp(entry.entity, entity)) continue;
        if (!entry.available || strlen(entry.snapshot) >= size) return false;
        strlcpy(snapshot, entry.snapshot, size);
        return true;
    }
    return false;
}

struct HaCollectContext { bool saved; unsigned depth; };
static void collect_token(const char* scheme, size_t length, const char* params, void* argument) {
    auto& context = *static_cast<HaCollectContext*>(argument);
    if (length == 2 && strncmp(scheme, "ha", length) == 0) {
        const size_t size = strcspn(params, ";");
        if (size >= HA_LIVE_ENTITY_BYTES) { LOGW(TAG, "HA entity ID exceeds limit"); return; }
        char entity[HA_LIVE_ENTITY_BYTES];
        memcpy(entity, params, size);
        entity[size] = '\0';
        ensure_entity(entity, context.saved);
    } else if (length == 3 && strncmp(scheme, "pad", length) == 0 && context.depth < 8) {
        const PadBinding* bindings = nullptr;
        uint8_t count = 0;
        pad_binding_get_bindings(&bindings, &count);
        const size_t name_size = strcspn(params, ";");
        for (uint8_t i = 0; i < count; ++i) {
            if (strlen(bindings[i].name) != name_size || strncmp(params, bindings[i].name, name_size)) continue;
            ++context.depth;
            binding_template_visit_tokens(bindings[i].value, collect_token, &context);
            --context.depth;
            break;
        }
    }
}

static void collect_source(const char* source, void* context) {
    binding_template_visit_tokens(source, collect_token, context);
}

static bool checkpoint(HaRuntime& runtime) {
    if (ota_activity_is_active() || ota_activity_epoch() != runtime.epoch ||
        WiFi.status() != WL_CONNECTED) return false;
    HaLock lock;
    return runtime.revision == g_revision;
}

static void fail(HaRuntime& runtime, const char* error, bool terminal) {
    if (!runtime.failure) runtime.failure = error;
    runtime.terminal = runtime.terminal || terminal;
}

class HaSocket : public WebSocketsClient {
public:
    explicit HaSocket(HaRuntime& runtime) : runtime_(runtime) { _client.cWsHeaderDecode = {}; }
    ~HaSocket() {
        onEvent(nullptr);
        WebSocketsClient::clientDisconnect(&_client);
    }
    bool start() {
        begin(runtime_.host, runtime_.port, "/api/websocket", "");
        setReconnectInterval(0);
        loop();
        setReconnectInterval(60000);
        return _client.status != WSC_NOT_CONNECTED;
    }
protected:
    void clientDisconnect(WSclient_t* client) override {
        if (client->cWsHeaderDecode.payloadLen > MESSAGE_BYTES)
            fail(runtime_, "HA receive frame exceeds 15 KiB", true);
        WebSocketsClient::clientDisconnect(client);
    }
private:
    HaRuntime& runtime_;
};

static bool send(HaRuntime& runtime, JsonDocument& document) {
    if (!checkpoint(runtime)) { fail(runtime, "Configuration/network/OTA transition", false); return false; }
    // The snapshot buffer is unused outside event processing.
    if (document.overflowed() || measureJson(document) >= sizeof(runtime.snapshot)) {
        fail(runtime, "HA outgoing JSON capacity exceeded", true);
        return false;
    }
    const size_t size = serializeJson(document, runtime.snapshot, sizeof(runtime.snapshot));
    const bool ok = runtime.socket->sendTXT(static_cast<const char*>(runtime.snapshot), size);
    memset(runtime.snapshot, 0, sizeof(runtime.snapshot));
    if (!ok) fail(runtime, "HA send failed", false);
    return ok;
}

static void apply_event(HaRuntime& runtime, JsonObjectConst event) {
    JsonDocument state(&runtime.allocator);
    for (size_t i = 0; i < HA_LIVE_MAX_ENTITIES; ++i) {
        const char* entity = runtime.entities[i];
        if (!entity[0]) continue;
        bool affected = !event["a"][entity].isUnbound() || !event["c"][entity].isUnbound();
        for (auto removed : event["r"].as<JsonArrayConst>()) if (removed == entity) affected = true;
        if (!affected) continue;
        {
            HaLock lock;
            if (g_revision != runtime.revision) return;
            strlcpy(runtime.snapshot, runtime.entries[i].snapshot, sizeof(runtime.snapshot));
        }
        state.clear();
        const char* error = nullptr;
        if (runtime.snapshot[0] && deserializeJson(state, static_cast<const char*>(runtime.snapshot)))
            error = "HA cached JSON invalid";
        if (!error) error = ha_live_apply_state(state, event, entity);
        if (!error && measureJson(state) >= HA_LIVE_SNAPSHOT_BYTES) error = "HA entity snapshot exceeds 2047 bytes";
        if (!error && state.overflowed()) error = "HA JSON budget exceeded";
        if (!error) serializeJson(state, runtime.snapshot, sizeof(runtime.snapshot));
        {
            HaLock lock;
            if (g_revision != runtime.revision) return;
            auto& entry = runtime.entries[i];
            entry.available = !error && ha_live_state_available(state.as<JsonVariantConst>());
            if (error) entry.snapshot[0] = '\0';
            else strlcpy(entry.snapshot, runtime.snapshot, sizeof(entry.snapshot));
            if (error) g_status.last_error = error;
        }
        if (error) LOGW(TAG, "%s: %s", entity, error);
    }
}

static void process_message(HaRuntime& runtime) {
    if (!checkpoint(runtime)) { fail(runtime, "Configuration/network/OTA transition", false); return; }
    runtime.largest_message = std::max(runtime.largest_message, runtime.frame.size);
    JsonDocument document(&runtime.allocator);
    const auto error = deserializeJson(document, static_cast<const char*>(runtime.message),
                                       runtime.frame.size, DeserializationOption::NestingLimit(16));
    if (error) { fail(runtime, "HA message invalid or JSON budget exceeded", true); return; }
    {
        HaLock lock;
        ++g_status.messages;
    }
    const char* type = document["type"] | "";
    if (!strcmp(type, "auth_required") && runtime.phase == 0) {
        JsonDocument auth(&runtime.allocator);
        auth["type"] = "auth";
        auth["access_token"] = runtime.token;
        runtime.phase = 1;
        set_status("authenticating", "", runtime.revision);
        send(runtime, auth);
    } else if (!strcmp(type, "auth_ok") && runtime.phase == 1) {
        runtime.phase = 2;
        set_status("subscribing", "", runtime.revision);
        // Separate subscriptions bound initial-snapshot size per entity rather
        // than depending on HA fitting all 64 snapshots into one frame.
        for (size_t i = 0; i < HA_LIVE_MAX_ENTITIES; ++i) {
            if (!runtime.entities[i][0]) continue;
            JsonDocument subscription(&runtime.allocator);
            subscription["id"] = i + 1;
            subscription["type"] = "subscribe_entities";
            subscription["entity_ids"].to<JsonArray>().add(runtime.entities[i]);
            if (!send(runtime, subscription)) break;
            taskYIELD();
        }
    } else if (!strcmp(type, "auth_invalid")) {
        fail(runtime, "HA authentication rejected; update token", true);
    } else if (!strcmp(type, "result")) {
        const unsigned id = document["id"] | 0U;
        if (!id || id > HA_LIVE_MAX_ENTITIES || !runtime.entities[id - 1][0] ||
            !document["success"].is<bool>() || !document["success"].as<bool>()) {
            fail(runtime, "HA subscription rejected", true);
            return;
        }
        runtime.subscribed[id - 1] = true;
        size_t active = 0;
        for (bool subscribed : runtime.subscribed) if (subscribed) ++active;
        if (active == runtime.count) {
            runtime.phase = 3;
            set_status("connected", "", runtime.revision);
            LOGI(TAG, "Connected entities=%u revision=%lu", unsigned(active), (unsigned long)runtime.revision);
        }
    } else if (!strcmp(type, "event")) {
        const unsigned id = document["id"] | 0U;
        if (!id || id > HA_LIVE_MAX_ENTITIES || !runtime.entities[id - 1][0] ||
            !document["event"].is<JsonObjectConst>()) {
            fail(runtime, "HA invalid subscription event", true);
            return;
        }
        apply_event(runtime, document["event"].as<JsonObjectConst>());
    } else fail(runtime, "Unexpected HA protocol message", true);
}

static void socket_event(HaRuntime& runtime, WStype_t type, uint8_t* bytes, size_t size) {
    if (type == WStype_DISCONNECTED) { fail(runtime, "HA disconnected", false); return; }
    if (runtime.failure || type == WStype_PING || type == WStype_PONG) return;
    if (!checkpoint(runtime)) { fail(runtime, "Configuration/network/OTA transition", false); return; }
    if (type == WStype_CONNECTED) return;
    bool more = false;
    bool start = false;
    if (type == WStype_TEXT || type == WStype_FRAGMENT_TEXT_START) {
        start = true;
        runtime.fragment_ms = millis();
        more = type == WStype_FRAGMENT_TEXT_START;
    } else if (type == WStype_FRAGMENT || type == WStype_FRAGMENT_FIN) {
        more = type == WStype_FRAGMENT;
    } else { fail(runtime, "Unsupported HA WebSocket message", true); return; }
    if (const char* error = runtime.frame.append(bytes, size, start, more)) {
        fail(runtime, error, true);
        return;
    }
    if (!more) process_message(runtime);
}

static void worker(void* argument) {
    auto& runtime = *static_cast<HaRuntime*>(argument);
    uint32_t retry_at = 0, retry_ms = 2000, blocked_revision = 0, last_report = millis();
    for (;;) {
        {
            HaLock lock;
            if (runtime.revision != g_revision) { retry_at = millis(); retry_ms = 2000; blocked_revision = 0; }
            runtime.revision = g_revision;
            strlcpy(runtime.host, g_host, sizeof(runtime.host));
            strlcpy(runtime.token, g_token, sizeof(runtime.token));
            runtime.port = g_port;
            runtime.count = 0;
            for (size_t i = 0; i < HA_LIVE_MAX_ENTITIES; ++i) {
                strlcpy(runtime.entities[i], runtime.entries[i].entity, HA_LIVE_ENTITY_BYTES);
                if (runtime.entities[i][0]) ++runtime.count;
            }
        }
        if (!runtime.count || !runtime.host[0] || !runtime.token[0] ||
            blocked_revision == runtime.revision) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (ota_activity_is_active() || WiFi.status() != WL_CONNECTED) {
            set_status("paused", ota_activity_is_active() ? "OTA active" : "WiFi disconnected", runtime.revision);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        if (int32_t(millis() - retry_at) < 0) { vTaskDelay(pdMS_TO_TICKS(50)); continue; }
        runtime.failure = nullptr;
        runtime.terminal = false;
        runtime.phase = 0;
        runtime.frame.fragmented = false;
        runtime.frame.size = 0;
        memset(runtime.subscribed, 0, sizeof(runtime.subscribed));
        runtime.epoch = ota_activity_epoch();
        {
            HaLock lock;
            for (auto& entry : runtime.entries) { entry.available = false; entry.snapshot[0] = '\0'; }
            if (g_status.messages) ++g_status.reconnects;
        }
        set_status("connecting", "", runtime.revision);
        LOGI(TAG, "Connecting entities=%u revision=%lu", unsigned(runtime.count), (unsigned long)runtime.revision);
        const uint32_t started = millis();
        {
            HaSocket socket(runtime);
            runtime.socket = &socket;
            socket.onEvent([&runtime](WStype_t type, uint8_t* bytes, size_t size) {
                socket_event(runtime, type, bytes, size);
            });
            socket.enableHeartbeat(15000, 5000, 2);
            if (!checkpoint(runtime)) fail(runtime, "Configuration/network/OTA transition", false);
            else if (!socket.start()) fail(runtime, "HA TCP connection failed", false);
            while (!runtime.failure) {
                if (!checkpoint(runtime)) { fail(runtime, "Configuration/network/OTA transition", false); break; }
                if ((runtime.phase != 3 && uint32_t(millis() - started) > 15000) ||
                    (runtime.frame.fragmented && uint32_t(millis() - runtime.fragment_ms) > 15000)) {
                    fail(runtime, "HA handshake/fragment timeout", false);
                    break;
                }
                socket.loop();
                if (uint32_t(millis() - last_report) >= 300000) {
                    LOGT(TAG, "entities=%u max_message=%u JSON_peak=%u stack_free_min=%u int_free=%u psram_free=%u",
                         unsigned(runtime.count), unsigned(runtime.largest_message), unsigned(runtime.allocator.peak),
                         unsigned(uxTaskGetStackHighWaterMark(nullptr)),
                         unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                         unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
                    last_report = millis();
                }
                vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
        runtime.socket = nullptr;
        bool subscriptions_changed;
        {
            HaLock lock;
            subscriptions_changed = runtime.revision != g_revision;
            for (auto& entry : runtime.entries) entry.available = false;
        }
        const bool expected_transition = !runtime.terminal &&
            !strcmp(runtime.failure, "Configuration/network/OTA transition") &&
            (subscriptions_changed || ota_activity_is_active() || ota_activity_epoch() != runtime.epoch);
        if (expected_transition) LOGT(TAG, "%s", runtime.failure);
        else LOGW(TAG, "%s%s", runtime.failure, runtime.terminal ? "; waiting for configuration/subscription change" : "");
        set_status(runtime.terminal ? "error" : "retrying", runtime.failure, runtime.revision);
        if (runtime.terminal) blocked_revision = runtime.revision;
        else {
            if (runtime.phase == 3 && uint32_t(millis() - started) >= 30000) retry_ms = 2000;
            retry_at = millis() + retry_ms;
            retry_ms = std::min(retry_ms * 2, uint32_t(60000));
        }
    }
}

void ha_live_tick() {
    if (!g_ready.load(std::memory_order_acquire)) return;
    static uint32_t generation = UINT32_MAX, config_revision = 0, checked_ms = 0;
    if (uint32_t(millis() - checked_ms) < 1000 || ota_activity_is_active()) return;
    checked_ms = millis();
    uint32_t revision;
    {
        HaLock lock;
        revision = g_revision;
    }
    if (generation != pad_config_get_generation() || config_revision != revision) {
        if (g_runtime) {
            HaLock lock;
            for (auto& entry : g_runtime->entries) entry.saved = false;
        }
        HaCollectContext context{true, 0};
        binding_sources_visit(collect_source, &context);
        generation = pad_config_get_generation();
    }
    if (!g_runtime) return;
    {
        HaLock lock;
        bool removed = false;
        for (auto& entry : g_runtime->entries) {
            if (!entry.entity[0] || entry.saved ||
                (entry.preview_until && int32_t(entry.preview_until - millis()) > 0)) continue;
            entry.entity[0] = '\0';
            entry.snapshot[0] = '\0';
            entry.preview_until = 0;
            entry.available = false;
            entry.saved = false;
            --g_status.entity_count;
            ++g_revision;
            removed = true;
        }
        config_revision = removed ? ~g_revision : g_revision;
        if (!g_status.entity_count && g_status.configured && g_host[0]) {
            g_status.state = "idle";
            g_status.connected = false;
        }
    }
    if (!g_task) {
        if (!rtos_create_task_psram_stack(worker, "ha_live", STACK_BYTES, g_runtime, 1, &g_task, nullptr)) {
            set_status("error", "HA PSRAM task allocation failed");
            LOGE(TAG, "PSRAM task allocation failed");
        }
    }
}
#endif
