#pragma once

#include "board_config.h"

#if HAS_AUTO_SHUTDOWN
#include <stdint.h>

void auto_shutdown_configure(uint16_t seconds);
void auto_shutdown_notify_activity();
void auto_shutdown_loop();
#endif
