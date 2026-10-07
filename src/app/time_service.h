#pragma once
#include <stddef.h>
#include <stdint.h>
#include <time.h>

bool time_service_set_timezone(const char* timezone);
bool time_service_timezone_valid(const char* timezone);
uint32_t time_service_generation();
struct timeval;
void time_service_start_ntp(void (*observer)(struct timeval*) = nullptr);
void time_service_loop();
bool time_service_ready();
bool time_service_localtime(time_t epoch, struct tm* result);
bool time_service_format(time_t epoch, const char* format, const char* timezone,
                         char* out, size_t capacity);
time_t time_service_alarm_candidate(time_t now, uint8_t hour, uint8_t minute,
                                    uint8_t weekdays);