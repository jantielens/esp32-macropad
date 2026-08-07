#pragma once

#include "board_config.h"

#if HAS_DISPLAY && HAS_MCP

#include "pad_config.h"
#include <ArduinoJson.h>
#include <stddef.h>

bool timer_mcp_parse_args(JsonObjectConst args, TimerPayload* payload,
                          char* error, size_t error_len);

#endif
