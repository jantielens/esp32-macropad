#include "action_dispatch.h"

#if HAS_DISPLAY || HAS_BUTTON

#include "action_continuation.h"
#include "action_list.h"
#include "action_registry.h"
#include "log_manager.h"

#if HAS_DISPLAY
#include "display_manager.h"
#include "message_bubble.h"
#include "visual_alert.h"
#include "screen_saver_manager.h"
#endif

#if HAS_MQTT
#include "binding_template.h"
#include "mqtt_manager.h"
#endif
#if HAS_BLE_HID
#include "ble_hid.h"
#endif
#if HAS_AUDIO
#include "audio.h"
#endif

#include "wifi_manager.h"
#include "ha_service.h"

#include <math.h>

#define TAG "Action"

// Compute a clamped percentage value from a string, optionally as a delta from current.
#if HAS_AUDIO || HAS_DISPLAY
static uint8_t compute_clamped_percent(const char* value_str, uint8_t current, bool is_adjust, int min_val) {
    int v = is_adjust ? (int)current + lroundf(atof(value_str)) : lroundf(atof(value_str));
    if (v > 100) v = 100;
    if (v < min_val) v = min_val;
    return (uint8_t)v;
}
#endif

ActionResult action_dispatch_back(const ButtonAction&, const char* label, uint32_t) {
#if HAS_DISPLAY
    if (!display_manager_go_back()) {
        LOGW(TAG, "%s back: no previous screen", label);
    }
#else
    LOGW(TAG, "%s back: no display", label);
#endif
    return ACTION_COMPLETE;
}

ActionResult action_dispatch_volume(const ButtonAction& act, const char* label, uint32_t) {
#if HAS_AUDIO
    const auto& volume = act.payload.volume;
    bool is_adjust = strcmp(volume.volume_mode, "adjust") == 0;
    uint8_t result = compute_clamped_percent(volume.volume_value, audio_get_volume(), is_adjust, 0);
    audio_set_volume(result);
    LOGI(TAG, "%s volume %s %s -> %u%%", label, is_adjust ? "adjust" : "set",
         volume.volume_value, result);
#else
    LOGW(TAG, "%s volume: not compiled", label);
#endif
    return ACTION_COMPLETE;
}

ActionResult action_dispatch_brightness(const ButtonAction& act, const char* label, uint32_t) {
#if HAS_DISPLAY
    const auto& brightness = act.payload.brightness;
    bool is_adjust = strcmp(brightness.brightness_mode, "adjust") == 0;
    uint8_t result = compute_clamped_percent(brightness.brightness_value,
                                               display_manager_get_backlight_brightness(),
                                               is_adjust, MIN_USER_BRIGHTNESS);
    screen_saver_manager_set_brightness(result);
    LOGI(TAG, "%s brightness %s %s -> %u%%", label, is_adjust ? "adjust" : "set",
         brightness.brightness_value, result);
#else
    LOGW(TAG, "%s brightness: no display", label);
#endif
    return ACTION_COMPLETE;
}

#if HAS_MQTT
// Resolve binding templates in the active payload arm's resolvable fields.
// Structural fields (commands, modes, ids) are excluded — only fields that
// users may template are visited. Type-dispatched so we only touch the
// active arm of the discriminated union (writing a non-active arm is UB).
static bool resolve_action_bindings(ButtonAction& act) {
    auto try_resolve = [](char* field, size_t len, bool reject_overflow = false) {
        return action_resolve_binding_field(field, len, reject_overflow);
    };

    if (strcmp(act.type, ACTION_TYPE_SCREEN) == 0) {
        try_resolve(act.payload.screen.screen_id, sizeof(act.payload.screen.screen_id));
    } else if (strcmp(act.type, ACTION_TYPE_MQTT) == 0) {
        try_resolve(act.payload.mqtt.mqtt_topic,   sizeof(act.payload.mqtt.mqtt_topic));
        try_resolve(act.payload.mqtt.mqtt_payload, sizeof(act.payload.mqtt.mqtt_payload));
    } else if (strcmp(act.type, ACTION_TYPE_KEY) == 0) {
        try_resolve(act.payload.key.key_sequence, sizeof(act.payload.key.key_sequence));
    } else if (strcmp(act.type, ACTION_TYPE_SOUND_ALERT) == 0 &&
               strcmp(act.payload.sound_alert.sound_alert_kind, "tone") == 0) {
        try_resolve(act.payload.sound_alert.sound_alert_pattern,
                    sizeof(act.payload.sound_alert.sound_alert_pattern));
    } else if (strcmp(act.type, ACTION_TYPE_VOLUME) == 0) {
        try_resolve(act.payload.volume.volume_value, sizeof(act.payload.volume.volume_value));
    } else if (strcmp(act.type, ACTION_TYPE_BRIGHTNESS) == 0) {
        try_resolve(act.payload.brightness.brightness_value, sizeof(act.payload.brightness.brightness_value));
    } else if (strcmp(act.type, ACTION_TYPE_TIMER) == 0) {
        if (!try_resolve(act.payload.timer.timer_value,
                         sizeof(act.payload.timer.timer_value), true)) return false;
    } else if (strcmp(act.type, ACTION_TYPE_NOTIFY) == 0) {
        try_resolve(act.payload.notify.notify_text,         sizeof(act.payload.notify.notify_text));
        try_resolve(act.payload.notify.notify_duration_ms,  sizeof(act.payload.notify.notify_duration_ms));
        try_resolve(act.payload.notify.notify_text_color,   sizeof(act.payload.notify.notify_text_color));
        try_resolve(act.payload.notify.notify_bg_color,     sizeof(act.payload.notify.notify_bg_color));
        try_resolve(act.payload.notify.notify_border_color, sizeof(act.payload.notify.notify_border_color));
    } else if (strcmp(act.type, ACTION_TYPE_VISUAL_ALERT) == 0) {
        try_resolve(act.payload.visual_alert.va_color, sizeof(act.payload.visual_alert.va_color));
    } else {
        // Device-class action types (e.g. shutter) self-register via the
        // action type registry; resolve bindings generically against their
        // value_field accessor (if any).
        const ActionTypeDef* t = action_type_find(act.type);
        if (!action_type_resolve_bindings(t, act)) return false;
    }
    // sound, system, back, ble_pair: no bindable fields today.
    return true;
}

// Quick scan: return true if the active payload arm contains a binding token.
// Checks only for '[' to avoid the ButtonAction copy for the common case.
// Type-dispatched so we only read the active union arm.
static bool action_has_any_binding(const ButtonAction& act) {
    auto has = [](const char* f) { return f[0] && memchr(f, '[', strlen(f)); };
    if (strcmp(act.type, ACTION_TYPE_SCREEN) == 0) {
        return has(act.payload.screen.screen_id);
    } else if (strcmp(act.type, ACTION_TYPE_MQTT) == 0) {
        return has(act.payload.mqtt.mqtt_topic) || has(act.payload.mqtt.mqtt_payload);
    } else if (strcmp(act.type, ACTION_TYPE_KEY) == 0) {
        return has(act.payload.key.key_sequence);
    } else if (strcmp(act.type, ACTION_TYPE_SOUND_ALERT) == 0 &&
               strcmp(act.payload.sound_alert.sound_alert_kind, "tone") == 0) {
        return has(act.payload.sound_alert.sound_alert_pattern);
    } else if (strcmp(act.type, ACTION_TYPE_VOLUME) == 0) {
        return has(act.payload.volume.volume_value);
    } else if (strcmp(act.type, ACTION_TYPE_BRIGHTNESS) == 0) {
        return has(act.payload.brightness.brightness_value);
    } else if (strcmp(act.type, ACTION_TYPE_TIMER) == 0) {
        return has(act.payload.timer.timer_value);
    } else if (strcmp(act.type, ACTION_TYPE_NOTIFY) == 0) {
        return has(act.payload.notify.notify_text)
            || has(act.payload.notify.notify_duration_ms)
            || has(act.payload.notify.notify_text_color)
            || has(act.payload.notify.notify_bg_color)
            || has(act.payload.notify.notify_border_color);
    } else if (strcmp(act.type, ACTION_TYPE_VISUAL_ALERT) == 0) {
        return has(act.payload.visual_alert.va_color);
    }
    const ActionTypeDef* t = action_type_find(act.type);
    return action_type_has_binding(t, act);
}

// Collect MQTT topics from every bindable field of an action. Mirrors the
// field set in resolve_action_bindings() so a token used only inside a button
// action still gets subscribed by mqtt_sub_store's scan.
void action_collect_binding_topics(const ButtonAction& act, void* user_data) {
    auto collect = [&](const char* field) {
        if (field[0]) binding_template_collect_topics(field, user_data);
    };
    if (strcmp(act.type, ACTION_TYPE_SCREEN) == 0) {
        collect(act.payload.screen.screen_id);
    } else if (strcmp(act.type, ACTION_TYPE_MQTT) == 0) {
        collect(act.payload.mqtt.mqtt_topic);
        collect(act.payload.mqtt.mqtt_payload);
    } else if (strcmp(act.type, ACTION_TYPE_KEY) == 0) {
        collect(act.payload.key.key_sequence);
    } else if (strcmp(act.type, ACTION_TYPE_SOUND_ALERT) == 0 &&
               strcmp(act.payload.sound_alert.sound_alert_kind, "tone") == 0) {
        collect(act.payload.sound_alert.sound_alert_pattern);
    } else if (strcmp(act.type, ACTION_TYPE_VOLUME) == 0) {
        collect(act.payload.volume.volume_value);
    } else if (strcmp(act.type, ACTION_TYPE_BRIGHTNESS) == 0) {
        collect(act.payload.brightness.brightness_value);
    } else if (strcmp(act.type, ACTION_TYPE_TIMER) == 0) {
        collect(act.payload.timer.timer_value);
    } else if (strcmp(act.type, ACTION_TYPE_NOTIFY) == 0) {
        collect(act.payload.notify.notify_text);
        collect(act.payload.notify.notify_duration_ms);
        collect(act.payload.notify.notify_text_color);
        collect(act.payload.notify.notify_bg_color);
        collect(act.payload.notify.notify_border_color);
    } else if (strcmp(act.type, ACTION_TYPE_VISUAL_ALERT) == 0) {
        collect(act.payload.visual_alert.va_color);
    } else {
        const ActionTypeDef* t = action_type_find(act.type);
        action_type_collect_topics(t, act, user_data);
    }
}
#endif // HAS_MQTT

static ActionResult action_dispatch_resolved(const ButtonAction& act, const char* label,
                                             uint32_t continuation_token);

ActionResult action_dispatch(const ButtonAction& act_in, const char* label,
                             uint32_t continuation_token) {
    if (!act_in.type[0]) return ACTION_COMPLETE;

    // Resolve binding templates in value fields before dispatch.
    // binding_template_resolve accesses MQTT subscription state shared with the
    // LVGL task and may call LVGL APIs. When invoked from another task (e.g. a
    // hardware button on the loop() task), serialize against the LVGL task with
    // the display lock; lock_if_needed is a no-op when already on the LVGL task.
#if HAS_MQTT
    if (action_has_any_binding(act_in)) {
        ButtonAction act = act_in;
#if HAS_DISPLAY
        bool did_lock = false;
        display_manager_lock_if_needed(&did_lock);
    bool resolved = resolve_action_bindings(act);
        display_manager_unlock_if_needed(did_lock);
#else
    bool resolved = resolve_action_bindings(act);
#endif
    if (!resolved) {
        LOGW(TAG, "%s binding result exceeds action field capacity", label);
        return ACTION_COMPLETE;
    }
        return action_dispatch_resolved(act, label, continuation_token);
    } else {
        return action_dispatch_resolved(act_in, label, continuation_token);
    }
#else
    return action_dispatch_resolved(act_in, label, continuation_token);
#endif
}

static ActionResult action_dispatch_resolved(const ButtonAction& act, const char* label,
                                             uint32_t continuation_token) {

    const ActionTypeDef* registered_type = action_type_find(act.type);
    if (registered_type && registered_type->dispatch) {
        return registered_type->dispatch(act, label, continuation_token);
    }

    if (strcmp(act.type, ACTION_TYPE_SCREEN) == 0) {
#if HAS_DISPLAY
        const char* screen_id = act.payload.screen.screen_id;
        if (screen_id[0]) {
            bool ok = false;
            display_manager_show_screen(screen_id, &ok);
            if (!ok) {
                LOGW(TAG, "%s nav failed: '%s'", label, screen_id);
            }
        }
#else
        LOGW(TAG, "%s screen: no display", label);
#endif
    } else if (strcmp(act.type, ACTION_TYPE_CYCLE_PAD) == 0) {
#if HAS_DISPLAY
    const auto& cycle = act.payload.cycle_pad;
    if (!display_manager_cycle_pad(cycle.direction, cycle.wrap,
                       cycle.excluded_mask)) {
        LOGD(TAG, "%s cycle_pad: no eligible destination", label);
    }
#else
    LOGW(TAG, "%s cycle_pad: no display", label);
#endif
    } else if (strcmp(act.type, ACTION_TYPE_DELAY) == 0) {
        const uint32_t duration_ms = act.payload.delay.duration_ms;
        if (!action_delay_duration_is_valid(duration_ms)) {
            LOGW(TAG, "%s delay: duration must be 1-%u ms", label,
                 (unsigned)ACTION_DELAY_MAX_DURATION_MS);
            return ACTION_FAILED;
        }
        if (!continuation_token) {
            LOGW(TAG, "%s delay: %s", label,
                 action_continuation_is_full()
                     ? "all pausable action slots are occupied"
                     : "must be used in an action list");
            return ACTION_FAILED;
        }
        if (!action_continuation_schedule_success(continuation_token, duration_ms)) {
            LOGW(TAG, "%s delay: continuation is no longer available", label);
            return ACTION_FAILED;
        }
        LOGI(TAG, "%s delay: %lu ms", label, (unsigned long)duration_ms);
        return ACTION_PENDING;
    } else if (strcmp(act.type, ACTION_TYPE_MQTT) == 0) {
#if HAS_MQTT
        const auto& m = act.payload.mqtt;
        if (m.mqtt_topic[0]) {
            bool ok = mqtt_manager.publish(m.mqtt_topic, m.mqtt_payload, false);
            LOGI(TAG, "%s mqtt: topic='%s' payload='%s' %s", label, m.mqtt_topic, m.mqtt_payload, ok ? "ok" : "FAIL");
        } else {
            LOGW(TAG, "%s mqtt: empty topic", label);
        }
#else
        LOGW(TAG, "%s mqtt: not compiled", label);
#endif
    } else if (strcmp(act.type, ACTION_TYPE_KEY) == 0) {
#if HAS_BLE_HID
        const char* seq = act.payload.key.key_sequence;
        if (!ble_hid_is_initialized()) {
            LOGW(TAG, "%s key: BLE disabled", label);
        } else if (seq[0]) {
            LOGI(TAG, "%s key: '%s'", label, seq);
            ble_hid_request_sequence(seq);
        } else {
            LOGW(TAG, "%s key: empty sequence", label);
        }
#else
        LOGW(TAG, "%s key: not compiled", label);
#endif
    } else if (strcmp(act.type, ACTION_TYPE_BLE_PAIR) == 0) {
#if HAS_BLE_HID
        if (!ble_hid_is_initialized()) {
            LOGW(TAG, "%s ble_pair: BLE disabled", label);
        } else {
            LOGI(TAG, "%s ble_pair: starting re-pairing", label);
            ble_hid_request_pairing();
        }
#else
        LOGW(TAG, "%s ble_pair: not compiled", label);
#endif
    } else if (strcmp(act.type, ACTION_TYPE_MUSIC) == 0) {
    #if HAS_SOUND_PLAYER
        MusicCommand command;
        if (!music_command_parse(act.payload.music.music_command, &command)) {
            LOGW(TAG, "%s music: invalid command", label);
        } else if (audio_music_command(command) != AUDIO_MUSIC_SUBMIT_QUEUED) {
            LOGW(TAG, "%s music: audio worker busy", label);
        }
#else
        LOGW(TAG, "%s music: not compiled", label);
#endif
    } else if (strcmp(act.type, ACTION_TYPE_SOUND_ALERT) == 0) {
#if HAS_AUDIO
        const auto& sound_alert = act.payload.sound_alert;
        if (strcmp(sound_alert.sound_alert_kind, "tone") == 0) {
            audio_beep(sound_alert.sound_alert_pattern, sound_alert.sound_alert_volume);
        } else if (strcmp(sound_alert.sound_alert_kind, "mp3") == 0) {
#if HAS_SOUND_PLAYER
            audio_play_sound(sound_alert.sound_alert_file, sound_alert.sound_alert_volume);
#else
            LOGW(TAG, "%s sound_alert MP3: not compiled", label);
#endif
        } else {
            LOGW(TAG, "%s sound_alert: invalid kind", label);
        }
#else
        LOGW(TAG, "%s sound_alert: not compiled", label);
#endif
    } else if (strcmp(act.type, ACTION_TYPE_TIMER) == 0) {
#if HAS_DISPLAY
        const auto& t = act.payload.timer;
        char error[96];
        if (timer_command_run(t, error, sizeof(error))) {
            LOGI(TAG, "%s timer: %u:%s", label, t.timer_id, t.timer_command);
        } else {
            LOGW(TAG, "%s timer: %s", label, error);
        }
#else
        LOGW(TAG, "%s timer: no display", label);
#endif
    } else if (strcmp(act.type, ACTION_TYPE_NOTIFY) == 0) {
#if HAS_DISPLAY
        const auto& n = act.payload.notify;
        MessageBubbleParams params = {};

        strlcpy(params.text, n.notify_text, sizeof(params.text));

        if (!params.text[0]) {
            message_bubble_dismiss();
            LOGI(TAG, "%s notify: dismiss", label);
        } else {
            // Duration — apply default for empty (already resolved by generic pass)
            const char* dur = n.notify_duration_ms[0] ? n.notify_duration_ms : "3000";
            params.duration_ms = (uint16_t)atoi(dur);

            // Colors — apply defaults for empty (already resolved by generic pass)
            const char* tc = n.notify_text_color[0] ? n.notify_text_color : "#ffffff";
            if (!parse_hex_color(tc, &params.text_color)) params.text_color = 0xFFFFFF;

            const char* bg = n.notify_bg_color[0] ? n.notify_bg_color : "#333333";
            if (!parse_hex_color(bg, &params.bg_color)) params.bg_color = 0x333333;

            if (n.notify_border_color[0]) {
                params.has_border = parse_hex_color(n.notify_border_color, &params.border_color);
            }

            params.opacity = n.notify_opacity;
            params.font_size = n.notify_font_size;
            params.location = notify_location_from_str(n.notify_location);

            message_bubble_show(&params);
            LOGI(TAG, "%s notify: '%s' dur=%u loc=%s", label, params.text,
                 params.duration_ms, n.notify_location[0] ? n.notify_location : "bottom");
        }
#else
        LOGW(TAG, "%s notify: no display", label);
#endif
    } else if (strcmp(act.type, ACTION_TYPE_VISUAL_ALERT) == 0) {
#if HAS_DISPLAY
        const auto& va = act.payload.visual_alert;
        if (strcmp(va.va_op, "stop") == 0) {
            visual_alert_stop();
            LOGI(TAG, "%s visual_alert: stop", label);
        } else {
            // Wake first so the alert is visible even if the screen is asleep.
            screen_saver_manager_notify_activity(true);

            VisualAlertParams params = {};
            const char* col = va.va_color[0] ? va.va_color : "#FF0000";
            if (!parse_hex_color(col, &params.color)) params.color = 0xFF0000;
            params.pattern     = visual_alert_pattern_from_str(va.va_pattern);
            params.period_ms   = va.va_period_ms > 0 ? va.va_period_ms : VA_DEFAULT_PERIOD_MS;
            params.intensity   = va.va_intensity  > 0 ? (uint8_t)va.va_intensity : VA_DEFAULT_INTENSITY;
            params.duration_ms = va.va_duration_ms;

            visual_alert_show(&params);
            LOGI(TAG, "%s visual_alert: start pat=%u per=%u int=%u dur=%u", label,
                 params.pattern, params.period_ms, params.intensity, params.duration_ms);
        }
#else
        LOGW(TAG, "%s visual_alert: no display", label);
#endif
    } else if (strcmp(act.type, ACTION_TYPE_SYSTEM) == 0) {
        const char* syscmd = act.payload.system.system_command;
        if (strcmp(syscmd, "reboot") == 0) {
            LOGI(TAG, "%s system: reboot", label);
            delay(200);
            ESP.restart();
        } else if (strcmp(syscmd, "wifi_reconnect") == 0) {
            LOGI(TAG, "%s system: wifi_reconnect", label);
            wifi_manager_request_reconnect();
        } else if (strcmp(syscmd, "screensaver") == 0) {
#if HAS_DISPLAY
            LOGI(TAG, "%s system: screensaver", label);
            screen_saver_manager_sleep_now();
#else
            LOGW(TAG, "%s system: screensaver unavailable (no display)", label);
#endif
        } else {
            LOGW(TAG, "%s system: unknown command '%s'", label, syscmd);
        }
    } else if (strcmp(act.type, ACTION_TYPE_HA_SERVICE) == 0) {
        const auto& h = act.payload.ha_service;
        if (h.entity_id[0] && h.service[0]) {
            LOGI(TAG, "%s ha_service: %s.%s", label, h.entity_id, h.service);
            if (ha_service_enqueue(h) == HA_SERVICE_QUEUE_FULL) {
                LOGW(TAG, "%s ha_service queue full: entity='%s' service='%s'",
                     label, h.entity_id, h.service);
            }
        } else {
            LOGW(TAG, "%s ha_service: missing entity_id/service", label);
        }
    } else {
        // Device-class action types (e.g. shutter) self-register via the
        // action type registry; delegate dispatch when found.
        const ActionTypeDef* t = action_type_find(act.type);
        if (t && t->dispatch) {
            return t->dispatch(act, label, continuation_token);
        } else {
            LOGW(TAG, "%s unknown action type: '%s'", label, act.type);
        }
    }
    return ACTION_COMPLETE;
}

// ---------------------------------------------------------------------------
// Called from main loop() — runs deferred action I/O off the LVGL task.
// ---------------------------------------------------------------------------
void action_dispatch_loop() {
    ha_service_execute();
    action_list_dispatch_continuation(ACTION_CONTINUATION_OWNER_LOOP);
}

#endif // HAS_DISPLAY || HAS_BUTTON
