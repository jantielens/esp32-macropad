#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

struct RtosTaskPsramAlloc {
		StaticTask_t* tcb;
		StackType_t* stack;
		uint32_t stackDepthWords;
};

// Create a FreeRTOS task whose stack is allocated from PSRAM.
// Returns false if PSRAM is not available or allocation/task creation fails.
//
// Notes:
// - `stackDepthWords` is in FreeRTOS stack words (not bytes).
// - The task control block (TCB) is allocated from internal 8-bit RAM.
bool rtos_create_task_psram_stack(
		TaskFunction_t taskFunction,
		const char* name,
		uint32_t stackDepthWords,
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
		uint32_t stackDepthWords,
		void* param,
		UBaseType_t priority,
		TaskHandle_t* outHandle,
		RtosTaskPsramAlloc* outAlloc,
		BaseType_t coreId
);
