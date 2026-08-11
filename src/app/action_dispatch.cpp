#include "action_dispatch.h"

#if HAS_DISPLAY || HAS_BUTTON

#include "action_continuation.h"
#include "action_list.h"
#include "action_registry.h"
#include "log_manager.h"
#include "music_command.h"

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

ActionResult action_dispatch_system(const ButtonAction& act, const char* label, uint32_t) {
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
    return ACTION_COMPLETE;
}

ActionResult action_dispatch_music(const ButtonAction& act, const char* label, uint32_t) {
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
    return ACTION_COMPLETE;
}

ActionResult action_dispatch_screen(const ButtonAction& act, const char* label, uint32_t) {
#if HAS_DISPLAY
    const char* screen_id = act.payload.screen.screen_id;
    if (screen_id[0]) {
        bool ok = false;
        display_manager_show_screen(screen_id, &ok);
        if (!ok) LOGW(TAG, "%s nav failed: '%s'", label, screen_id);
    }
#else
    LOGW(TAG, "%s screen: no display", label);
#endif
    return ACTION_COMPLETE;
}

ActionResult action_dispatch_key(const ButtonAction& act, const char* label, uint32_t) {
#if HAS_BLE_HID
    const char* sequence = act.payload.key.key_sequence;
    if (!ble_hid_is_initialized()) {
        LOGW(TAG, "%s key: BLE disabled", label);
    } else if (sequence[0]) {
        LOGI(TAG, "%s key: '%s'", label, sequence);
        ble_hid_request_sequence(sequence);
    } else {
        LOGW(TAG, "%s key: empty sequence", label);
    }
#else
    LOGW(TAG, "%s key: not compiled", label);
#endif
    return ACTION_COMPLETE;
}

ActionResult action_dispatch_ble_pair(const ButtonAction&, const char* label, uint32_t) {
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
    return ACTION_COMPLETE;
}

ActionResult action_dispatch_delay(const ButtonAction& act, const char* label,
                                   uint32_t continuation_token) {
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
}

ActionResult action_dispatch_cycle_pad(const ButtonAction& act, const char* label, uint32_t) {
#if HAS_DISPLAY
    const auto& cycle = act.payload.cycle_pad;
    if (!display_manager_cycle_pad(cycle.direction, cycle.wrap, cycle.excluded_mask)) {
        LOGD(TAG, "%s cycle_pad: no eligible destination", label);
    }
#else
    LOGW(TAG, "%s cycle_pad: no display", label);
#endif
    return ACTION_COMPLETE;
}

ActionResult action_dispatch_mqtt(const ButtonAction& act, const char* label, uint32_t) {
#if HAS_MQTT
    const auto& mqtt = act.payload.mqtt;
    if (mqtt.mqtt_topic[0]) {
        bool ok = mqtt_manager.publish(mqtt.mqtt_topic, mqtt.mqtt_payload, false);
        LOGI(TAG, "%s mqtt: topic='%s' payload='%s' %s", label, mqtt.mqtt_topic,
             mqtt.mqtt_payload, ok ? "ok" : "FAIL");
    } else {
        LOGW(TAG, "%s mqtt: empty topic", label);
    }
#else
    LOGW(TAG, "%s mqtt: not compiled", label);
#endif
    return ACTION_COMPLETE;
}

ActionResult action_dispatch_sound_alert(const ButtonAction& act, const char* label, uint32_t) {
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
    return ACTION_COMPLETE;
}

ActionResult action_dispatch_timer(const ButtonAction& act, const char* label, uint32_t) {
#if HAS_DISPLAY
    const auto& timer = act.payload.timer;
    char error[96];
    if (timer_command_run(timer, error, sizeof(error))) {
        LOGI(TAG, "%s timer: %u:%s", label, timer.timer_id, timer.timer_command);
    } else {
        LOGW(TAG, "%s timer: %s", label, error);
    }
#else
    LOGW(TAG, "%s timer: no display", label);
#endif
    return ACTION_COMPLETE;
}

ActionResult action_dispatch_notify(const ButtonAction& act, const char* label, uint32_t) {
#if HAS_DISPLAY
    const auto& notify = act.payload.notify;
    MessageBubbleParams params = {};
    strlcpy(params.text, notify.notify_text, sizeof(params.text));
    if (!params.text[0]) {
        message_bubble_dismiss();
        LOGI(TAG, "%s notify: dismiss", label);
    } else {
        const char* duration = notify.notify_duration_ms[0] ? notify.notify_duration_ms : "3000";
        params.duration_ms = (uint16_t)atoi(duration);
        const char* text_color = notify.notify_text_color[0] ? notify.notify_text_color : "#ffffff";
        if (!parse_hex_color(text_color, &params.text_color)) params.text_color = 0xFFFFFF;
        const char* background = notify.notify_bg_color[0] ? notify.notify_bg_color : "#333333";
        if (!parse_hex_color(background, &params.bg_color)) params.bg_color = 0x333333;
        if (notify.notify_border_color[0]) {
            params.has_border = parse_hex_color(notify.notify_border_color, &params.border_color);
        }
        params.opacity = notify.notify_opacity;
        params.font_size = notify.notify_font_size;
        params.location = notify_location_from_str(notify.notify_location);
        message_bubble_show(&params);
        LOGI(TAG, "%s notify: '%s' dur=%u loc=%s", label, params.text, params.duration_ms,
             notify.notify_location[0] ? notify.notify_location : "bottom");
    }
#else
    LOGW(TAG, "%s notify: no display", label);
#endif
    return ACTION_COMPLETE;
}

ActionResult action_dispatch_visual_alert(const ButtonAction& act, const char* label, uint32_t) {
#if HAS_DISPLAY
    const auto& visual_alert = act.payload.visual_alert;
    if (strcmp(visual_alert.va_op, "stop") == 0) {
        visual_alert_stop();
        LOGI(TAG, "%s visual_alert: stop", label);
    } else {
        screen_saver_manager_notify_activity(true);
        VisualAlertParams params = {};
        const char* color = visual_alert.va_color[0] ? visual_alert.va_color : "#FF0000";
        if (!parse_hex_color(color, &params.color)) params.color = 0xFF0000;
        params.pattern = visual_alert_pattern_from_str(visual_alert.va_pattern);
        params.period_ms = visual_alert.va_period_ms > 0 ? visual_alert.va_period_ms : VA_DEFAULT_PERIOD_MS;
        params.intensity = visual_alert.va_intensity > 0 ? (uint8_t)visual_alert.va_intensity : VA_DEFAULT_INTENSITY;
        params.duration_ms = visual_alert.va_duration_ms;
        visual_alert_show(&params);
        LOGI(TAG, "%s visual_alert: start pat=%u per=%u int=%u dur=%u", label,
             params.pattern, params.period_ms, params.intensity, params.duration_ms);
    }
#else
    LOGW(TAG, "%s visual_alert: no display", label);
#endif
    return ACTION_COMPLETE;
}

ActionResult action_dispatch_ha_service(const ButtonAction& act, const char* label, uint32_t) {
    const auto& service = act.payload.ha_service;
    if (service.entity_id[0] && service.service[0]) {
        LOGI(TAG, "%s ha_service: %s.%s", label, service.entity_id, service.service);
        if (ha_service_enqueue(service) == HA_SERVICE_QUEUE_FULL) {
            LOGW(TAG, "%s ha_service queue full: entity='%s' service='%s'", label,
                 service.entity_id, service.service);
        }
    } else {
        LOGW(TAG, "%s ha_service: missing entity_id/service", label);
    }
    return ACTION_COMPLETE;
}

#if HAS_MQTT
// Resolve binding templates in the active payload arm's resolvable fields.
// Structural fields (commands, modes, ids) are excluded — only fields that
// users may template are visited. Type-dispatched so we only touch the
// active arm of the discriminated union (writing a non-active arm is UB).
static bool resolve_action_bindings(ButtonAction& act) {
    return action_type_resolve_bindings(action_type_find(act.type), act);
}

// Quick scan: return true if the active payload arm contains a binding token.
// Checks only for '[' to avoid the ButtonAction copy for the common case.
// Type-dispatched so we only read the active union arm.
static bool action_has_any_binding(const ButtonAction& act) {
    return action_type_has_binding(action_type_find(act.type), act);
}

// Collect MQTT topics from every bindable field of an action. Mirrors the
// field set in resolve_action_bindings() so a token used only inside a button
// action still gets subscribed by mqtt_sub_store's scan.
void action_collect_binding_topics(const ButtonAction& act, void* user_data) {
    action_type_collect_topics(action_type_find(act.type), act, user_data);
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
    const ActionTypeDef* type = action_type_find(act.type);
    if (type && type->dispatch) return type->dispatch(act, label, continuation_token);
    LOGW(TAG, "%s unknown action type: '%s'", label, act.type);
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
