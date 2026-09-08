#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

struct RtosTaskPsramAlloc {
		StaticTask_t* tcb;
		StackType_t* stack;
		uint32_t stackDepthBytes;
};

using RtosTaskInternalAlloc = RtosTaskPsramAlloc;

// Create a FreeRTOS task whose stack is allocated from PSRAM. Use this only
// for compute, network, decode, or render work that never accesses LittleFS,
// Preferences/NVS, OTA, or other flash-backed operations.
// Returns false if PSRAM is not available or allocation/task creation fails.
//
// Notes:
// - `stackDepthBytes` is in bytes, matching the ESP-IDF FreeRTOS API.
// - The task control block (TCB) is allocated from internal 8-bit RAM.
bool rtos_create_task_psram_stack(
		TaskFunction_t taskFunction,
		const char* name,
		uint32_t stackDepthBytes,
		void* param,
		UBaseType_t priority,
		TaskHandle_t* outHandle,
		RtosTaskPsramAlloc* outAlloc
);

// Core-pinned variant of rtos_create_task_psram_stack.
// Pins the task to the specified core (0 or 1). Use tskNO_AFFINITY for no pinning.
bool rtos_create_task_psram_stack_pinned(
		TaskFunction_t taskFunction,
		const char* name,
		uint32_t stackDepthBytes,
		void* param,
		UBaseType_t priority,
		TaskHandle_t* outHandle,
		RtosTaskPsramAlloc* outAlloc,
		BaseType_t coreId
);

// Create a FreeRTOS task whose stack is allocated from internal RAM.
// Use this for tasks that may access LittleFS, Preferences/NVS, OTA, or other
// flash-backed operations. Returns false if allocation, placement validation,
// or task creation fails.
bool rtos_create_task_internal_stack(
		TaskFunction_t taskFunction,
		const char* name,
		uint32_t stackDepthBytes,
		void* param,
		UBaseType_t priority,
		TaskHandle_t* outHandle,
		RtosTaskInternalAlloc* outAlloc
);

// Core-pinned variant of rtos_create_task_internal_stack.
// Pins the task to the specified core (0 or 1). Use tskNO_AFFINITY for no pinning.
bool rtos_create_task_internal_stack_pinned(
		TaskFunction_t taskFunction,
		const char* name,
		uint32_t stackDepthBytes,
		void* param,
		UBaseType_t priority,
		TaskHandle_t* outHandle,
		RtosTaskInternalAlloc* outAlloc,
		BaseType_t coreId
);
