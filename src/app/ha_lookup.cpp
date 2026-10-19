#include "ha_lookup.h"
#if HAS_HA_LIVE
#include "ha_live.h"
#include "ha_lookup_data.h"
#include "config_manager.h"
#include "log_manager.h"
#include "net_activity.h"
#include "ota_activity.h"
#include "bounded_psram_json_allocator.h"
#include "rtos_task_utils.h"
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <freertos/semphr.h>
#include <new>
#include <string.h>

// User input is a template variable, never executable Jinja source.
static const char SEARCH_TEMPLATE[] =
    "{% set ns = namespace(items=[], more=false) %}"
    "{% set q = query|lower %}"
    "{% for s in states %}"
    "{% if s.entity_id|length < 96 and "
    "(q in s.entity_id|lower or q in (s.attributes.friendly_name|default('',true)|string|lower)) %}"
    "{% if ns.items|length == 20 %}{% set ns.more = true %}{% break %}{% endif %}"
    "{% set name = s.attributes.friendly_name|default(s.entity_id,true)|string %}"
    "{% set ns.items = ns.items + [{'entity_id':s.entity_id,'name':name[:64]}] %}"
    "{% endif %}{% endfor %}"
    "{{ {'entities':ns.items,'more':ns.more}|to_json(ensure_ascii=false) }}";

struct LookupJob {
    char url[CONFIG_HA_URL_MAX_LEN] = {};
    char token[CONFIG_HA_TOKEN_MAX_LEN] = {};
    char query[HA_LIVE_ENTITY_BYTES] = {};
    char response[HA_LOOKUP_RESPONSE_BYTES + 1] = {};
    HaLookupContext context{};
    uint32_t started_ms = 0;
    bool entity = false;
};

static StaticSemaphore_t mutex_storage;
static SemaphoreHandle_t mutex = nullptr;
static char configured_url[CONFIG_HA_URL_MAX_LEN] = {};
static char configured_token[CONFIG_HA_TOKEN_MAX_LEN] = {};
static uint32_t revision = 0, next_id = 0;
static LookupJob* job = nullptr;
static TaskHandle_t task = nullptr;
static HaLookupResult result{404, 0, "Lookup not found; retry the lookup."};

class LookupLock {
public:
    LookupLock() { xSemaphoreTake(mutex, portMAX_DELAY); }
    ~LookupLock() { xSemaphoreGive(mutex); }
};

void ha_lookup_configure(const char* url, const char* token) {
    if (!mutex) mutex = xSemaphoreCreateMutexStatic(&mutex_storage);
    if (!mutex) { LOGE("HALookup", "Mutex allocation failed"); return; }
    LookupLock lock;
    strlcpy(configured_url, url ? url : "", sizeof(configured_url));
    strlcpy(configured_token, token ? token : "", sizeof(configured_token));
    ++revision;
}

static bool checkpoint() {
    if (WiFi.status() != WL_CONNECTED || uint32_t(millis() - job->started_ms) >= 8000) return false;
    LookupLock lock;
    return job->context.current(revision, ota_activity_epoch(), ota_activity_is_active());
}

class LookupStream : public Stream {
public:
    size_t used = 0;
    bool oversized = false, interrupted = false;
    size_t write(uint8_t value) override { return write(&value, 1); }
    size_t write(const uint8_t* data, size_t size) override {
        if (!checkpoint()) { interrupted = true; return 0; }
        if (size > HA_LOOKUP_RESPONSE_BYTES - used) { oversized = true; return 0; }
        memcpy(job->response + used, data, size);
        used += size;
        job->response[used] = '\0';
        return size;
    }
    int available() override { return 0; }
    int read() override { return -1; }
    int peek() override { return -1; }
    void flush() override {}
};

static HaLookupResult fetch_lookup() {
    auto error = [](unsigned status, const char* message) {
        return HaLookupResult{status, 0, message};
    };
    if (!checkpoint()) return error(503, "Lookup interrupted by network, configuration or OTA activity.");
    if (strncmp(job->url, "http://", 7))
        return error(400, "HA lookup requires the local HTTP URL used by live HA bindings.");
    char url[CONFIG_HA_URL_MAX_LEN + HA_LIVE_ENTITY_BYTES + 24];
    snprintf(url, sizeof(url), "%s%s", job->url,
             job->url[strlen(job->url) - 1] == '/' ? "api/" : "/api/");
    strlcat(url, job->entity ? "states/" : "template", sizeof(url));
    if (job->entity) strlcat(url, job->query, sizeof(url));

    WiFiClient client;
    HTTPClient http;
    http.setConnectTimeout(4000);
    http.setTimeout(4000);
    if (!http.begin(client, url)) return error(502, "Could not start HA lookup.");
    http.addHeader("Authorization", String("Bearer ") + job->token);
    http.addHeader("Content-Type", "application/json");
    BoundedPsramJsonAllocator allocator(48 * 1024);
    JsonDocument input(&allocator);
    String body;
    if (!job->entity) {
        input["template"] = SEARCH_TEMPLATE;
        input["variables"]["query"] = job->query;
        if (input.overflowed()) { http.end(); return error(503, "Lookup JSON allocation failed."); }
        serializeJson(input, body);
    }
    if (!checkpoint()) { http.end(); return error(503, "Lookup interrupted by network, configuration or OTA activity."); }
    int code = job->entity ? http.GET() : http.POST(body);
    net_activity_mark(NET_CH_HTTP);
    LookupStream stream;
    int received = -1;
    if (code == 200 && http.getSize() <= int(HA_LOOKUP_RESPONSE_BYTES))
        received = http.writeToStream(&stream);
    else if (code == 200) stream.oversized = true;
    http.end();
    if (!checkpoint() || stream.interrupted)
        return error(503, "Lookup interrupted by network, configuration or OTA activity.");
    // HA also uses 401 for require_admin failures. Probe basic authentication
    // before calling a valid non-admin token invalid.
    if (code == 401 && !job->entity) {
        char* api = strstr(url + 7, "/api/");
        if (!api) return error(502, "Invalid HA lookup URL.");
        api[5] = '\0';
        if (!http.begin(client, url)) return error(502, "Could not verify HA authentication.");
        http.addHeader("Authorization", String("Bearer ") + job->token);
        const int auth_code = http.GET();
        http.end();
        if (!checkpoint()) return error(503, "Lookup interrupted by network, configuration or OTA activity.");
        if (auth_code == 200) code = 403;
        else if (auth_code != 401) return error(502, "Could not verify HA authentication. Retry the lookup.");
    }
    if (code == 401) return error(401, "Home Assistant rejected the token. Check the saved HA credentials.");
    if (code == 403) return error(403, job->entity
        ? "This Home Assistant user cannot read that entity."
        : "Entity search requires a Home Assistant administrator token. Enter an entity ID manually; HA bindings and binding generation still work.");
    if (code == 404) return error(404, "Home Assistant entity or API endpoint not found.");
    if (code <= 0) return error(502, "Home Assistant lookup timed out or could not connect.");
    if (code != 200) return error(502, "Home Assistant lookup failed. Check HA availability and API support.");
    if (stream.oversized) return error(413, "HA response exceeds the 12 KiB lookup limit. Enter the entity and path manually.");
    if (received < 0 || !stream.used) return error(502, "Could not read the complete HA lookup response.");

    input.clear();
    JsonDocument document(&allocator);
    if (deserializeJson(document, static_cast<const char*>(job->response),
                        DeserializationOption::NestingLimit(12)))
        return error(502, "Home Assistant returned invalid or oversized JSON.");
    if (job->entity) {
        JsonDocument detail(&allocator);
        const char* detail_error = ha_lookup_describe_entity(document.as<JsonObjectConst>(), job->query, detail);
        if (detail_error) return error(502, detail_error);
        if (detail.overflowed() || measureJson(detail) > HA_LOOKUP_RESPONSE_BYTES)
            return error(413, "Entity details exceed the lookup limit. Enter the entity and path manually.");
        serializeJson(detail, job->response, sizeof(job->response));
    } else if (!document["entities"].is<JsonArrayConst>() || !document["more"].is<bool>()) {
        return error(502, "Home Assistant returned an unexpected search response.");
    }
    return {200, 0, nullptr};
}

static void lookup_worker(void*) {
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        HaLookupResult completed = fetch_lookup();
        if (completed.error) LOGW("HALookup", "%s", completed.error);
        LookupLock lock;
        completed.id = result.id;
        result = completed;
        memset(job->token, 0, sizeof(job->token));
    }
}

HaLookupResult ha_lookup_start(const char* query, bool entity) {
    if (!mutex) return {503, 0, "HA lookup is not initialized."};
    const size_t length = query ? strlen(query) : 0;
    if ((entity && !ha_live_valid_entity(query)) || (!entity && (length < 3 || length > 80)))
        return {400, 0, "Enter a valid entity ID, or a search of 3 to 80 characters."};
    LookupLock lock;
    if (result.status == 202) return {409, 0, "Another HA lookup is running. Retry shortly."};
    if (!configured_url[0] || !configured_token[0])
        return {400, 0, "Configure the Home Assistant URL and token first. Manual binding generation remains available."};
    if (ota_activity_is_active()) return {503, 0, "HA lookup is paused during OTA."};
    if (!job) {
        void* memory = heap_caps_malloc(sizeof(LookupJob), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        if (!memory) return {503, 0, "HA lookup PSRAM allocation failed."};
        job = new (memory) LookupJob;
    }
    if (!task && !rtos_create_task_psram_stack(lookup_worker, "ha_lookup", 8192, nullptr, 1, &task, nullptr))
        return {503, 0, "HA lookup PSRAM task allocation failed."};
    strlcpy(job->url, configured_url, sizeof(job->url));
    strlcpy(job->token, configured_token, sizeof(job->token));
    strlcpy(job->query, query, sizeof(job->query));
    job->entity = entity;
    job->context = {revision, ota_activity_epoch()};
    job->started_ms = millis();
    job->response[0] = '\0';
    if (!++next_id) ++next_id;
    result = {202, next_id, nullptr};
    xTaskNotifyGive(task);
    return result;
}

HaLookupResult ha_lookup_poll(uint32_t id, char* response, size_t size) {
    if (!mutex) return {404, 0, "Lookup not found; retry the lookup."};
    LookupLock lock;
    if (!id || id != result.id) return {404, 0, "Lookup expired; retry the lookup."};
    if (!job->context.current(revision, ota_activity_epoch(), ota_activity_is_active()))
        return {503, id, "Lookup interrupted by configuration or OTA activity; retry."};
    if (result.status == 200 && response) {
        if (strlen(job->response) >= size) return {503, id, "Lookup response buffer is too small."};
        strlcpy(response, job->response, size);
    }
    return result;
}
#endif
