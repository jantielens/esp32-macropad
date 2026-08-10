(function () {
    if (typeof _actionEditorExtensions === 'undefined') return;

    _actionEditorExtensions.push({
        groups: function (prefix) {
            return '<div id="' + prefix + '-voice-group" style="display:none">' +
                '<label class="form-label" for="' + prefix + '-voice-command">Voice command</label>' +
                '<select class="form-select form-select-sm" id="' + prefix + '-voice-command">' +
                '<option value="record_start">Start recording</option>' +
                '<option value="record_stop_transcribe">Stop and transcribe</option>' +
                '</select></div>';
        },
        typeChanged: function (prefix, type) {
            var group = document.getElementById(prefix + '-voice-group');
            if (group) group.style.display = type === 'voice' ? '' : 'none';
        },
        load: function (prefix, action) {
            if (action.type !== 'voice') return;
            var command = document.getElementById(prefix + '-voice-command');
            if (command) command.value = action.command || 'record_start';
        },
        build: function (prefix, type) {
            if (type !== 'voice') return null;
            var command = document.getElementById(prefix + '-voice-command');
            return { command: command ? command.value : 'record_start' };
        }
    });
})();