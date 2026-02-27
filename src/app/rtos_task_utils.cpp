#include "rtos_task_utils.h"

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
static bool create_task_psram_impl(
		TaskFunction_t taskFunction,
		const char* name,
		uint32_t stackDepthWords,
		void* param,
		UBaseType_t priority,
		TaskHandle_t* outHandle,
		RtosTaskPsramAlloc* outAlloc,
		BaseType_t coreId   // tskNO_AFFINITY = no pinning
) {
		if (!taskFunction || !name || stackDepthWords == 0 || !outHandle) {
				return false;
		}

		*outHandle = nullptr;

		if (!psram_available()) {
				return false;
		}

		StackType_t* stack = static_cast<StackType_t*>(
				heap_caps_malloc(stackDepthWords * sizeof(StackType_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
		);

		if (stack == nullptr) {
				return false;
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
				handle = xTaskCreateStaticPinnedToCore(taskFunction, name, stackDepthWords, param, priority, stack, tcb, coreId);
		} else
#endif
		{
				handle = xTaskCreateStatic(taskFunction, name, stackDepthWords, param, priority, stack, tcb);
		}

		if (handle == nullptr) {
				heap_caps_free(tcb);
				heap_caps_free(stack);
				return false;
		}

		if (outAlloc) {
				outAlloc->tcb = tcb;
				outAlloc->stack = stack;
				outAlloc->stackDepthWords = stackDepthWords;
		}

		*outHandle = handle;
		return true;
}

bool rtos_create_task_psram_stack(
		TaskFunction_t taskFunction,
		const char* name,
		uint32_t stackDepthWords,
		void* param,
		UBaseType_t priority,
		TaskHandle_t* outHandle,
		RtosTaskPsramAlloc* outAlloc
) {
		return create_task_psram_impl(taskFunction, name, stackDepthWords, param, priority, outHandle, outAlloc, tskNO_AFFINITY);
}

bool rtos_create_task_psram_stack_pinned(
		TaskFunction_t taskFunction,
		const char* name,
		uint32_t stackDepthWords,
		void* param,
		UBaseType_t priority,
		TaskHandle_t* outHandle,
		RtosTaskPsramAlloc* outAlloc,
		BaseType_t coreId
) {
		return create_task_psram_impl(taskFunction, name, stackDepthWords, param, priority, outHandle, outAlloc, coreId);
}
