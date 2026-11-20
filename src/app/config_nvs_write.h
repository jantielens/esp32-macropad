#pragma once

#include <Preferences.h>
#include <string.h>

inline bool config_nvs_put_string(Preferences& preferences, const char* key, const char* value) {
    const size_t size = strlen(value);
    if (size) return preferences.putString(key, value) == size;
    // Preferences returns zero for an empty string as well as a write failure.
    preferences.putString(key, value);
    char verify[1];
    return preferences.getString(key, verify, sizeof(verify)) == 1 && verify[0] == '\0';
}
