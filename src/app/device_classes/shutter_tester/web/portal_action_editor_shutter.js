// ============================================================================
// Shutter Tester action editor extension
// ============================================================================
// Registers the "shutter" action type with the shared action editor via the
// _actionEditorExtensions hook array (see portal_action_editor.js). All UI
// code for the shutter command picker lives in this file so non-shutter
// boards never pay the flash cost.
//
// Bundled into the portal.js [chunk:shutter_actions IS_SHUTTER_TESTER] chunk
// from portal.js.bundle, which is itself #if IS_SHUTTER_TESTER guarded in
// web_assets.h — non-shutter boards link a portal.js variant without these
// bytes.

(function() {
    var SPEEDS = ['1s','1/2s','1/4s','1/5s','1/8s','1/10s','1/15s','1/25s','1/30s','1/50s','1/60s','1/100s','1/125s','1/200s','1/250s','1/500s','1/1000s','1/2000s'];

    function options() {
        return '<option value="shutter">Shutter Speed Control</option>';
    }

    function groups(prefix, _opts) {
        var h = '';
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
        // Set: speed picker
        h += '<div class="form-group" id="' + prefix + '-shutter-set-group" style="display:none;">';
        h += '<label class="form-label" for="' + prefix + '-shutter-set-speed">Target Speed</label>';
        h += '<select class="form-select form-select-sm" id="' + prefix + '-shutter-set-speed">';
        SPEEDS.forEach(function(spd) {
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
        // Free-text argument (camera / test_id)
        h += '<div class="form-group" id="' + prefix + '-shutter-arg-group" style="display:none;">';
        h += '<label class="form-label" for="' + prefix + '-shutter-arg" id="' + prefix + '-shutter-arg-label">Argument</label>';
        h += '<input type="text" class="form-control form-control-sm" id="' + prefix + '-shutter-arg" maxlength="63" placeholder="">';
        h += '<small id="' + prefix + '-shutter-arg-hint"></small>';
        h += '</div>';
        h += '</div>';
        return h;
    }

    function typeChanged(prefix, type) {
        var shutterGrp = document.getElementById(prefix + '-shutter-group');
        if (shutterGrp) shutterGrp.style.display = (type === 'shutter') ? '' : 'none';
        if (type === 'shutter') actionEditorShutterChanged(prefix);
    }

    function load(prefix, action) {
        if (action.type === 'shutter') {
            var sc = document.getElementById(prefix + '-shutter-command');
            if (sc) sc.value = action.shutter_command || 'toggle_lock';
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
            var scd = document.getElementById(prefix + '-shutter-command');
            if (scd) scd.value = 'toggle_lock';
        }
    }

    function build(prefix, type) {
        if (type !== 'shutter') return null;
        var extra = {};
        var sc = document.getElementById(prefix + '-shutter-command');
        if (!sc) return null;
        extra.shutter_command = sc.value || 'toggle_lock';
        if (extra.shutter_command === 'set') {
            var ss = document.getElementById(prefix + '-shutter-set-speed');
            if (ss) extra.shutter_value = ss.value || '';
        } else if (extra.shutter_command === 'adjust') {
            var sd = document.getElementById(prefix + '-shutter-adjust-dir');
            if (sd) extra.shutter_value = sd.value || 'faster';
        } else if (extra.shutter_command === 'sess_start' ||
                   extra.shutter_command === 'sess_toggle' ||
                   extra.shutter_command === 'guide_start') {
            var sa = document.getElementById(prefix + '-shutter-arg');
            if (sa && sa.value) extra.shutter_value = sa.value.trim();
        }
        return extra;
    }

    if (typeof _actionEditorExtensions !== 'undefined') {
        _actionEditorExtensions.push({
            options: options,
            groups: groups,
            typeChanged: typeChanged,
            load: load,
            build: build,
        });
    }
})();

// Exposed globally for inline onchange="actionEditorShutterChanged(...)" handlers
// rendered into the shutter command <select> above.
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
