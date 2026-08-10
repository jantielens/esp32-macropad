#include "board_config.h"

#if IS_VOICE_ASSISTANT

#include "device_class.h"
#include "power_config.h"
#include "voice_assistant/voice.h"
#include "voice_assistant/voice_binding.h"
#include "voice_assistant/voice_config.h"

#include "voice_assistant/voice_wav.cpp"
#include "voice_assistant/voice_config.cpp"
#include "voice_assistant/voice.cpp"
#include "voice_assistant/voice_binding.cpp"
#include "voice_assistant/voice_actions.cpp"

static void on_setup_late_hook(DeviceConfig*, PowerMode current_mode) {
    voice_binding_init();
    if (current_mode == PowerMode::Ap) return;
    voice_init();
}

static void config_defaults_hook(DeviceConfig*) {
    voice_config_defaults();
}

static void config_load_hook(DeviceConfig*, Preferences& prefs) {
    voice_config_load(prefs);
}

static void config_save_hook(const DeviceConfig*, Preferences& prefs) {
    voice_config_save(prefs);
}

static void config_api_get_hook(const DeviceConfig*, JsonObject& root) {
    VoiceConfig config = {};
    voice_config_snapshot(&config);
    root["voice_azure_host"] = config.azure_host;
    root["voice_azure_model"] = config.azure_deployment;
    root["voice_azure_language"] = config.azure_language;
    root["voice_azure_api_key"] = "";
    root["voice_api_key_configured"] = voice_api_key_configured();
}

static void config_api_set_hook(DeviceConfig*, JsonObject& body) {
    if (body.containsKey("voice_azure_host")) {
        voice_config_set_host(body["voice_azure_host"] | "");
    }
    if (body.containsKey("voice_azure_model")) {
        voice_config_set_deployment(body["voice_azure_model"] | "");
    }
    if (body.containsKey("voice_azure_language")) {
        voice_config_set_language(body["voice_azure_language"] | "");
    }
    if (body.containsKey("voice_azure_api_key")) {
        voice_config_set_api_key(body["voice_azure_api_key"]);
    }
}

static const DeviceClass kVoiceAssistantClass = {
    "voice_assistant",
    PowerMode::AlwaysOn,
    nullptr,
    on_setup_late_hook,
    nullptr,
    nullptr,
    nullptr,
    nullptr,
    config_defaults_hook,
    config_load_hook,
    config_save_hook,
    config_api_get_hook,
    config_api_set_hook,
    nullptr,
    nullptr,
};

void voice_assistant_device_class_register() {
    device_class_register(&kVoiceAssistantClass);
}

#endif // IS_VOICE_ASSISTANT