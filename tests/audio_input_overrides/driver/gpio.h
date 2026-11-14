#pragma once

enum gpio_num_t { GPIO_NUM_19 = 19, GPIO_NUM_20 = 20, GPIO_NUM_38 = 38 };
using esp_err_t = int;
constexpr esp_err_t ESP_OK = 0;
constexpr esp_err_t ESP_ERR_TIMEOUT = 1;
constexpr esp_err_t ESP_ERR_INVALID_SIZE = 2;
inline int audio_test_power = 0;
inline bool audio_test_power_fails = false;
inline esp_err_t gpio_set_level(gpio_num_t pin, int value) {
    if (pin != GPIO_NUM_38 || audio_test_power_fails) return 3;
    audio_test_power = value;
    return ESP_OK;
}
inline const char* esp_err_to_name(esp_err_t) { return "test error"; }
