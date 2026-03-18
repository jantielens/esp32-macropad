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
    if (label) h += '<label for="' + prefix + '-type">' + label + '</label>';
    h += '<select id="' + prefix + '-type" onchange="actionEditorTypeChanged(\'' + prefix + '\')">';
    h += '<option value="">(none)</option>';
    h += '<option value="screen">Navigate to Screen</option>';
    h += '<option value="back">Navigate Back</option>';
    h += '<option value="mqtt">MQTT Publish</option>';
    h += '<option value="key">Send BLE Keys</option>';
    h += '<option value="ble_pair">Start BLE Pairing</option>';
    h += '<option value="beep">Play Beep</option>';
    h += '<option value="volume">Set Volume</option>';
    h += '<option value="timer">Timer Control</option>';
    h += '</select>';
    if (opts.showBleHint) {
        h += '<small id="' + prefix + '-ble-hint" style="display:none; color:#86868b;">Requires BLE Keyboard support on your board and BLE enabled in <b>Home &rarr; Operating Mode</b>.</small>';
    }
    h += '</div>';
    // Screen target
    h += '<div id="' + prefix + '-screen-group" style="display:none;">';
    h += '<div class="form-group">';
    h += '<label for="' + prefix + '-target">Target Screen</label>';
    h += '<select id="' + prefix + '-target"><option value="">(none)</option></select>';
    h += '</div></div>';
    // MQTT
    h += '<div id="' + prefix + '-mqtt-group" style="display:none;">';
    h += '<div class="form-group">';
    h += '<label for="' + prefix + '-topic">MQTT Topic</label>';
    h += '<input type="text" id="' + prefix + '-topic" maxlength="127" placeholder="e.g. home/light/toggle">';
    h += '</div>';
    h += '<div class="form-group">';
    h += '<label for="' + prefix + '-payload">MQTT Payload</label>';
    h += '<input type="text" id="' + prefix + '-payload" maxlength="127" placeholder="e.g. ON">';
    h += '</div></div>';
    // Key sequence
    h += '<div id="' + prefix + '-key-group" style="display:none;">';
    h += '<div class="form-group">';
    h += '<label for="' + prefix + '-sequence">Keys to Send</label>';
    h += '<input type="text" id="' + prefix + '-sequence" maxlength="255" placeholder=\'e.g. ctrl+c, "hello", 200ms\'>';
    if (opts.showKeyHelp) {
        h += '<small>Space-separated steps. <b>Modifiers:</b> ctrl, shift, alt, gui &mdash; <b>Keys:</b> a&ndash;z, 0&ndash;9, enter, tab, esc, space, backspace, delete, up/down/left/right, f1&ndash;f12, home, end, pageup, pagedown, insert, printscreen, capslock &mdash; <b>Media:</b> vol_up, vol_down, mute, play_pause, next_track, prev_track &mdash; <b>Combos:</b> ctrl+c, ctrl+shift+t, gui+l &mdash; <b>Text:</b> &quot;hello&quot; &mdash; <b>Delay:</b> 200ms</small>';
    }
    h += '</div></div>';
    // Beep
    h += '<div id="' + prefix + '-beep-group" style="display:none;">';
    h += '<div class="form-group">';
    h += '<label for="' + prefix + '-beep-pattern">Beep Pattern</label>';
    h += '<input type="text" id="' + prefix + '-beep-pattern" maxlength="127" placeholder="e.g. 1000:200 100 1000:200">';
    h += '<small>Space-separated steps. <b>freq:dur</b> = tone, bare <b>dur</b> = silence gap (ms). E.g. <b>1000:200</b> (single beep), <b>1000:200 100 1000:200</b> (double beep), <b>800:100 50 1200:100</b> (two-tone chirp).</small>';
    h += '</div>';
    h += '<div class="form-group">';
    h += '<label for="' + prefix + '-beep-volume">Volume Override (%)</label>';
    h += '<input type="number" id="' + prefix + '-beep-volume" min="0" max="100" placeholder="(use device volume)">';
    h += '<small>Optional. If empty, uses the device volume from Home &rarr; Audio.</small>';
    h += '</div></div>';
    // Volume
    h += '<div id="' + prefix + '-volume-group" style="display:none;">';
    h += '<div class="form-group">';
    h += '<label for="' + prefix + '-volume-mode">Volume Action</label>';
    h += '<select id="' + prefix + '-volume-mode" onchange="actionEditorTypeChanged(\'' + prefix + '\')">';
    h += '<option value="set">Set to value</option>';
    h += '<option value="up">Volume Up (+10%)</option>';
    h += '<option value="down">Volume Down (&minus;10%)</option>';
    h += '</select>';
    h += '</div>';
    h += '<div class="form-group" id="' + prefix + '-volume-value-group">';
    h += '<label for="' + prefix + '-volume-value">Volume (%)</label>';
    h += '<input type="number" id="' + prefix + '-volume-value" min="0" max="100" placeholder="e.g. 50">';
    h += '</div></div>';
    // Timer — structured dropdowns
    h += '<div id="' + prefix + '-timer-group" style="display:none;">';
    h += '<div class="form-group">';
    h += '<label for="' + prefix + '-timer-action">Timer Action</label>';
    h += '<select id="' + prefix + '-timer-action" onchange="actionEditorTimerChanged(\'' + prefix + '\')">';
    for (var t = 1; t <= 3; t++) {
        h += '<optgroup label="Timer ' + t + '">';
        h += '<option value="' + t + ':toggle">Toggle</option>';
        h += '<option value="' + t + ':start">Start</option>';
        h += '<option value="' + t + ':stop">Stop</option>';
        h += '<option value="' + t + ':pause">Pause</option>';
        h += '<option value="' + t + ':resume">Resume</option>';
        h += '<option value="' + t + ':reset">Reset</option>';
        h += '<option value="' + t + ':lap">Lap</option>';
        h += '<option value="' + t + ':adjust">Adjust Countdown Time</option>';
        h += '<option value="' + t + ':countdown">Set Countdown</option>';
        h += '<option value="' + t + ':mode">Set Mode</option>';
        h += '</optgroup>';
    }
    h += '</select>';
    h += '</div>';
    h += '<div class="form-group" id="' + prefix + '-timer-cd-group" style="display:none;">';
    h += '<label for="' + prefix + '-timer-cd-sec">Countdown (seconds)</label>';
    h += '<input type="number" id="' + prefix + '-timer-cd-sec" min="1" max="86400" placeholder="e.g. 300">';
    h += '</div>';
    h += '<div class="form-group" id="' + prefix + '-timer-adjust-group" style="display:none;">';
    h += '<label for="' + prefix + '-timer-adjust-sec">Adjust (seconds)</label>';
    h += '<input type="number" id="' + prefix + '-timer-adjust-sec" min="-86400" max="86400" placeholder="e.g. 15 or -10">';
    h += '<small>Positive adds time, negative subtracts. Applied to the countdown preset.</small>';
    h += '</div>';
    h += '<div class="form-group" id="' + prefix + '-timer-mode-group" style="display:none;">';
    h += '<label for="' + prefix + '-timer-mode-val">Mode</label>';
    h += '<select id="' + prefix + '-timer-mode-val">';
    h += '<option value="up">Count Up</option>';
    h += '<option value="down">Count Down</option>';
    h += '</select>';
    h += '</div>';
    h += '<div class="form-group" id="' + prefix + '-timer-default-cd-group" style="display:none;">';
    h += '<label for="' + prefix + '-timer-default-cd">Default Countdown (seconds)</label>';
    h += '<input type="number" id="' + prefix + '-timer-default-cd" min="0" max="86400" placeholder="e.g. 300">';
    h += '<small>When navigating to this pad, if the timer is stopped and fresh, auto-configure it as a countdown with this duration. Leave empty or 0 to skip.</small>';
    h += '</div>';
    h += '<div class="form-group" id="' + prefix + '-timer-expire-beep-group" style="display:none;">';
    h += '<label for="' + prefix + '-timer-expire-beep">Expire Beep Pattern</label>';
    h += '<input type="text" id="' + prefix + '-timer-expire-beep" maxlength="127" placeholder="e.g. 1000:300 200 1000:300 200 1000:300">';
    h += '<small>Beep pattern to play when the countdown reaches zero. Same DSL as Beep action: <b>freq:dur</b> = tone, bare <b>dur</b> = silence gap (ms).</small>';
    h += '</div>';
    h += '<div class="form-group" id="' + prefix + '-timer-expire-vol-group" style="display:none;">';
    h += '<label for="' + prefix + '-timer-expire-vol">Expire Beep Volume (%)</label>';
    h += '<input type="number" id="' + prefix + '-timer-expire-vol" min="0" max="100" placeholder="(use device volume)">';
    h += '<small>Optional volume override for the expire beep. If empty, uses the device volume.</small>';
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
    var volGrp = document.getElementById(prefix + '-volume-group');
    if (screenGrp) screenGrp.style.display = (type === 'screen') ? '' : 'none';
    if (mqttGrp) mqttGrp.style.display = (type === 'mqtt') ? '' : 'none';
    if (keyGrp) keyGrp.style.display = (type === 'key') ? '' : 'none';
    if (bleHint) bleHint.style.display = (type === 'key' || type === 'ble_pair') ? '' : 'none';
    if (beepGrp) beepGrp.style.display = (type === 'beep') ? '' : 'none';
    if (volGrp) volGrp.style.display = (type === 'volume') ? '' : 'none';
    var timerGrp = document.getElementById(prefix + '-timer-group');
    if (timerGrp) timerGrp.style.display = (type === 'timer') ? '' : 'none';
    if (type === 'timer') actionEditorTimerChanged(prefix);
    // Show/hide volume value field depending on mode
    if (type === 'volume') {
        var modeEl = document.getElementById(prefix + '-volume-mode');
        var valGrp = document.getElementById(prefix + '-volume-value-group');
        if (modeEl && valGrp) valGrp.style.display = (modeEl.value === 'set') ? '' : 'none';
    }
}

// Show/hide timer sub-fields based on the timer action dropdown.
function actionEditorTimerChanged(prefix) {
    var sel = document.getElementById(prefix + '-timer-action');
    if (!sel) return;
    var val = sel.value; // e.g. "1:countdown", "2:mode", "1:toggle"
    var parts = val.split(':');
    var cmd = parts[1] || '';
    var cdGrp = document.getElementById(prefix + '-timer-cd-group');
    var adjustGrp = document.getElementById(prefix + '-timer-adjust-group');
    var modeGrp = document.getElementById(prefix + '-timer-mode-group');
    var defCdGrp = document.getElementById(prefix + '-timer-default-cd-group');
    var expBeepGrp = document.getElementById(prefix + '-timer-expire-beep-group');
    var expVolGrp = document.getElementById(prefix + '-timer-expire-vol-group');
    if (cdGrp) cdGrp.style.display = (cmd === 'countdown') ? '' : 'none';
    if (adjustGrp) adjustGrp.style.display = (cmd === 'adjust') ? '' : 'none';
    if (modeGrp) modeGrp.style.display = (cmd === 'mode') ? '' : 'none';
    // Show default countdown and expire beep for actions that start/toggle a timer
    var showDefCd = (cmd === 'toggle' || cmd === 'start');
    if (defCdGrp) defCdGrp.style.display = showDefCd ? '' : 'none';
    if (expBeepGrp) expBeepGrp.style.display = showDefCd ? '' : 'none';
    if (expVolGrp) expVolGrp.style.display = showDefCd ? '' : 'none';
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
        if (el.selectedIndex < 0) el.value = '';
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
    el = document.getElementById(prefix + '-volume-mode');
    if (el) el.value = action.volume_mode || 'set';
    el = document.getElementById(prefix + '-volume-value');
    if (el) el.value = (action.volume_value !== undefined && action.volume_value > 0) ? action.volume_value : '';
    // Timer: parse DSL string "N:command[:arg]" into structured fields
    if (action.timer_command) {
        var tc = action.timer_command;
        var m = tc.match(/^(\d):(\w+)(?::(.+))?$/);
        if (m) {
            var tid = m[1], cmd = m[2], arg = m[3] || '';
            el = document.getElementById(prefix + '-timer-action');
            if (el) {
                el.value = tid + ':' + cmd;
                if (el.selectedIndex < 0) el.value = '1:toggle';
            }
            if (cmd === 'countdown') {
                el = document.getElementById(prefix + '-timer-cd-sec');
                if (el) el.value = arg;
            } else if (cmd === 'adjust') {
                el = document.getElementById(prefix + '-timer-adjust-sec');
                if (el) el.value = arg;
            } else if (cmd === 'mode') {
                el = document.getElementById(prefix + '-timer-mode-val');
                if (el) el.value = arg || 'up';
            }
        }
    } else {
        el = document.getElementById(prefix + '-timer-action');
        if (el) el.value = '1:toggle';
    }
    // Timer default countdown
    el = document.getElementById(prefix + '-timer-default-cd');
    if (el) el.value = (action.timer_countdown > 0) ? action.timer_countdown : '';
    // Timer expire beep
    el = document.getElementById(prefix + '-timer-expire-beep');
    if (el) el.value = action.timer_expire_beep || '';
    el = document.getElementById(prefix + '-timer-expire-vol');
    if (el) el.value = (action.timer_expire_volume > 0) ? action.timer_expire_volume : '';
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
    if (type === 'volume') {
        var vm = document.getElementById(prefix + '-volume-mode');
        if (vm) act.volume_mode = vm.value;
        if (vm && vm.value === 'set') {
            var vv = document.getElementById(prefix + '-volume-value');
            if (vv && vv.value !== '') act.volume_value = parseInt(vv.value, 10);
        }
    }
    if (type === 'timer') {
        var sel = document.getElementById(prefix + '-timer-action');
        if (sel) {
            var val = sel.value; // e.g. "1:toggle", "2:countdown"
            var parts = val.split(':');
            var cmd = parts[1] || '';
            if (cmd === 'countdown') {
                var cdSec = document.getElementById(prefix + '-timer-cd-sec');
                var sec = cdSec ? parseInt(cdSec.value, 10) : 0;
                if (sec > 0) val = val + ':' + sec;
            } else if (cmd === 'adjust') {
                var adjSec = document.getElementById(prefix + '-timer-adjust-sec');
                var adj = adjSec ? parseInt(adjSec.value, 10) : 0;
                if (adj !== 0 && !isNaN(adj)) val = val + ':' + adj;
            } else if (cmd === 'mode') {
                var modeVal = document.getElementById(prefix + '-timer-mode-val');
                val = val + ':' + (modeVal ? modeVal.value : 'up');
            }
            act.timer_command = val;
        }
        var defCd = document.getElementById(prefix + '-timer-default-cd');
        if (defCd && defCd.value !== '' && parseInt(defCd.value, 10) > 0) {
            act.timer_countdown = parseInt(defCd.value, 10);
        }
        var expBeep = document.getElementById(prefix + '-timer-expire-beep');
        if (expBeep && (expBeep.value || '').trim()) {
            act.timer_expire_beep = expBeep.value.trim();
        }
        var expVol = document.getElementById(prefix + '-timer-expire-vol');
        if (expVol && expVol.value !== '' && parseInt(expVol.value, 10) > 0) {
            act.timer_expire_volume = parseInt(expVol.value, 10);
        }
    }
    return act;
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
    });
}
