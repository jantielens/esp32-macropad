#pragma once

#include <stdint.h>

constexpr uint32_t STICKY_SHUTDOWN_MAX_DELAY_MS = 30000;

bool sticky_early_power_init();
bool sticky_hardware_ready();
bool sticky_request_shutdown(uint32_t continuation_token, const char* message = "", uint32_t delay_ms = 0);
#if HAS_AUTO_SHUTDOWN
enum DeviceNotificationState : uint8_t;
DeviceNotificationState sticky_prepare_shutdown_notice(uint32_t generation);
void sticky_cancel_shutdown_notice(uint32_t generation);
bool sticky_display_is_busy();
bool sticky_display_hold_for_shutdown(uint32_t request_sequence);
void sticky_display_release_shutdown();
#endif
void sticky_hardware_loop();

#if USE_SD_STORAGE
class SPIClass;
SPIClass& sticky_shared_spi();
bool sticky_prepare_sd();
void sticky_sd_mount_failed();
#endif
