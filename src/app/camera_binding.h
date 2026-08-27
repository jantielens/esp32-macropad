#pragma once

#include <stdint.h>

void camera_binding_init();
uint8_t camera_binding_key_count();
const char* camera_binding_key_at(uint8_t index);
