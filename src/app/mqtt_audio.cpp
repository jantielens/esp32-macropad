#include "mqtt_audio.h"

#include "board_config.h"

#if HAS_AUDIO && HAS_MQTT

#include "mqtt_manager.h"
#include "audio.h"
#include "config_manager.h"
#include "web_portal_state.h"
#include "log_manager.h"

#include <ArduinoJson.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#include <string.h>

static const char* TAG = "MqttAudio";

// ---------------------------------------------------------------------------
// Topics
// ---------------------------------------------------------------------------
static char g_siren_cmd_topic[128]    = {0};
static char g_siren_state_topic[128]  = {0};
static char g_volume_cmd_topic[128]   = {0};
static char g_volume_state_topic[128] = {0};
static char g_beep_topic[128]         = {0};
static char g_beep_double_topic[128]  = {0};
static char g_beep_triple_topic[128]  = {0};
static char g_custom_tone_cmd_topic[128]   = {0};
static char g_custom_tone_state_topic[128] = {0};

// ---------------------------------------------------------------------------
// Pending commands (cross-task from MQTT callback → main loop)
// ---------------------------------------------------------------------------
static portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;

#define MQTT_AUDIO_PATTERN_MAX 128

// Siren pending
static volatile bool g_siren_pending = false;
static volatile bool g_siren_pending_on = false;  // true = ON, false = OFF
static char g_siren_pending_tone[MQTT_AUDIO_PATTERN_MAX] = {0};
static volatile uint8_t g_siren_pending_volume = 0; // 0 = use device
static volatile uint16_t g_siren_pending_duration = 0; // 0 = indefinite

// Volume pending
static volatile bool g_volume_pending = false;
static volatile uint8_t g_volume_pending_value = 0;

// Beep pending
enum BeepPending { BEEP_NONE = 0, BEEP_SINGLE, BEEP_DOUBLE, BEEP_TRIPLE };
static volatile BeepPending g_beep_pending = BEEP_NONE;

// Custom tone pattern (set via text entity, used when siren tone is "custom")
static char g_custom_tone_pattern[MQTT_AUDIO_PATTERN_MAX] = "1000:200";
static volatile bool g_custom_tone_pending = false;
static char g_custom_tone_pending_pattern[MQTT_AUDIO_PATTERN_MAX] = {0};

// State tracking
static bool g_siren_on = false;
static unsigned long g_siren_stop_at_ms = 0; // 0 = no auto-stop
static uint8_t g_last_published_volume = 255; // force initial publish

// ---------------------------------------------------------------------------
// Preset tones → DSL patterns (trailing pause controls loop cadence)
// ---------------------------------------------------------------------------
static const char* resolve_tone(const char* tone) {
    if (!tone || !tone[0] || strcmp(tone, "default") == 0) return "1000:200 800";
    if (strcmp(tone, "alert") == 0)    return "1000:150 100 1000:150 100 1000:150 800";
    if (strcmp(tone, "doorbell") == 0) return "800:150 1200:300 800";
    if (strcmp(tone, "warning") == 0)  return "500:400 800";
    if (strcmp(tone, "custom") == 0)   return g_custom_tone_pattern;
    // Not a preset — treat as raw DSL pattern
    return tone;
}

// Beep patterns (one-shot, no trailing gap needed)
static const char* PATTERN_SINGLE = "1000:200";
static const char* PATTERN_DOUBLE = "1000:200 150 1000:200";
static const char* PATTERN_TRIPLE = "1000:200 150 1000:200 150 1000:200";

// ---------------------------------------------------------------------------
// Init
// ---------------------------------------------------------------------------
void mqtt_audio_init() {
    const char* base = mqtt_manager.baseTopic();
    snprintf(g_siren_cmd_topic,    sizeof(g_siren_cmd_topic),    "%s/audio/siren/set",   base);
    snprintf(g_siren_state_topic,  sizeof(g_siren_state_topic),  "%s/audio/siren/state", base);
    snprintf(g_volume_cmd_topic,   sizeof(g_volume_cmd_topic),   "%s/audio/volume/set",  base);
    snprintf(g_volume_state_topic, sizeof(g_volume_state_topic), "%s/audio/volume/state", base);
    snprintf(g_beep_topic,         sizeof(g_beep_topic),         "%s/audio/beep",         base);
    snprintf(g_beep_double_topic,  sizeof(g_beep_double_topic),  "%s/audio/beep_double",  base);
    snprintf(g_beep_triple_topic,  sizeof(g_beep_triple_topic),  "%s/audio/beep_triple",  base);
    snprintf(g_custom_tone_cmd_topic,   sizeof(g_custom_tone_cmd_topic),   "%s/audio/custom_tone/set",   base);
    snprintf(g_custom_tone_state_topic, sizeof(g_custom_tone_state_topic), "%s/audio/custom_tone/state", base);

    g_siren_on = false;
    g_last_published_volume = 255;
    LOGI(TAG, "Init: siren_cmd=%s volume_cmd=%s", g_siren_cmd_topic, g_volume_cmd_topic);
}

// ---------------------------------------------------------------------------
// On MQTT connect
// ---------------------------------------------------------------------------
void mqtt_audio_on_connected() {
    mqtt_manager.subscribe(g_siren_cmd_topic);
    mqtt_manager.subscribe(g_volume_cmd_topic);
    mqtt_manager.subscribe(g_beep_topic);
    mqtt_manager.subscribe(g_beep_double_topic);
    mqtt_manager.subscribe(g_beep_triple_topic);
    mqtt_manager.subscribe(g_custom_tone_cmd_topic);

    // Publish initial custom tone state
    mqtt_manager.publish(g_custom_tone_state_topic, g_custom_tone_pattern, true);

    // Publish initial siren state
    mqtt_manager.publish(g_siren_state_topic, "OFF", true);
    g_siren_on = false;

    // Publish initial volume state
    char vol_str[4];
    snprintf(vol_str, sizeof(vol_str), "%u", audio_get_volume());
    mqtt_manager.publish(g_volume_state_topic, vol_str, true);
    g_last_published_volume = audio_get_volume();
}

// ---------------------------------------------------------------------------
// On MQTT message (called from MQTT callback — keep fast, no blocking)
// ---------------------------------------------------------------------------
void mqtt_audio_on_message(const char* topic, const uint8_t* payload, unsigned int length) {
    if (!topic) return;

    // Siren command: JSON {"state":"ON","tone":"alert","volume_level":0.8}
    if (strcmp(topic, g_siren_cmd_topic) == 0) {
        char buf[256];
        size_t n = length < sizeof(buf) - 1 ? length : sizeof(buf) - 1;
        memcpy(buf, payload, n);
        buf[n] = '\0';

        StaticJsonDocument<256> doc;
        DeserializationError err = deserializeJson(doc, buf);
        if (err) {
            LOGW(TAG, "Siren JSON parse error: %s", err.c_str());
            return;
        }

        const char* state = doc["state"] | "";
        bool on = (strcasecmp(state, "ON") == 0);

        portENTER_CRITICAL(&g_mux);
        g_siren_pending_on = on;
        if (on) {
            const char* tone = doc["tone"] | "";
            strlcpy(g_siren_pending_tone, tone, sizeof(g_siren_pending_tone));
            // volume_level: 0.0-1.0 → 0-100
            if (doc.containsKey("volume_level")) {
                float vl = doc["volume_level"] | 0.0f;
                g_siren_pending_volume = (uint8_t)(vl * 100.0f + 0.5f);
                if (g_siren_pending_volume > 100) g_siren_pending_volume = 100;
            } else {
                g_siren_pending_volume = 0; // use device volume
            }
            // duration in seconds; 0 = indefinite
            g_siren_pending_duration = doc["duration"] | (uint16_t)0;
        }
        g_siren_pending = true;
        portEXIT_CRITICAL(&g_mux);
        LOGI(TAG, "Siren command: %s", state);
        return;
    }

    // Volume command: plain number "0"-"100"
    if (strcmp(topic, g_volume_cmd_topic) == 0) {
        char buf[8];
        size_t n = length < sizeof(buf) - 1 ? length : sizeof(buf) - 1;
        memcpy(buf, payload, n);
        buf[n] = '\0';
        int v = atoi(buf);
        if (v < 0) v = 0;
        if (v > 100) v = 100;

        portENTER_CRITICAL(&g_mux);
        g_volume_pending_value = (uint8_t)v;
        g_volume_pending = true;
        portEXIT_CRITICAL(&g_mux);
        LOGI(TAG, "Volume command: %d", v);
        return;
    }

    // Custom tone text entity: stores DSL pattern for "custom" siren tone
    if (strcmp(topic, g_custom_tone_cmd_topic) == 0) {
        char buf[MQTT_AUDIO_PATTERN_MAX];
        size_t n = length < sizeof(buf) - 1 ? length : sizeof(buf) - 1;
        memcpy(buf, payload, n);
        buf[n] = '\0';
        if (buf[0] != '\0') {
            portENTER_CRITICAL(&g_mux);
            strlcpy(g_custom_tone_pending_pattern, buf, sizeof(g_custom_tone_pending_pattern));
            g_custom_tone_pending = true;
            portEXIT_CRITICAL(&g_mux);
            LOGI(TAG, "Custom tone: %s", buf);
        }
        return;
    }

    // Beep buttons: payload is "PRESS" (HA button entity)
    if (strcmp(topic, g_beep_topic) == 0) {
        portENTER_CRITICAL(&g_mux);
        g_beep_pending = BEEP_SINGLE;
        portEXIT_CRITICAL(&g_mux);
        LOGI(TAG, "Beep single");
        return;
    }
    if (strcmp(topic, g_beep_double_topic) == 0) {
        portENTER_CRITICAL(&g_mux);
        g_beep_pending = BEEP_DOUBLE;
        portEXIT_CRITICAL(&g_mux);
        LOGI(TAG, "Beep double");
        return;
    }
    if (strcmp(topic, g_beep_triple_topic) == 0) {
        portENTER_CRITICAL(&g_mux);
        g_beep_pending = BEEP_TRIPLE;
        portEXIT_CRITICAL(&g_mux);
        LOGI(TAG, "Beep triple");
        return;
    }
}

// ---------------------------------------------------------------------------
// Loop (main task)
// ---------------------------------------------------------------------------
void mqtt_audio_loop() {
    // --- Process siren command ---
    if (g_siren_pending) {
        bool on;
        char tone[MQTT_AUDIO_PATTERN_MAX];
        uint8_t vol;
        uint16_t dur;

        portENTER_CRITICAL(&g_mux);
        on = g_siren_pending_on;
        strlcpy(tone, g_siren_pending_tone, sizeof(tone));
        vol = g_siren_pending_volume;
        dur = g_siren_pending_duration;
        g_siren_pending = false;
        portEXIT_CRITICAL(&g_mux);

        if (on) {
            const char* pattern = resolve_tone(tone);
            audio_play_loop(pattern, vol);
            g_siren_stop_at_ms = (dur > 0) ? millis() + (unsigned long)dur * 1000UL : 0;
            if (!g_siren_on) {
                g_siren_on = true;
                mqtt_manager.publish(g_siren_state_topic, "ON", true);
            }
        } else {
            audio_stop();
            g_siren_stop_at_ms = 0;
            if (g_siren_on) {
                g_siren_on = false;
                mqtt_manager.publish(g_siren_state_topic, "OFF", true);
            }
        }
    }

    // Auto-stop after duration elapsed
    if (g_siren_on && g_siren_stop_at_ms > 0 && millis() >= g_siren_stop_at_ms) {
        audio_stop();
        g_siren_stop_at_ms = 0;
        g_siren_on = false;
        mqtt_manager.publish(g_siren_state_topic, "OFF", true);
        LOGI(TAG, "Siren duration elapsed — stopped");
    }

    // Auto-detect siren stopped (loop ended naturally — shouldn't happen, but safety)
    if (g_siren_on && !audio_is_playing()) {
        g_siren_on = false;
        g_siren_stop_at_ms = 0;
        mqtt_manager.publish(g_siren_state_topic, "OFF", true);
    }

    // --- Process beep command ---
    BeepPending beep;
    portENTER_CRITICAL(&g_mux);
    beep = g_beep_pending;
    g_beep_pending = BEEP_NONE;
    portEXIT_CRITICAL(&g_mux);

    if (beep != BEEP_NONE) {
        // A beep stops any running siren
        if (g_siren_on) {
            g_siren_on = false;
            g_siren_stop_at_ms = 0;
            mqtt_manager.publish(g_siren_state_topic, "OFF", true);
        }
        const char* pattern = (beep == BEEP_TRIPLE) ? PATTERN_TRIPLE
                            : (beep == BEEP_DOUBLE) ? PATTERN_DOUBLE
                            : PATTERN_SINGLE;
        audio_beep(pattern, 0);
    }

    // --- Process volume command ---
    if (g_volume_pending) {
        uint8_t vol;
        portENTER_CRITICAL(&g_mux);
        vol = g_volume_pending_value;
        g_volume_pending = false;
        portEXIT_CRITICAL(&g_mux);

        audio_set_volume(vol);

        // Persist to NVS
        DeviceConfig *cfg = web_portal_get_current_config();
        if (cfg) {
            cfg->audio_volume = vol;
            config_manager_save(cfg);
        }

        LOGI(TAG, "Volume set: %u%%", vol);
    }

    // --- Process custom tone update ---
    if (g_custom_tone_pending) {
        portENTER_CRITICAL(&g_mux);
        strlcpy(g_custom_tone_pattern, g_custom_tone_pending_pattern, sizeof(g_custom_tone_pattern));
        g_custom_tone_pending = false;
        portEXIT_CRITICAL(&g_mux);

        mqtt_manager.publish(g_custom_tone_state_topic, g_custom_tone_pattern, true);
        LOGI(TAG, "Custom tone updated: %s", g_custom_tone_pattern);
    }

    // --- Publish volume state on change (any source) ---
    uint8_t cur_vol = audio_get_volume();
    if (cur_vol != g_last_published_volume) {
        if (mqtt_manager.connected()) {
            char vol_str[4];
            snprintf(vol_str, sizeof(vol_str), "%u", cur_vol);
            mqtt_manager.publish(g_volume_state_topic, vol_str, true);
            g_last_published_volume = cur_vol;
        }
    }
}

#endif // HAS_AUDIO && HAS_MQTT
