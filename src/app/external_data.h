#pragma once

#include "external_data_config.h"

#if HAS_EXTERNAL_DATA
#include <ArduinoJson.h>
#include <stddef.h>

bool external_data_parse_config(JsonVariantConst input, ExternalDataConfig& candidate);
bool external_data_config_valid(const ExternalDataConfig& config);
#if HAS_MCP
bool external_data_parse_update(JsonObjectConst input, ExternalDataSourceUpdate& update);
bool external_data_apply_update(ExternalDataConfig& config, const ExternalDataSourceUpdate& update);
enum ExternalDataInspectResult {
    EXTERNAL_DATA_INSPECT_OK,
    EXTERNAL_DATA_INSPECT_BUSY,
    EXTERNAL_DATA_INSPECT_UNAVAILABLE,
    EXTERNAL_DATA_INSPECT_NOT_FOUND,
    EXTERNAL_DATA_INSPECT_BAD_PATH,
};
ExternalDataInspectResult external_data_inspect(const char* name, const char* path,
                                               size_t offset, JsonObject result);
#endif
void external_data_init(const ExternalDataConfig& config);
void external_data_configure(const ExternalDataConfig& config);
bool external_data_get_value(const char* name, const char* path, char* out, size_t out_len);
bool external_data_get_stat(const char* name, const char* key, char* out, size_t out_len);
void external_data_write_status(JsonArray output, const ExternalDataConfig& config,
                                bool include_url = true);
#endif
