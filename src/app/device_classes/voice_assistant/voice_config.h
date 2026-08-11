#pragma once

#include "board_config.h"

#if IS_VOICE_ASSISTANT

#include <Preferences.h>

#define VOICE_CONFIG_HOST_MAX_LEN 128
#define VOICE_CONFIG_MODEL_MAX_LEN 64
#define VOICE_CONFIG_API_KEY_MAX_LEN 128
#define VOICE_CONFIG_LANGUAGE_MAX_LEN 3
#define VOICE_CONFIG_TTS_VOICE_MAX_LEN 8
#define VOICE_CONFIG_TTS_INSTRUCTIONS_MAX_LEN 161

struct VoiceConfig {
    char azure_host[VOICE_CONFIG_HOST_MAX_LEN];
    char azure_deployment[VOICE_CONFIG_MODEL_MAX_LEN];
    char azure_api_key[VOICE_CONFIG_API_KEY_MAX_LEN];
    char azure_language[VOICE_CONFIG_LANGUAGE_MAX_LEN];
    char tts_host[VOICE_CONFIG_HOST_MAX_LEN];
    char tts_deployment[VOICE_CONFIG_MODEL_MAX_LEN];
    char tts_api_key[VOICE_CONFIG_API_KEY_MAX_LEN];
    char tts_language[VOICE_CONFIG_LANGUAGE_MAX_LEN];
    char tts_voice[VOICE_CONFIG_TTS_VOICE_MAX_LEN];
    char tts_instructions[VOICE_CONFIG_TTS_INSTRUCTIONS_MAX_LEN];
};

void voice_config_defaults();
void voice_config_load(Preferences& prefs);
void voice_config_save(Preferences& prefs);
void voice_config_snapshot(VoiceConfig* out);
void voice_config_set_host(const char* host);
void voice_config_set_deployment(const char* deployment);
void voice_config_set_api_key(const char* api_key);
bool voice_config_set_language(const char* language);
void voice_config_set_tts_host(const char* host);
void voice_config_set_tts_deployment(const char* deployment);
void voice_config_set_tts_api_key(const char* api_key);
bool voice_config_set_tts_language(const char* language);
bool voice_config_set_tts_voice(const char* voice);
void voice_config_set_tts_instructions(const char* instructions);

#endif // IS_VOICE_ASSISTANT