#pragma once
#ifndef EPAPER_FRAME_WAKE_LOG_H
#define EPAPER_FRAME_WAKE_LOG_H

#include "board_config.h"

#if IS_EPAPER_FRAME

#include "epaper_frame_timing.h"

constexpr const char* EPAPER_FRAME_WAKE_LOG_PATH = "/epaper-wake-log.csv";

void epaper_frame_wake_log_append(const EpaperWakeRecord& record);
bool epaper_frame_wake_log_clear();

#endif // IS_EPAPER_FRAME

#endif // EPAPER_FRAME_WAKE_LOG_H