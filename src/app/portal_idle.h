#ifndef PORTAL_IDLE_H
#define PORTAL_IDLE_H

#include <stdint.h>
#include "power_config.h"

void portal_idle_init();
void portal_idle_notify_activity();
void portal_idle_set_timeout_seconds(uint16_t seconds);
void portal_idle_set_mode(PowerMode mode);
void portal_idle_set_config_upload_in_progress(bool in_progress);
void portal_idle_loop();

#endif // PORTAL_IDLE_H
