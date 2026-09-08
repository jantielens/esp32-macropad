#include "rtos_task_utils.h"

#include "esp_memory_utils.h"
#include "soc/soc_caps.h"

#include <esp_heap_caps.h>

static inline bool psram_available() {
#if SOC_SPIRAM_SUPPORTED
		return heap_caps_get_total_size(MALLOC_CAP_SPIRAM) > 0;
#else
		return false;
#endif
}

// Internal helper: allocate PSRAM stack + internal TCB, create task.
static bool create_task_impl(
		TaskFunction_t taskFunction,
		const char* name,
		uint32_t stackDepthBytes,
		void* param,
		UBaseType_t priority,
		TaskHandle_t* outHandle,
		RtosTaskPsramAlloc* outAlloc,
		BaseType_t coreId,
		uint32_t stackCaps,
		bool requiresPsram,
		bool requiresInternal
) {
		if (!taskFunction || !name || stackDepthBytes == 0 || !outHandle) {
				return false;
		}

		*outHandle = nullptr;

		if (requiresPsram && !psram_available()) {
				return false;
		}

		StackType_t* stack = static_cast<StackType_t*>(
				heap_caps_malloc(stackDepthBytes, stackCaps)
		);

		if (stack == nullptr) {
				return false;
		}
		if (requiresInternal) {
				uint8_t* stackStart = reinterpret_cast<uint8_t*>(stack);
				uint8_t* stackEnd = stackStart + stackDepthBytes - 1;
				if (!esp_ptr_internal(stackStart) || !esp_ptr_internal(stackEnd)) {
					heap_caps_free(stack);
					return false;
				}
		}

		StaticTask_t* tcb = static_cast<StaticTask_t*>(
				heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
		);

		if (tcb == nullptr) {
				heap_caps_free(stack);
				return false;
		}

		TaskHandle_t handle;
#if !CONFIG_FREERTOS_UNICORE
		if (coreId != tskNO_AFFINITY) {
				handle = xTaskCreateStaticPinnedToCore(taskFunction, name, stackDepthBytes, param, priority, stack, tcb, coreId);
		} else
#endif
		{
				handle = xTaskCreateStatic(taskFunction, name, stackDepthBytes, param, priority, stack, tcb);
		}

		if (handle == nullptr) {
				heap_caps_free(tcb);
				heap_caps_free(stack);
				return false;
		}

		if (outAlloc) {
				outAlloc->tcb = tcb;
				outAlloc->stack = stack;
				outAlloc->stackDepthBytes = stackDepthBytes;
		}

		*outHandle = handle;
		return true;
}

bool rtos_create_task_psram_stack(
		TaskFunction_t taskFunction,
		const char* name,
		uint32_t stackDepthBytes,
		void* param,
		UBaseType_t priority,
		TaskHandle_t* outHandle,
		RtosTaskPsramAlloc* outAlloc
) {
		return create_task_impl(taskFunction, name, stackDepthBytes, param, priority, outHandle,
				outAlloc, tskNO_AFFINITY, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, true, false);
}

bool rtos_create_task_psram_stack_pinned(
		TaskFunction_t taskFunction,
		const char* name,
		uint32_t stackDepthBytes,
		void* param,
		UBaseType_t priority,
		TaskHandle_t* outHandle,
		RtosTaskPsramAlloc* outAlloc,
		BaseType_t coreId
) {
		return create_task_impl(taskFunction, name, stackDepthBytes, param, priority, outHandle,
				outAlloc, coreId, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT, true, false);
}

bool rtos_create_task_internal_stack(
		TaskFunction_t taskFunction,
		const char* name,
		uint32_t stackDepthBytes,
		void* param,
		UBaseType_t priority,
		TaskHandle_t* outHandle,
		RtosTaskInternalAlloc* outAlloc
) {
		return create_task_impl(taskFunction, name, stackDepthBytes, param, priority, outHandle,
				outAlloc, tskNO_AFFINITY,
				MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, false, true);
}

bool rtos_create_task_internal_stack_pinned(
		TaskFunction_t taskFunction,
		const char* name,
		uint32_t stackDepthBytes,
		void* param,
		UBaseType_t priority,
		TaskHandle_t* outHandle,
		RtosTaskInternalAlloc* outAlloc,
		BaseType_t coreId
) {
		return create_task_impl(taskFunction, name, stackDepthBytes, param, priority, outHandle,
				outAlloc, coreId,
				MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, false, true);
}
