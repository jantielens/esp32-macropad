#pragma once

// Shared ownership gate for firmware updates. Exactly one OTA path may hold
// the activity at a time; background workers use the query as a cooperative
// checkpoint before starting nonessential work.
bool ota_activity_try_begin();
void ota_activity_finish();
bool ota_activity_is_active();