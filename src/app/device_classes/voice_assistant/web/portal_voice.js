if (typeof window.registerConfigFields === 'function') {
    window.registerConfigFields(['voice_azure_host', 'voice_azure_model', 'voice_azure_language', 'voice_azure_api_key',
        'voice_tts_host', 'voice_tts_deployment', 'voice_tts_language', 'voice_tts_voice', 'voice_tts_instructions', 'voice_tts_api_key']);
}

if (typeof bindingRegisterScheme === 'function') {
    bindingRegisterScheme('stt', {
        firstParamRequired: true,
        firstParamLabel: 'Speech-to-text key',
        keysLabel: 'speech-to-text key',
        keys: ['status', 'text']
    });
}

function updateVoiceCredentialStatus(config, statusId, apiKeyFieldId, configuredField) {
    updateWriteOnlySecretField(apiKeyFieldId, statusId, config[configuredField] === true,
        '', 'API key');
}

window.init_voice_fragment = function () {
    initConfigFragment('voice-save-btn', false);
    initConfigFragment('voice-tts-save-btn', false);
    fetch('/api/config').then(function (response) { return response.json(); }).then(function (config) {
        updateVoiceCredentialStatus(config, 'voice-credentials-status', 'voice_azure_api_key',
            'voice_api_key_configured');
        updateVoiceCredentialStatus(config, 'voice-tts-credentials-status', 'voice_tts_api_key',
            'voice_tts_api_key_configured');
    }).catch(function () {});
};