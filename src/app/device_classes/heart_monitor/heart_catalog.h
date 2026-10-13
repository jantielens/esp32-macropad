#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

namespace heart_monitor {
bool catalog_metadata(JsonVariantConst report, JsonDocument& metadata);
using CatalogRead = int (*)(void* context);
bool catalog_read_metadata(CatalogRead read, void* context, const char* session_id,
						   JsonDocument& metadata, uint64_t& csv_bytes);
bool catalog_init();
bool catalog_prepare();
bool catalog_add(const char* session_id);
String catalog_list();
bool catalog_remove(const char* session_id);
void catalog_import_tick();
bool catalog_request_import();
}