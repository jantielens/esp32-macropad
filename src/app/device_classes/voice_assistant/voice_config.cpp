#include "voice_config.h"
#include "voice_tts_request.h"

#if IS_VOICE_ASSISTANT

#include <ctype.h>
#include <freertos/FreeRTOS.h>
#include <string.h>

namespace {
constexpr const char* KEY_AZURE_HOST = "voice_host";
constexpr const char* KEY_AZURE_MODEL = "voice_model";
constexpr const char* KEY_AZURE_API_KEY = "voice_key";
constexpr const char* KEY_AZURE_LANGUAGE = "voice_lang";
constexpr const char* KEY_TTS_HOST = "tts_host";
constexpr const char* KEY_TTS_MODEL = "tts_model";
constexpr const char* KEY_TTS_API_KEY = "tts_key";
constexpr const char* KEY_TTS_LANGUAGE = "tts_lang";
constexpr const char* KEY_TTS_VOICE = "tts_voice";
constexpr const char* KEY_TTS_INSTRUCTIONS = "tts_instr";
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
    voice_config.tts_host[0] = '\0';
    voice_config.tts_deployment[0] = '\0';
    voice_config.tts_api_key[0] = '\0';
    voice_config.tts_language[0] = '\0';
    strlcpy(voice_config.tts_voice, "alloy", sizeof(voice_config.tts_voice));
    voice_config.tts_instructions[0] = '\0';
    portEXIT_CRITICAL(&voice_config_mux);
}

void voice_config_load(Preferences& prefs) {
    VoiceConfig loaded = {};
    prefs.getString(KEY_AZURE_HOST, loaded.azure_host, sizeof(loaded.azure_host));
    prefs.getString(KEY_AZURE_MODEL, loaded.azure_deployment, sizeof(loaded.azure_deployment));
    prefs.getString(KEY_AZURE_API_KEY, loaded.azure_api_key, sizeof(loaded.azure_api_key));
    prefs.getString(KEY_AZURE_LANGUAGE, loaded.azure_language, sizeof(loaded.azure_language));
    prefs.getString(KEY_TTS_HOST, loaded.tts_host, sizeof(loaded.tts_host));
    prefs.getString(KEY_TTS_MODEL, loaded.tts_deployment, sizeof(loaded.tts_deployment));
    prefs.getString(KEY_TTS_API_KEY, loaded.tts_api_key, sizeof(loaded.tts_api_key));
    prefs.getString(KEY_TTS_LANGUAGE, loaded.tts_language, sizeof(loaded.tts_language));
    prefs.getString(KEY_TTS_VOICE, loaded.tts_voice, sizeof(loaded.tts_voice));
    prefs.getString(KEY_TTS_INSTRUCTIONS, loaded.tts_instructions, sizeof(loaded.tts_instructions));
    if (!loaded.tts_voice[0]) strlcpy(loaded.tts_voice, "alloy", sizeof(loaded.tts_voice));
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
    prefs.putString(KEY_TTS_HOST, saved.tts_host);
    prefs.putString(KEY_TTS_MODEL, saved.tts_deployment);
    prefs.putString(KEY_TTS_API_KEY, saved.tts_api_key);
    prefs.putString(KEY_TTS_LANGUAGE, saved.tts_language);
    prefs.putString(KEY_TTS_VOICE, saved.tts_voice);
    prefs.putString(KEY_TTS_INSTRUCTIONS, saved.tts_instructions);
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

void voice_config_set_tts_host(const char* host) {
    portENTER_CRITICAL(&voice_config_mux);
    strlcpy(voice_config.tts_host, host ? host : "", sizeof(voice_config.tts_host));
    portEXIT_CRITICAL(&voice_config_mux);
}

void voice_config_set_tts_deployment(const char* deployment) {
    portENTER_CRITICAL(&voice_config_mux);
    strlcpy(voice_config.tts_deployment, deployment ? deployment : "", sizeof(voice_config.tts_deployment));
    portEXIT_CRITICAL(&voice_config_mux);
}

void voice_config_set_tts_api_key(const char* api_key) {
    if (!api_key || !api_key[0]) return;
    portENTER_CRITICAL(&voice_config_mux);
    strlcpy(voice_config.tts_api_key, api_key, sizeof(voice_config.tts_api_key));
    portEXIT_CRITICAL(&voice_config_mux);
}

bool voice_config_set_tts_language(const char* language) {
    if (!voice_tts_language_valid(language)) return false;
    portENTER_CRITICAL(&voice_config_mux);
    voice_config.tts_language[0] = language && language[0] ? tolower((unsigned char)language[0]) : '\0';
    voice_config.tts_language[1] = language && language[1] ? tolower((unsigned char)language[1]) : '\0';
    voice_config.tts_language[2] = '\0';
    portEXIT_CRITICAL(&voice_config_mux);
    return true;
}

bool voice_config_set_tts_voice(const char* voice) {
    if (!voice || !voice[0] || strlen(voice) >= VOICE_CONFIG_TTS_VOICE_MAX_LEN) return false;
    portENTER_CRITICAL(&voice_config_mux);
    strlcpy(voice_config.tts_voice, voice, sizeof(voice_config.tts_voice));
    portEXIT_CRITICAL(&voice_config_mux);
    return true;
}

void voice_config_set_tts_instructions(const char* instructions) {
    portENTER_CRITICAL(&voice_config_mux);
    strlcpy(voice_config.tts_instructions, instructions ? instructions : "",
            sizeof(voice_config.tts_instructions));
    portEXIT_CRITICAL(&voice_config_mux);
}

#endif // IS_VOICE_ASSISTANT