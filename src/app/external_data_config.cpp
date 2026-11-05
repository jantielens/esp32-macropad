#include "external_data.h"

#if HAS_EXTERNAL_DATA
#include <string.h>

static bool valid_name(const char* name) {
    return name && external_data_valid_name(name, strlen(name));
}

bool external_data_split_url(const char* url, ExternalDataUrl& parts) {
    if (!url) return false;
    const size_t length = strlen(url);
    if (!length || length >= EXTERNAL_DATA_URL_BYTES) return false;
    const char* authority = nullptr;
    parts = {};
    if (strncmp(url, "http://", 7) == 0) {
        authority = url + 7;
        parts.port = 80;
    } else if (strncmp(url, "https://", 8) == 0) {
        authority = url + 8;
        parts.https = true;
        parts.port = 443;
    }
    else return false;
    const size_t authority_length = strcspn(authority, "/?");
    if (!authority_length) return false;
    for (const char* p = url; *p; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c <= 0x20 || c >= 0x7f || c == '#' || c == '\\' || c == '"') return false;
    }
    for (size_t i = 0; i < authority_length; ++i) {
        if (authority[i] == '@') return false;
    }
    const char* end = authority + authority_length;
    const char* host = authority;
    const char* host_end = end;
    const char* port = nullptr;
    if (*host == '[') {
        parts.ipv6 = true;
        ++host;
        host_end = static_cast<const char*>(memchr(host, ']', end - host));
        if (!host_end || host_end == host) return false;
        if (host_end + 1 < end) {
            if (host_end[1] != ':') return false;
            port = host_end + 2;
        }
    } else {
        const char* colon = static_cast<const char*>(memchr(host, ':', end - host));
        if (colon) {
            host_end = colon;
            port = colon + 1;
        }
    }
    const size_t host_length = host_end - host;
    if (!host_length || host_length >= sizeof(parts.host)) return false;
    for (const char* p = host; p < host_end; ++p) {
        const char c = *p;
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '.' || c == '-' ||
              (parts.ipv6 && c == ':'))) return false;
    }
    memcpy(parts.host, host, host_length);
    parts.host[host_length] = '\0';
    if (port) {
        unsigned value = 0;
        if (port == end) return false;
        for (const char* p = port; p < end; ++p) {
            if (*p < '0' || *p > '9') return false;
            value = value * 10 + (*p - '0');
            if (value > 65535) return false;
        }
        if (!value) return false;
        parts.port = static_cast<uint16_t>(value);
    }
    if (!*end) memcpy(parts.target, "/", 2);
    else if (*end == '?') {
        parts.target[0] = '/';
        memcpy(parts.target + 1, end, strlen(end) + 1);
    } else {
        memcpy(parts.target, end, strlen(end) + 1);
    }
    return true;
}

static bool valid_url(const char* url) {
    ExternalDataUrl parts;
    return external_data_split_url(url, parts);
}

bool external_data_config_valid(const ExternalDataConfig& config) {
    if (config.count > EXTERNAL_DATA_MAX_SOURCES) return false;
    for (uint32_t i = 0; i < config.count; ++i) {
        const auto& source = config.sources[i];
        if (!memchr(source.name, '\0', sizeof(source.name)) ||
            !memchr(source.url, '\0', sizeof(source.url)) ||
            !valid_name(source.name) || !valid_url(source.url) ||
            source.refresh_seconds < 60 || source.refresh_seconds > 86400) return false;
        for (uint32_t j = 0; j < i; ++j)
            if (strcmp(source.name, config.sources[j].name) == 0) return false;
    }
    return true;
}

bool external_data_parse_config(JsonVariantConst input, ExternalDataConfig& candidate) {
    if (!input.is<JsonArrayConst>()) return false;
    JsonArrayConst array = input.as<JsonArrayConst>();
    if (array.size() > EXTERNAL_DATA_MAX_SOURCES) return false;
    candidate = {};
    for (JsonVariantConst item : array) {
        if (!item.is<JsonObjectConst>() || !item["name"].is<const char*>()) return false;
        const char* name = item["name"].as<const char*>();
        if (!valid_name(name)) return false;
        auto& next = candidate.sources[candidate.count++];
        memcpy(next.name, name, strlen(name) + 1);
        const JsonObjectConst object = item.as<JsonObjectConst>();
        const JsonVariantConst url_field = object["url"];
        if (!url_field.is<const char*>()) return false;
        const char* url = url_field.as<const char*>();
        if (!valid_url(url)) return false;
        memcpy(next.url, url, strlen(url) + 1);
        const JsonVariantConst interval = item["refresh_seconds"];
        if (!object.containsKey("refresh_seconds")) next.refresh_seconds = EXTERNAL_DATA_DEFAULT_INTERVAL;
        else if (interval.is<uint32_t>()) next.refresh_seconds = interval.as<uint32_t>();
        else return false;
    }
    return external_data_config_valid(candidate);
}
#endif
