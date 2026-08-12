#pragma once

#include <Arduino.h>
#include "board_config.h"

#if HAS_NATIVE_EXTENSIONS

#include <stddef.h>
#include <stdint.h>

#include "native_extension_api.h"

#define NATIVE_EXTENSION_PARTITION_SUBTYPE 0x40
#define NATIVE_EXTENSION_PARTITION_LABEL "extensions"
#define NATIVE_EXTENSION_SLOT_COUNT 3
#define NATIVE_EXTENSION_ID_MAX_LEN 32
#define NATIVE_EXTENSION_VERSION_MAX_LEN 16
#define NATIVE_EXTENSION_CONFIG_MAX_LEN 512

struct NativeExtensionSlotInfo {
	uint8_t slot;
	bool installed;
	bool staged;
	bool pending_delete;
	bool incompatible_abi;
	bool enabled;
	bool loaded;
	uint32_t capacity;
	uint32_t elf_size;
	uint32_t staged_size;
	uint32_t abi_version;
	char id[NATIVE_EXTENSION_ID_MAX_LEN];
	char version[NATIVE_EXTENSION_VERSION_MAX_LEN];
};

struct NativeExtensionSlotHeader {
	uint32_t magic;
	uint32_t abi_version;
	uint32_t elf_size;
	uint8_t enabled;
	uint8_t reserved[3];
	char id[NATIVE_EXTENSION_ID_MAX_LEN];
	char version[NATIVE_EXTENSION_VERSION_MAX_LEN];
};

bool native_extension_init();
uint8_t native_extension_slot_count();
bool native_extension_get_slot(uint8_t slot, NativeExtensionSlotInfo* out);
bool native_extension_stage_file(uint8_t slot, const char* filename, const char* source_path);
bool native_extension_set_enabled(uint8_t slot, bool enabled);
bool native_extension_delete(uint8_t slot);

// These APIs must only be called from the LVGL task.
bool native_extension_create_instance(const char* extension_id, uint32_t instance_id,
									  void* root, const char* config_json);
void native_extension_destroy_instance(const char* extension_id, uint32_t instance_id);
NativeExtensionEventResult native_extension_on_tap(const char* extension_id, uint32_t instance_id);
NativeExtensionEventResult native_extension_on_long_press(const char* extension_id, uint32_t instance_id);
void native_extension_tick_instance(const char* extension_id, uint32_t instance_id);

#endif