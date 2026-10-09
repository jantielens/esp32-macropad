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
static const BindingParamDoc kAlarmParams[] = {
    {"key", "Alarm key, see below."},
};
static const BindingKeyDoc kAlarmKeyDocs[] = {
    {"1_time", "Alarm time as HH:MM", "Schedule"},
    {"1_hour", "Alarm hour 0-23", "Schedule"},
    {"1_minute", "Alarm minute 0-59", "Schedule"},
    {"1_minutes", "Minutes after midnight", "Schedule"},
    {"1_enabled", "ON when the alarm is enabled", "Schedule"},
    {"1_ready", "ON once the alarm is loaded and the clock has synced", "Schedule"},
    {"1_repeat", "once or weekly", "Schedule"},
    {"1_weekdays", "Repeat days as a bitmask, bit 0 = Sunday", "Schedule"},
    {"1_day_#", "ON when weekday 0-6 (Sunday-Saturday) repeats", "Schedule"},
    {"1_snooze_minutes", "Snooze length in minutes", "Schedule"},
    {"1_auto_dismiss_minutes", "Minutes until a ringing alarm stops", "Schedule"},
    {"1_once_epoch", "One-time alarm as epoch seconds; empty if none", "Schedule"},
    {"1_once_local", "One-time alarm as YYYY-MM-DD HH:MM; empty if none", "Schedule"},
    {"1_once_available", "ON when a one-time alarm is set", "Schedule"},
    {"1_state", "idle, ringing, or snoozed", "Now"},
    {"active_id", "1 while ringing or snoozed, else 0", "Now"},
    {"1_next_epoch", "Next scheduled alarm as epoch seconds; empty if none", "Now"},
    {"1_next_local", "Next scheduled alarm as YYYY-MM-DD HH:MM", "Now"},
    {"1_next_seconds", "Seconds until the next scheduled alarm, excluding snooze", "Now"},
    {"1_next_available", "ON when 1_next_seconds has a value", "Now"},
    {"1_next_ring_seconds", "Seconds until the next ring, including snooze", "Now"},
    {"1_next_ring_available", "ON when 1_next_ring_seconds has a value", "Now"},
    {"1_snooze_seconds", "Seconds until snooze ends; empty unless snoozed", "Now"},
    {"1_snooze_available", "ON while snoozed", "Now"},
    {"1_dismiss_seconds", "Seconds until auto-dismiss; empty unless ringing", "Now"},
    {"1_dismiss_available", "ON while ringing", "Now"},
    {"1_save_state", "saved, pending, or failed", "Diagnostics"},
    {"1_pending_commands", "Queued alarm commands", "Diagnostics"},
    {"1_completed_commands", "Completed alarm commands", "Diagnostics"},
    {"1_command_error", "ON when the last command failed", "Diagnostics"},
    {"1_command_message", "Message from the last command", "Diagnostics"},
    {"1_ota_deferred", "ON when an update waits for the alarm", "Diagnostics"},
    {"1_storage_error", "ON when settings could not be saved", "Diagnostics"},
    {"1_hook_error", "ON when an alarm action failed", "Diagnostics"},
};
static const BindingExampleDoc kAlarmExamples[] = {
    {"[alarm:1_time]", "The configured alarm time."},
    {"[expr:[alarm:1_state]==\"ringing\"?\"#dc2626\":\"#334155\"]", "Red while the alarm rings."},
    {"[expr:[alarm:1_next_available]==\"ON\"?\"Next alarm set\":\"No alarm\"]", "Check availability before using a countdown."},
};
static const BindingSchemeDoc kAlarmDoc = {
    "Device", "Read-only state of alarm 1.",
    BINDING_DOC_LIST(kAlarmParams), BINDING_DOC_LIST(kAlarmKeyDocs), BINDING_DOC_LIST(kAlarmExamples),
    nullptr, false, BINDING_DOC_NONE,
    "Countdowns are empty, not 0, when unavailable; check the matching _available key.", nullptr,
};
void alarm_binding_init() {
    binding_template_register("alarm", alarm_resolve, nullptr,
        {1, 1, 1, 1, BINDING_VALIDATION_STANDARD, false, alarm_key_count, alarm_key_at, &kAlarmDoc});
}
#else
void alarm_binding_init() {}
#endif