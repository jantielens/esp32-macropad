#pragma once
#include <sys/time.h>

void esp_sntp_set_time_sync_notification_cb(void (*callback)(struct timeval*));
void configTzTime(const char* timezone, const char* server1, const char* server2);
