// ============================================================================
// Shared Action Editor — reusable UI component for ButtonAction editing
// ============================================================================
// Both the pad button editor (pads.html) and swipe action editor (home.html)
// use the same action types (screen, back, mqtt, key, ble_pair). This module
// provides shared HTML generation, load/save, and type-change handlers so that
// adding a new action type requires exactly one code change.

// Generate the HTML for one action editor instance.
// prefix: unique ID prefix (e.g. "pad-edit-action", "swipe-right")
// label:  optional label shown above the type dropdown (e.g. "Tap Action")
// opts:   { showBleHint: bool, showKeyHelp: bool }
function actionEditorHTML(prefix, label, opts) {
    opts = opts || {};
    var h = '';
    h += '<div class="form-group">';
    if (label) h += '<label class="form-label" for="' + prefix + '-type">' + label + '</label>';
    h += '<select class="form-select form-select-sm" id="' + prefix + '-type" onchange="actionEditorTypeChanged(\'' + prefix + '\')">'; 
    h += '<option value="">(none)</option>';
    h += '<option value="screen">Navigate to Screen</option>';
    h += '<option value="back">Navigate Back</option>';
    h += '<option value="mqtt">MQTT Publish</option>';
    h += '<option value="key">Send BLE Keys</option>';
    h += '<option value="ble_pair">Start BLE Pairing</option>';
    h += '<option value="beep">Play Beep</option>';
    h += '<option value="sound">Play Sound</option>';
    h += '<option value="timer">Timer Control</option>';
    h += '<option value="notify">Show Notification</option>';
    h += '<option value="system">System Command</option>';
    h += '<option value="shutter">Shutter Speed Control</option>';
    h += '</select>';
    if (opts.showBleHint) {
        h += '<small id="' + prefix + '-ble-hint" style="display:none; color:#86868b;">Requires BLE Keyboard support on your board and BLE enabled in <b>Home &rarr; Operating Mode</b>.</small>';
    }
    h += '</div>';
    // Screen target
    h += '<div id="' + prefix + '-screen-group" style="display:none;">';
    h += '<div class="form-group">';
    h += '<label class="form-label" for="' + prefix + '-target">Target Screen</label>';
    h += '<select class="form-select form-select-sm" id="' + prefix + '-target"><option value="">(none)</option></select>';
    h += '</div></div>';
    // MQTT
    h += '<div id="' + prefix + '-mqtt-group" style="display:none;">';
    h += '<div class="form-group">';
    h += '<label class="form-label" for="' + prefix + '-topic">MQTT Topic <span class="fx-hint" onclick="showBindingHelp()">fx</span></label>';
    h += '<input type="text" class="form-control form-control-sm" id="' + prefix + '-topic" maxlength="127" placeholder="e.g. home/light/toggle">';
    h += '</div>';
    h += '<div class="form-group">';
    h += '<label class="form-label" for="' + prefix + '-payload">MQTT Payload <span class="fx-hint" onclick="showBindingHelp()">fx</span></label>';
    h += '<input type="text" class="form-control form-control-sm" id="' + prefix + '-payload" maxlength="127" placeholder="e.g. ON or [health:cpu]">';
    h += '</div></div>';
    // Key sequence
    h += '<div id="' + prefix + '-key-group" style="display:none;">';
    h += '<div class="form-group">';
    h += '<label class="form-label" for="' + prefix + '-sequence">Keys to Send <span class="fx-hint" onclick="showBindingHelp()">fx</span></label>';
    h += '<input type="text" class="form-control form-control-sm" id="' + prefix + '-sequence" maxlength="255" placeholder=\'e.g. ctrl+c, "hello", 200ms\'>';
    if (opts.showKeyHelp) {
        h += '<small>Space-separated steps. <b>Modifiers:</b> ctrl, shift, alt, gui &mdash; <b>Keys:</b> a&ndash;z, 0&ndash;9, enter, tab, esc, space, backspace, delete, up/down/left/right, f1&ndash;f12, home, end, pageup, pagedown, insert, printscreen, capslock &mdash; <b>Media:</b> vol_up, vol_down, mute, play_pause, next_track, prev_track &mdash; <b>Combos:</b> ctrl+c, ctrl+shift+t, gui+l &mdash; <b>Text:</b> &quot;hello&quot; &mdash; <b>Delay:</b> 200ms. Supports bindings.</small>';
    }
    h += '</div></div>';
    // Beep
    h += '<div id="' + prefix + '-beep-group" style="display:none;">';
    h += '<div class="form-group">';
    h += '<label class="form-label" for="' + prefix + '-beep-pattern">Beep Pattern <span class="fx-hint" onclick="showBindingHelp()">fx</span></label>';
    h += '<input type="text" class="form-control form-control-sm" id="' + prefix + '-beep-pattern" maxlength="127" placeholder="e.g. 1000:200 100 1000:200">';
    h += '<small>Space-separated steps. <b>freq:dur</b> = tone, bare <b>dur</b> = silence gap (ms). E.g. <b>1000:200</b> (single beep), <b>1000:200 100 1000:200</b> (double beep), <b>800:100 50 1200:100</b> (two-tone chirp). Supports bindings.</small>';
    h += '</div>';
    h += '<div class="form-group">';
    h += '<label class="form-label" for="' + prefix + '-beep-volume">Volume Override (%)</label>';
    h += '<input type="number" class="form-control form-control-sm" id="' + prefix + '-beep-volume" min="0" max="100" placeholder="(use device volume)">';
    h += '<small>Optional. If empty, uses the device volume from Home &rarr; Audio.</small>';
    h += '</div></div>';
    // Sound file
    h += '<div id="' + prefix + '-sound-group" style="display:none;">';
    h += '<div class="form-group">';
    h += '<label class="form-label" for="' + prefix + '-sound-file">Sound File</label>';
    h += '<select class="form-select form-select-sm" id="' + prefix + '-sound-file"><option value="">(none)</option></select>';
    h += '<small>MP3 files uploaded via the web portal. Upload sounds in Home &rarr; Sound Files.</small>';
    h += '</div>';
    h += '<div class="form-group">';
    h += '<label class="form-label" for="' + prefix + '-sound-volume">Volume Override (%)</label>';
    h += '<input type="number" class="form-control form-control-sm" id="' + prefix + '-sound-volume" min="0" max="100" placeholder="(use device volume)">';
    h += '<small>Optional. If empty, uses the device volume from Home &rarr; Audio.</small>';
    h += '</div></div>';
    // Timer — structured dropdowns
    h += '<div id="' + prefix + '-timer-group" style="display:none;">';
    h += '<div class="form-group">';
    h += '<label class="form-label" for="' + prefix + '-timer-action">Timer Action</label>';
    h += '<select class="form-select form-select-sm" id="' + prefix + '-timer-action" onchange="actionEditorTimerChanged(\'' + prefix + '\')">'; 
    for (var t = 1; t <= 3; t++) {
        h += '<optgroup label="Timer ' + t + '">';
        h += '<option value="' + t + ':toggle">T' + t + ': Toggle</option>';
        h += '<option value="' + t + ':start">T' + t + ': Start</option>';
        h += '<option value="' + t + ':stop">T' + t + ': Stop</option>';
        h += '<option value="' + t + ':pause">T' + t + ': Pause</option>';
        h += '<option value="' + t + ':resume">T' + t + ': Resume</option>';
        h += '<option value="' + t + ':reset">T' + t + ': Reset</option>';
        h += '<option value="' + t + ':lap">T' + t + ': Lap</option>';
        h += '<option value="' + t + ':set">T' + t + ': Set Countdown</option>';
        h += '<option value="' + t + ':adjust">T' + t + ': Adjust Countdown</option>';
        h += '</optgroup>';
    }
    h += '</select>';
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
    // Notify
    h += '<div id="' + prefix + '-notify-group" style="display:none;">';
    h += '<div class="form-group">';
    h += '<label class="form-label" for="' + prefix + '-notify-text">Message <span class="fx-hint" onclick="showBindingHelp()">fx</span></label>';
    h += '<input type="text" class="form-control form-control-sm" id="' + prefix + '-notify-text" maxlength="127" placeholder="e.g. Brightness is at 100%">';
    h += '<small>Supports binding templates. Empty = dismiss current notification.</small>';
    h += '</div>';
    h += '<div class="form-group">';
    h += '<label class="form-label" for="' + prefix + '-notify-duration">Duration (ms) <span class="fx-hint" onclick="showBindingHelp()">fx</span></label>';
    h += '<input type="text" class="form-control form-control-sm" id="' + prefix + '-notify-duration" value="3000" placeholder="3000">';
    h += '<small>0 = persistent (tap to dismiss). Supports bindings.</small>';
    h += '</div>';
    h += '<div class="grid-2col">';
    h += '<div class="form-group">';
    h += '<label style="font-size:13px; font-weight:600; margin-bottom:2px; display:block;">Text Color <span class="fx-hint" onclick="showBindingHelp()">fx</span></label>';
    h += '<div class="bindable-color" id="' + prefix + '-notify-text-color-wrap"><div class="bc-swatch"></div>';
    h += '<input type="text" id="' + prefix + '-notify-text-color" class="bc-input" maxlength="191" spellcheck="false" placeholder="#ffffff or [binding]"></div>';
    h += '</div>';
    h += '<div class="form-group">';
    h += '<label style="font-size:13px; font-weight:600; margin-bottom:2px; display:block;">Background Color <span class="fx-hint" onclick="showBindingHelp()">fx</span></label>';
    h += '<div class="bindable-color" id="' + prefix + '-notify-bg-color-wrap"><div class="bc-swatch"></div>';
    h += '<input type="text" id="' + prefix + '-notify-bg-color" class="bc-input" maxlength="191" spellcheck="false" placeholder="#333333 or [binding]"></div>';
    h += '</div>';
    h += '</div>';
    h += '<div class="grid-2col">';
    h += '<div class="form-group">';
    h += '<label style="font-size:13px; font-weight:600; margin-bottom:2px; display:block;">Border Color <span class="fx-hint" onclick="showBindingHelp()">fx</span></label>';
    h += '<div class="bindable-color" id="' + prefix + '-notify-border-color-wrap"><div class="bc-swatch"></div>';
    h += '<input type="text" id="' + prefix + '-notify-border-color" class="bc-input" maxlength="191" spellcheck="false" placeholder="Empty = no border"></div>';
    h += '</div>';
    h += '<div class="form-group">';
    h += '<label class="form-label" for="' + prefix + '-notify-opacity">Opacity (%)</label>';
    h += '<input type="number" class="form-control form-control-sm" id="' + prefix + '-notify-opacity" min="0" max="100" placeholder="85">';
    h += '</div>';
    h += '</div>';
    h += '<div class="grid-2col">';
    h += '<div class="form-group">';
    h += '<label class="form-label" for="' + prefix + '-notify-font-size">Font Size</label>';
    h += '<input type="number" class="form-control form-control-sm" id="' + prefix + '-notify-font-size" min="0" max="48" placeholder="0 = auto">';
    h += '</div>';
    h += '<div class="form-group">';
    h += '<label class="form-label" for="' + prefix + '-notify-location">Location</label>';
    h += '<select class="form-select form-select-sm" id="' + prefix + '-notify-location">';
    h += '<option value="bottom" selected>Bottom</option>';
    h += '<option value="center">Center</option>';
    h += '<option value="top">Top</option>';
    h += '</select>';
    h += '</div>';
    h += '</div>';
    h += '</div>';
    // System command
    h += '<div id="' + prefix + '-system-group" style="display:none;">';
    h += '<div class="form-group">';
    h += '<label class="form-label" for="' + prefix + '-system-command">Command</label>';
    h += '<select class="form-select form-select-sm" id="' + prefix + '-system-command" onchange="actionEditorSystemChanged(\'' + prefix + '\')">'; 
    h += '<option value="reboot">Reboot Device</option>';
    h += '<option value="wifi_reconnect">Reconnect WiFi</option>';
    h += '<option value="screensaver">Enable Screensaver</option>';
    h += '<option value="volume_set">Set Volume</option>';
    h += '<option value="volume_adjust">Adjust Volume</option>';
    h += '<option value="brightness_set">Set Brightness</option>';
    h += '<option value="brightness_adjust">Adjust Brightness</option>';
    h += '</select>';
    h += '</div>';
    // Set value sub-field (volume/brightness set)
    h += '<div class="form-group" id="' + prefix + '-sys-set-group" style="display:none;">';
    h += '<label class="form-label" for="' + prefix + '-sys-set-value" id="' + prefix + '-sys-set-label">Value (%)</label>';
    h += '<input type="number" class="form-control form-control-sm" id="' + prefix + '-sys-set-value" min="0" max="100" placeholder="e.g. 50">';
    h += '</div>';
    // Adjust value sub-field (volume/brightness adjust)
    h += '<div class="form-group" id="' + prefix + '-sys-adjust-group" style="display:none;">';
    h += '<label class="form-label" for="' + prefix + '-sys-adjust-value" id="' + prefix + '-sys-adjust-label">Adjust (%)</label>';
    h += '<input type="text" class="form-control form-control-sm" id="' + prefix + '-sys-adjust-value" placeholder="e.g. 10, -10, or {step}">';
    h += '<small>Positive increases, negative decreases. Use <code>{step}</code> as a placeholder for Numeric Rocker widgets.</small>';
    h += '</div>';
    h += '</div>';
    // Shutter speed control
    h += '<div id="' + prefix + '-shutter-group" style="display:none;">';
    h += '<div class="form-group">';
    h += '<label class="form-label" for="' + prefix + '-shutter-command">Command</label>';
    h += '<select class="form-select form-select-sm" id="' + prefix + '-shutter-command" onchange="actionEditorShutterChanged(\'' + prefix + '\')">';
    h += '<optgroup label="Target Speed">';
    h += '<option value="toggle_lock">Toggle Lock</option>';
    h += '<option value="set">Set Target Speed</option>';
    h += '<option value="adjust">Adjust Target Speed</option>';
    h += '</optgroup>';
    h += '<optgroup label="Session">';
    h += '<option value="sess_toggle">Session: Toggle Start/Stop</option>';
    h += '<option value="sess_start">Session: Start</option>';
    h += '<option value="sess_stop">Session: Stop</option>';
    h += '<option value="sess_discard">Session: Discard Last Shot</option>';
    h += '</optgroup>';
    h += '<optgroup label="Guided Test">';
    h += '<option value="guide_start">Guide: Start Test</option>';
    h += '<option value="guide_stop">Guide: Stop</option>';
    h += '<option value="guide_skip">Guide: Skip Step</option>';
    h += '<option value="guide_redo">Guide: Redo Step</option>';
    h += '</optgroup>';
    h += '<optgroup label="Alignment">';
    h += '<option value="align_start">Alignment: Start</option>';
    h += '<option value="align_stop">Alignment: Stop</option>';
    h += '<option value="recalibrate">Recalibrate Baseline</option>';
    h += '</optgroup>';
    h += '</select>';
    h += '</div>';
    // Set: speed label input
    h += '<div class="form-group" id="' + prefix + '-shutter-set-group" style="display:none;">';
    h += '<label class="form-label" for="' + prefix + '-shutter-set-speed">Target Speed</label>';
    h += '<select class="form-select form-select-sm" id="' + prefix + '-shutter-set-speed">';
    ['1s','1/2s','1/4s','1/5s','1/8s','1/10s','1/15s','1/25s','1/30s','1/50s','1/60s','1/100s','1/125s','1/200s','1/250s','1/500s','1/1000s','1/2000s'].forEach(function(spd) {
        h += '<option value="' + spd + '">' + spd + '</option>';
    });
    h += '</select>';
    h += '</div>';
    // Adjust: faster/slower
    h += '<div class="form-group" id="' + prefix + '-shutter-adjust-group" style="display:none;">';
    h += '<label class="form-label" for="' + prefix + '-shutter-adjust-dir">Direction</label>';
    h += '<select class="form-select form-select-sm" id="' + prefix + '-shutter-adjust-dir">';
    h += '<option value="faster">Faster (shorter)</option>';
    h += '<option value="slower">Slower (longer)</option>';
    h += '</select>';
    h += '</div>';
    // Free-text argument (camera for sess_start/sess_toggle, test_id for guide_start)
    h += '<div class="form-group" id="' + prefix + '-shutter-arg-group" style="display:none;">';
    h += '<label class="form-label" for="' + prefix + '-shutter-arg" id="' + prefix + '-shutter-arg-label">Argument</label>';
    h += '<input type="text" class="form-control form-control-sm" id="' + prefix + '-shutter-arg" maxlength="63" placeholder="">';
    h += '<small id="' + prefix + '-shutter-arg-hint"></small>';
    h += '</div>';
    h += '</div>';
    return h;
}

// Show/hide sub-groups when the action type dropdown changes.
function actionEditorTypeChanged(prefix) {
    var typeEl = document.getElementById(prefix + '-type');
    if (!typeEl) return;
    var type = typeEl.value;
    var screenGrp = document.getElementById(prefix + '-screen-group');
    var mqttGrp = document.getElementById(prefix + '-mqtt-group');
    var keyGrp = document.getElementById(prefix + '-key-group');
    var bleHint = document.getElementById(prefix + '-ble-hint');
    var beepGrp = document.getElementById(prefix + '-beep-group');
    var soundGrp = document.getElementById(prefix + '-sound-group');
    if (screenGrp) screenGrp.style.display = (type === 'screen') ? '' : 'none';
    // List widget: inject synthetic "Selected … Item" option in screen dropdown
    if (type === 'screen') {
        listInjectSyntheticScreenOption(prefix);
        // Re-apply pending target value (deferred when option didn't exist).
        // Only clear pending if the value was actually applied — otherwise the
        // option may be injected later (after widget type / provider id is set).
        var tgt = document.getElementById(prefix + '-target');
        if (tgt && tgt.hasAttribute('data-pending-value')) {
            var pv = tgt.getAttribute('data-pending-value');
            tgt.value = pv;
            if (tgt.value === pv) tgt.removeAttribute('data-pending-value');
        }
    }
    if (mqttGrp) mqttGrp.style.display = (type === 'mqtt') ? '' : 'none';
    if (keyGrp) keyGrp.style.display = (type === 'key') ? '' : 'none';
    if (bleHint) bleHint.style.display = (type === 'key' || type === 'ble_pair') ? '' : 'none';
    if (beepGrp) beepGrp.style.display = (type === 'beep') ? '' : 'none';
    if (soundGrp) soundGrp.style.display = (type === 'sound') ? '' : 'none';
    var timerGrp = document.getElementById(prefix + '-timer-group');
    if (timerGrp) timerGrp.style.display = (type === 'timer') ? '' : 'none';
    var notifyGrp = document.getElementById(prefix + '-notify-group');
    if (notifyGrp) notifyGrp.style.display = (type === 'notify') ? '' : 'none';
    var systemGrp = document.getElementById(prefix + '-system-group');
    if (systemGrp) systemGrp.style.display = (type === 'system') ? '' : 'none';
    var shutterGrp = document.getElementById(prefix + '-shutter-group');
    if (shutterGrp) shutterGrp.style.display = (type === 'shutter') ? '' : 'none';
    if (['notify', 'mqtt', 'key', 'beep', 'timer'].indexOf(type) >= 0) actionEditorInitBindings(prefix);
    if (type === 'timer') actionEditorTimerChanged(prefix);
    if (type === 'system') actionEditorSystemChanged(prefix);
    if (type === 'shutter') actionEditorShutterChanged(prefix);
}

// Show/hide system command sub-fields based on the command dropdown.
function actionEditorSystemChanged(prefix) {
    var sel = document.getElementById(prefix + '-system-command');
    if (!sel) return;
    var cmd = sel.value;
    var setGrp = document.getElementById(prefix + '-sys-set-group');
    var adjustGrp = document.getElementById(prefix + '-sys-adjust-group');
    var setLabel = document.getElementById(prefix + '-sys-set-label');
    var setInput = document.getElementById(prefix + '-sys-set-value');
    var adjustLabel = document.getElementById(prefix + '-sys-adjust-label');
    var isSet = (cmd === 'volume_set' || cmd === 'brightness_set');
    var isAdjust = (cmd === 'volume_adjust' || cmd === 'brightness_adjust');
    if (setGrp) setGrp.style.display = isSet ? '' : 'none';
    if (adjustGrp) adjustGrp.style.display = isAdjust ? '' : 'none';
    if (isSet) {
        var isBright = (cmd === 'brightness_set');
        if (setInput) setInput.min = isBright ? '5' : '0';
        if (setLabel) setLabel.textContent = isBright ? 'Brightness (%)' : 'Volume (%)';
    }
    if (isAdjust) {
        var isBright = (cmd === 'brightness_adjust');
        if (adjustLabel) adjustLabel.textContent = isBright ? 'Adjust Brightness (%)' : 'Adjust Volume (%)';
    }
}

// Show/hide timer sub-fields based on the timer action dropdown.
function actionEditorTimerChanged(prefix) {
    var sel = document.getElementById(prefix + '-timer-action');
    if (!sel) return;
    var val = sel.value; // e.g. "1:toggle", "2:adjust"
    var parts = val.split(':');
    var cmd = parts[1] || '';
    var setGrp = document.getElementById(prefix + '-timer-set-group');
    var adjustGrp = document.getElementById(prefix + '-timer-adjust-group');
    if (setGrp) setGrp.style.display = (cmd === 'set') ? '' : 'none';
    if (adjustGrp) adjustGrp.style.display = (cmd === 'adjust') ? '' : 'none';
}

// Show/hide shutter sub-fields based on the command dropdown.
function actionEditorShutterChanged(prefix) {
    var sel = document.getElementById(prefix + '-shutter-command');
    if (!sel) return;
    var cmd = sel.value;
    var setGrp = document.getElementById(prefix + '-shutter-set-group');
    var adjGrp = document.getElementById(prefix + '-shutter-adjust-group');
    var argGrp = document.getElementById(prefix + '-shutter-arg-group');
    var argLbl = document.getElementById(prefix + '-shutter-arg-label');
    var argHint = document.getElementById(prefix + '-shutter-arg-hint');
    if (setGrp) setGrp.style.display = (cmd === 'set') ? '' : 'none';
    if (adjGrp) adjGrp.style.display = (cmd === 'adjust') ? '' : 'none';
    var takesArg = (cmd === 'sess_start' || cmd === 'sess_toggle' || cmd === 'guide_start');
    if (argGrp) argGrp.style.display = takesArg ? '' : 'none';
    if (argLbl && argHint) {
        if (cmd === 'guide_start') {
            argLbl.textContent = 'Test ID';
            argHint.textContent = 'ID of the guided test to start (e.g. id from /api/shutter/tests).';
        } else {
            argLbl.textContent = 'Camera (optional)';
            argHint.textContent = 'Camera name to record with the session. Leave blank to skip.';
        }
    }
}

// Suffixes for binding-capable action text inputs (shared with binding validator).
var _ACTION_BIND_SUFFIXES = [
    '-notify-text', '-notify-duration', '-topic', '-payload', '-sequence',
    '-beep-pattern', '-timer-set-sec', '-timer-adjust-sec'
];

// Initialize bindable-color pickers and binding font toggles for all bindable fields.
// Idempotent — safe to call on every type-change.
function actionEditorInitBindings(prefix) {
    // Init color pickers (notify only)
    ['-notify-text-color-wrap', '-notify-bg-color-wrap', '-notify-border-color-wrap'].forEach(function(suffix) {
        var wrap = document.getElementById(prefix + suffix);
        if (wrap) padInitBindableColor(wrap);
    });
    // Wire binding validation on all binding-capable text inputs
    _ACTION_BIND_SUFFIXES.forEach(function(suffix) {
        var el = document.getElementById(prefix + suffix);
        if (el && !el.dataset.bcBind) {
            el.dataset.bcBind = '1';
            if (typeof bindingAttachValidation === 'function') bindingAttachValidation(el);
        }
    });
}

// Load an action object { type, target, topic, payload, sequence } into the form.
function actionEditorLoad(prefix, action) {
    if (!action) action = {};
    var el;
    el = document.getElementById(prefix + '-type');
    if (el) el.value = action.type || '';
    el = document.getElementById(prefix + '-target');
    if (el) {
        el.value = action.target || '';
        // If the option doesn't exist (e.g., synthetic [list:…] not yet injected),
        // defer the value until the option is added.
        if (action.target && el.value !== action.target) {
            el.setAttribute('data-pending-value', action.target);
            el.value = '';
        }
    }
    el = document.getElementById(prefix + '-topic');
    if (el) el.value = action.topic || '';
    el = document.getElementById(prefix + '-payload');
    if (el) el.value = action.payload || '';
    el = document.getElementById(prefix + '-sequence');
    if (el) el.value = action.sequence || '';
    el = document.getElementById(prefix + '-beep-pattern');
    if (el) el.value = action.beep_pattern || '';
    el = document.getElementById(prefix + '-beep-volume');
    if (el) el.value = (action.beep_volume > 0) ? action.beep_volume : '';
    el = document.getElementById(prefix + '-sound-file');
    if (el) {
        el.value = action.sound_file || '';
        if (el.selectedIndex < 0) el.value = '';
    }
    el = document.getElementById(prefix + '-sound-volume');
    if (el) el.value = (action.sound_volume > 0) ? action.sound_volume : '';

    // Timer: load from proper fields
    if (action.timer_id && action.timer_command) {
        el = document.getElementById(prefix + '-timer-action');
        if (el) {
            el.value = action.timer_id + ':' + action.timer_command;
            if (el.selectedIndex < 0) el.value = '1:toggle';
        }
        if (action.timer_command === 'set') {
            el = document.getElementById(prefix + '-timer-set-sec');
            if (el) el.value = action.timer_value || '';
        } else if (action.timer_command === 'adjust') {
            el = document.getElementById(prefix + '-timer-adjust-sec');
            if (el) el.value = action.timer_value || '';
        }
    } else {
        el = document.getElementById(prefix + '-timer-action');
        if (el) el.value = '1:toggle';
    }
    // Notify fields
    el = document.getElementById(prefix + '-notify-text');
    if (el) { el.value = action.notify_text || ''; }
    el = document.getElementById(prefix + '-notify-duration');
    if (el) { el.value = action.notify_duration_ms || '3000'; }
    padSetBindableColor(prefix + '-notify-text-color', action.notify_text_color || '', '#ffffff');
    padSetBindableColor(prefix + '-notify-bg-color', action.notify_bg_color || '', '#333333');
    padSetBindableColor(prefix + '-notify-border-color', action.notify_border_color || '', '');
    el = document.getElementById(prefix + '-notify-opacity');
    if (el) el.value = (action.notify_opacity > 0) ? action.notify_opacity : '';
    el = document.getElementById(prefix + '-notify-font-size');
    if (el) el.value = (action.notify_font_size > 0) ? action.notify_font_size : '';
    el = document.getElementById(prefix + '-notify-location');
    if (el) el.value = action.notify_location || 'bottom';
    // System fields — also handles volume/brightness mapped into system command
    if (action.type === 'volume' || action.type === 'brightness') {
        // Map volume/brightness type into system command UI
        el = document.getElementById(prefix + '-type');
        if (el) el.value = 'system';
        var mode = (action.type === 'volume') ? (action.volume_mode || 'set') : (action.brightness_mode || 'set');
        el = document.getElementById(prefix + '-system-command');
        if (el) el.value = action.type + '_' + mode;
        var val = (action.type === 'volume') ? (action.volume_value || '') : (action.brightness_value || '');
        if (mode === 'set') {
            el = document.getElementById(prefix + '-sys-set-value');
            if (el) el.value = val;
        } else {
            el = document.getElementById(prefix + '-sys-adjust-value');
            if (el) el.value = val;
        }
    } else {
        el = document.getElementById(prefix + '-system-command');
        if (el) el.value = action.system_command || 'reboot';
    }
    // Shutter fields
    if (action.type === 'shutter') {
        var sc2 = document.getElementById(prefix + '-shutter-command');
        if (sc2) sc2.value = action.shutter_command || 'toggle_lock';
        if (action.shutter_command === 'set') {
            var ss = document.getElementById(prefix + '-shutter-set-speed');
            if (ss) ss.value = action.shutter_value || '1/125s';
        } else if (action.shutter_command === 'adjust') {
            var sd = document.getElementById(prefix + '-shutter-adjust-dir');
            if (sd) sd.value = action.shutter_value || 'faster';
        } else {
            var sa = document.getElementById(prefix + '-shutter-arg');
            if (sa) sa.value = action.shutter_value || '';
        }
    } else {
        var sc2d = document.getElementById(prefix + '-shutter-command');
        if (sc2d) sc2d.value = 'toggle_lock';
    }
    actionEditorTypeChanged(prefix);
}

// Build an action object from the form. Returns {} if type is empty.
function actionEditorBuild(prefix) {
    var typeEl = document.getElementById(prefix + '-type');
    if (!typeEl) return {};
    var type = typeEl.value;
    if (!type) return {};
    var act = { type: type };
    if (type === 'screen') {
        var t = document.getElementById(prefix + '-target');
        if (t) act.target = t.value;
    }
    if (type === 'mqtt') {
        var topic = document.getElementById(prefix + '-topic');
        var payload = document.getElementById(prefix + '-payload');
        if (topic) act.topic = (topic.value || '').trim();
        if (payload) act.payload = (payload.value || '').trim();
    }
    if (type === 'key') {
        var seq = document.getElementById(prefix + '-sequence');
        if (seq) act.sequence = (seq.value || '').trim();
    }
    if (type === 'beep') {
        var bp = document.getElementById(prefix + '-beep-pattern');
        if (bp) act.beep_pattern = (bp.value || '').trim();
        var bv = document.getElementById(prefix + '-beep-volume');
        if (bv && bv.value !== '') act.beep_volume = parseInt(bv.value, 10);
    }
    if (type === 'sound') {
        var sf = document.getElementById(prefix + '-sound-file');
        if (sf) act.sound_file = sf.value || '';
        var sv = document.getElementById(prefix + '-sound-volume');
        if (sv && sv.value !== '') act.sound_volume = parseInt(sv.value, 10);
    }

    if (type === 'timer') {
        var sel = document.getElementById(prefix + '-timer-action');
        if (sel) {
            var val = sel.value; // e.g. "1:toggle", "2:adjust"
            var parts = val.split(':');
            act.timer_id = parseInt(parts[0], 10);
            act.timer_command = parts[1] || '';
            if (act.timer_command === 'set') {
                var setSec = document.getElementById(prefix + '-timer-set-sec');
                if (setSec && setSec.value !== '') act.timer_value = (setSec.value || '').trim();
            } else if (act.timer_command === 'adjust') {
                var adjSec = document.getElementById(prefix + '-timer-adjust-sec');
                if (adjSec && adjSec.value !== '') act.timer_value = (adjSec.value || '').trim();
            }
        }
    }
    if (type === 'notify') {
        var nt = document.getElementById(prefix + '-notify-text');
        if (nt) act.notify_text = (nt.value || '').trim();
        var nd = document.getElementById(prefix + '-notify-duration');
        if (nd && nd.value !== '') act.notify_duration_ms = (nd.value || '').trim();
        var ntc = padGetBindableColor(prefix + '-notify-text-color');
        if (ntc) act.notify_text_color = ntc;
        var nbc = padGetBindableColor(prefix + '-notify-bg-color');
        if (nbc) act.notify_bg_color = nbc;
        var nbrc = padGetBindableColor(prefix + '-notify-border-color');
        if (nbrc) act.notify_border_color = nbrc;
        var nop = document.getElementById(prefix + '-notify-opacity');
        if (nop && nop.value !== '') act.notify_opacity = parseInt(nop.value, 10);
        var nfs = document.getElementById(prefix + '-notify-font-size');
        if (nfs && nfs.value !== '') act.notify_font_size = parseInt(nfs.value, 10);
        var nloc = document.getElementById(prefix + '-notify-location');
        if (nloc) act.notify_location = nloc.value;
    }
    if (type === 'shutter') {
        var sc3 = document.getElementById(prefix + '-shutter-command');
        if (sc3) {
            act.shutter_command = sc3.value || 'toggle_lock';
            if (act.shutter_command === 'set') {
                var ss2 = document.getElementById(prefix + '-shutter-set-speed');
                if (ss2) act.shutter_value = ss2.value || '';
            } else if (act.shutter_command === 'adjust') {
                var sd2 = document.getElementById(prefix + '-shutter-adjust-dir');
                if (sd2) act.shutter_value = sd2.value || 'faster';
            } else if (act.shutter_command === 'sess_start' || act.shutter_command === 'sess_toggle' || act.shutter_command === 'guide_start') {
                var sa2 = document.getElementById(prefix + '-shutter-arg');
                if (sa2 && sa2.value) act.shutter_value = sa2.value.trim();
            }
        }
    }
    if (type === 'system') {
        var sc = document.getElementById(prefix + '-system-command');
        if (sc) {
            var cmd = sc.value;
            var sysMap = { volume_set: ['volume','set'], volume_adjust: ['volume','adjust'],
                           brightness_set: ['brightness','set'], brightness_adjust: ['brightness','adjust'] };
            var m = sysMap[cmd];
            if (m) {
                act.type = m[0];
                var modeKey = m[0] + '_mode', valKey = m[0] + '_value';
                act[modeKey] = m[1];
                var inputId = prefix + (m[1] === 'set' ? '-sys-set-value' : '-sys-adjust-value');
                var inp = document.getElementById(inputId);
                if (inp && inp.value !== '') act[valKey] = (inp.value || '').trim();
            } else {
                act.system_command = cmd;
            }
        }
    }
    return act;
}

// Inject a synthetic "Selected {Title} Item" option into a screen target dropdown.
// Only injects when the current widget type is "list" and a provider ID is set.
function listInjectSyntheticScreenOption(prefix) {
    var sel = document.getElementById(prefix + '-target');
    if (!sel || sel.tagName !== 'SELECT') return;
    // Capture the current value so we can restore it after removing the old
    // synthetic option (which may itself be the currently selected option,
    // since removing a selected <option> resets the dropdown to the first item).
    var prevValue = sel.value;
    // Helper: restore the previous value if it still maps to an existing option.
    var restorePrev = function() {
        if (prevValue && sel.value !== prevValue) sel.value = prevValue;
    };
    // Remove any previously injected synthetic option
    var existing = sel.querySelector('option[data-synthetic]');
    if (existing) existing.remove();
    // Only inject for list widget with a provider ID
    var wtEl = document.getElementById('pad-edit-widget-type');
    var provInput = document.getElementById('pad-edit-list-provider-id');
    var provId = provInput ? provInput.value.trim() : '';
    if (!wtEl || wtEl.value !== 'list' || !provId) {
        restorePrev();
        return;
    }
    var title = provId.charAt(0).toUpperCase() + provId.slice(1);
    var opt = document.createElement('option');
    opt.value = '[list:' + provId + '.selected]';
    opt.textContent = 'Selected ' + title + ' Item';
    opt.setAttribute('data-synthetic', '1');
    // Insert after "(none)" option
    if (sel.options.length > 1) {
        sel.insertBefore(opt, sel.options[1]);
    } else {
        sel.appendChild(opt);
    }
    // Re-apply pending value if it matches the newly injected synthetic option
    if (sel.hasAttribute('data-pending-value') &&
        sel.getAttribute('data-pending-value') === opt.value) {
        sel.value = opt.value;
        sel.removeAttribute('data-pending-value');
    } else {
        // Restore the previous selection (may be the just-injected synthetic option)
        restorePrev();
    }
}

// Refresh synthetic options in all tap and long-press action screen dropdowns.
// Called when widget type changes to/from "list" or when provider ID changes.
function listRefreshSyntheticOptions() {
    for (var i = 0; i < (typeof MAX_ACTIONS !== 'undefined' ? MAX_ACTIONS : 3); i++) {
        listInjectSyntheticScreenOption('pad-edit-action-' + i);
        listInjectSyntheticScreenOption('pad-edit-lp-action-' + i);
    }
}

// Populate the screen target dropdown(s) for one or more action editor prefixes.
// screens: array of { id, name } from deviceInfoCache.available_screens
// prefixes: array of prefix strings
function actionEditorPopulateScreens(prefixes, screens) {
    if (!screens) return;
    prefixes.forEach(function(prefix) {
        var sel = document.getElementById(prefix + '-target');
        if (!sel) return;
        while (sel.options.length > 1) sel.remove(1);
        screens.forEach(function(s) {
            var opt = document.createElement('option');
            opt.value = s.id;
            opt.textContent = s.name;
            sel.appendChild(opt);
        });
        // Apply pending value deferred by actionEditorLoad() when the option
        // did not yet exist at load time.
        if (sel.hasAttribute('data-pending-value')) {
            var pv = sel.getAttribute('data-pending-value');
            sel.value = pv;
            if (sel.value === pv) sel.removeAttribute('data-pending-value');
        }
    });
}

// Populate the sound file dropdown(s) for one or more action editor prefixes.
// sounds: array of sound file names (strings) from /api/sounds/list
// prefixes: array of prefix strings
function actionEditorPopulateSounds(prefixes, sounds) {
    if (!sounds) return;
    prefixes.forEach(function(prefix) {
        var sel = document.getElementById(prefix + '-sound-file');
        if (!sel) return;
        while (sel.options.length > 1) sel.remove(1);
        sounds.forEach(function(name) {
            var opt = document.createElement('option');
            opt.value = name;
            opt.textContent = name;
            sel.appendChild(opt);
        });
    });
}

// ============================================================================
// Action list helpers — DRY plumbing for fragments that host N action editors
// ============================================================================

// Render N action editor groups inside containerId, one per prefix in prefixes[].
// labels[i] is the <summary> text for that group (defaults to prefix).
function actionEditorListRender(containerId, prefixes, labels) {
    var container = document.getElementById(containerId);
    if (!container) return;
    var html = '';
    prefixes.forEach(function(prefix, i) {
        var summary = (labels && labels[i]) || prefix;
        html += '<details class="editor-group" id="' + prefix + '-group">';
        html += '<summary>' + summary + '</summary>';
        html += '<div class="editor-group-body">';
        html += actionEditorHTML(prefix);
        html += '</div></details>';
    });
    container.innerHTML = html;
}

// Load an array of action objects into N editor prefixes (positional).
function actionEditorListLoad(prefixes, actions) {
    actions = actions || [];
    prefixes.forEach(function(prefix, i) {
        actionEditorLoad(prefix, actions[i] || {});
    });
}

// Build an array of action objects from N editor prefixes.
// opts.trimEmpty (default true): drop trailing actions with empty type.
function actionEditorListBuild(prefixes, opts) {
    opts = opts || {};
    var trim = (opts.trimEmpty !== false);
    var out = prefixes.map(function(prefix) { return actionEditorBuild(prefix); });
    if (trim) {
        while (out.length > 0 && !out[out.length - 1].type) out.pop();
    }
    return out;
}

// Wire screen + sound dropdowns for a fragment hosting one or more action editors.
// Fetches deviceInfo (screens) and /api/sounds/list once and populates the given prefixes.
function actionEditorWireFragment(prefixes) {
    if (typeof getDeviceInfo === 'function') {
        getDeviceInfo().then(function(info) {
            if (info && info.available_screens) {
                actionEditorPopulateScreens(prefixes, info.available_screens);
            }
        });
    }
    fetch('/api/sounds/list').then(function(r) { return r.ok ? r.json() : []; })
        .then(function(sounds) { actionEditorPopulateSounds(prefixes, sounds); })
        .catch(function() {});
}
