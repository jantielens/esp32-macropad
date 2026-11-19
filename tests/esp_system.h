#pragma once

#include <stdint.h>

uint32_t esp_random();
void esp_restart();
enum esp_reset_reason_t { ESP_RST_UNKNOWN, ESP_RST_POWERON, ESP_RST_SW };
esp_reset_reason_t esp_reset_reason();
