#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

constexpr size_t STORAGE_BROWSER_LIST_MAX_ENTRIES = 128;
constexpr size_t STORAGE_BROWSER_PATH_MAX_LEN = 192;

bool storage_browser_path_is_safe(const String& path);
const char* storage_browser_file_content_type(const String& path);
void storage_browser_status_to_json(JsonObject result);
bool storage_browser_list(const String& path, JsonObject result, const char*& error);