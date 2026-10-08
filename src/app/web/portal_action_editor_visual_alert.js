// Show/hide the visual-alert config fields based on the op dropdown.
// Stop only needs the op selector; start needs color/pattern/period/etc.
function actionEditorVaOpChanged(prefix) {
    var op = document.getElementById(prefix + '-va-op');
    var cfg = document.getElementById(prefix + '-va-config-group');
    if (cfg) cfg.style.display = (op && op.value === 'stop') ? 'none' : '';
}

_actionEditorExtensions.push({
    type: 'visual_alert',
    colorSuffixes: ["-va-color-wrap"],
    groups: function(prefix, opts) {
        var h = '';
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
        return h;
    },
    typeChanged: function(prefix, type) {
        var group = document.getElementById(prefix + '-va-group');
        if (group) group.style.display = type === 'visual_alert' ? '' : 'none';
        if (type === 'visual_alert') {
            var vaCol = document.getElementById(prefix + '-va-color');
            if (vaCol && !vaCol.value) padSetBindableColor(prefix + '-va-color', '#ff0000', '#ff0000');
            actionEditorVaOpChanged(prefix);
        }
    },
    load: function(prefix, action) {
        var el;
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
    },
    build: function(prefix, type) {
        if (type !== 'visual_alert') return null;
        var act = {};
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
        return act;
    }
});
