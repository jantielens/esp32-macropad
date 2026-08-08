// ============================================================================
// Shared Action Editor — reusable UI component for ButtonAction editing
// ============================================================================
// Both the pad button editor (pads.html) and swipe action editor (home.html)
// use the same action types (screen, back, mqtt, key, ble_pair). This module
// provides shared HTML generation, load/save, and type-change handlers so that
// adding a new action type requires exactly one code change.

// Extension modules (e.g. portal_action_editor_shutter.js) can register into
// _actionEditorExtensions to add action types without modifying this file.
// Each extension provides: groups, typeChanged, load, build hooks. Type-select
// options are no longer contributed by extensions — action_catalog.cpp (see
// GET /api/info?catalog=1) is the single source for every type's group and
// label, built-in or device-class.
var _actionEditorExtensions = [];

// Shared fixed slot count for every action-array host (pad tap/long-press,
// pad-level, boot, hardware button, MQTT trigger, timer expiry, Shutter
// Tester session actions). Must match MAX_BUTTON_ACTIONS in pad_config.h.
const MAX_ACTIONS = 3;

// 1-based slot-id generator for hosts whose ids follow "<base><1..MAX_ACTIONS>"
// (boot actions, hardware buttons, MQTT triggers, timer expiry, Shutter
// Tester session actions). The pad editor uses its own 0-based prefixes
// (padActionPrefixes in portal_pad_editor.js) since its ids predate this.
function actionEditorSlotPrefixes(base) {
    var out = [];
    for (var i = 1; i <= MAX_ACTIONS; i++) out.push(base + i);
    return out;
}

// Presentation-only lookups into the firmware-authored catalog cached on
// deviceInfoCache.catalog. Every host awaits getDeviceInfo() before calling
// actionEditorHTML(), so the catalog is always populated by the time these
// run — no fetch here, no fallback table, no later refresh pass.
function actionEditorCatalog() {
    return (typeof deviceInfoCache !== 'undefined' && deviceInfoCache && deviceInfoCache.catalog) || [];
}

function actionEditorCatalogEntry(type) {
    var catalog = actionEditorCatalog();
    for (var i = 0; i < catalog.length; i++) {
        if (catalog[i].type === type) return catalog[i];
    }
    return null;
}

// Grouped <optgroup> markup for the type <select>, in catalog order.
function actionEditorTypeOptionsHTML() {
    var order = [];
    var byGroup = {};
    actionEditorCatalog().forEach(function(entry) {
        if (!byGroup[entry.group]) { byGroup[entry.group] = []; order.push(entry.group); }
        byGroup[entry.group].push(entry);
    });
    var html = '<option value="">(none)</option>';
    order.forEach(function(group) {
        html += '<optgroup label="' + group + '">';
        byGroup[group].forEach(function(entry) {
            html += '<option value="' + entry.type + '">' + entry.label + '</option>';
        });
        html += '</optgroup>';
    });
    return html;
}

// <option> tags for a multi-command type's Command selector. '' for a
// direct action (no commands) or a type absent from this build's catalog.
function actionEditorCommandOptionsHTML(type) {
    var entry = actionEditorCatalogEntry(type);
    if (!entry || !entry.commands) return '';
    return entry.commands.map(function(c) {
        return '<option value="' + c.id + '">' + c.label + '</option>';
    }).join('');
}

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

// Family-then-command types (currently only Shutter Tester).
function actionEditorFamilyOptionsHTML(type) {
    var entry = actionEditorCatalogEntry(type);
    if (!entry || !entry.command_families) return '';
    return entry.command_families.map(function(f) {
        return '<option value="' + f.id + '">' + f.label + '</option>';
    }).join('');
}

function actionEditorFamilyCommandOptionsHTML(type, familyId) {
    var entry = actionEditorCatalogEntry(type);
    if (!entry || !entry.command_families) return '';
    var family = entry.command_families.filter(function(f) { return f.id === familyId; })[0];
    if (!family || !family.commands) return '';
    return family.commands.map(function(c) {
        return '<option value="' + c.id + '">' + c.label + '</option>';
    }).join('');
}

function actionEditorFamilyForCommand(type, commandId) {
    var entry = actionEditorCatalogEntry(type);
    if (!entry || !entry.command_families) return null;
    var match = entry.command_families.filter(function(f) {
        return (f.commands || []).some(function(c) { return c.id === commandId; });
    })[0];
    return match ? match.id : null;
}

// Preserves a persisted action whose type this build's catalog does not
// contain, keyed by prefix, so a save round-trips it untouched instead of
// silently reducing it to a bare {type}. Cleared once the user picks a
// different type for that slot.
var _actionEditorUnsupported = {};

// Give the <select> a disabled placeholder option for an unsupported type so
// el.value = type actually sticks (a <select> silently ignores an unknown value).
function actionEditorEnsureUnsupportedOption(select, type) {
    if (!select || !type) return;
    for (var i = 0; i < select.options.length; i++) {
        if (select.options[i].value === type) return;
    }
    var opt = document.createElement('option');
    opt.value = type;
    opt.textContent = type + ' (unsupported by this build)';
    opt.disabled = true;
    select.appendChild(opt);
}

// Generate the HTML for one action editor instance.
// prefix: unique ID prefix (e.g. "pad-edit-action", "swipe-right")
// label:  optional label shown above the type dropdown (e.g. "Tap Action")
// opts:   { showBleHint: bool, showKeyHelp: bool }
function actionEditorHTML(prefix, label, opts) {
    opts = opts || {};
    var h = '';
    h += '<div class="form-group">';
    if (label) h += '<label class="form-label" for="' + prefix + '-type">' + label + '</label>';
    h += '<select class="form-select form-select-sm action-type-select" id="' + prefix + '-type" onchange="actionEditorTypeChanged(\'' + prefix + '\')">';
    h += actionEditorTypeOptionsHTML();
    h += '</select>';
    h += '<small id="' + prefix + '-context" class="action-context" style="display:none; color:#86868b;"></small>';
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
    // Pad sequence navigation
    h += '<div id="' + prefix + '-cycle-pad-group" style="display:none;">';
    h += '<div class="form-group">';
    h += '<label class="form-label" for="' + prefix + '-cycle-pad-direction">Direction</label>';
    h += '<select class="form-select form-select-sm" id="' + prefix + '-cycle-pad-direction">';
    h += '<option value="next">Next</option><option value="previous">Previous</option>';
    h += '</select></div>';
    h += '<div class="form-group">';
    h += '<label><input type="checkbox" id="' + prefix + '-cycle-pad-wrap" checked> Wrap at first or last pad</label>';
    h += '</div>';
    h += '<div class="form-group">';
    h += '<label class="form-label" for="' + prefix + '-cycle-pad-exclusions">Excluded Pads</label>';
    h += '<input type="text" class="form-control form-control-sm" id="' + prefix + '-cycle-pad-exclusions" placeholder="e.g. 2, 5, 8">';
    h += '<small>Optional comma-separated 1-based pad numbers.</small>';
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
    // Music
    h += '<div id="' + prefix + '-music-group" style="display:none;">';
    h += '<div class="form-group">';
    h += '<label class="form-label" for="' + prefix + '-music-command">Command</label>';
    h += '<select class="form-select form-select-sm" id="' + prefix + '-music-command">';
    h += actionEditorCommandOptionsHTML('music');
    h += '</select></div></div>';
    // Sound Alert
    h += '<div id="' + prefix + '-sound-alert-group" style="display:none;">';
    h += '<div class="form-group"><label class="form-label" for="' + prefix + '-sound-alert-kind">Kind</label>';
    h += '<select class="form-select form-select-sm" id="' + prefix + '-sound-alert-kind" onchange="actionEditorSoundAlertChanged(\'' + prefix + '\')">';
    h += '<option value="tone">Tone Alert</option><option value="mp3">MP3 Alert</option></select></div>';
    h += '<div id="' + prefix + '-sound-alert-tone-group">';
    h += '<div class="form-group"><label class="form-label" for="' + prefix + '-sound-alert-pattern">Tone Pattern <span class="fx-hint" onclick="showBindingHelp()">fx</span></label>';
    h += '<input type="text" class="form-control form-control-sm" id="' + prefix + '-sound-alert-pattern" maxlength="127" placeholder="e.g. 1000:200 100 1000:200"></div></div>';
    h += '<div id="' + prefix + '-sound-alert-mp3-group" style="display:none;">';
    h += '<div class="form-group">';
    h += '<label class="form-label" for="' + prefix + '-sound-alert-file">MP3 Alert File</label>';
    h += '<select class="form-select form-select-sm" id="' + prefix + '-sound-alert-file"><option value="">(none)</option></select>';
    h += '</div>';
    h += '<div class="form-group">';
    h += '<label class="form-label" for="' + prefix + '-sound-alert-volume">Volume Override (%)</label>';
    h += '<input type="number" class="form-control form-control-sm" id="' + prefix + '-sound-alert-volume" min="0" max="100" placeholder="(use device volume)">';
    h += '<small>Optional. If empty, uses the device volume from Home &rarr; Audio.</small>';
    h += '</div></div></div>';
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
    h += '<small>Positive whole seconds. Supports bindings.</small>';
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
    // Visual Alert
    h += '<div id="' + prefix + '-va-group" style="display:none;">';
    h += '<div class="form-group">';
    h += '<label class="form-label" for="' + prefix + '-va-op">Command</label>';
    h += '<select class="form-select form-select-sm" id="' + prefix + '-va-op" onchange="actionEditorVaOpChanged(\'' + prefix + '\')">';
    h += actionEditorCommandOptionsHTML('visual_alert');
    h += '</select>';
    h += '<small>Start raises a full-screen pulsing overlay (wakes the screen). Stop clears it.</small>';
    h += '</div>';
    // Config fields — only relevant for "start" (hidden for "stop")
    h += '<div id="' + prefix + '-va-config-group">';
    h += '<div class="form-group">';
    h += '<label style="font-size:13px; font-weight:600; margin-bottom:2px; display:block;">Color <span class="fx-hint" onclick="showBindingHelp()">fx</span></label>';
    h += '<div class="bindable-color" id="' + prefix + '-va-color-wrap"><div class="bc-swatch"></div>';
    h += '<input type="text" id="' + prefix + '-va-color" class="bc-input" maxlength="63" spellcheck="false" placeholder="#ff0000 or [binding]"></div>';
    h += '<small>Overlay tint. Supports bindings (e.g. red via [expr:...]). Empty = red.</small>';
    h += '</div>';
    h += '<div class="form-group">';
    h += '<label class="form-label" for="' + prefix + '-va-pattern">Pattern</label>';
    h += '<select class="form-select form-select-sm" id="' + prefix + '-va-pattern">';
    h += '<option value="breathe" selected>Breathe</option>';
    h += '<option value="blink">Blink</option>';
    h += '<option value="solid">Solid</option>';
    h += '</select>';
    h += '</div>';
    h += '<div class="grid-2col">';
    h += '<div class="form-group">';
    h += '<label class="form-label" for="' + prefix + '-va-period">Period (ms)</label>';
    h += '<input type="number" class="form-control form-control-sm" id="' + prefix + '-va-period" min="0" max="10000" placeholder="800">';
    h += '</div>';
    h += '<div class="form-group">';
    h += '<label class="form-label" for="' + prefix + '-va-intensity">Intensity (%)</label>';
    h += '<input type="number" class="form-control form-control-sm" id="' + prefix + '-va-intensity" min="0" max="100" placeholder="100">';
    h += '</div>';
    h += '</div>';
    h += '<div class="form-group">';
    h += '<label class="form-label" for="' + prefix + '-va-duration">Duration (ms)</label>';
    h += '<input type="number" class="form-control form-control-sm" id="' + prefix + '-va-duration" min="0" placeholder="0 = until stopped">';
    h += '<small>0 = persist until Stop, tap, or another alert. Tap the overlay to dismiss.</small>';
    h += '</div>';
    h += '</div>';  // va-config-group
    h += '</div>';  // va-group
    // Device command (reboot / Wi-Fi / screensaver)
    h += '<div id="' + prefix + '-system-group" style="display:none;">';
    h += '<div class="form-group">';
    h += '<label class="form-label" for="' + prefix + '-system-command">Command</label>';
    h += '<select class="form-select form-select-sm" id="' + prefix + '-system-command">';
    h += actionEditorCommandOptionsHTML('system');
    h += '</select>';
    h += '</div></div>';
    // Volume
    h += '<div id="' + prefix + '-volume-group" style="display:none;">';
    h += '<div class="form-group">';
    h += '<label class="form-label" for="' + prefix + '-volume-command">Command</label>';
    h += '<select class="form-select form-select-sm" id="' + prefix + '-volume-command" onchange="actionEditorVolumeBrightnessChanged(\'' + prefix + '\', \'volume\')">';
    h += actionEditorCommandOptionsHTML('volume');
    h += '</select>';
    h += '</div>';
    h += '<div class="form-group" id="' + prefix + '-volume-set-group" style="display:none;">';
    h += '<label class="form-label" for="' + prefix + '-volume-set-value">Volume (%)</label>';
    h += '<input type="number" class="form-control form-control-sm" id="' + prefix + '-volume-set-value" min="0" max="100" placeholder="e.g. 50">';
    h += '</div>';
    h += '<div class="form-group" id="' + prefix + '-volume-adjust-group" style="display:none;">';
    h += '<label class="form-label" for="' + prefix + '-volume-adjust-value">Adjust Volume (%)</label>';
    h += '<input type="text" class="form-control form-control-sm" id="' + prefix + '-volume-adjust-value" placeholder="e.g. 10, -10, or {step}">';
    h += '<small>Positive increases, negative decreases. Use <code>{step}</code> as a placeholder for Numeric Rocker widgets.</small>';
    h += '</div>';
    h += '</div>';
    // Brightness
    h += '<div id="' + prefix + '-brightness-group" style="display:none;">';
    h += '<div class="form-group">';
    h += '<label class="form-label" for="' + prefix + '-brightness-command">Command</label>';
    h += '<select class="form-select form-select-sm" id="' + prefix + '-brightness-command" onchange="actionEditorVolumeBrightnessChanged(\'' + prefix + '\', \'brightness\')">';
    h += actionEditorCommandOptionsHTML('brightness');
    h += '</select>';
    h += '</div>';
    h += '<div class="form-group" id="' + prefix + '-brightness-set-group" style="display:none;">';
    h += '<label class="form-label" for="' + prefix + '-brightness-set-value">Brightness (%)</label>';
    h += '<input type="number" class="form-control form-control-sm" id="' + prefix + '-brightness-set-value" min="5" max="100" placeholder="e.g. 50">';
    h += '</div>';
    h += '<div class="form-group" id="' + prefix + '-brightness-adjust-group" style="display:none;">';
    h += '<label class="form-label" for="' + prefix + '-brightness-adjust-value">Adjust Brightness (%)</label>';
    h += '<input type="text" class="form-control form-control-sm" id="' + prefix + '-brightness-adjust-value" placeholder="e.g. 10, -10, or {step}">';
    h += '<small>Positive increases, negative decreases. Use <code>{step}</code> as a placeholder for Numeric Rocker widgets.</small>';
    h += '</div>';
    h += '</div>';
    // Extension-contributed groups (e.g. shutter command UI on shutter-tester builds)
    _actionEditorExtensions.forEach(function(ext) { if (ext.groups) h += ext.groups(prefix, opts); });
    return h;
}

// Show/hide sub-groups when the action type dropdown changes.
function actionEditorTypeChanged(prefix) {
    var typeEl = document.getElementById(prefix + '-type');
    if (!typeEl) return;
    var type = typeEl.value;
    if (_actionEditorUnsupported[prefix] && _actionEditorUnsupported[prefix].type !== type) {
        delete _actionEditorUnsupported[prefix];
    }
    var contextEl = document.getElementById(prefix + '-context');
    if (contextEl) {
        var entry = actionEditorCatalogEntry(type);
        contextEl.textContent = entry ? (entry.group + ' / ' + entry.label) : '';
        contextEl.style.display = entry ? '' : 'none';
    }
    actionEditorListRefreshSlot(prefix);
    var screenGrp = document.getElementById(prefix + '-screen-group');
    var mqttGrp = document.getElementById(prefix + '-mqtt-group');
    var keyGrp = document.getElementById(prefix + '-key-group');
    var bleHint = document.getElementById(prefix + '-ble-hint');
    var musicGrp = document.getElementById(prefix + '-music-group');
    var soundAlertGrp = document.getElementById(prefix + '-sound-alert-group');
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
    if (musicGrp) musicGrp.style.display = (type === 'music') ? '' : 'none';
    if (soundAlertGrp) soundAlertGrp.style.display = (type === 'sound_alert') ? '' : 'none';
    if (type === 'sound_alert') actionEditorSoundAlertChanged(prefix);
    var cyclePadGrp = document.getElementById(prefix + '-cycle-pad-group');
    if (cyclePadGrp) cyclePadGrp.style.display = (type === 'cycle_pad') ? '' : 'none';
    var timerGrp = document.getElementById(prefix + '-timer-group');
    if (timerGrp) timerGrp.style.display = (type === 'timer') ? '' : 'none';
    var notifyGrp = document.getElementById(prefix + '-notify-group');
    if (notifyGrp) notifyGrp.style.display = (type === 'notify') ? '' : 'none';
    var vaGrp = document.getElementById(prefix + '-va-group');
    if (vaGrp) vaGrp.style.display = (type === 'visual_alert') ? '' : 'none';
    if (type === 'visual_alert') {
        // Default the color to pure red when unset, so the swatch shows red.
        var vaCol = document.getElementById(prefix + '-va-color');
        if (vaCol && !vaCol.value) padSetBindableColor(prefix + '-va-color', '#ff0000', '#ff0000');
        actionEditorVaOpChanged(prefix);
    }
    var systemGrp = document.getElementById(prefix + '-system-group');
    if (systemGrp) systemGrp.style.display = (type === 'system') ? '' : 'none';
    var volumeGrp = document.getElementById(prefix + '-volume-group');
    if (volumeGrp) volumeGrp.style.display = (type === 'volume') ? '' : 'none';
    var brightnessGrp = document.getElementById(prefix + '-brightness-group');
    if (brightnessGrp) brightnessGrp.style.display = (type === 'brightness') ? '' : 'none';
    if (['notify', 'visual_alert', 'mqtt', 'key', 'sound_alert', 'timer'].indexOf(type) >= 0) actionEditorInitBindings(prefix);
    if (type === 'timer') actionEditorTimerChanged(prefix);
    if (type === 'volume') actionEditorVolumeBrightnessChanged(prefix, 'volume');
    if (type === 'brightness') actionEditorVolumeBrightnessChanged(prefix, 'brightness');
    // Extension-contributed type-change hooks (e.g. shutter group visibility)
    _actionEditorExtensions.forEach(function(ext) { if (ext.typeChanged) ext.typeChanged(prefix, type); });
}

function actionEditorNormalizeCyclePadExclusions(value) {
    var configuredMax = (typeof deviceInfoCache !== 'undefined' && deviceInfoCache)
        ? Number(deviceInfoCache.max_pads) : 0;
    var maxPads = configuredMax > 0 ? Math.floor(configuredMax) : 16;
    var unique = {};
    String(value || '').split(',').forEach(function(token) {
        token = token.trim();
        if (!/^[0-9]+$/.test(token)) return;
        var pad = Number(token);
        if (pad >= 1 && pad <= maxPads) unique[pad] = true;
    });
    return Object.keys(unique).map(Number).sort(function(a, b) { return a - b; }).join(',');
}

function actionEditorSoundAlertChanged(prefix) {
    var kind = document.getElementById(prefix + '-sound-alert-kind');
    var tone = document.getElementById(prefix + '-sound-alert-tone-group');
    var mp3 = document.getElementById(prefix + '-sound-alert-mp3-group');
    var isMp3 = kind && kind.value === 'mp3';
    if (tone) tone.style.display = isMp3 ? 'none' : '';
    if (mp3) mp3.style.display = isMp3 ? '' : 'none';
}

// Show/hide system command sub-fields based on the command dropdown.
// Show/hide the volume/brightness set-vs-adjust value field for the given kind.
function actionEditorVolumeBrightnessChanged(prefix, kind) {
    var sel = document.getElementById(prefix + '-' + kind + '-command');
    if (!sel) return;
    var cmd = sel.value;
    var setGrp = document.getElementById(prefix + '-' + kind + '-set-group');
    var adjustGrp = document.getElementById(prefix + '-' + kind + '-adjust-group');
    if (setGrp) setGrp.style.display = (cmd === 'set') ? '' : 'none';
    if (adjustGrp) adjustGrp.style.display = (cmd === 'adjust') ? '' : 'none';
}

// Show/hide the visual-alert config fields based on the op dropdown.
// Stop only needs the op selector; start needs color/pattern/period/etc.
function actionEditorVaOpChanged(prefix) {
    var op = document.getElementById(prefix + '-va-op');
    var cfg = document.getElementById(prefix + '-va-config-group');
    if (cfg) cfg.style.display = (op && op.value === 'stop') ? 'none' : '';
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

// Suffixes for binding-capable action text inputs (shared with binding validator).
var _ACTION_BIND_SUFFIXES = [
    '-notify-text', '-notify-duration', '-topic', '-payload', '-sequence',
    '-sound-alert-pattern', '-timer-duration', '-timer-set-sec', '-timer-adjust-sec'
];

// Initialize bindable-color pickers and binding font toggles for all bindable fields.
// Idempotent — safe to call on every type-change.
function actionEditorInitBindings(prefix) {
    // Init action color pickers (notify + visual alert)
    ['-notify-text-color-wrap', '-notify-bg-color-wrap', '-notify-border-color-wrap', '-va-color-wrap'].forEach(function(suffix) {
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
    if (el) {
        el.value = action.type || '';
        if (action.type && !actionEditorCatalogEntry(action.type)) {
            actionEditorEnsureUnsupportedOption(el, action.type);
            el.value = action.type;
            _actionEditorUnsupported[prefix] = action;
        } else {
            delete _actionEditorUnsupported[prefix];
        }
    }
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
    el = document.getElementById(prefix + '-music-command');
    if (el) el.value = action.music_command || 'play_pause';
    el = document.getElementById(prefix + '-sound-alert-kind');
    if (el) el.value = action.sound_alert_kind || 'tone';
    el = document.getElementById(prefix + '-sound-alert-pattern');
    if (el) el.value = action.sound_alert_pattern || '';
    el = document.getElementById(prefix + '-sound-alert-file');
    if (el) {
        el.value = action.sound_alert_file || '';
        if (el.selectedIndex < 0) el.value = '';
    }
    el = document.getElementById(prefix + '-sound-alert-volume');
    if (el) el.value = (action.sound_alert_volume > 0) ? action.sound_alert_volume : '';
    el = document.getElementById(prefix + '-cycle-pad-direction');
    if (el) el.value = action.direction === 'previous' ? 'previous' : 'next';
    el = document.getElementById(prefix + '-cycle-pad-wrap');
    if (el) el.checked = action.wrap !== false;
    el = document.getElementById(prefix + '-cycle-pad-exclusions');
    if (el) el.value = actionEditorNormalizeCyclePadExclusions(action.excluded_pads || '');

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
    // Visual alert fields
    el = document.getElementById(prefix + '-va-op');
    if (el) el.value = action.op || 'start';
    padSetBindableColor(prefix + '-va-color', action.color || '', '#ff0000');
    el = document.getElementById(prefix + '-va-pattern');
    if (el) el.value = action.pattern || 'breathe';
    el = document.getElementById(prefix + '-va-period');
    if (el) el.value = (action.period_ms > 0) ? action.period_ms : '';
    el = document.getElementById(prefix + '-va-intensity');
    if (el) el.value = (action.intensity > 0) ? action.intensity : '';
    el = document.getElementById(prefix + '-va-duration');
    if (el) el.value = (action.duration_ms > 0) ? action.duration_ms : '';
    // Device command (reboot / Wi-Fi / screensaver)
    el = document.getElementById(prefix + '-system-command');
    if (el) el.value = action.system_command || 'reboot';
    // Volume / Brightness — first-class types, each with its own Command
    // (set/adjust) select and set/adjust value inputs.
    el = document.getElementById(prefix + '-volume-command');
    if (el) el.value = action.volume_mode || 'set';
    el = document.getElementById(prefix + '-volume-set-value');
    if (el) el.value = (action.type === 'volume' && action.volume_mode !== 'adjust') ? (action.volume_value || '') : '';
    el = document.getElementById(prefix + '-volume-adjust-value');
    if (el) el.value = (action.type === 'volume' && action.volume_mode === 'adjust') ? (action.volume_value || '') : '';
    el = document.getElementById(prefix + '-brightness-command');
    if (el) el.value = action.brightness_mode || 'set';
    el = document.getElementById(prefix + '-brightness-set-value');
    if (el) el.value = (action.type === 'brightness' && action.brightness_mode !== 'adjust') ? (action.brightness_value || '') : '';
    el = document.getElementById(prefix + '-brightness-adjust-value');
    if (el) el.value = (action.type === 'brightness' && action.brightness_mode === 'adjust') ? (action.brightness_value || '') : '';
    // Extension-contributed load hooks (e.g. shutter field population)
    _actionEditorExtensions.forEach(function(ext) { if (ext.load) ext.load(prefix, action); });
    actionEditorTypeChanged(prefix);
}

// Build an action object from the form. Returns {} if type is empty.
function actionEditorBuild(prefix) {
    var typeEl = document.getElementById(prefix + '-type');
    if (!typeEl) return {};
    var type = typeEl.value;
    if (!type) return {};
    // Round-trip a persisted action whose type this build's catalog does not
    // contain — its fields have no editor, so save it back exactly as loaded.
    if (_actionEditorUnsupported[prefix] && _actionEditorUnsupported[prefix].type === type) {
        return _actionEditorUnsupported[prefix];
    }
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
    if (type === 'music') {
        var musicCommand = document.getElementById(prefix + '-music-command');
        if (musicCommand) act.music_command = musicCommand.value;
    }
    if (type === 'sound_alert') {
        var kind = document.getElementById(prefix + '-sound-alert-kind');
        act.sound_alert_kind = kind ? kind.value : 'tone';
        var volume = document.getElementById(prefix + '-sound-alert-volume');
        if (volume && volume.value !== '') act.sound_alert_volume = parseInt(volume.value, 10);
        if (act.sound_alert_kind === 'tone') {
            var pattern = document.getElementById(prefix + '-sound-alert-pattern');
            if (pattern) act.sound_alert_pattern = (pattern.value || '').trim();
        } else {
            var file = document.getElementById(prefix + '-sound-alert-file');
            if (file) act.sound_alert_file = file.value || '';
        }
    }
    if (type === 'cycle_pad') {
        var cycleDirection = document.getElementById(prefix + '-cycle-pad-direction');
        var cycleWrap = document.getElementById(prefix + '-cycle-pad-wrap');
        var cycleExclusions = document.getElementById(prefix + '-cycle-pad-exclusions');
        act.direction = cycleDirection && cycleDirection.value === 'previous' ? 'previous' : 'next';
        act.wrap = cycleWrap ? cycleWrap.checked : true;
        var normalizedExclusions = actionEditorNormalizeCyclePadExclusions(
            cycleExclusions ? cycleExclusions.value : '');
        if (cycleExclusions) cycleExclusions.value = normalizedExclusions;
        if (normalizedExclusions) act.excluded_pads = normalizedExclusions;
    }

    if (type === 'timer') {
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
    if (type === 'visual_alert') {
        var vaOp = document.getElementById(prefix + '-va-op');
        if (vaOp) act.op = vaOp.value;
        var vaCol = padGetBindableColor(prefix + '-va-color');
        if (vaCol) act.color = vaCol;
        var vaPat = document.getElementById(prefix + '-va-pattern');
        if (vaPat) act.pattern = vaPat.value;
        var vaPer = document.getElementById(prefix + '-va-period');
        if (vaPer && vaPer.value !== '') act.period_ms = parseInt(vaPer.value, 10);
        var vaInt = document.getElementById(prefix + '-va-intensity');
        if (vaInt && vaInt.value !== '') act.intensity = parseInt(vaInt.value, 10);
        var vaDur = document.getElementById(prefix + '-va-duration');
        if (vaDur && vaDur.value !== '') act.duration_ms = parseInt(vaDur.value, 10);
    }
    if (type === 'system') {
        var sc = document.getElementById(prefix + '-system-command');
        if (sc) act.system_command = sc.value;
    }
    if (type === 'volume' || type === 'brightness') {
        var vbCmd = document.getElementById(prefix + '-' + type + '-command');
        var mode = vbCmd ? vbCmd.value : 'set';
        act[type + '_mode'] = mode;
        var vbInput = document.getElementById(prefix + '-' + type + '-' + mode + '-value');
        if (vbInput && vbInput.value !== '') act[type + '_value'] = (vbInput.value || '').trim();
    }
    // Extension-contributed build hooks (e.g. shutter merges shutter_command/shutter_value).
    _actionEditorExtensions.forEach(function(ext) {
        if (ext.build) {
            var extra = ext.build(prefix, type);
            if (extra) { for (var k in extra) act[k] = extra[k]; }
        }
    });
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
        var sel = document.getElementById(prefix + '-sound-alert-file');
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
// Every action array uses this same fixed-slot pattern: N ordered positions,
// an unused slot collapsed as "Add action", and no drag/reorder — slot order
// is execution order. Default slot labels are "Action 1".."Action N"; a host
// whose slots carry distinct meaning (rocker zones, list selection) supplies
// its own via the labels argument or actionEditorListSetLabels().

// Render N fixed action slots inside containerId, one per prefix in prefixes[].
// labels[i] overrides the default "Action N" slot label. opts.actionOptions is
// forwarded to actionEditorHTML for every slot (e.g. { showBleHint: true }).
function actionEditorListRender(containerId, prefixes, labels, opts) {
    opts = opts || {};
    var container = document.getElementById(containerId);
    if (!container) return;
    var html = '';
    prefixes.forEach(function(prefix, i) {
        var label = (labels && labels[i]) || ('Action ' + (i + 1));
        html += '<details class="editor-group action-list-slot" id="' + prefix + '-group" data-slot-label="' + label + '">';
        html += '<summary>' + actionEditorSlotAddLabel(label) + '</summary>';
        html += '<div class="editor-group-body">';
        html += actionEditorHTML(prefix, '', opts.actionOptions);
        html += '</div></details>';
    });
    container.innerHTML = html;
}

// Derives the collapsed-slot placeholder from a slot label by stripping its
// trailing slot number and prefixing "Add " (e.g. "Tap action 1" -> "Add tap
// action", "Action 1" -> "Add action"), so slots in different sections read
// distinctly even before the user notices which section they're in.
function actionEditorSlotAddLabel(label) {
    var base = String(label || 'Action').replace(/\s+\d+$/, '');
    return 'Add ' + base.charAt(0).toLowerCase() + base.slice(1);
}

// Update slot labels in place (e.g. when a widget's axis/type changes the
// meaning of its slots) without rebuilding already-rendered editor markup.
// A slot currently showing its "Add ..." placeholder stays that way until
// populated; a populated slot's visible summary updates immediately.
function actionEditorListSetLabels(prefixes, labels) {
    prefixes.forEach(function(prefix, i) {
        var group = document.getElementById(prefix + '-group');
        if (!group) return;
        var label = (labels && labels[i]) || ('Action ' + (i + 1));
        group.dataset.slotLabel = label;
        var typeEl = document.getElementById(prefix + '-type');
        var summary = group.querySelector('summary');
        if (summary) summary.textContent = (typeEl && typeEl.value) ? label : actionEditorSlotAddLabel(label);
    });
}

// Sync one slot's collapsed state and summary text to whether it currently
// has an action type selected. Called from actionEditorTypeChanged() so it
// runs on load and on every user-driven type change; a no-op for editors that
// aren't inside an actionEditorListRender() slot.
function actionEditorListRefreshSlot(prefix) {
    var group = document.getElementById(prefix + '-group');
    if (!group || !group.classList.contains('action-list-slot')) return;
    var typeEl = document.getElementById(prefix + '-type');
    var hasAction = !!(typeEl && typeEl.value);
    var summary = group.querySelector('summary');
    if (summary) summary.textContent = hasAction ? group.dataset.slotLabel : actionEditorSlotAddLabel(group.dataset.slotLabel);
    if (hasAction) group.open = true;
}

// Load an array of action objects into N editor prefixes (positional).
function actionEditorListLoad(prefixes, actions) {
    actions = actions || [];
    prefixes.forEach(function(prefix, i) {
        actionEditorLoad(prefix, actions[i] || {});
    });
}

// Build an array of action objects from N editor prefixes. Empty slots are
// omitted; non-empty actions keep their configured order.
function actionEditorListBuild(prefixes) {
    return prefixes.map(function(prefix) { return actionEditorBuild(prefix); })
        .filter(function(action) { return !!action.type; });
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
