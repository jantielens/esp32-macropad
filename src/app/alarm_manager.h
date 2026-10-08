#pragma once
#include "board_config.h"
#if ALARM_ENABLED
#include "alarm_clock_core.h"
#include "pad_config.h"
#include <ArduinoJson.h>
#include <string.h>

static const char* const alarm_command_names[] = {"", "cancel", "snooze", "set_time", "adjust_minutes", "enable", "disable", "toggle", "weekday_enable", "weekday_disable", "weekday_toggle"};
inline uint8_t alarm_command_operation(const char* command) {
    if (command) for (uint8_t index = 1; index < sizeof(alarm_command_names) / sizeof(alarm_command_names[0]); ++index)
        if (!strcmp(command, alarm_command_names[index])) return index;
    return 0;
}
inline const char* alarm_command_validate(const char* command, uint8_t id, int value = 0, uint8_t day = 0) {
    if (id > 1) return "alarm_id must be 0 or 1";
    const uint8_t operation = alarm_command_operation(command);
    if (!operation) return "Unknown alarm command";
    if (operation > 2 && id != 1) return "Configuration commands require alarm_id 1";
    if (operation == 3 && (value < 0 || value > 1439)) return "set_time requires minutes since midnight (0-1439)";
    if (operation >= 8 && day > 6) return "alarm_day must be 0 (Sunday) through 6 (Saturday)";
    return nullptr;
}

struct AlarmDefinition {
    uint16_t lateness_minutes = 360;
    bool enabled = false;
    uint8_t hour = 7;
    uint8_t minute = 0;
    uint8_t weekdays = 0;
    uint16_t snooze_minutes = 9;
    uint16_t dismiss_minutes = 30;
    uint8_t ring_count = 0;
    uint8_t stop_count = 0;
    ButtonAction on_ring[MAX_BUTTON_ACTIONS] = {};
    ButtonAction on_stop[MAX_BUTTON_ACTIONS] = {};
};

struct AlarmSnapshot {
    bool enabled;
    uint8_t hour;
    uint8_t minute;
    AlarmState state;
    bool ready;
    bool deferred;
    bool storage_error;
    bool hook_error;
    time_t once_epoch;
    uint16_t lateness_minutes = 360;
    uint8_t weekdays = 0;
    uint16_t snooze_minutes = 9;
    uint16_t dismiss_minutes = 30;
    time_t next_epoch = 0;
    uint32_t next_seconds = 0;
    uint32_t next_ring_seconds = 0;
    uint32_t snooze_remaining = 0;
    uint32_t dismiss_remaining = 0;
    uint8_t pending_commands = 0;
    uint32_t completed_commands = 0;
    bool command_error = false;
    char command_message[128] = {};
    bool save_pending = false;
    bool save_failed = false;
    const char* save_state() const {
        return save_failed ? "failed" : pending_commands || save_pending ? "pending"
            : command_error ? "failed" : completed_commands ? "saved" : "idle";
    }
};

void alarm_manager_init();
void alarm_manager_loop();
bool alarm_config_save_raw(const uint8_t* data, size_t length);
enum class AlarmConfigSection { Schedule, Behavior };
bool alarm_config_save_section_raw(const uint8_t* data, size_t length, AlarmConfigSection section);
const char* alarm_last_error();
void alarm_config_to_json(JsonObject root);
void alarm_status_to_json(JsonObject root);
AlarmSnapshot alarm_snapshot();
void alarm_command_report_error(const char* message);
bool alarm_command_submit(const char* command, uint8_t id = 0, int value = 0, uint8_t day = 0);
#endif