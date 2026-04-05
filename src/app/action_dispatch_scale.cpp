#include "action_dispatch_scale.h"

#if HAS_DISPLAY

#include "log_manager.h"

#if HAS_SCALE
#include "scale_hal.h"
#include "brew_manager.h"
#include "binding_template.h"
#endif

#define TAG "Action"

bool scale_action_dispatch(const ButtonAction& act, const char* label) {
    if (strcmp(act.type, ACTION_TYPE_SCALE) == 0) {
#if HAS_SCALE
        const char* cmd = act.mqtt_payload;
        if (!cmd || !cmd[0] || strcmp(cmd, "tare") == 0) {
            LOGI(TAG, "%s scale: tare (deferred)", label);
            scale_request_tare();
        } else if (strcmp(cmd, "calibrate") == 0) {
            LOGI(TAG, "%s scale: calibrate (deferred)", label);
            scale_request_calibrate();
        } else if (strncmp(cmd, "cal_weight:", 11) == 0) {
            float delta = strtof(cmd + 11, nullptr);
            if (delta != 0.0f) {
                scale_adjust_cal_weight(delta);
                LOGI(TAG, "%s scale: cal_weight delta=%.1f -> %.1f g", label, delta, scale_get_cal_weight());
            } else {
                LOGW(TAG, "%s scale: cal_weight invalid delta '%s'", label, cmd + 11);
            }
        } else if (strncmp(cmd, "cal_weight_set:", 15) == 0) {
            float val = strtof(cmd + 15, nullptr);
            if (val >= 1.0f) {
                scale_set_cal_weight(val);
                LOGI(TAG, "%s scale: cal_weight_set %.1f g", label, scale_get_cal_weight());
            } else {
                LOGW(TAG, "%s scale: cal_weight_set invalid '%s'", label, cmd + 15);
            }
        } else {
            LOGW(TAG, "%s scale: unknown cmd '%s'", label, cmd);
        }
#else
        LOGW(TAG, "%s scale: not compiled", label);
#endif
        return true;
    }

    if (strcmp(act.type, ACTION_TYPE_BREW) == 0) {
#if HAS_SCALE
        const char* cmd = act.mqtt_payload;
        if (!cmd || !cmd[0]) cmd = "advance";
        if (strncmp(cmd, "set_template:", 13) == 0) {
            const char* tpl = cmd + 13;
            if (strchr(tpl, '[')) {
                char resolved[64];
                binding_template_resolve(tpl, resolved, sizeof(resolved));
                LOGI(TAG, "%s brew: set_template='%s' (resolved from '%s')", label, resolved, tpl);
                brew_hint_template(resolved);
            } else {
                LOGI(TAG, "%s brew: set_template='%s'", label, tpl);
                brew_hint_template(tpl);
            }
        } else if (strcmp(cmd, "advance") == 0) {
            LOGI(TAG, "%s brew: advance", label);
            brew_advance(nullptr);
        } else if (strcmp(cmd, "start") == 0) {
            LOGI(TAG, "%s brew: start", label);
            brew_start(nullptr);
        } else if (strcmp(cmd, "next") == 0) {
            LOGI(TAG, "%s brew: next", label);
            brew_next();
        } else if (strcmp(cmd, "stop") == 0) {
            LOGI(TAG, "%s brew: stop", label);
            brew_stop();
        } else if (strcmp(cmd, "reset") == 0) {
            LOGI(TAG, "%s brew: reset", label);
            brew_reset();
        } else if (strcmp(cmd, "tare") == 0) {
            LOGI(TAG, "%s brew: tare", label);
            scale_request_tare_no_persist();
        } else {
            LOGW(TAG, "%s brew: unknown cmd '%s'", label, cmd);
        }
#else
        LOGW(TAG, "%s brew: not compiled", label);
#endif
        return true;
    }

    return false;
}

#endif // HAS_DISPLAY
