#pragma once

#include <stdint.h>

bool sticky_early_power_init();
bool sticky_hardware_ready();
bool sticky_request_shutdown(uint32_t continuation_token);
#if HAS_AUTO_SHUTDOWN
bool sticky_display_is_busy();
#endif
void sticky_hardware_loop();
