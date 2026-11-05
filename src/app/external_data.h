#pragma once

#include "external_data_config.h"

#if HAS_EXTERNAL_DATA
#include <ArduinoJson.h>
#include <stddef.h>

bool external_data_parse_config(JsonVariantConst input, ExternalDataConfig& candidate);
bool external_data_config_valid(const ExternalDataConfig& config);
void external_data_init(const ExternalDataConfig& config);
void external_data_configure(const ExternalDataConfig& config);
bool external_data_get_value(const char* name, const char* path, char* out, size_t out_len);
bool external_data_get_stat(const char* name, const char* key, char* out, size_t out_len);
void external_data_write_status(JsonArray output, const ExternalDataConfig& config);
#endif
