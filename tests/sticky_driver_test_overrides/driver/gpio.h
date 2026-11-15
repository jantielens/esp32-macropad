#pragma once

using esp_err_t = int;
using gpio_num_t = int;
constexpr int ESP_OK = 0, ESP_ERR_INVALID_STATE = 1;
constexpr int GPIO_NUM_9 = 9, GPIO_MODE_INPUT = 0, GPIO_MODE_OUTPUT = 1;
constexpr int GPIO_PULLUP_ONLY = 1;
esp_err_t gpio_set_level(gpio_num_t pin, int level);
esp_err_t gpio_set_direction(gpio_num_t pin, int mode);
esp_err_t gpio_set_pull_mode(gpio_num_t pin, int mode);
