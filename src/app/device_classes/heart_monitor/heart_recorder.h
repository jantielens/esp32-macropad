#pragma once

#include "board_config.h"
#include "heart_capture.h"

#if IS_HEART_MONITOR
void heart_recorder_init();
bool heart_recorder_command(heart_monitor::Command command);
heart_monitor::Snapshot heart_recorder_snapshot();
void heart_binding_init();
#endif