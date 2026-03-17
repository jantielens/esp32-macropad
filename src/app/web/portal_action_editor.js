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
    // Show/hide volume value field depending on mode
    if (type === 'volume') {
        var modeEl = document.getElementById(prefix + '-volume-mode');
        var valGrp = document.getElementById(prefix + '-volume-value-group');
        if (modeEl && valGrp) valGrp.style.display = (modeEl.value === 'set') ? '' : 'none';
    }
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
