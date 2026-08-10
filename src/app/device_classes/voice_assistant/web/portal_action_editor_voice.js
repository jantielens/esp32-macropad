function voiceActionUpdateAutoStopFields(prefix) {
    var command = document.getElementById(prefix + '-voice-command');
    var fields = document.getElementById(prefix + '-voice-auto-stop-fields');
    if (fields) fields.style.display = command && command.value === 'record_until_silence' ? '' : 'none';
}

(function () {
    if (typeof _actionEditorExtensions === 'undefined') return;

    _actionEditorExtensions.push({
        groups: function (prefix) {
            return '<div id="' + prefix + '-voice-group" style="display:none">' +
                '<label class="form-label" for="' + prefix + '-voice-command">Voice command</label>' +
                '<select class="form-select form-select-sm" id="' + prefix + '-voice-command" onchange="voiceActionUpdateAutoStopFields(\'' + prefix + '\')">' +
                '<option value="record_start">Start recording</option>' +
                '<option value="record_stop_transcribe">Stop and transcribe</option>' +
                '<option value="record_until_silence">Record until silence</option>' +
                '<option value="record_cancel">Cancel recording</option>' +
                '</select></div>' +
                '<div id="' + prefix + '-voice-auto-stop-fields" style="display:none">' +
                '<label class="form-label" for="' + prefix + '-voice-silence-ms">Trailing silence (ms)</label>' +
                '<input type="number" class="form-control form-control-sm" id="' + prefix + '-voice-silence-ms" min="100" max="10000" value="1000">' +
                '<div class="form-text">Stop after this much silence once speech has been detected.</div>' +
                '<label class="form-label mt-2" for="' + prefix + '-voice-speech-threshold">Speech level threshold</label>' +
                '<input type="number" class="form-control form-control-sm" id="' + prefix + '-voice-speech-threshold" min="0" max="100" value="2">' +
                '<div class="form-text">Uses the same 0-100 RMS scale as <code>[audio:input.rms]</code>.</div></div>';
        },
        typeChanged: function (prefix, type) {
            var group = document.getElementById(prefix + '-voice-group');
            if (group) group.style.display = type === 'voice' ? '' : 'none';
            voiceActionUpdateAutoStopFields(prefix);
        },
        load: function (prefix, action) {
            if (action.type !== 'voice') return;
            var command = document.getElementById(prefix + '-voice-command');
            if (command) command.value = action.command || 'record_start';
            var silence = document.getElementById(prefix + '-voice-silence-ms');
            if (silence) silence.value = action.silence_ms === undefined ? 1000 : action.silence_ms;
            var threshold = document.getElementById(prefix + '-voice-speech-threshold');
            if (threshold) threshold.value = action.speech_threshold === undefined ? 2 : action.speech_threshold;
            voiceActionUpdateAutoStopFields(prefix);
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
            return action;
        }
    });
})();