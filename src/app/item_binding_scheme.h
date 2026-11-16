#pragma once

#include "board_config.h"

#if HAS_DISPLAY && HAS_PSRAM
#include "binding_template.h"

bool item_binding_field_valid(const char* value, size_t length);
BindingResolverStatus item_binding_resolve(const char* params, char* out, size_t out_len);
void item_binding_init();
#endif
