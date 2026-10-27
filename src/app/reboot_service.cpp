#include "reboot_service.h"
#include "audio.h"
#include "log_manager.h"
#include "ota_activity.h"
#include <Arduino.h>
#include <esp_system.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>

static portMUX_TYPE reboot_mux = portMUX_INITIALIZER_UNLOCKED;
static bool reboot_pending = false;
static bool reboot_ota_complete = false;
static bool reboot_owns_transition = false;
static uint32_t reboot_not_before = 0;
static bool reboot_shutdown_started = false;
static uint32_t reboot_shutdown_at = 0;
static constexpr uint32_t REBOOT_AUDIO_TIMEOUT_MS = 1000;

void reboot_request(uint32_t grace_ms, RebootOwnership ownership) {
    const uint32_t not_before = millis() + grace_ms;
    portENTER_CRITICAL(&reboot_mux);
    const bool new_handoff =
        (ownership == REBOOT_OTA_COMPLETE && !reboot_ota_complete) ||
        (ownership == REBOOT_NETWORK_TRANSITION && !reboot_owns_transition);
    if (!reboot_pending || (new_handoff && (int32_t)(not_before - reboot_not_before) > 0))
        reboot_not_before = not_before;
    reboot_pending = true;
    if (ownership == REBOOT_OTA_COMPLETE) reboot_ota_complete = true;
    if (ownership == REBOOT_NETWORK_TRANSITION) reboot_owns_transition = true;
    portEXIT_CRITICAL(&reboot_mux);
}

void reboot_service_loop() {
    portENTER_CRITICAL(&reboot_mux);
    const bool pending = reboot_pending;
    const bool ota_complete = reboot_ota_complete;
    const bool owns_transition = reboot_owns_transition;
    portEXIT_CRITICAL(&reboot_mux);
    if (!pending) return;

    if (!reboot_shutdown_started) {
        if (!ota_complete && !owns_transition && !ota_activity_try_begin_network_transition()) return;
        reboot_shutdown_started = true;
        reboot_shutdown_at = millis();
#if HAS_AUDIO
        audio_request_reboot();
#endif
    }

#if HAS_AUDIO
    const AudioRebootStatus status = audio_reboot_status();
    const bool timed_out = status == AUDIO_REBOOT_PENDING &&
        (uint32_t)(millis() - reboot_shutdown_at) >= REBOOT_AUDIO_TIMEOUT_MS;
    if (status == AUDIO_REBOOT_PENDING && !timed_out) return;
#endif
    portENTER_CRITICAL(&reboot_mux);
    const bool grace_elapsed = (int32_t)(millis() - reboot_not_before) >= 0;
    portEXIT_CRITICAL(&reboot_mux);
    if (!grace_elapsed) return;
#if HAS_AUDIO
    if (timed_out) LOGW("Reboot", "Audio shutdown timed out");
    else if (status == AUDIO_REBOOT_ERROR) LOGW("Reboot", "Audio shutdown failed");
#endif
    LOGI("Reboot", "Restarting device");
    esp_restart();
}