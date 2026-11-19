#pragma once

#include <stdint.h>

enum RebootOwnership : uint8_t {
    REBOOT_NORMAL,
    REBOOT_OTA_COMPLETE,
    REBOOT_NETWORK_TRANSITION
};

void reboot_request(uint32_t grace_ms, RebootOwnership ownership = REBOOT_NORMAL);
bool reboot_request_ota_mode(uint32_t grace_ms);
void reboot_service_boot_init();
bool reboot_is_ota_mode();
void reboot_service_loop();