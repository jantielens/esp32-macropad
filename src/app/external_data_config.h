#pragma once

#include "board_config.h"

#if HAS_EXTERNAL_DATA
#include <stddef.h>
#include <stdint.h>

constexpr unsigned EXTERNAL_DATA_MAX_SOURCES = 4;
constexpr unsigned EXTERNAL_DATA_NAME_BYTES = 33;
constexpr unsigned EXTERNAL_DATA_URL_BYTES = 512;
constexpr uint32_t EXTERNAL_DATA_DEFAULT_INTERVAL = 1800;

inline bool external_data_valid_name(const char* name, size_t length) {
    if (!name || !length || length >= EXTERNAL_DATA_NAME_BYTES) return false;
    for (size_t i = 0; i < length; ++i) {
        const char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) return false;
    }
    return true;
}

struct ExternalDataSourceConfig {
    char name[EXTERNAL_DATA_NAME_BYTES];
    char url[EXTERNAL_DATA_URL_BYTES];
    uint32_t refresh_seconds;
};

struct ExternalDataConfig {
    uint32_t count;
    ExternalDataSourceConfig sources[EXTERNAL_DATA_MAX_SOURCES];
};

struct ExternalDataSourceUpdate {
    ExternalDataSourceConfig source;
    bool has_url;
    bool has_refresh_seconds;
};

struct ExternalDataUrl {
    char host[256];
    char target[EXTERNAL_DATA_URL_BYTES + 1];
    uint16_t port;
    bool https;
    bool ipv6;
};

bool external_data_split_url(const char* url, ExternalDataUrl& parts);
#endif
