#ifndef PORTAL_IDLE_H
#define PORTAL_IDLE_H

#include <stdint.h>
#include "power_config.h"

void portal_idle_init();
void portal_idle_notify_activity();
void portal_idle_set_timeout_seconds(uint16_t seconds, uint32_t sleep_seconds);
void portal_idle_set_mode(PowerMode mode);
void portal_idle_set_config_upload_in_progress(bool in_progress);
// Main-loop only. Success reserves the transition through deep sleep.
bool portal_idle_request_sleep(uint32_t seconds, uint32_t grace_ms);
void portal_idle_loop();

#endif // PORTAL_IDLE_H
