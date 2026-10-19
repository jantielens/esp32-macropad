#pragma once

#include "board_config.h"

#if HAS_HA_LIVE
#include <stddef.h>
#include <stdint.h>

constexpr size_t HA_LIVE_MAX_ENTITIES = 64;
constexpr size_t HA_LIVE_ENTITY_BYTES = 96;
constexpr size_t HA_LIVE_SNAPSHOT_BYTES = 2048;

struct HaLiveStatus {
    bool configured;
    bool connected;
    uint16_t entity_count;
    uint32_t messages;
    uint32_t reconnects;
    const char* state;
    const char* last_error;
};

void ha_live_init(const char* url, const char* token);
// Copies validated persisted credentials; the worker never reads mutable config.
void ha_live_configure(const char* url, const char* token);
// Called only on the LVGL task, before data streams and screen updates.
void ha_live_tick();
bool ha_live_read(const char* entity, char* snapshot, size_t size);
void ha_live_status(HaLiveStatus& status);
bool ha_live_valid_entity(const char* entity);
#endif
