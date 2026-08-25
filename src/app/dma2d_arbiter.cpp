#include "dma2d_arbiter.h"

#include <sdkconfig.h>

#ifdef CONFIG_IDF_TARGET_ESP32P4

#include <esp_attr.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

static StaticSemaphore_t s_token_storage;
static SemaphoreHandle_t s_token = nullptr;

void dma2d_arbiter_init() {
    if (s_token) return;
    s_token = xSemaphoreCreateBinaryStatic(&s_token_storage);
    xSemaphoreGive(s_token);
}

bool dma2d_arbiter_acquire(uint32_t timeout_ms) {
    if (!s_token) return true;
    return xSemaphoreTake(s_token, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

void dma2d_arbiter_release() {
    if (s_token) xSemaphoreGive(s_token);
}

void IRAM_ATTR dma2d_arbiter_release_from_isr() {
    if (!s_token) return;
    BaseType_t higher_priority_task_woken = pdFALSE;
    xSemaphoreGiveFromISR(s_token, &higher_priority_task_woken);
    if (higher_priority_task_woken) portYIELD_FROM_ISR();
}

#else

void dma2d_arbiter_init() {}
bool dma2d_arbiter_acquire(uint32_t) { return true; }
void dma2d_arbiter_release() {}
void dma2d_arbiter_release_from_isr() {}

#endif
