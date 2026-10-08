// Timer's Command selector is per-instance ("T1: Toggle", "T2: Start", ...);
// labels still come from the catalog's single 'timer' entry so the text
// shown for each command has one source regardless of which instance it's for.
function actionEditorTimerCommandOptionsHTML(instance) {
    var entry = actionEditorCatalogEntry('timer');
    if (!entry || !entry.commands) return '';
    return entry.commands.map(function(c) {
        return '<option value="' + instance + ':' + c.id + '">T' + instance + ': ' + c.label + '</option>';
    }).join('');
}

// Show/hide timer sub-fields based on the timer action dropdown.
function actionEditorTimerChanged(prefix) {
    var sel = document.getElementById(prefix + '-timer-action');
    if (!sel) return;
    var val = sel.value; // e.g. "1:toggle", "2:adjust"
    var parts = val.split(':');
    var cmd = parts[1] || '';
    var starts = cmd === 'start' || cmd === 'toggle';
    var mode = document.getElementById(prefix + '-timer-mode');
    var modeGrp = document.getElementById(prefix + '-timer-mode-group');
    var durationGrp = document.getElementById(prefix + '-timer-duration-group');
    var setGrp = document.getElementById(prefix + '-timer-set-group');
    var adjustGrp = document.getElementById(prefix + '-timer-adjust-group');
    if (modeGrp) modeGrp.style.display = starts ? '' : 'none';
    if (durationGrp) durationGrp.style.display = starts && mode && mode.value === 'down' ? '' : 'none';
    if (setGrp) setGrp.style.display = (cmd === 'set') ? '' : 'none';
    if (adjustGrp) adjustGrp.style.display = (cmd === 'adjust') ? '' : 'none';
}

_actionEditorExtensions.push({
    type: 'timer',
    bindingSuffixes: ["-timer-duration","-timer-set-sec","-timer-adjust-sec"],
    groups: function(prefix, opts) {
        var h = '';
        // Timer — structured dropdowns
        h += '<div id="' + prefix + '-timer-group" style="display:none;">';
        h += '<div class="form-group">';
        h += '<label class="form-label" for="' + prefix + '-timer-action">Command</label>';
        h += '<select class="form-select form-select-sm" id="' + prefix + '-timer-action" onchange="actionEditorTimerChanged(\'' + prefix + '\')">';
        for (var t = 1; t <= 3; t++) {
            h += '<optgroup label="Timer ' + t + '">' + actionEditorTimerCommandOptionsHTML(t) + '</optgroup>';
        }
        h += '</select>';
        h += '</div>';
        h += '<div class="form-group" id="' + prefix + '-timer-mode-group" style="display:none;">';
        h += '<label class="form-label" for="' + prefix + '-timer-mode">Mode</label>';
        h += '<select class="form-select form-select-sm" id="' + prefix + '-timer-mode" onchange="actionEditorTimerChanged(\'' + prefix + '\')">';
        h += '<option value="">Select mode</option>';
        h += '<option value="up">Stopwatch (Count Up)</option>';
        h += '<option value="down">Countdown</option>';
        h += '</select>';
        h += '</div>';
        h += '<div class="form-group" id="' + prefix + '-timer-duration-group" style="display:none;">';
        h += '<label class="form-label" for="' + prefix + '-timer-duration">Duration (seconds) <span class="fx-hint" onclick="showBindingHelp()">fx</span></label>';
        h += '<input type="text" class="form-control form-control-sm" id="' + prefix + '-timer-duration" placeholder="e.g. 300">';
        h += '<small>Positive whole seconds. Start always uses it; Toggle uses it only when starting a stopped timer, not when pausing or resuming. Supports bindings.</small>';
        h += '</div>';
        h += '<div class="form-group" id="' + prefix + '-timer-set-group" style="display:none;">';
        h += '<label class="form-label" for="' + prefix + '-timer-set-sec">Countdown (seconds) <span class="fx-hint" onclick="showBindingHelp()">fx</span></label>';
        h += '<input type="text" class="form-control form-control-sm" id="' + prefix + '-timer-set-sec" placeholder="e.g. 300">';
        h += '<small>Set the countdown to this many seconds. Supports bindings.</small>';
        h += '</div>';
        h += '<div class="form-group" id="' + prefix + '-timer-adjust-group" style="display:none;">';
        h += '<label class="form-label" for="' + prefix + '-timer-adjust-sec">Adjust (seconds) <span class="fx-hint" onclick="showBindingHelp()">fx</span></label>';
        h += '<input type="text" class="form-control form-control-sm" id="' + prefix + '-timer-adjust-sec" placeholder="e.g. 15, -10, or {step}">';
        h += '<small>Positive adds time, negative subtracts. Use <code>{step}</code> as a placeholder for Numeric Rocker widgets.</small>';
        h += '</div>';
        h += '</div>';
        return h;
    },
    typeChanged: function(prefix, type) {
        var group = document.getElementById(prefix + '-timer-group');
        if (group) group.style.display = type === 'timer' ? '' : 'none';
        if (type === 'timer') actionEditorTimerChanged(prefix);
    },
    load: function(prefix, action) {
        var el;
        // Timer: load from proper fields
        if (action.timer_id && action.timer_command) {
            el = document.getElementById(prefix + '-timer-action');
            if (el) {
                el.value = action.timer_id + ':' + action.timer_command;
                if (el.selectedIndex < 0) el.value = '1:toggle';
            }
            if (action.timer_command === 'start' || action.timer_command === 'toggle') {
                el = document.getElementById(prefix + '-timer-mode');
                if (el) el.value = action.timer_mode || '';
                el = document.getElementById(prefix + '-timer-duration');
                if (el) el.value = action.timer_mode === 'down' ? (action.timer_value || '') : '';
            } else if (action.timer_command === 'set') {
                el = document.getElementById(prefix + '-timer-set-sec');
                if (el) el.value = action.timer_value || '';
            } else if (action.timer_command === 'adjust') {
                el = document.getElementById(prefix + '-timer-adjust-sec');
                if (el) el.value = action.timer_value || '';
            }
        } else {
            el = document.getElementById(prefix + '-timer-action');
            if (el) el.value = '1:toggle';
            el = document.getElementById(prefix + '-timer-mode');
            if (el) el.value = '';
            el = document.getElementById(prefix + '-timer-duration');
            if (el) el.value = '';
        }
    },
    build: function(prefix, type) {
        if (type !== 'timer') return null;
        var act = {};
        var sel = document.getElementById(prefix + '-timer-action');
        if (sel) {
            var val = sel.value; // e.g. "1:toggle", "2:adjust"
            var parts = val.split(':');
            act.timer_id = parseInt(parts[0], 10);
            act.timer_command = parts[1] || '';
            if (act.timer_command === 'start' || act.timer_command === 'toggle') {
                var mode = document.getElementById(prefix + '-timer-mode');
                var duration = document.getElementById(prefix + '-timer-duration');
                act.timer_mode = mode ? mode.value : '';
                if (act.timer_mode !== 'up' && act.timer_mode !== 'down') {
                    if (mode) { mode.setCustomValidity('Select a Timer mode.'); mode.reportValidity(); mode.focus(); }
                    if (typeof showMessage === 'function') showMessage('Timer Mode is required for Start and Toggle.', 'error');
                    throw new Error('Timer Mode is required for Start and Toggle');
                }
                if (mode) mode.setCustomValidity('');
                if (act.timer_mode === 'down') {
                    var durationValue = duration ? (duration.value || '').trim() : '';
                    var durationTokens = typeof bindingTokenize === 'function'
                        ? bindingTokenize(durationValue) : [];
                    var bindingResult = typeof validateBinding === 'function'
                        ? validateBinding(durationValue, { requireKnownScheme: true })
                        : { valid: false };
                    var isBinding = durationTokens.length === 1
                        && durationTokens[0].start === 0
                        && durationTokens[0].end === durationValue.length
                        && durationTokens[0].raw === durationValue
                        && bindingResult.valid;
                    var isSeconds = /^[1-9][0-9]*$/.test(durationValue)
                        && Number(durationValue) <= 4294967;
                    if (!durationValue || (!isBinding && !isSeconds)) {
                        if (duration) { duration.setCustomValidity('Enter 1-4294967 whole seconds or a binding.'); duration.reportValidity(); duration.focus(); }
                        if (typeof showMessage === 'function') showMessage('Timer Duration must be 1-4294967 whole seconds or a binding.', 'error');
                        throw new Error('Timer Duration must be 1-4294967 whole seconds or a binding');
                    }
                    if (duration) duration.setCustomValidity('');
                    act.timer_value = durationValue;
                } else if (duration) {
                    duration.setCustomValidity('');
                }
            } else if (act.timer_command === 'set') {
                var setSec = document.getElementById(prefix + '-timer-set-sec');
                if (setSec && setSec.value !== '') act.timer_value = (setSec.value || '').trim();
            } else if (act.timer_command === 'adjust') {
                var adjSec = document.getElementById(prefix + '-timer-adjust-sec');
                if (adjSec && adjSec.value !== '') act.timer_value = (adjSec.value || '').trim();
            }
        }
        return act;
    }
});
