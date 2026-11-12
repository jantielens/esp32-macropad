#pragma once

#include <stdint.h>

bool sticky_early_power_init();
bool sticky_hardware_ready();
bool sticky_request_shutdown(uint32_t continuation_token);
void sticky_hardware_loop();
