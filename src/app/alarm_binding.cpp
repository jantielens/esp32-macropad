#include "board_config.h"
#include "alarm_binding.h"
#if ALARM_ENABLED
#include "alarm_manager.h"
#include "binding_template.h"
#include <stdio.h>
#include <string.h>
static const char* const alarm_keys[] = {"1_time", "1_enabled", "1_state", "1_ready", "active_id"};
static uint8_t alarm_key_count() { return sizeof(alarm_keys) / sizeof(alarm_keys[0]); }
static const char* alarm_key_at(uint8_t index) { return index < alarm_key_count() ? alarm_keys[index] : nullptr; }
static BindingResolverStatus alarm_resolve(const char* key, char* output, size_t length) {
    AlarmSnapshot value = alarm_snapshot();
    if (!strcmp(key, "1_time")) snprintf(output, length, "%02u:%02u", value.hour, value.minute);
    else if (!strcmp(key, "1_enabled")) snprintf(output, length, "%s", value.enabled ? "ON" : "OFF");
    else if (!strcmp(key, "1_ready")) snprintf(output, length, "%s", value.ready ? "ON" : "OFF");
    else if (!strcmp(key, "1_state")) snprintf(output, length, "%s", value.state == ALARM_RINGING ? "ringing" : value.state == ALARM_SNOOZED ? "snoozed" : "idle");
    else if (!strcmp(key, "active_id")) snprintf(output, length, "%u", value.state == ALARM_IDLE ? 0 : 1);
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