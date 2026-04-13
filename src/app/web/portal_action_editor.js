// ============================================================================
// Shared Action Editor — reusable UI component for ButtonAction editing
// ============================================================================
// Both the pad button editor (pads.html) and swipe action editor (home.html)
// use the same action types (screen, back, mqtt, key, ble_pair). This module
// provides shared HTML generation, load/save, and type-change handlers so that
// adding a new action type requires exactly one code change.
//
// Extension modules (e.g. portal_action_editor_darkroom.js) can register into
// _actionEditorExtensions to add action types without modifying this file.
var _actionEditorExtensions = [];

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
    h += '<option value="sound">Play Sound</option>';
    h += '<option value="volume">Set Volume</option>';
    h += '<option value="brightness">Set Brightness</option>';
    h += '<option value="timer">Timer Control</option>';
    h += '<option value="notify">Show Notification</option>';
    // Extension action types (e.g. expose, strip)
    _actionEditorExtensions.forEach(function(ext) { if (ext.options) h += ext.options(); });
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
    // Sound file
    h += '<div id="' + prefix + '-sound-group" style="display:none;">';
    h += '<div class="form-group">';
    h += '<label for="' + prefix + '-sound-file">Sound File</label>';
    h += '<select id="' + prefix + '-sound-file"><option value="">(none)</option></select>';
    h += '<small>MP3 files uploaded via the web portal. Upload sounds in Home &rarr; Sound Files.</small>';
    h += '</div>';
    h += '<div class="form-group">';
    h += '<label for="' + prefix + '-sound-volume">Volume Override (%)</label>';
    h += '<input type="number" id="' + prefix + '-sound-volume" min="0" max="100" placeholder="(use device volume)">';
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
    // Brightness
    h += '<div id="' + prefix + '-brightness-group" style="display:none;">';
    h += '<div class="form-group">';
    h += '<label for="' + prefix + '-brightness-mode">Brightness Action</label>';
    h += '<select id="' + prefix + '-brightness-mode" onchange="actionEditorTypeChanged(\'' + prefix + '\')">'; 
    h += '<option value="set">Set to value</option>';
    h += '<option value="up">Brightness Up (+10%)</option>';
    h += '<option value="down">Brightness Down (&minus;10%)</option>';
    h += '</select>';
    h += '</div>';
    h += '<div class="form-group" id="' + prefix + '-brightness-value-group">';
    h += '<label for="' + prefix + '-brightness-value">Brightness (%)</label>';
    h += '<input type="number" id="' + prefix + '-brightness-value" min="0" max="100" placeholder="e.g. 50">';
    h += '<small id="' + prefix + '-brightness-step-hint" style="display:none;">Step size (default 10 if empty).</small>';
    h += '</div></div>';
    // Timer — structured dropdowns
    h += '<div id="' + prefix + '-timer-group" style="display:none;">';
    h += '<div class="form-group">';
    h += '<label for="' + prefix + '-timer-action">Timer Action</label>';
    h += '<select id="' + prefix + '-timer-action" onchange="actionEditorTimerChanged(\'' + prefix + '\')">';
    for (var t = 1; t <= 3; t++) {
        h += '<optgroup label="Timer ' + t + '">';
        h += '<option value="' + t + ':toggle">T' + t + ': Toggle</option>';
        h += '<option value="' + t + ':start">T' + t + ': Start</option>';
        h += '<option value="' + t + ':stop">T' + t + ': Stop</option>';
        h += '<option value="' + t + ':pause">T' + t + ': Pause</option>';
        h += '<option value="' + t + ':resume">T' + t + ': Resume</option>';
        h += '<option value="' + t + ':reset">T' + t + ': Reset</option>';
        h += '<option value="' + t + ':lap">T' + t + ': Lap</option>';
        h += '<option value="' + t + ':adjust">T' + t + ': Adjust Countdown</option>';
        h += '</optgroup>';
    }
    h += '</select>';
    h += '</div>';
    h += '<div class="form-group" id="' + prefix + '-timer-adjust-group" style="display:none;">';
    h += '<label for="' + prefix + '-timer-adjust-sec">Adjust (seconds)</label>';
    h += '<input type="number" id="' + prefix + '-timer-adjust-sec" min="-86400" max="86400" placeholder="e.g. 15 or -10">';
    h += '<small>Positive adds time, negative subtracts. Applied to the countdown preset.</small>';
    h += '</div>';
    h += '</div>';
    // Notify
    h += '<div id="' + prefix + '-notify-group" style="display:none;">';
    h += '<div class="form-group">';
    h += '<label for="' + prefix + '-notify-text">Message <span class="fx-hint" onclick="showBindingHelp()">fx</span></label>';
    h += '<input type="text" id="' + prefix + '-notify-text" maxlength="127" placeholder="e.g. Brightness is at 100%">';
    h += '<small>Supports binding templates. Empty = dismiss current notification.</small>';
    h += '</div>';
    h += '<div class="form-group">';
    h += '<label for="' + prefix + '-notify-duration">Duration (ms) <span class="fx-hint" onclick="showBindingHelp()">fx</span></label>';
    h += '<input type="text" id="' + prefix + '-notify-duration" value="3000" placeholder="3000">';
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
    h += '<label for="' + prefix + '-notify-opacity">Opacity (%)</label>';
    h += '<input type="number" id="' + prefix + '-notify-opacity" min="0" max="100" placeholder="85">';
    h += '</div>';
    h += '</div>';
    h += '<div class="grid-2col">';
    h += '<div class="form-group">';
    h += '<label for="' + prefix + '-notify-font-size">Font Size</label>';
    h += '<input type="number" id="' + prefix + '-notify-font-size" min="0" max="48" placeholder="0 = auto">';
    h += '</div>';
    h += '<div class="form-group">';
    h += '<label for="' + prefix + '-notify-location">Location</label>';
    h += '<select id="' + prefix + '-notify-location">';
    h += '<option value="bottom" selected>Bottom</option>';
    h += '<option value="center">Center</option>';
    h += '<option value="top">Top</option>';
    h += '</select>';
    h += '</div>';
    h += '</div>';
    // Extension form groups (e.g. expose, strip)
    _actionEditorExtensions.forEach(function(ext) { if (ext.groups) h += ext.groups(prefix, opts); });
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
    var volGrp = document.getElementById(prefix + '-volume-group');
    if (screenGrp) screenGrp.style.display = (type === 'screen') ? '' : 'none';
    if (mqttGrp) mqttGrp.style.display = (type === 'mqtt') ? '' : 'none';
    if (keyGrp) keyGrp.style.display = (type === 'key') ? '' : 'none';
    if (bleHint) bleHint.style.display = (type === 'key' || type === 'ble_pair') ? '' : 'none';
    if (beepGrp) beepGrp.style.display = (type === 'beep') ? '' : 'none';
    if (soundGrp) soundGrp.style.display = (type === 'sound') ? '' : 'none';
    if (volGrp) volGrp.style.display = (type === 'volume') ? '' : 'none';
    var brightGrp = document.getElementById(prefix + '-brightness-group');
    if (brightGrp) brightGrp.style.display = (type === 'brightness') ? '' : 'none';
    var timerGrp = document.getElementById(prefix + '-timer-group');
    if (timerGrp) timerGrp.style.display = (type === 'timer') ? '' : 'none';
    var notifyGrp = document.getElementById(prefix + '-notify-group');
    if (notifyGrp) notifyGrp.style.display = (type === 'notify') ? '' : 'none';
    if (type === 'notify') actionEditorInitNotifyBindings(prefix);
    if (type === 'timer') actionEditorTimerChanged(prefix);
    // Extension type handlers (e.g. expose, strip)
    _actionEditorExtensions.forEach(function(ext) { if (ext.typeChanged) ext.typeChanged(prefix, type); });
    // Show/hide volume value field depending on mode
    if (type === 'volume') {
        var modeEl = document.getElementById(prefix + '-volume-mode');
        var valGrp = document.getElementById(prefix + '-volume-value-group');
        if (modeEl && valGrp) valGrp.style.display = (modeEl.value === 'set') ? '' : 'none';
    }
    // Show/hide brightness value field and step hint depending on mode
    if (type === 'brightness') {
        var bModeEl = document.getElementById(prefix + '-brightness-mode');
        var bValGrp = document.getElementById(prefix + '-brightness-value-group');
        var bStepHint = document.getElementById(prefix + '-brightness-step-hint');
        if (bModeEl && bValGrp) {
            bValGrp.style.display = '';
            if (bStepHint) bStepHint.style.display = (bModeEl.value !== 'set') ? '' : 'none';
        }
    }
}

// Show/hide timer sub-fields based on the timer action dropdown.
function actionEditorTimerChanged(prefix) {
    var sel = document.getElementById(prefix + '-timer-action');
    if (!sel) return;
    var val = sel.value; // e.g. "1:toggle", "2:adjust"
    var parts = val.split(':');
    var cmd = parts[1] || '';
    var adjustGrp = document.getElementById(prefix + '-timer-adjust-group');
    if (adjustGrp) adjustGrp.style.display = (cmd === 'adjust') ? '' : 'none';
}

// Initialize bindable-color pickers and binding font toggles for notify fields.
// Idempotent — safe to call on every type-change.
function actionEditorInitNotifyBindings(prefix) {
    // Init color pickers
    ['-notify-text-color-wrap', '-notify-bg-color-wrap', '-notify-border-color-wrap'].forEach(function(suffix) {
        var wrap = document.getElementById(prefix + suffix);
        if (wrap) padInitBindableColor(wrap);
    });
    // Wire monospace toggle on binding-capable text inputs
    ['-notify-text', '-notify-duration'].forEach(function(suffix) {
        var el = document.getElementById(prefix + suffix);
        if (el && !el.dataset.bcBind) {
            el.dataset.bcBind = '1';
            el.oninput = function() { padUpdateMixedBindingFont(el); };
            padUpdateMixedBindingFont(el);
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
    el = document.getElementById(prefix + '-sound-file');
    if (el) {
        el.value = action.sound_file || '';
        if (el.selectedIndex < 0) el.value = '';
    }
    el = document.getElementById(prefix + '-sound-volume');
    if (el) el.value = (action.sound_volume > 0) ? action.sound_volume : '';
    el = document.getElementById(prefix + '-volume-mode');
    if (el) el.value = action.volume_mode || 'set';
    el = document.getElementById(prefix + '-volume-value');
    if (el) el.value = (action.volume_value !== undefined && action.volume_value > 0) ? action.volume_value : '';
    el = document.getElementById(prefix + '-brightness-mode');
    if (el) el.value = action.brightness_mode || 'set';
    el = document.getElementById(prefix + '-brightness-value');
    if (el) el.value = (action.brightness_value !== undefined && action.brightness_value > 0) ? action.brightness_value : '';
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
            if (cmd === 'adjust') {
                el = document.getElementById(prefix + '-timer-adjust-sec');
                if (el) el.value = arg;
            }
        }
    } else {
        el = document.getElementById(prefix + '-timer-action');
        if (el) el.value = '1:toggle';
    }
    // Extension load handlers (e.g. expose, strip)
    _actionEditorExtensions.forEach(function(ext) { if (ext.load) ext.load(prefix, action); });
    // Notify fields
    el = document.getElementById(prefix + '-notify-text');
    if (el) { el.value = action.notify_text || ''; padUpdateMixedBindingFont(el); }
    el = document.getElementById(prefix + '-notify-duration');
    if (el) { el.value = action.notify_duration_ms || '3000'; padUpdateMixedBindingFont(el); }
    padSetBindableColor(prefix + '-notify-text-color', action.notify_text_color || '', '#ffffff');
    padSetBindableColor(prefix + '-notify-bg-color', action.notify_bg_color || '', '#333333');
    padSetBindableColor(prefix + '-notify-border-color', action.notify_border_color || '', '');
    el = document.getElementById(prefix + '-notify-opacity');
    if (el) el.value = (action.notify_opacity > 0) ? action.notify_opacity : '';
    el = document.getElementById(prefix + '-notify-font-size');
    if (el) el.value = (action.notify_font_size > 0) ? action.notify_font_size : '';
    el = document.getElementById(prefix + '-notify-location');
    if (el) el.value = action.notify_location || 'bottom';
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
    if (type === 'volume') {
        var vm = document.getElementById(prefix + '-volume-mode');
        if (vm) act.volume_mode = vm.value;
        if (vm && vm.value === 'set') {
            var vv = document.getElementById(prefix + '-volume-value');
            if (vv && vv.value !== '') act.volume_value = parseInt(vv.value, 10);
        }
    }
    if (type === 'brightness') {
        var bm = document.getElementById(prefix + '-brightness-mode');
        if (bm) act.brightness_mode = bm.value;
        var bv = document.getElementById(prefix + '-brightness-value');
        if (bv && bv.value !== '') act.brightness_value = parseInt(bv.value, 10);
    }
    if (type === 'timer') {
        var sel = document.getElementById(prefix + '-timer-action');
        if (sel) {
            var val = sel.value; // e.g. "1:toggle", "2:countdown"
            var parts = val.split(':');
            var cmd = parts[1] || '';
            if (cmd === 'adjust') {
                var adjSec = document.getElementById(prefix + '-timer-adjust-sec');
                var adj = adjSec ? parseInt(adjSec.value, 10) : 0;
                if (adj !== 0 && !isNaN(adj)) val = val + ':' + adj;
            }
            act.timer_command = val;
        }
    }
    // Extension build handlers (e.g. expose, strip)
    _actionEditorExtensions.forEach(function(ext) {
        if (ext.build) {
            var extra = ext.build(prefix, type);
            if (extra) { for (var k in extra) act[k] = extra[k]; }
        }
    });
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
