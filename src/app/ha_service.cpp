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

// Single pending request, guarded by a spinlock. The UI task writes it in
// ha_service_enqueue(); the main loop snapshots and clears it in
// ha_service_execute(). Holding only fast struct copies under the lock.
static volatile bool g_ha_request_pending = false;
static HaServicePayload g_ha_request;
static portMUX_TYPE g_ha_mux = portMUX_INITIALIZER_UNLOCKED;

void ha_service_enqueue(const HaServicePayload& payload) {
    portENTER_CRITICAL(&g_ha_mux);
    g_ha_request = payload;
    g_ha_request_pending = true;
    portEXIT_CRITICAL(&g_ha_mux);
}

void ha_service_execute() {
    if (!g_ha_request_pending) return;

    // Snapshot + clear the pending flag under the lock.
    HaServicePayload req;
    portENTER_CRITICAL(&g_ha_mux);
    req = g_ha_request;
    g_ha_request_pending = false;
    portEXIT_CRITICAL(&g_ha_mux);

    const DeviceConfig* cfg = web_portal_get_current_config();
    if (!cfg || !cfg->ha_url[0] || !cfg->ha_token[0]) {
        LOGW(TAG, "HA URL/token not configured — skipping");
        return;
    }
    if (WiFi.status() != WL_CONNECTED) {
        LOGW(TAG, "WiFi not connected — skipping");
        return;
    }
    if (!req.entity_id[0] || !req.service[0]) {
        LOGW(TAG, "empty entity_id/service — skipping");
        return;
    }

    // Domain = text before first '.' in entity_id (e.g. "light.lamp" -> "light").
    const char* dot = strchr(req.entity_id, '.');
    if (!dot || dot == req.entity_id) {
        LOGW(TAG, "invalid entity_id '%s' (no domain)", req.entity_id);
        return;
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
        return;
    }

    char auth[CONFIG_HA_TOKEN_MAX_LEN + 12];
    snprintf(auth, sizeof(auth), "Bearer %s", cfg->ha_token);
    http.addHeader("Authorization", auth);
    http.addHeader("Content-Type", "application/json");

    const int code = http.POST((uint8_t*)body, strlen(body));
    net_activity_mark(NET_CH_HTTP);
    if (code >= 200 && code < 300) {
        LOGI(TAG, "%s -> HTTP %d", url, code);
    } else {
        LOGW(TAG, "%s -> HTTP %d", url, code);
    }
    http.end();
}

#endif // HAS_DISPLAY || HAS_BUTTON
