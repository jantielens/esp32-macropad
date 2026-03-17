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
    if (screenGrp) screenGrp.style.display = (type === 'screen') ? '' : 'none';
    if (mqttGrp) mqttGrp.style.display = (type === 'mqtt') ? '' : 'none';
    if (keyGrp) keyGrp.style.display = (type === 'key') ? '' : 'none';
    if (bleHint) bleHint.style.display = (type === 'key' || type === 'ble_pair') ? '' : 'none';
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
