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

function updateVoiceCredentialStatus(config, statusId, apiKeyFieldId, configuredField, configuredText) {
    var status = document.getElementById(statusId);
    if (!status) return;
    var apiKeyField = document.getElementById(apiKeyFieldId);
    if (apiKeyField) {
        apiKeyField.value = '';
        apiKeyField.placeholder = config[configuredField]
            ? '(saved - leave blank to keep)' : 'Enter Azure API key';
    }
    status.textContent = config[configuredField]
        ? configuredText + ' is configured.'
        : configuredText + ' is not configured.';
}

window.init_voice_fragment = function () {
    initConfigFragment('voice-save-btn', false);
    initConfigFragment('voice-tts-save-btn', false);
    fetch('/api/config').then(function (response) { return response.json(); }).then(function (config) {
        updateVoiceCredentialStatus(config, 'voice-credentials-status', 'voice_azure_api_key',
            'voice_api_key_configured', 'Azure API key');
        updateVoiceCredentialStatus(config, 'voice-tts-credentials-status', 'voice_tts_api_key',
            'voice_tts_api_key_configured', 'Azure Text-to-Speech API key');
    }).catch(function () {});
};