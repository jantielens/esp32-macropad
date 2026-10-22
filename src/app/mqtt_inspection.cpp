#include "mqtt_inspection.h"

#if HAS_MQTT && HAS_DISPLAY

#include "mqtt_manager.h"
#include "mqtt_sub_store.h"
#include "log_manager.h"
#include "ota_activity.h"
#include "psram_json_allocator.h"
#include "web_portal_auth.h"
#include "web_portal_json.h"

#include <ArduinoJson.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <stdint.h>
#include <new>
#include <stdlib.h>
#include <string.h>

namespace {

constexpr size_t kTopicBytes = 127;
constexpr size_t kRequestBodyBytes = 826;
constexpr size_t kResponseBodyBytes =
    6 * (MQTT_SUB_STORE_MAX_VALUE_LEN - 1) + 256;
constexpr uint8_t kMaxHandles = 4;
constexpr uint8_t kExpiredHandleCapacity = 8;
constexpr uint8_t kBodySlotCapacity = 4;
constexpr uint32_t kAcquireTimeoutMs = 10000;
constexpr uint32_t kHandleLifetimeMs = 30000;
constexpr uint32_t kBodySlotLifetimeMs = 5000;
constexpr uint8_t kMaxCleanupAttempts = 3;
constexpr uint32_t kCleanupRetryMs = 500;

static_assert(MQTT_SUB_STORE_MAX_VALUE_LEN >= 2,
              "MQTT inspection requires a nonempty store buffer");
static_assert(CONFIG_MQTT_TOPIC_MAX_LEN > kTopicBytes,
              "MQTT topic storage must hold the inspection topic limit");

enum class HandleStatus : uint8_t {
    Queued,
    Waiting,
    Received,
    Failed,
    ReleasePending,
    Released,
    CleanupFailed,
    Expired,
};

enum class TopicParseResult : uint8_t {
    Valid,
    Invalid,
    OutOfMemory,
};

struct InspectionHandle {
    uint32_t id = 0;
    uint32_t created_ms = 0;
    char topic[kTopicBytes + 1] = {};
    char error[32] = {};
    uint8_t cleanup_attempts = 0;
    uint32_t last_cleanup_ms = 0;
    size_t sample_len = 0;
    uint8_t sample[MQTT_SUB_STORE_MAX_VALUE_LEN] = {};
    HandleStatus status = HandleStatus::Queued;
    bool used = false;
    bool cached = false;
    bool received = false;
    bool truncated = false;
    bool sample_available = false;
    bool unsupported_encoding = false;
    bool cleanup_failed = false;
    bool store_interest_held = false;
    bool release_requested = false;
    bool expire_requested = false;
    bool fail_after_release = false;
};

struct ExpiredHandle {
    uint32_t id = 0;
    bool cleanup_failed = false;
};

struct BodySlot {
    AsyncWebServerRequest* request = nullptr;
    uint32_t last_activity_ms = 0;
    size_t total = 0;
    size_t received = 0;
    char data[kRequestBodyBytes + 1] = {};
};

SemaphoreHandle_t g_inspection_mutex = nullptr;
SemaphoreHandle_t g_body_mutex = nullptr;
struct InspectionRuntime {
    InspectionHandle handles[kMaxHandles] = {};
    ExpiredHandle expired[kExpiredHandleCapacity] = {};
    BodySlot body_slots[kBodySlotCapacity] = {};
    uint8_t expired_next = 0;
};
InspectionRuntime* g_runtime = nullptr;

bool inspection_ready() {
    return g_runtime && g_inspection_mutex && g_body_mutex;
}

const char* status_name(HandleStatus status) {
    switch (status) {
        case HandleStatus::Queued: return "queued";
        case HandleStatus::Waiting: return "waiting";
        case HandleStatus::Received: return "received";
        case HandleStatus::Failed: return "failed";
        case HandleStatus::ReleasePending: return "release_pending";
        case HandleStatus::Released: return "released";
        case HandleStatus::CleanupFailed: return "cleanup_failed";
        case HandleStatus::Expired: return "expired";
    }
    return "failed";
}

bool valid_utf8(const uint8_t* text, size_t length, bool reject_nul) {
    if (!text && length != 0) return false;
    size_t i = 0;
    while (i < length) {
        const uint8_t first = text[i];
        if (first == 0 && reject_nul) return false;
        if (first <= 0x7F) {
            ++i;
            continue;
        }

        uint32_t codepoint = 0;
        size_t continuation_count = 0;
        if (first >= 0xC2 && first <= 0xDF) {
            codepoint = first & 0x1F;
            continuation_count = 1;
        } else if (first >= 0xE0 && first <= 0xEF) {
            codepoint = first & 0x0F;
            continuation_count = 2;
        } else if (first >= 0xF0 && first <= 0xF4) {
            codepoint = first & 0x07;
            continuation_count = 3;
        } else {
            return false;
        }

        if (continuation_count > length - i - 1) return false;
        for (size_t j = 1; j <= continuation_count; ++j) {
            const uint8_t next = text[i + j];
            if ((next & 0xC0) != 0x80) return false;
            codepoint = (codepoint << 6) | (next & 0x3F);
        }
        if ((continuation_count == 1 && codepoint < 0x80) ||
            (continuation_count == 2 && codepoint < 0x800) ||
            (continuation_count == 3 && codepoint < 0x10000) ||
            (codepoint >= 0xD800 && codepoint <= 0xDFFF) ||
            codepoint > 0x10FFFF) {
            return false;
        }
        i += continuation_count + 1;
    }
    return true;
}

bool valid_topic(const uint8_t* topic, size_t length) {
    if (!topic || length == 0 || length > kTopicBytes ||
        !valid_utf8(topic, length, true)) {
        return false;
    }
    for (size_t i = 0; i < length; ++i) {
        switch (topic[i]) {
            case '+':
            case '#':
            case ';':
            case '[':
            case ']':
            case '|':
                return false;
            default:
                break;
        }
    }
    return true;
}

bool lock(SemaphoreHandle_t mutex) {
    return mutex && xSemaphoreTake(mutex, pdMS_TO_TICKS(50)) == pdTRUE;
}

void send_error(AsyncWebServerRequest* request, int status, const char* error) {
    if (!request) return;
    char body[96];
    snprintf(body, sizeof(body), "{\"error\":\"%s\"}", error ? error : "request_failed");
    request->send(status, "application/json", body);
}

void send_status(AsyncWebServerRequest* request, int http_status,
                 uint32_t id, HandleStatus status, const char* error = nullptr,
                 bool cleanup_failed = false) {
    auto doc = make_psram_json_doc(384);
    if (!doc || doc->capacity() == 0) {
        send_error(request, 503, "out_of_memory");
        return;
    }
    (*doc)["handle"] = id;
    (*doc)["status"] = status_name(status);
    if (error && error[0]) (*doc)["error"] = error;
    if (status == HandleStatus::Expired) (*doc)["cleanup_failed"] = cleanup_failed;
    web_portal_send_json_sized(request, doc, http_status);
}

void send_handle(AsyncWebServerRequest* request, const InspectionHandle& handle) {
    if (handle.status == HandleStatus::Expired) {
        send_status(request, 410, handle.id, HandleStatus::Expired,
                    "expired", handle.cleanup_failed);
        return;
    }
    auto doc = make_psram_json_doc(4096);
    if (!doc || doc->capacity() == 0) {
        send_error(request, 503, "out_of_memory");
        return;
    }
    (*doc)["handle"] = handle.id;
    (*doc)["status"] = status_name(handle.status);
    (*doc)["received"] = handle.received;
    (*doc)["truncated"] = handle.truncated;
    (*doc)["cached"] = handle.cached;
    if (handle.error[0]) (*doc)["error"] = handle.error;
    if (handle.sample_available) {
        (*doc)["sample"] = JsonString(
            reinterpret_cast<const char*>(handle.sample), handle.sample_len);
    }
    if (measureJson(*doc) > kResponseBodyBytes) {
        send_error(request, 500, "response_too_large");
        return;
    }
    web_portal_send_json_sized(request, doc, 200);
}

InspectionHandle* find_handle(uint32_t id) {
    for (InspectionHandle& handle : g_runtime->handles) {
        if (handle.used && handle.id == id) return &handle;
    }
    return nullptr;
}

const ExpiredHandle* find_expired(uint32_t id) {
    for (const ExpiredHandle& handle : g_runtime->expired) {
        if (handle.id == id) return &handle;
    }
    return nullptr;
}

bool id_in_use(uint32_t id) {
    return id == 0 || find_handle(id) || find_expired(id);
}

void remember_expired(uint32_t id, bool cleanup_failed) {
    ExpiredHandle& handle = g_runtime->expired[g_runtime->expired_next];
    handle.id = id;
    handle.cleanup_failed = cleanup_failed;
    g_runtime->expired_next = static_cast<uint8_t>((g_runtime->expired_next + 1) % kExpiredHandleCapacity);
}

void reset_handle(InspectionHandle& handle) {
    memset(&handle, 0, sizeof(handle));
}

InspectionHandle* allocate_handle() {
    for (InspectionHandle& handle : g_runtime->handles) {
        if (!handle.used || handle.status == HandleStatus::Failed ||
            handle.status == HandleStatus::Released) {
            reset_handle(handle);
            handle.used = true;
            return &handle;
        }
    }
    return nullptr;
}

bool get_handle_id(AsyncWebServerRequest* request, uint32_t* id) {
    if (!request || !id || !request->hasParam("handle")) return false;
    const String value = request->getParam("handle")->value();
    if (!value.length()) return false;
    uint32_t parsed = 0;
    for (size_t i = 0; i < value.length(); ++i) {
        const char ch = value[i];
        if (ch < '0' || ch > '9') return false;
        const uint32_t digit = static_cast<uint32_t>(ch - '0');
        if (parsed > (UINT32_MAX - digit) / 10) return false;
        parsed = parsed * 10 + digit;
    }
    if (parsed == 0) return false;
    *id = parsed;
    return true;
}

BodySlot* find_body_slot(AsyncWebServerRequest* request) {
    for (BodySlot& slot : g_runtime->body_slots) {
        if (slot.request == request) return &slot;
    }
    return nullptr;
}

void clear_body_slot(BodySlot& slot) {
    slot = {};
}

void cleanup_stale_body_slots(uint32_t now) {
    for (BodySlot& slot : g_runtime->body_slots) {
        if (slot.request && now - slot.last_activity_ms >= kBodySlotLifetimeMs) {
            clear_body_slot(slot);
        }
    }
}

TopicParseResult parse_topic(const char* body, size_t body_length, char* topic_out) {
    BasicJsonDocument<PsramJsonAllocator> doc(1024);
    if (doc.capacity() == 0) return TopicParseResult::OutOfMemory;
    const DeserializationError error = deserializeJson(doc, body, body_length);
    if (error) {
        return error == DeserializationError::NoMemory
            ? TopicParseResult::OutOfMemory
            : TopicParseResult::Invalid;
    }
    if (!doc.is<JsonObject>()) return TopicParseResult::Invalid;
    JsonObjectConst object = doc.as<JsonObjectConst>();
    if (object.size() != 1 || !object.containsKey("topic")) return TopicParseResult::Invalid;
    JsonVariantConst value = object["topic"];
    if (!value.is<JsonString>()) return TopicParseResult::Invalid;
    const JsonString topic = value.as<JsonString>();
    if (!valid_topic(reinterpret_cast<const uint8_t*>(topic.c_str()), topic.size())) {
        return TopicParseResult::Invalid;
    }
    memcpy(topic_out, topic.c_str(), topic.size());
    topic_out[topic.size()] = '\0';
    return TopicParseResult::Valid;
}

void begin_release(InspectionHandle& handle, bool expired, bool failed) {
    handle.release_requested = true;
    handle.expire_requested = expired;
    handle.fail_after_release = failed;
    if (expired) {
        handle.status = HandleStatus::Expired;
    } else {
        handle.status = HandleStatus::ReleasePending;
    }
}

void complete_release(InspectionHandle& handle, uint32_t now) {
    if (handle.store_interest_held) {
        if (handle.cleanup_attempts &&
            static_cast<uint32_t>(now - handle.last_cleanup_ms) < kCleanupRetryMs) {
            return;
        }
        if (!mqtt_sub_store_inspection_release(handle.topic)) {
            handle.cleanup_attempts = static_cast<uint8_t>(
                handle.cleanup_attempts < UINT8_MAX ? handle.cleanup_attempts + 1 : UINT8_MAX);
            handle.last_cleanup_ms = now;
            if (handle.cleanup_attempts >= kMaxCleanupAttempts) {
                handle.status = HandleStatus::CleanupFailed;
                handle.cleanup_failed = true;
                strlcpy(handle.error, "store_release_failed", sizeof(handle.error));
            }
            return;
        }
        handle.store_interest_held = false;
        handle.cleanup_attempts = 0;
        handle.last_cleanup_ms = now;
    }

    const MqttSubscriptionCleanup cleanup = mqtt_manager.subscriptionCleanup(handle.topic);
    if (cleanup == MqttSubscriptionCleanup::Pending) {
        if (handle.expire_requested) return;
        handle.status = HandleStatus::ReleasePending;
        return;
    }

    if (cleanup == MqttSubscriptionCleanup::Failed) {
        handle.cleanup_failed = true;
        if (handle.expire_requested) {
            remember_expired(handle.id, true);
            reset_handle(handle);
        } else {
            handle.status = HandleStatus::CleanupFailed;
            strlcpy(handle.error, "unsubscribe_failed", sizeof(handle.error));
        }
        return;
    }

    if (handle.expire_requested) {
        remember_expired(handle.id, false);
        reset_handle(handle);
    } else if (handle.fail_after_release) {
        handle.status = HandleStatus::Failed;
    } else {
        handle.status = HandleStatus::Released;
    }
}

void process_handle(InspectionHandle& handle, uint32_t now) {
    if (handle.status == HandleStatus::CleanupFailed) {
        if (static_cast<uint32_t>(now - handle.created_ms) >= kHandleLifetimeMs) {
            remember_expired(handle.id, true);
            reset_handle(handle);
        }
        return;
    }
    if (static_cast<uint32_t>(now - handle.created_ms) >= kHandleLifetimeMs &&
        !handle.release_requested) {
        begin_release(handle, true, false);
    }

    if (handle.release_requested) {
        complete_release(handle, now);
        return;
    }

    if (handle.status == HandleStatus::Received || handle.status == HandleStatus::Failed) {
        return;
    }

    if (static_cast<uint32_t>(now - handle.created_ms) >= kAcquireTimeoutMs) {
        strlcpy(handle.error, "timeout", sizeof(handle.error));
        begin_release(handle, false, true);
        complete_release(handle, now);
        return;
    }

    const MqttSubscriptionState subscription =
        mqtt_manager.subscriptionState(handle.topic, MqttSubscriptionOwner::Inspection);
    if (subscription == MqttSubscriptionState::Failed) {
        strlcpy(handle.error, "subscribe_failed", sizeof(handle.error));
        begin_release(handle, false, true);
        complete_release(handle, now);
        return;
    }
    if (subscription == MqttSubscriptionState::Accepted) {
        handle.status = HandleStatus::Waiting;
    }

    bool received = false;
    handle.sample_len = 0;
    handle.sample_available = false;
    if (!mqtt_sub_store_inspection_snapshot(
            handle.topic, handle.sample, sizeof(handle.sample),
            &handle.sample_len, &received, &handle.truncated) ||
        !received) {
        return;
    }

    handle.received = true;
    if (!valid_utf8(handle.sample, handle.sample_len, true)) {
        handle.unsupported_encoding = true;
        strlcpy(handle.error, "unsupported_encoding", sizeof(handle.error));
        handle.sample_available = false;
    } else {
        handle.sample[handle.sample_len] = '\0';
        handle.sample_available = true;
    }
    handle.status = HandleStatus::Received;
}

void handle_post_body(AsyncWebServerRequest* request, uint8_t* data,
                      size_t length, size_t index, size_t total) {
    if (!request || request->isSent()) return;
    if (!portal_auth_gate(request)) return;
    if (!inspection_ready()) {
        send_error(request, 503, "inspection_unavailable");
        return;
    }
    if (!data || total == 0 || total > kRequestBodyBytes) {
        send_error(request, 400, total > kRequestBodyBytes ? "body_too_large" : "invalid_body");
        return;
    }

    char body[kRequestBodyBytes + 1] = {};
    size_t body_length = 0;
    bool body_complete = false;
    const uint32_t now = millis();
    if (!lock(g_body_mutex)) {
        send_error(request, 503, "body_capacity_unavailable");
        return;
    }
    cleanup_stale_body_slots(now);

    BodySlot* slot = find_body_slot(request);
    if (index == 0) {
        if (!slot) {
            for (BodySlot& candidate : g_runtime->body_slots) {
                if (!candidate.request) {
                    slot = &candidate;
                    break;
                }
            }
        }
        if (!slot) {
            xSemaphoreGive(g_body_mutex);
            send_error(request, 429, "request_capacity_exhausted");
            return;
        }
        clear_body_slot(*slot);
        slot->request = request;
        slot->total = total;
    }

    if (!slot || slot->total != total || slot->received != index ||
        length > total - index) {
        if (slot) clear_body_slot(*slot);
        xSemaphoreGive(g_body_mutex);
        send_error(request, 400, "invalid_body_sequence");
        return;
    }

    if (length) memcpy(slot->data + index, data, length);
    slot->received += length;
    slot->last_activity_ms = now;
    if (slot->received == slot->total) {
        memcpy(body, slot->data, slot->total);
        body[slot->total] = '\0';
        body_length = slot->total;
        clear_body_slot(*slot);
        body_complete = true;
    }
    xSemaphoreGive(g_body_mutex);
    if (!body_complete) return;

    char topic[kTopicBytes + 1] = {};
    const TopicParseResult parsed = parse_topic(body, body_length, topic);
    if (parsed == TopicParseResult::OutOfMemory) {
        send_error(request, 503, "out_of_memory");
        return;
    }
    if (parsed != TopicParseResult::Valid) {
        send_error(request, 400, "invalid_topic_or_json");
        return;
    }
    if (ota_activity_is_active()) {
        send_error(request, 503, "ota_active");
        return;
    }
    if (!mqtt_manager.enabled() || !mqtt_sub_store_active()) {
        send_error(request, 503, "broker_unconfigured");
        return;
    }
    if (!mqtt_manager.connectionAvailableSnapshot()) {
        send_error(request, 503, "broker_disconnected");
        return;
    }
    if (!lock(g_inspection_mutex)) {
        send_error(request, 503, "inspection_unavailable");
        return;
    }

    InspectionHandle* handle = allocate_handle();
    if (!handle) {
        xSemaphoreGive(g_inspection_mutex);
        send_error(request, 429, "inspection_capacity_exhausted");
        return;
    }
    uint32_t id = esp_random();
    for (uint8_t attempt = 0; attempt < kExpiredHandleCapacity + kMaxHandles + 1 &&
         id_in_use(id); ++attempt) {
        ++id;
        if (id == 0) ++id;
    }
    if (id_in_use(id)) {
        reset_handle(*handle);
        xSemaphoreGive(g_inspection_mutex);
        send_error(request, 503, "handle_generation_failed");
        return;
    }

    bool cached = false;
    if (!mqtt_sub_store_inspection_acquire(topic, &cached)) {
        reset_handle(*handle);
        xSemaphoreGive(g_inspection_mutex);
        send_error(request, 503, "subscription_unavailable");
        return;
    }
    handle->id = id;
    handle->created_ms = millis();
    memcpy(handle->topic, topic, strlen(topic) + 1);
    handle->cached = cached;
    handle->store_interest_held = true;
    xSemaphoreGive(g_inspection_mutex);
    send_status(request, 202, id, HandleStatus::Queued);
}

void handle_get(AsyncWebServerRequest* request) {
    if (!portal_auth_gate(request)) return;
    if (!inspection_ready()) {
        send_error(request, 503, "inspection_unavailable");
        return;
    }
    uint32_t id = 0;
    if (!get_handle_id(request, &id)) {
        send_error(request, 400, "invalid_handle");
        return;
    }
    if (!lock(g_inspection_mutex)) {
        send_error(request, 503, "inspection_unavailable");
        return;
    }
    InspectionHandle snapshot{};
    const InspectionHandle* handle = find_handle(id);
    if (handle) snapshot = *handle;
    const ExpiredHandle* expired = find_expired(id);
    ExpiredHandle expired_snapshot{};
    if (expired) expired_snapshot = *expired;
    xSemaphoreGive(g_inspection_mutex);

    if (handle) {
        send_handle(request, snapshot);
    } else if (expired) {
        send_status(request, 410, id, HandleStatus::Expired, "expired",
                    expired_snapshot.cleanup_failed);
    } else {
        send_error(request, 404, "unknown_handle");
    }
}

void handle_delete(AsyncWebServerRequest* request) {
    if (!portal_auth_gate(request)) return;
    if (!inspection_ready()) {
        send_error(request, 503, "inspection_unavailable");
        return;
    }
    uint32_t id = 0;
    if (!get_handle_id(request, &id)) {
        send_error(request, 400, "invalid_handle");
        return;
    }
    if (!lock(g_inspection_mutex)) {
        send_error(request, 503, "inspection_unavailable");
        return;
    }
    InspectionHandle* handle = find_handle(id);
    if (handle) {
        if (!handle->release_requested && handle->status != HandleStatus::Released &&
            handle->status != HandleStatus::CleanupFailed) {
            begin_release(*handle, false, handle->status == HandleStatus::Failed);
        }
        const InspectionHandle snapshot = *handle;
        xSemaphoreGive(g_inspection_mutex);
        send_status(request, 202, id, snapshot.status,
                    snapshot.error[0] ? snapshot.error : nullptr);
        return;
    }
    const ExpiredHandle* expired = find_expired(id);
    const ExpiredHandle expired_snapshot = expired ? *expired : ExpiredHandle{};
    xSemaphoreGive(g_inspection_mutex);
    if (expired) {
        send_status(request, 202, id,
                    expired_snapshot.cleanup_failed ? HandleStatus::CleanupFailed
                                                    : HandleStatus::Released,
                    expired_snapshot.cleanup_failed ? "unsubscribe_failed" : nullptr);
    } else {
        send_error(request, 404, "unknown_handle");
    }
}

void register_routes(AsyncWebServer* server) {
    if (!server) return;
    server->on(
        "/api/mqtt/inspect",
        HTTP_POST,
        [](AsyncWebServerRequest* request) {
            if (!portal_auth_gate(request)) return;
        },
        nullptr,
        handle_post_body);
    server->on("/api/mqtt/inspect", HTTP_GET, handle_get);
    server->on("/api/mqtt/inspect", HTTP_DELETE, handle_delete);
}

} // namespace

void mqtt_inspection_init() {
    if (!g_runtime) {
        constexpr bool use_psram = HAS_PSRAM;
        const uint32_t caps = MALLOC_CAP_8BIT | (use_psram ? MALLOC_CAP_SPIRAM : MALLOC_CAP_INTERNAL);
        void* memory = heap_caps_malloc(sizeof(InspectionRuntime), caps);
        if (!memory) {
            LOGE("MQTT_INSPECT", "Could not allocate runtime (%u bytes in %s)",
                 (unsigned)sizeof(InspectionRuntime), use_psram ? "PSRAM" : "internal RAM");
            return;
        }
        g_runtime = new (memory) InspectionRuntime();
        LOGD("MQTT_INSPECT", "Runtime: %u bytes in %s",
             (unsigned)sizeof(InspectionRuntime), use_psram ? "PSRAM" : "internal RAM");
    }
    if (!g_inspection_mutex) g_inspection_mutex = xSemaphoreCreateMutex();
    if (!g_body_mutex) g_body_mutex = xSemaphoreCreateMutex();
    if (!g_inspection_mutex || !g_body_mutex) {
        LOGE("MQTT_INSPECT", "Failed to create inspection mutexes");
    }
}

void mqtt_inspection_tick() {
    if (!inspection_ready() || !lock(g_inspection_mutex)) return;
    const uint32_t now = millis();
    for (InspectionHandle& handle : g_runtime->handles) {
        if (handle.used) process_handle(handle, now);
    }
    xSemaphoreGive(g_inspection_mutex);
}

void mqtt_inspection_register_routes(AsyncWebServer* server) {
    register_routes(server);
}

#endif // HAS_MQTT && HAS_DISPLAY
