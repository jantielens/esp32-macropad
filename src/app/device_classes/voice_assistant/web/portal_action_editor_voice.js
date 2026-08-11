var VOICE_TTS_TEXT_MAX_CHARS = 59;

function voiceActionUpdateCommandFields(prefix) {
    var command = document.getElementById(prefix + '-voice-command');
    var fields = document.getElementById(prefix + '-voice-auto-stop-fields');
    if (fields) fields.style.display = command && command.value === 'record_until_silence' ? '' : 'none';
    var ttsFields = document.getElementById(prefix + '-voice-tts-fields');
    if (ttsFields) ttsFields.style.display = command && command.value === 'speak' ? '' : 'none';
}

(function () {
    if (typeof _actionEditorExtensions === 'undefined') return;

    _actionEditorExtensions.push({
        groups: function (prefix) {
            return '<div id="' + prefix + '-voice-group" style="display:none">' +
                '<label class="form-label" for="' + prefix + '-voice-command">Voice command</label>' +
                '<select class="form-select form-select-sm" id="' + prefix + '-voice-command" onchange="voiceActionUpdateCommandFields(\'' + prefix + '\')">' +
                '<option value="record_start">Start recording</option>' +
                '<option value="record_stop_transcribe">Stop and transcribe</option>' +
                '<option value="record_until_silence">Record until silence</option>' +
                '<option value="record_cancel">Cancel recording</option>' +
                '<option value="speak">Speak text</option>' +
                '</select></div>' +
                '<div id="' + prefix + '-voice-auto-stop-fields" style="display:none">' +
                '<label class="form-label" for="' + prefix + '-voice-silence-ms">Trailing silence (ms)</label>' +
                '<input type="number" class="form-control form-control-sm" id="' + prefix + '-voice-silence-ms" min="100" max="10000" value="1000">' +
                '<div class="form-text">Stop after this much silence once speech has been detected.</div>' +
                '<label class="form-label mt-2" for="' + prefix + '-voice-speech-threshold">Speech level threshold</label>' +
                '<input type="number" class="form-control form-control-sm" id="' + prefix + '-voice-speech-threshold" min="0" max="100" value="2">' +
                '<div class="form-text">Uses the same 0-100 RMS scale as <code>[audio:input.rms]</code>.</div></div>' +
                '<div id="' + prefix + '-voice-tts-fields" style="display:none">' +
                '<label class="form-label mt-2" for="' + prefix + '-voice-text">Text <span class="fx-hint" onclick="showBindingHelp()">fx</span></label>' +
                '<input type="text" class="form-control form-control-sm" id="' + prefix + '-voice-text" maxlength="' + VOICE_TTS_TEXT_MAX_CHARS + '" placeholder="Text to speak or [binding]">' +
                '<div class="form-text">Supports binding templates. Maximum ' + VOICE_TTS_TEXT_MAX_CHARS + ' bytes after binding resolution.</div>' +
                '<div class="grid-2col mt-2"><div>' +
                '<label class="form-label" for="' + prefix + '-voice-tts-voice">Azure voice override</label>' +
                '<input type="text" class="form-control form-control-sm" id="' + prefix + '-voice-tts-voice" maxlength="7" placeholder="alloy, nova, shimmer...">' +
                '<div class="form-text">Voice name only; configure speech language and instructions in device Text-to-Speech settings.</div>' +
                '</div><div><label class="form-label" for="' + prefix + '-voice-volume">Volume override (%)</label>' +
                '<input type="number" class="form-control form-control-sm" id="' + prefix + '-voice-volume" min="0" max="100" placeholder="Use device volume">' +
                '</div></div></div>';
        },
        typeChanged: function (prefix, type) {
            var group = document.getElementById(prefix + '-voice-group');
            if (group) group.style.display = type === 'voice' ? '' : 'none';
            voiceActionUpdateCommandFields(prefix);
        },
        load: function (prefix, action) {
            if (action.type !== 'voice') return;
            var command = document.getElementById(prefix + '-voice-command');
            if (command) command.value = action.command || 'record_start';
            var silence = document.getElementById(prefix + '-voice-silence-ms');
            if (silence) silence.value = action.silence_ms === undefined ? 1000 : action.silence_ms;
            var threshold = document.getElementById(prefix + '-voice-speech-threshold');
            if (threshold) threshold.value = action.speech_threshold === undefined ? 2 : action.speech_threshold;
            var text = document.getElementById(prefix + '-voice-text');
            if (text) text.value = action.text || '';
            var voice = document.getElementById(prefix + '-voice-tts-voice');
            if (voice) voice.value = action.voice || '';
            var volume = document.getElementById(prefix + '-voice-volume');
            if (volume) volume.value = action.volume === undefined ? '' : action.volume;
            voiceActionUpdateCommandFields(prefix);
        },
        build: function (prefix, type) {
            if (type !== 'voice') return null;
            var command = document.getElementById(prefix + '-voice-command');
            var action = { command: command ? command.value : 'record_start' };
            if (action.command === 'record_until_silence') {
                var silence = document.getElementById(prefix + '-voice-silence-ms');
                var threshold = document.getElementById(prefix + '-voice-speech-threshold');
                action.silence_ms = silence ? Number(silence.value) : 1000;
                action.speech_threshold = threshold ? Number(threshold.value) : 2;
            }
            if (action.command === 'speak') {
                var text = document.getElementById(prefix + '-voice-text');
                var voice = document.getElementById(prefix + '-voice-tts-voice');
                var volume = document.getElementById(prefix + '-voice-volume');
                if (text && text.value.trim()) action.text = text.value.trim();
                if (voice && voice.value.trim()) action.voice = voice.value.trim();
                if (volume && volume.value !== '') action.volume = Number(volume.value);
            }
            return action;
        }
    });
})();