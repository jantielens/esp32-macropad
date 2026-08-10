if (typeof window.registerConfigFields === 'function') {
    window.registerConfigFields(['voice_azure_host', 'voice_azure_model', 'voice_azure_language', 'voice_azure_api_key']);
}

if (typeof bindingRegisterScheme === 'function') {
    bindingRegisterScheme('stt', {
        firstParamRequired: true,
        firstParamLabel: 'Speech-to-text key',
        keysLabel: 'speech-to-text key',
        keys: ['status', 'text']
    });
}

window.init_voice_fragment = function () {
    initConfigFragment('voice-save-btn', false);
    fetch('/api/config').then(function (response) { return response.json(); }).then(function (config) {
        var status = document.getElementById('voice-credentials-status');
        if (status) {
            var apiKeyField = document.getElementById('voice_azure_api_key');
            if (apiKeyField) {
                apiKeyField.value = '';
                apiKeyField.placeholder = config.voice_api_key_configured
                    ? '(saved - leave blank to keep)' : 'Enter Azure API key';
            }
            status.textContent = config.voice_api_key_configured
                ? 'Azure API key is configured.'
                : 'Azure API key is not configured.';
        }
    }).catch(function () {});
};