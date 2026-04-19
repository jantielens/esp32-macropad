#include "mqtt_notify.h"

#include "board_config.h"

#if HAS_DISPLAY && HAS_MQTT

#include "mqtt_manager.h"
#include "message_bubble.h"
#include "pad_config.h"
#include "log_manager.h"

#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <string.h>

static const char* TAG = "MqttNotify";

// ---------------------------------------------------------------------------
// Topics
// ---------------------------------------------------------------------------
static char g_cmd_topic[128]   = {0};
static char g_state_topic[128] = {0};

// ---------------------------------------------------------------------------
// Pending command (cross-task from MQTT callback → main loop)
// ---------------------------------------------------------------------------
static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
static volatile bool g_pending = false;
static MessageBubbleParams g_pending_params;
static char g_pending_state_text[128] = {0};  // for state publish

// ---------------------------------------------------------------------------
// Default values for optional fields
// ---------------------------------------------------------------------------
static const uint32_t DEFAULT_TEXT_COLOR   = 0xFFFFFF;
static const uint32_t DEFAULT_BG_COLOR     = 0x333333;
static const uint16_t DEFAULT_DURATION_MS  = 3000;
static const uint8_t  DEFAULT_OPACITY      = 85;

// ---------------------------------------------------------------------------
// Parse payload into MessageBubbleParams
// ---------------------------------------------------------------------------
static void parse_payload(const uint8_t* payload, unsigned int length, MessageBubbleParams* params, char* state_text) {
    memset(params, 0, sizeof(*params));
    state_text[0] = '\0';

    if (length == 0) {
        // Empty = dismiss
        return;
    }

    // Copy payload to null-terminated buffer
    char buf[256];
    unsigned int copy_len = (length < sizeof(buf) - 1) ? length : sizeof(buf) - 1;
    memcpy(buf, payload, copy_len);
    buf[copy_len] = '\0';

    // Detect JSON vs plain text
    if (buf[0] == '{') {
        // JSON mode
        StaticJsonDocument<384> doc;
        DeserializationError err = deserializeJson(doc, buf);
        if (err) {
            LOGW(TAG, "JSON parse error: %s", err.c_str());
            // Treat as plain text fallback
            strlcpy(params->text, buf, sizeof(params->text));
            params->text_color = DEFAULT_TEXT_COLOR;
            params->bg_color = DEFAULT_BG_COLOR;
            params->duration_ms = DEFAULT_DURATION_MS;
            params->opacity = DEFAULT_OPACITY;
            strlcpy(state_text, params->text, 128);
            return;
        }

        strlcpy(params->text, doc["text"] | "", sizeof(params->text));
        params->duration_ms = (uint16_t)(doc["duration_ms"] | (int)DEFAULT_DURATION_MS);
        params->opacity = (uint8_t)(doc["opacity"] | (int)DEFAULT_OPACITY);
        params->font_size = (uint8_t)(doc["font_size"] | 0);

        // Colors
        const char* tc = doc["text_color"] | "";
        if (!parse_hex_color(tc, &params->text_color)) params->text_color = DEFAULT_TEXT_COLOR;

        const char* bg = doc["bg_color"] | "";
        if (!parse_hex_color(bg, &params->bg_color)) params->bg_color = DEFAULT_BG_COLOR;

        const char* bc = doc["border_color"] | "";
        params->has_border = parse_hex_color(bc, &params->border_color);

        // Location
        const char* loc = doc["location"] | "bottom";
        params->location = notify_location_from_str(loc);
    } else {
        // Plain text mode — default styling
        strlcpy(params->text, buf, sizeof(params->text));
        params->text_color = DEFAULT_TEXT_COLOR;
        params->bg_color = DEFAULT_BG_COLOR;
        params->duration_ms = DEFAULT_DURATION_MS;
        params->opacity = DEFAULT_OPACITY;
        params->location = NOTIFY_LOC_BOTTOM;
    }

    strlcpy(state_text, params->text, 128);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void mqtt_notify_init() {
    const char* base = mqtt_manager.baseTopic();
    snprintf(g_cmd_topic, sizeof(g_cmd_topic), "%s/notify/set", base);
    snprintf(g_state_topic, sizeof(g_state_topic), "%s/notify/state", base);
    LOGI(TAG, "Init: cmd=%s state=%s", g_cmd_topic, g_state_topic);
}

void mqtt_notify_on_connected() {
    mqtt_manager.subscribe(g_cmd_topic);
    // Publish empty state on connect
    mqtt_manager.publish(g_state_topic, "", true);
}

void mqtt_notify_on_message(const char* topic, const uint8_t* payload, unsigned int length) {
    if (strcmp(topic, g_cmd_topic) != 0) return;

    MessageBubbleParams params;
    char state_text[128];
    parse_payload(payload, length, &params, state_text);

    portENTER_CRITICAL(&g_mux);
    memcpy(&g_pending_params, &params, sizeof(params));
    strlcpy(g_pending_state_text, state_text, sizeof(g_pending_state_text));
    g_pending = true;
    portEXIT_CRITICAL(&g_mux);
}

void mqtt_notify_loop() {
    bool has_pending = false;
    MessageBubbleParams params;
    char state_text[128];

    portENTER_CRITICAL(&g_mux);
    if (g_pending) {
        memcpy(&params, &g_pending_params, sizeof(params));
        strlcpy(state_text, g_pending_state_text, sizeof(state_text));
        g_pending = false;
        has_pending = true;
    }
    portEXIT_CRITICAL(&g_mux);

    if (!has_pending) return;

    if (params.text[0]) {
        message_bubble_show(&params);
        LOGI(TAG, "Show: '%s'", params.text);
    } else {
        message_bubble_dismiss();
        LOGI(TAG, "Dismiss");
    }

    // Publish state (retained)
    mqtt_manager.publish(g_state_topic, state_text, true);
}

#endif // HAS_DISPLAY && HAS_MQTT
