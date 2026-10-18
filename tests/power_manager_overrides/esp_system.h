#pragma once

#define RTC_DATA_ATTR
enum esp_reset_reason_t { ESP_RST_POWERON, ESP_RST_SW, ESP_RST_DEEPSLEEP, ESP_RST_PANIC };
inline esp_reset_reason_t test_power_reset_reason = ESP_RST_POWERON;
inline esp_reset_reason_t esp_reset_reason() { return test_power_reset_reason; }
