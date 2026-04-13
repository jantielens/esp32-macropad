// ============================================================================
// Darkroom Action Editor Extension
// ============================================================================
// Extends portal_action_editor.js with Exposure Timer and Test Strip actions.
// Self-registers into the action editor extension system on load.
// No-ops gracefully if the extension system is absent.

(function() {
    if (typeof _actionEditorExtensions === 'undefined') return;

    _actionEditorExtensions.push({
        // Extra <option> tags for the type dropdown
        options: function() {
            return '<option value="expose">Exposure Timer</option>'
                 + '<option value="strip">Test Strip</option>';
        },

        // Extra form group HTML
        groups: function(prefix) {
            var h = '';
            // Expose timer control
            h += '<div id="' + prefix + '-expose-group" style="display:none;">';
            h += '<div class="form-group">';
            h += '<label for="' + prefix + '-expose-action">Exposure Timer Action</label>';
            h += '<select id="' + prefix + '-expose-action" onchange="_actionEditorDarkroomExposeChanged(\'' + prefix + '\')">';
            h += '<option value="toggle">Toggle (Start/Pause/Resume)</option>';
            h += '<option value="start">Start</option>';
            h += '<option value="stop">Stop</option>';
            h += '<option value="pause">Pause</option>';
            h += '<option value="resume">Resume</option>';
            h += '<option value="reset">Reset</option>';
            h += '<option value="focus">Focus Light ON</option>';
            h += '<option value="focus_off">Focus Light OFF</option>';
            h += '<option value="focus_toggle">Focus Light Toggle</option>';
            h += '<option value="set_time">Set Time</option>';
            h += '<option value="add_seconds">Add Seconds</option>';
            h += '<option value="add_stops">Add F-Stops</option>';
            h += '</select>';
            h += '</div>';
            h += '<div class="form-group" id="' + prefix + '-expose-value-group" style="display:none;">';
            h += '<label for="' + prefix + '-expose-value">Value</label>';
            h += '<input type="text" id="' + prefix + '-expose-value" placeholder="e.g. 10 or 0.5 or -0.333">';
            h += '<small id="' + prefix + '-expose-value-hint">Seconds for set_time/add_seconds, f-stops for add_stops (e.g. 0.333 = 1/3 stop).</small>';
            h += '</div>';
            h += '</div>';
            // Test strip control
            h += '<div id="' + prefix + '-strip-group" style="display:none;">';
            h += '<div class="form-group">';
            h += '<label for="' + prefix + '-strip-action">Test Strip Action</label>';
            h += '<select id="' + prefix + '-strip-action" onchange="_actionEditorDarkroomStripChanged(\'' + prefix + '\')">';
            h += '<option value="start">Start Sequence</option>';
            h += '<option value="cancel">Cancel Sequence</option>';
            h += '<option value="set_base">Set Base Time</option>';
            h += '<option value="add_base">Add Base Time</option>';
            h += '<option value="step_up">Step Interval Up</option>';
            h += '<option value="step_down">Step Interval Down</option>';
            h += '<option value="add_segments">Add Segments</option>';
            h += '<option value="set_segments">Set Segments</option>';
            h += '<option value="set_countdown">Set Initial Countdown</option>';
            h += '<option value="set_pause">Set Pause Duration</option>';
            h += '<option value="set_tick">Set Exposure Tick</option>';
            h += '</select>';
            h += '</div>';
            h += '<div class="form-group" id="' + prefix + '-strip-value-group" style="display:none;">';
            h += '<label for="' + prefix + '-strip-value">Value</label>';
            h += '<input type="text" id="' + prefix + '-strip-value" placeholder="">';
            h += '<small id="' + prefix + '-strip-value-hint"></small>';
            h += '</div>';
            h += '</div>';
            return h;
        },

        // Show/hide groups on type change
        typeChanged: function(prefix, type) {
            var exposeGrp = document.getElementById(prefix + '-expose-group');
            if (exposeGrp) exposeGrp.style.display = (type === 'expose') ? '' : 'none';
            if (type === 'expose') _actionEditorDarkroomExposeChanged(prefix);
            var stripGrp = document.getElementById(prefix + '-strip-group');
            if (stripGrp) stripGrp.style.display = (type === 'strip') ? '' : 'none';
            if (type === 'strip') _actionEditorDarkroomStripChanged(prefix);
        },

        // Load saved action data into form fields
        load: function(prefix, action) {
            var el;
            // Expose: parse DSL string "command[:value]" into structured fields
            if (action.expose_command) {
                var ec = action.expose_command;
                var colonIdx = ec.indexOf(':');
                var expCmd, expVal;
                if (colonIdx >= 0) {
                    expCmd = ec.substring(0, colonIdx);
                    expVal = ec.substring(colonIdx + 1);
                } else {
                    expCmd = ec;
                    expVal = '';
                }
                el = document.getElementById(prefix + '-expose-action');
                if (el) {
                    el.value = expCmd;
                    if (el.selectedIndex < 0) el.value = 'toggle';
                }
                if (expVal) {
                    el = document.getElementById(prefix + '-expose-value');
                    if (el) el.value = expVal;
                }
            } else {
                el = document.getElementById(prefix + '-expose-action');
                if (el) el.value = 'toggle';
            }
            // Strip: parse DSL string "command[:value]" into structured fields
            if (action.strip_command) {
                var sc = action.strip_command;
                var colonIdx = sc.indexOf(':');
                var stripCmd, stripVal;
                if (colonIdx >= 0) {
                    stripCmd = sc.substring(0, colonIdx);
                    stripVal = sc.substring(colonIdx + 1);
                } else {
                    stripCmd = sc;
                    stripVal = '';
                }
                el = document.getElementById(prefix + '-strip-action');
                if (el) {
                    el.value = stripCmd;
                    if (el.selectedIndex < 0) el.value = 'start';
                }
                if (stripVal) {
                    el = document.getElementById(prefix + '-strip-value');
                    if (el) el.value = stripVal;
                }
            } else {
                el = document.getElementById(prefix + '-strip-action');
                if (el) el.value = 'start';
            }
        },

        // Build action object fields from form
        build: function(prefix, type) {
            if (type === 'expose') {
                var eSel = document.getElementById(prefix + '-expose-action');
                if (eSel) {
                    var eCmd = eSel.value;
                    if (eCmd === 'set_time' || eCmd === 'add_seconds' || eCmd === 'add_stops') {
                        var eVal = document.getElementById(prefix + '-expose-value');
                        if (eVal && eVal.value !== '') eCmd = eCmd + ':' + eVal.value.trim();
                    }
                    return { expose_command: eCmd };
                }
            }
            if (type === 'strip') {
                var sSel = document.getElementById(prefix + '-strip-action');
                if (sSel) {
                    var sCmd = sSel.value;
                    var needsVal = (sCmd === 'set_base' || sCmd === 'add_base' || sCmd === 'add_segments' || sCmd === 'set_segments' ||
                                    sCmd === 'set_countdown' || sCmd === 'set_pause' || sCmd === 'set_tick');
                    if (needsVal) {
                        var sVal = document.getElementById(prefix + '-strip-value');
                        if (sVal && sVal.value !== '') sCmd = sCmd + ':' + sVal.value.trim();
                    }
                    return { strip_command: sCmd };
                }
            }
            return null;
        }
    });

    // Show/hide expose sub-fields based on the expose action dropdown.
    window._actionEditorDarkroomExposeChanged = function(prefix) {
        var sel = document.getElementById(prefix + '-expose-action');
        if (!sel) return;
        var val = sel.value;
        var valGrp = document.getElementById(prefix + '-expose-value-group');
        var needsValue = (val === 'set_time' || val === 'add_seconds' || val === 'add_stops');
        if (valGrp) valGrp.style.display = needsValue ? '' : 'none';
    };

    // Show/hide strip sub-fields based on the strip action dropdown.
    window._actionEditorDarkroomStripChanged = function(prefix) {
        var sel = document.getElementById(prefix + '-strip-action');
        if (!sel) return;
        var val = sel.value;
        var valGrp = document.getElementById(prefix + '-strip-value-group');
        var hint = document.getElementById(prefix + '-strip-value-hint');
        var valInput = document.getElementById(prefix + '-strip-value');
        var needsValue = (val === 'set_base' || val === 'add_base' || val === 'add_segments' || val === 'set_segments' ||
                          val === 'set_countdown' || val === 'set_pause' || val === 'set_tick');
        if (valGrp) valGrp.style.display = needsValue ? '' : 'none';
        if (hint) {
            if (val === 'set_base') hint.textContent = 'Base exposure time in seconds (1.0\u2013999.9).';
            else if (val === 'add_base') hint.textContent = 'Seconds to add (positive or negative, e.g. 1, -0.5).';
            else if (val === 'step_up') hint.textContent = 'Cycles through: 1/5 \u2192 1/4 \u2192 1/3 \u2192 1/2 \u2192 1/1 stop.';
            else if (val === 'step_down') hint.textContent = 'Cycles through: 1/1 \u2192 1/2 \u2192 1/3 \u2192 1/4 \u2192 1/5 stop.';
            else if (val === 'add_segments') hint.textContent = 'Add or remove segments (e.g. 2 or -2). Clamped to 3\u201311 odd.';
            else if (val === 'set_segments') hint.textContent = 'Number of segments (3\u201311 odd). Center segment matches your base time.';
            else if (val === 'set_countdown') hint.textContent = 'Initial countdown before first segment (2\u201310 seconds).';
            else if (val === 'set_pause') hint.textContent = 'Pause between segments for mask movement (3\u201315 seconds).';
            else if (val === 'set_tick') hint.textContent = 'Enter "on" or "off" to enable/disable per-second tick during exposure.';
            else hint.textContent = '';
        }
        if (valInput) {
            if (val === 'set_base') valInput.placeholder = 'e.g. 8.0';
            else if (val === 'add_base') valInput.placeholder = 'e.g. 1 or -0.5';
            else if (val === 'add_segments') valInput.placeholder = 'e.g. 1 or -1';
            else if (val === 'set_segments') valInput.placeholder = 'e.g. 7';
            else if (val === 'set_countdown') valInput.placeholder = 'e.g. 5';
            else if (val === 'set_pause') valInput.placeholder = 'e.g. 3';
            else if (val === 'set_tick') valInput.placeholder = 'on or off';
            else valInput.placeholder = '';
        }
    };
})();
