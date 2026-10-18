#pragma once

#include <stdint.h>

// Shared ownership gate for firmware updates. Exactly one OTA path may hold
// the activity at a time; background workers use the query as a cooperative
// checkpoint before starting nonessential work.
bool ota_activity_try_begin();
void ota_activity_finish();
bool ota_activity_is_active();
uint32_t ota_activity_epoch();

// Config handlers may nest a save inside an admitted update. Each successful
// begin must be paired with finish; OTA and network teardown exclude all writers.
bool ota_activity_try_begin_config_write();
void ota_activity_finish_config_write();

// Hold through live radio teardown/startup, or through a scheduled reboot.
bool ota_activity_try_begin_network_transition();
void ota_activity_finish_network_transition();