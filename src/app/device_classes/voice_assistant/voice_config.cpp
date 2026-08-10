#include "voice_config.h"

#if IS_VOICE_ASSISTANT

#include <ctype.h>
#include <freertos/FreeRTOS.h>
#include <string.h>

namespace {
constexpr const char* KEY_AZURE_HOST = "voice_host";
constexpr const char* KEY_AZURE_MODEL = "voice_model";
constexpr const char* KEY_AZURE_API_KEY = "voice_key";
constexpr const char* KEY_AZURE_LANGUAGE = "voice_lang";
} // namespace

namespace {
VoiceConfig voice_config = {};
portMUX_TYPE voice_config_mux = portMUX_INITIALIZER_UNLOCKED;
} // namespace

void voice_config_defaults() {
    portENTER_CRITICAL(&voice_config_mux);
    voice_config.azure_host[0] = '\0';
    voice_config.azure_deployment[0] = '\0';
    voice_config.azure_api_key[0] = '\0';
    voice_config.azure_language[0] = '\0';
    portEXIT_CRITICAL(&voice_config_mux);
}

void voice_config_load(Preferences& prefs) {
    VoiceConfig loaded = {};
    prefs.getString(KEY_AZURE_HOST, loaded.azure_host, sizeof(loaded.azure_host));
    prefs.getString(KEY_AZURE_MODEL, loaded.azure_deployment, sizeof(loaded.azure_deployment));
    prefs.getString(KEY_AZURE_API_KEY, loaded.azure_api_key, sizeof(loaded.azure_api_key));
    prefs.getString(KEY_AZURE_LANGUAGE, loaded.azure_language, sizeof(loaded.azure_language));
    portENTER_CRITICAL(&voice_config_mux);
    voice_config = loaded;
    portEXIT_CRITICAL(&voice_config_mux);
}

void voice_config_save(Preferences& prefs) {
    VoiceConfig saved = {};
    voice_config_snapshot(&saved);
    prefs.putString(KEY_AZURE_HOST, saved.azure_host);
    prefs.putString(KEY_AZURE_MODEL, saved.azure_deployment);
    prefs.putString(KEY_AZURE_API_KEY, saved.azure_api_key);
    prefs.putString(KEY_AZURE_LANGUAGE, saved.azure_language);
}

void voice_config_snapshot(VoiceConfig* out) {
    if (!out) return;
    portENTER_CRITICAL(&voice_config_mux);
    *out = voice_config;
    portEXIT_CRITICAL(&voice_config_mux);
}

void voice_config_set_host(const char* host) {
    portENTER_CRITICAL(&voice_config_mux);
    strlcpy(voice_config.azure_host, host ? host : "", sizeof(voice_config.azure_host));
    portEXIT_CRITICAL(&voice_config_mux);
}

void voice_config_set_deployment(const char* deployment) {
    portENTER_CRITICAL(&voice_config_mux);
    strlcpy(voice_config.azure_deployment, deployment ? deployment : "", sizeof(voice_config.azure_deployment));
    portEXIT_CRITICAL(&voice_config_mux);
}

void voice_config_set_api_key(const char* api_key) {
    if (!api_key || !api_key[0]) return;
    portENTER_CRITICAL(&voice_config_mux);
    strlcpy(voice_config.azure_api_key, api_key, sizeof(voice_config.azure_api_key));
    portEXIT_CRITICAL(&voice_config_mux);
}

bool voice_config_set_language(const char* language) {
    if (!language) language = "";
    const size_t language_len = strlen(language);
    if (language_len != 0 && (language_len != 2 || !isalpha((unsigned char)language[0]) ||
                              !isalpha((unsigned char)language[1]))) {
        return false;
    }
    portENTER_CRITICAL(&voice_config_mux);
    voice_config.azure_language[0] = language_len ? tolower((unsigned char)language[0]) : '\0';
    voice_config.azure_language[1] = language_len ? tolower((unsigned char)language[1]) : '\0';
    voice_config.azure_language[2] = '\0';
    portEXIT_CRITICAL(&voice_config_mux);
    return true;
}

#endif // IS_VOICE_ASSISTANT