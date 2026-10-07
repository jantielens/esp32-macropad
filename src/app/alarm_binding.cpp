#include "board_config.h"
#include "alarm_binding.h"
#if ALARM_ENABLED
#include "alarm_manager.h"
#include "binding_template.h"
#include "time_service.h"
#include <stdio.h>
#include <string.h>
static const char* const alarm_keys[] = {
    "1_time", "1_enabled", "1_state", "1_ready", "active_id", "1_hour", "1_minute", "1_minutes",
    "1_weekdays", "1_day_0", "1_day_1", "1_day_2", "1_day_3", "1_day_4", "1_day_5", "1_day_6",
    "1_repeat", "1_snooze_minutes", "1_auto_dismiss_minutes", "1_once_epoch", "1_once_local", "1_once_available",
    "1_next_epoch", "1_next_local", "1_next_seconds", "1_next_available", "1_snooze_seconds", "1_snooze_available",
    "1_dismiss_seconds", "1_dismiss_available", "1_next_ring_seconds", "1_next_ring_available",
    "1_ota_deferred", "1_storage_error", "1_hook_error", "1_command_error", "1_command_message",
    "1_pending_commands", "1_completed_commands", "1_save_state"
};
static uint8_t alarm_key_count() { return sizeof(alarm_keys) / sizeof(alarm_keys[0]); }
static const char* alarm_key_at(uint8_t index) { return index < alarm_key_count() ? alarm_keys[index] : nullptr; }
static BindingResolverStatus alarm_resolve(const char* key, char* output, size_t length) {
    AlarmSnapshot value = alarm_snapshot();
    if (!length) return BINDING_RESOLVER_RESOLVED;
    output[0] = '\0';
    if (!strcmp(key, "1_time")) snprintf(output, length, "%02u:%02u", value.hour, value.minute);
    else if (!strcmp(key, "1_enabled")) snprintf(output, length, "%s", value.enabled ? "ON" : "OFF");
    else if (!strcmp(key, "1_ready")) snprintf(output, length, "%s", value.ready ? "ON" : "OFF");
    else if (!strcmp(key, "1_state")) snprintf(output, length, "%s", value.state == ALARM_RINGING ? "ringing" : value.state == ALARM_SNOOZED ? "snoozed" : "idle");
    else if (!strcmp(key, "active_id")) snprintf(output, length, "%u", value.state == ALARM_IDLE ? 0 : 1);
    else if (!strcmp(key, "1_hour")) snprintf(output, length, "%u", value.hour);
    else if (!strcmp(key, "1_minute")) snprintf(output, length, "%u", value.minute);
    else if (!strcmp(key, "1_minutes")) snprintf(output, length, "%u", value.hour * 60 + value.minute);
    else if (!strcmp(key, "1_weekdays")) snprintf(output, length, "%u", value.weekdays);
    else if (!strncmp(key, "1_day_", 6) && key[6] >= '0' && key[6] <= '6' && !key[7])
        snprintf(output, length, "%s", value.weekdays & (1U << (key[6] - '0')) ? "ON" : "OFF");
    else if (!strcmp(key, "1_repeat")) snprintf(output, length, "%s", value.weekdays ? "weekly" : "once");
    else if (!strcmp(key, "1_snooze_minutes")) snprintf(output, length, "%u", value.snooze_minutes);
    else if (!strcmp(key, "1_auto_dismiss_minutes")) snprintf(output, length, "%u", value.dismiss_minutes);
    else if (!strcmp(key, "1_once_available")) snprintf(output, length, "%s", value.once_epoch ? "ON" : "OFF");
    else if (!strcmp(key, "1_next_available")) snprintf(output, length, "%s", value.next_epoch ? "ON" : "OFF");
    else if (!strcmp(key, "1_snooze_available")) snprintf(output, length, "%s", value.state == ALARM_SNOOZED ? "ON" : "OFF");
    else if (!strcmp(key, "1_dismiss_available")) snprintf(output, length, "%s", value.state == ALARM_RINGING ? "ON" : "OFF");
    else if (!strcmp(key, "1_next_ring_available")) snprintf(output, length, "%s", value.state != ALARM_IDLE || value.next_epoch ? "ON" : "OFF");
    else if (!strcmp(key, "1_once_epoch") || !strcmp(key, "1_next_epoch")) {
        const time_t epoch = !strcmp(key, "1_once_epoch") ? value.once_epoch : value.next_epoch;
        if (epoch) snprintf(output, length, "%lld", static_cast<long long>(epoch));
    } else if (!strcmp(key, "1_once_local") || !strcmp(key, "1_next_local")) {
        const time_t epoch = !strcmp(key, "1_once_local") ? value.once_epoch : value.next_epoch;
        if (epoch) time_service_format(epoch, "%Y-%m-%d %H:%M", nullptr, output, length);
    } else if (!strcmp(key, "1_next_seconds")) {
        if (value.next_epoch) snprintf(output, length, "%u", value.next_seconds);
    } else if (!strcmp(key, "1_snooze_seconds")) {
        if (value.state == ALARM_SNOOZED) snprintf(output, length, "%u", value.snooze_remaining);
    } else if (!strcmp(key, "1_dismiss_seconds")) {
        if (value.state == ALARM_RINGING) snprintf(output, length, "%u", value.dismiss_remaining);
    } else if (!strcmp(key, "1_next_ring_seconds")) {
        if (value.state != ALARM_IDLE || value.next_epoch) snprintf(output, length, "%u", value.next_ring_seconds);
    }
    else if (!strcmp(key, "1_ota_deferred")) snprintf(output, length, "%s", value.deferred ? "ON" : "OFF");
    else if (!strcmp(key, "1_storage_error")) snprintf(output, length, "%s", value.storage_error ? "ON" : "OFF");
    else if (!strcmp(key, "1_hook_error")) snprintf(output, length, "%s", value.hook_error ? "ON" : "OFF");
    else if (!strcmp(key, "1_command_error")) snprintf(output, length, "%s", value.command_error ? "ON" : "OFF");
    else if (!strcmp(key, "1_command_message")) snprintf(output, length, "%s", value.command_message);
    else if (!strcmp(key, "1_pending_commands")) snprintf(output, length, "%u", value.pending_commands);
    else if (!strcmp(key, "1_completed_commands")) snprintf(output, length, "%u", value.completed_commands);
    else if (!strcmp(key, "1_save_state")) snprintf(output, length, "%s", value.save_state());
    else return BINDING_RESOLVER_UNKNOWN;
    return BINDING_RESOLVER_RESOLVED;
}
void alarm_binding_init() {
    binding_template_register("alarm", alarm_resolve, nullptr,
        {1, 1, 1, 1, BINDING_VALIDATION_STANDARD, false, alarm_key_count, alarm_key_at});
}
#else
void alarm_binding_init() {}
#endif