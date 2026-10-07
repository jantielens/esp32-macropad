#pragma once
#include "board_config.h"
#if ALARM_ENABLED
#include "alarm_clock_core.h"
#include "pad_config.h"
#include <ArduinoJson.h>

struct AlarmDefinition {
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
};

void alarm_manager_init();
void alarm_manager_loop();
bool alarm_config_save_raw(const uint8_t* data, size_t length);
const char* alarm_last_error();
void alarm_config_to_json(JsonObject root);
void alarm_status_to_json(JsonObject root);
AlarmSnapshot alarm_snapshot();
bool alarm_command_submit(const char* command, uint8_t id = 0);
#endif