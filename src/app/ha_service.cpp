#include "ha_service.h"

#if HAS_DISPLAY || HAS_BUTTON

#include "config_manager.h"
#include "web_portal_state.h"  // web_portal_get_current_config()
#include "log_manager.h"
#include "net_activity.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <string.h>

#define TAG "HASvc"
// Bounds the worst-case stall on the main loop() when HA is slow/unreachable:
// the synchronous POST runs on loop(), so this is the longest one call can block
// other loop() work (power manager, device-class loop). A local HA responds in
// well under a second; keep this short enough that an outage is barely felt.
#define HA_HTTP_TIMEOUT_MS 4000

static HaServiceDelivery g_ha_delivery;
static portMUX_TYPE g_ha_mux = portMUX_INITIALIZER_UNLOCKED;

static_assert(sizeof(g_ha_delivery) + sizeof(g_ha_mux) < 2048,
              "HA queue and result store exceed 2 KiB");

HaServiceEnqueueResult ha_service_enqueue(const HaServicePayload& payload,
                                          uint32_t execution_id,
                                          uint8_t action_index) {
    portENTER_CRITICAL(&g_ha_mux);
    const HaServiceEnqueueResult result =
        g_ha_delivery.enqueue(payload, execution_id, action_index);
    portEXIT_CRITICAL(&g_ha_mux);
    return result;
}

#if HAS_MCP
bool ha_service_execution_reserve(uint8_t action_count, uint32_t* execution_id) {
    if (!execution_id) return false;
    const uint32_t now_ms = millis();
    portENTER_CRITICAL(&g_ha_mux);
    const bool reserved =
        g_ha_delivery.execution_reserve(action_count, now_ms, *execution_id);
    portEXIT_CRITICAL(&g_ha_mux);
    return reserved;
}

bool ha_service_execution_set_action(uint32_t execution_id, uint8_t result_index,
                                     uint8_t action_index,
                                     const HaServicePayload& payload) {
    portENTER_CRITICAL(&g_ha_mux);
    const bool set = g_ha_delivery.execution_set_action(
        execution_id, result_index, action_index, payload);
    portEXIT_CRITICAL(&g_ha_mux);
    return set;
}

bool ha_service_execution_record(const HaServiceResult& result) {
    const uint32_t now_ms = millis();
    portENTER_CRITICAL(&g_ha_mux);
    const bool recorded = g_ha_delivery.execution_complete(result, now_ms);
    portEXIT_CRITICAL(&g_ha_mux);
    return recorded;
}

HaExecutionLookupResult ha_service_execution_snapshot(
    uint32_t execution_id, HaExecutionSnapshot& snapshot) {
    const uint32_t now_ms = millis();
    portENTER_CRITICAL(&g_ha_mux);
    const HaExecutionLookupResult lookup =
        g_ha_delivery.execution_snapshot(execution_id, now_ms, snapshot);
    portEXIT_CRITICAL(&g_ha_mux);
    return lookup;
}
#endif

static HaServiceResult execute_request(const HaServiceRequest& request) {
    const uint32_t started_ms = millis();
    HaServiceResult result = {};
    result.execution_id = request.execution_id;
    result.action_index = request.action_index;
    result.status = HA_STATUS_INVALID_REQUEST;
    result.http_status = HA_HTTP_STATUS_NONE;
    strlcpy(result.entity_id, request.payload.entity_id, sizeof(result.entity_id));
    strlcpy(result.service, request.payload.service, sizeof(result.service));

    auto finish = [&](HaServiceStatus status) {
        result.status = status;
        result.duration_ms = (uint32_t)(millis() - started_ms);
        return result;
    };

    const HaServicePayload& req = request.payload;
    if (!req.entity_id[0] || !req.service[0]) {
        LOGW(TAG, "empty entity_id/service — skipping");
        return finish(HA_STATUS_INVALID_REQUEST);
    }

    const char* dot = strchr(req.entity_id, '.');
    if (!dot || dot == req.entity_id) {
        LOGW(TAG, "invalid entity_id '%s' (no domain)", req.entity_id);
        return finish(HA_STATUS_INVALID_REQUEST);
    }


    const DeviceConfig* cfg = web_portal_get_current_config();
    if (!cfg || !cfg->ha_url[0] || !cfg->ha_token[0]) {
        LOGW(TAG, "HA URL/token not configured — skipping");
        return finish(HA_STATUS_NOT_CONFIGURED);
    }
    if (WiFi.status() != WL_CONNECTED) {
        LOGW(TAG, "WiFi not connected — skipping");
        return finish(HA_STATUS_WIFI_DISCONNECTED);
    }
    const int domain_len = (int)(dot - req.entity_id);

    // Build URL: {ha_url}/api/services/{domain}/{service}
    char url[CONFIG_HA_URL_MAX_LEN + sizeof(req.entity_id) + sizeof(req.service) + 24];
    snprintf(url, sizeof(url), "%s/api/services/%.*s/%s",
             cfg->ha_url, domain_len, req.entity_id, req.service);

    // Build body. data_json (if a non-empty object) is a complete JSON object
    // like {"brightness_pct":80}; splice entity_id in by dropping its opening
    // brace. An empty object ("{}" / "{ }") falls back to the entity-only body
    // so we never emit a trailing comma.
    char body[sizeof(req.entity_id) + sizeof(req.data_json) + 24];
    bool has_data = false;
    if (req.data_json[0] == '{') {
        const char* p = req.data_json + 1;
        while (*p == ' ' || *p == '\t') p++;
        has_data = (*p != '}' && *p != '\0');
    }
    if (has_data) {
        snprintf(body, sizeof(body), "{\"entity_id\":\"%s\",%s",
                 req.entity_id, req.data_json + 1);
    } else {
        snprintf(body, sizeof(body), "{\"entity_id\":\"%s\"}", req.entity_id);
    }

    const bool is_https = strncmp(cfg->ha_url, "https://", 8) == 0;

    HTTPClient http;
    http.setConnectTimeout(HA_HTTP_TIMEOUT_MS);
    http.setTimeout(HA_HTTP_TIMEOUT_MS);

    WiFiClientSecure tls_client;
    WiFiClient plain_client;
    bool began;
    if (is_https) {
        tls_client.setInsecure();  // HA installs often use self-signed certs
        tls_client.setTimeout(HA_HTTP_TIMEOUT_MS);
        began = http.begin(tls_client, url);
    } else {
        plain_client.setTimeout(HA_HTTP_TIMEOUT_MS);
        began = http.begin(plain_client, url);
    }
    if (!began) {
        LOGW(TAG, "HTTP begin failed: %s", url);
        return finish(HA_STATUS_HTTP_BEGIN_FAILED);
    }

    char auth[CONFIG_HA_TOKEN_MAX_LEN + 12];
    snprintf(auth, sizeof(auth), "Bearer %s", cfg->ha_token);
    http.addHeader("Authorization", auth);
    http.addHeader("Content-Type", "application/json");

    const int code = http.POST((uint8_t*)body, strlen(body));
    net_activity_mark(NET_CH_HTTP);
    if (code > 0) result.http_status = (int16_t)code;
    if (code >= 200 && code < 300) {
        LOGI(TAG, "%s -> HTTP %d", url, code);
    } else {
        LOGW(TAG, "%s -> HTTP %d (%s)", url, code,
             HTTPClient::errorToString(code).c_str());
    }
    http.end();
    if (code >= 200 && code < 300) return finish(HA_STATUS_SUCCESS);
    if (code == HTTPC_ERROR_READ_TIMEOUT) return finish(HA_STATUS_TIMEOUT);
    if (code < 0) return finish(HA_STATUS_TRANSPORT_ERROR);
    return finish(HA_STATUS_HTTP_ERROR);
}

void ha_service_execute() {
    HaServiceRequest request = {};
    portENTER_CRITICAL(&g_ha_mux);
    const bool dequeued = g_ha_delivery.dequeue(request);
    portEXIT_CRITICAL(&g_ha_mux);
    if (!dequeued) return;

    const HaServiceResult result = execute_request(request);
#if HAS_MCP
    if (result.execution_id) ha_service_execution_record(result);
#endif
}

#endif // HAS_DISPLAY || HAS_BUTTON
