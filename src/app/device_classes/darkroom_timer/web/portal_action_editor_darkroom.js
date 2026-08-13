// ============================================================================
// Darkroom Action Editor Extension
// ============================================================================
// Extends portal_action_editor.js with darkroom actions: Exposure Timer,
// Test Strip, and Light Meter.
// Self-registers into the action editor extension system on load.
// No-ops gracefully if the extension system is absent.

(function() {
    if (typeof _actionEditorExtensions === 'undefined') return;

    _actionEditorExtensions.push({
        // Extra form group HTML
        groups: function(prefix) {
            var h = '';
            // Expose timer control
            h += '<div id="' + prefix + '-expose-group" style="display:none;">';
            h += '<div class="form-group">';
            h += '<label for="' + prefix + '-expose-action">Command</label>';
            h += '<select id="' + prefix + '-expose-action" onchange="_actionEditorDarkroomExposeChanged(\'' + prefix + '\')">';
            h += actionEditorCommandOptionsHTML('expose');
            h += '</select>';
            h += '</div>';
            h += '<div class="form-group" id="' + prefix + '-expose-value-group" style="display:none;">';
            h += '<label for="' + prefix + '-expose-value">Value</label>';
            h += '<input type="text" id="' + prefix + '-expose-value" placeholder="e.g. 10 or 0.5 or -0.333">';
            h += '<small id="' + prefix + '-expose-value-hint">Seconds for set_time/adjust_seconds, f-stops for adjust_stops. Use <code>{step}</code> for Numeric Rocker widgets.</small>';
            h += '</div>';
            h += '</div>';
            // Test strip control
            h += '<div id="' + prefix + '-strip-group" style="display:none;">';
            h += '<div class="form-group">';
            h += '<label for="' + prefix + '-strip-action">Command</label>';
            h += '<select id="' + prefix + '-strip-action" onchange="_actionEditorDarkroomStripChanged(\'' + prefix + '\')">';
            h += actionEditorCommandOptionsHTML('strip');
            h += '</select>';
            h += '</div>';
            h += '<div class="form-group" id="' + prefix + '-strip-value-group" style="display:none;">';
            h += '<label for="' + prefix + '-strip-value">Value</label>';
            h += '<input type="text" id="' + prefix + '-strip-value" placeholder="">';
            h += '<small id="' + prefix + '-strip-value-hint"></small>';
            h += '</div>';
            h += '</div>';
            // Light meter control
            h += '<div id="' + prefix + '-meter-group" style="display:none;">';
            h += '<div class="form-group">';
            h += '<label for="' + prefix + '-meter-action">Command</label>';
            h += '<select id="' + prefix + '-meter-action" onchange="_actionEditorDarkroomMeterChanged(\'' + prefix + '\')">'; 
            h += actionEditorCommandOptionsHTML('meter');
            h += '</select>';
            h += '</div>';
            h += '<div class="form-group" id="' + prefix + '-meter-value-group" style="display:none;">';
            h += '<label for="' + prefix + '-meter-value">Value</label>';
            h += '<input type="text" id="' + prefix + '-meter-value" placeholder="">';
            h += '<small id="' + prefix + '-meter-value-hint"></small>';
            h += '</div>';
            h += '</div>';
            // Print log control
            h += '<div id="' + prefix + '-print-group" style="display:none;">';
            h += '<div class="form-group">';
            h += '<label for="' + prefix + '-print-action">Command</label>';
            h += '<select id="' + prefix + '-print-action" onchange="_actionEditorDarkroomPrintChanged(\'' + prefix + '\')">'; 
            h += actionEditorCommandOptionsHTML('print');
            h += '</select>';
            h += '</div>';
            h += '<div class="form-group" id="' + prefix + '-print-value-group" style="display:none;">';
            h += '<label for="' + prefix + '-print-value">Value</label>';
            h += '<input type="text" id="' + prefix + '-print-value" placeholder="1 or 0">';
            h += '<small id="' + prefix + '-print-value-hint">1 = starred, 0 = not starred</small>';
            h += '</div>';
            h += '</div>';
            // Shelly relay control
            h += '<div id="' + prefix + '-shelly-group" style="display:none;">';
            h += '<div class="form-group">';
            h += '<label for="' + prefix + '-shelly-host">Shelly Host</label>';
            h += '<input type="text" id="' + prefix + '-shelly-host" placeholder="192.168.1.100" maxlength="63">';
            h += '<small>IP address or hostname of the Shelly device.</small>';
            h += '</div>';
            h += '<div class="form-group">';
            h += '<label for="' + prefix + '-shelly-relay">Relay Index</label>';
            h += '<input type="number" id="' + prefix + '-shelly-relay" min="0" max="3" value="0">';
            h += '<small>Relay output index (0 for single-relay devices).</small>';
            h += '</div>';
            h += '<div class="form-group">';
            h += '<label for="' + prefix + '-shelly-on">Turn</label>';
            h += '<select id="' + prefix + '-shelly-on">';
            h += '<option value="true">On</option>';
            h += '<option value="false">Off</option>';
            h += '</select>';
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
            var meterGrp = document.getElementById(prefix + '-meter-group');
            if (meterGrp) meterGrp.style.display = (type === 'meter') ? '' : 'none';
            if (type === 'meter') _actionEditorDarkroomMeterChanged(prefix);
            var printGrp = document.getElementById(prefix + '-print-group');
            if (printGrp) printGrp.style.display = (type === 'print') ? '' : 'none';
            if (type === 'print') _actionEditorDarkroomPrintChanged(prefix);
            var shellyGrp = document.getElementById(prefix + '-shelly-group');
            if (shellyGrp) shellyGrp.style.display = (type === 'shelly') ? '' : 'none';
        },

        // Load saved action data into form fields
        load: function(prefix, action) {
            var el;
            // Expose: load structured command + value fields
            if (action.expose_command) {
                el = document.getElementById(prefix + '-expose-action');
                if (el) {
                    el.value = action.expose_command;
                    if (el.selectedIndex < 0) el.value = 'toggle';
                }
                if (action.expose_value) {
                    el = document.getElementById(prefix + '-expose-value');
                    if (el) el.value = action.expose_value;
                }
            } else {
                el = document.getElementById(prefix + '-expose-action');
                if (el) el.value = 'toggle';
            }
            // Strip: load structured command + value fields
            if (action.strip_command) {
                el = document.getElementById(prefix + '-strip-action');
                if (el) {
                    el.value = action.strip_command;
                    if (el.selectedIndex < 0) el.value = 'start';
                }
                if (action.strip_value) {
                    el = document.getElementById(prefix + '-strip-value');
                    if (el) el.value = action.strip_value;
                }
            } else {
                el = document.getElementById(prefix + '-strip-action');
                if (el) el.value = 'start';
            }
            // Meter: load structured command + value fields
            if (action.meter_command) {
                el = document.getElementById(prefix + '-meter-action');
                if (el) {
                    el.value = action.meter_command;
                    if (el.selectedIndex < 0) el.value = 'read_lref';
                }
                if (action.meter_value) {
                    el = document.getElementById(prefix + '-meter-value');
                    if (el) el.value = action.meter_value;
                }
            } else {
                el = document.getElementById(prefix + '-meter-action');
                if (el) el.value = 'read_lref';
            }
            _actionEditorDarkroomMeterChanged(prefix);
            // Print: load structured command + value fields
            if (action.print_command) {
                el = document.getElementById(prefix + '-print-action');
                if (el) {
                    el.value = action.print_command;
                    if (el.selectedIndex < 0) el.value = 'toggle_star';
                }
                if (action.print_value) {
                    el = document.getElementById(prefix + '-print-value');
                    if (el) el.value = action.print_value;
                }
            } else {
                el = document.getElementById(prefix + '-print-action');
                if (el) el.value = 'toggle_star';
            }
            _actionEditorDarkroomPrintChanged(prefix);
            // Shelly: load host + relay index
            if (action.shelly_host) {
                el = document.getElementById(prefix + '-shelly-host');
                if (el) el.value = action.shelly_host;
            }
            if (action.shelly_relay !== undefined) {
                el = document.getElementById(prefix + '-shelly-relay');
                if (el) el.value = action.shelly_relay;
            }
            el = document.getElementById(prefix + '-shelly-on');
            if (el) el.value = (action.shelly_on === false) ? 'false' : 'true';
        },

        // Build action object fields from form
        build: function(prefix, type) {
            if (type === 'expose') {
                var eSel = document.getElementById(prefix + '-expose-action');
                if (eSel) {
                    var result = { expose_command: eSel.value };
                    if (eSel.value === 'set_time' || eSel.value === 'adjust_seconds' || eSel.value === 'adjust_stops' || eSel.value === 'set_dry_down' || eSel.value === 'adjust_dry_down') {
                        var eVal = document.getElementById(prefix + '-expose-value');
                        if (eVal && eVal.value !== '') result.expose_value = eVal.value.trim();
                    }
                    return result;
                }
            }
            if (type === 'strip') {
                var sSel = document.getElementById(prefix + '-strip-action');
                if (sSel) {
                    var result = { strip_command: sSel.value };
                    var needsVal = (sSel.value === 'set_base' || sSel.value === 'adjust_base' || sSel.value === 'adjust_segments' || sSel.value === 'set_segments' ||
                                    sSel.value === 'set_countdown' || sSel.value === 'adjust_countdown' || sSel.value === 'set_pause' || sSel.value === 'adjust_pause' || sSel.value === 'set_tick');
                    if (needsVal) {
                        var sVal = document.getElementById(prefix + '-strip-value');
                        if (sVal && sVal.value !== '') result.strip_value = sVal.value.trim();
                    }
                    return result;
                }
            }
            if (type === 'meter') {
                var mSel = document.getElementById(prefix + '-meter-action');
                if (mSel) {
                    var result = { meter_command: mSel.value };
                    if (mSel.value === 'set_lref' || mSel.value === 'adjust_lref' || mSel.value === 'set_zone5' || mSel.value === 'adjust_zone5') {
                        var mVal = document.getElementById(prefix + '-meter-value');
                        if (mVal && mVal.value !== '') result.meter_value = mVal.value.trim();
                    }
                    return result;
                }
            }
            if (type === 'print') {
                var pSel = document.getElementById(prefix + '-print-action');
                if (pSel) {
                    var result = { print_command: pSel.value };
                    if (pSel.value === 'set_star') {
                        var pVal = document.getElementById(prefix + '-print-value');
                        if (pVal && pVal.value !== '') result.print_value = pVal.value.trim();
                    }
                    return result;
                }
            }
            if (type === 'shelly') {
                var result = {};
                var hEl = document.getElementById(prefix + '-shelly-host');
                if (hEl && hEl.value.trim()) result.shelly_host = hEl.value.trim();
                var rEl = document.getElementById(prefix + '-shelly-relay');
                if (rEl) result.shelly_relay = parseInt(rEl.value) || 0;
                var oEl = document.getElementById(prefix + '-shelly-on');
                if (oEl) result.shelly_on = (oEl.value === 'true');
                return result;
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
        var hint = document.getElementById(prefix + '-expose-value-hint');
        var valInput = document.getElementById(prefix + '-expose-value');
        var needsValue = (val === 'set_time' || val === 'adjust_seconds' || val === 'adjust_stops' || val === 'set_dry_down' || val === 'adjust_dry_down');
        if (valGrp) valGrp.style.display = needsValue ? '' : 'none';
        if (hint) {
            if (val === 'set_time') hint.textContent = 'Exposure time in seconds (e.g. 10, 8.5).';
            else if (val === 'adjust_seconds') hint.innerHTML = 'Seconds to add or subtract (e.g. 2.5 or -1). Use <code>{step}</code> for Numeric Rocker widgets.';
            else if (val === 'adjust_stops') hint.innerHTML = 'F-stops to adjust (e.g. 0.333 = 1/3 stop, -0.5 = 1/2 stop). Use <code>{step}</code> for Numeric Rocker widgets.';
            else if (val === 'set_dry_down') hint.textContent = 'Dry-down compensation percentage (0–15). Paper-dependent; typically 5–10%.';
            else if (val === 'adjust_dry_down') hint.innerHTML = 'Percentage points to add or subtract (e.g. 1 or -0.5). Use <code>{step}</code> for Numeric Rocker widgets.';
            else hint.textContent = '';
        }
        if (valInput) {
            if (val === 'set_time') valInput.placeholder = 'e.g. 10 or 8.5';
            else if (val === 'adjust_seconds') valInput.placeholder = 'e.g. 2.5, -1, or {step}';
            else if (val === 'adjust_stops') valInput.placeholder = 'e.g. 0.333, -0.5, or {step}';
            else if (val === 'set_dry_down') valInput.placeholder = 'e.g. 8';
            else if (val === 'adjust_dry_down') valInput.placeholder = 'e.g. 1, -0.5, or {step}';
            else valInput.placeholder = '';
        }
    };

    // Show/hide strip sub-fields based on the strip action dropdown.
    window._actionEditorDarkroomStripChanged = function(prefix) {
        var sel = document.getElementById(prefix + '-strip-action');
        if (!sel) return;
        var val = sel.value;
        var valGrp = document.getElementById(prefix + '-strip-value-group');
        var hint = document.getElementById(prefix + '-strip-value-hint');
        var valInput = document.getElementById(prefix + '-strip-value');
        var needsValue = (val === 'set_base' || val === 'adjust_base' || val === 'adjust_segments' || val === 'set_segments' ||
                          val === 'set_countdown' || val === 'adjust_countdown' || val === 'set_pause' || val === 'adjust_pause' || val === 'set_tick');
        if (valGrp) valGrp.style.display = needsValue ? '' : 'none';
        if (hint) {
            if (val === 'set_base') hint.textContent = 'Base exposure time in seconds (1.0\u2013999.9).';
            else if (val === 'adjust_base') hint.innerHTML = 'Seconds to add or subtract (e.g. 1, -0.5). Use <code>{step}</code> for Numeric Rocker widgets.';
            else if (val === 'step_up') hint.textContent = 'Cycles through: 1/5 \u2192 1/4 \u2192 1/3 \u2192 1/2 \u2192 1/1 stop.';
            else if (val === 'step_down') hint.textContent = 'Cycles through: 1/1 \u2192 1/2 \u2192 1/3 \u2192 1/4 \u2192 1/5 stop.';
            else if (val === 'adjust_segments') hint.innerHTML = 'Add or remove segments (e.g. 2 or -2). Clamped to 3\u201311 odd. Use <code>{step}</code> for Numeric Rocker widgets.';
            else if (val === 'set_segments') hint.textContent = 'Number of segments (3\u201311 odd). Center segment matches your base time.';
            else if (val === 'set_countdown') hint.textContent = 'Initial countdown before first segment (2\u201310 seconds).';
            else if (val === 'adjust_countdown') hint.innerHTML = 'Seconds to add or remove from countdown (e.g. 1 or -1). Clamped to 2\u201310. Use <code>{step}</code> for Numeric Rocker widgets.';
            else if (val === 'set_pause') hint.textContent = 'Pause between segments for mask movement (3\u201315 seconds).';
            else if (val === 'adjust_pause') hint.innerHTML = 'Seconds to add or remove from pause (e.g. 1 or -1). Clamped to 3\u201315. Use <code>{step}</code> for Numeric Rocker widgets.';
            else if (val === 'set_tick') hint.textContent = 'Enter "on" or "off" to enable/disable per-second tick during exposure.';
            else hint.textContent = '';
        }
        if (valInput) {
            if (val === 'set_base') valInput.placeholder = 'e.g. 8.0';
            else if (val === 'adjust_base') valInput.placeholder = 'e.g. 1, -0.5, or {step}';
            else if (val === 'adjust_segments') valInput.placeholder = 'e.g. 1, -1, or {step}';
            else if (val === 'set_segments') valInput.placeholder = 'e.g. 7';
            else if (val === 'set_countdown') valInput.placeholder = 'e.g. 5';
            else if (val === 'adjust_countdown') valInput.placeholder = 'e.g. 1, -1, or {step}';
            else if (val === 'set_pause') valInput.placeholder = 'e.g. 3';
            else if (val === 'adjust_pause') valInput.placeholder = 'e.g. 1, -1, or {step}';
            else if (val === 'set_tick') valInput.placeholder = 'on or off';
            else valInput.placeholder = '';
        }
    };

    // Show/hide meter sub-fields based on the meter action dropdown.
    window._actionEditorDarkroomMeterChanged = function(prefix) {
        var sel = document.getElementById(prefix + '-meter-action');
        if (!sel) return;
        var val = sel.value;
        var valGrp = document.getElementById(prefix + '-meter-value-group');
        var hint = document.getElementById(prefix + '-meter-value-hint');
        var valInput = document.getElementById(prefix + '-meter-value');
        var needsValue = (val === 'set_lref' || val === 'adjust_lref' || val === 'set_zone5' || val === 'adjust_zone5');
        if (valGrp) valGrp.style.display = needsValue ? '' : 'none';
        if (hint) {
            if (val === 'set_lref') hint.textContent = 'Lref value in lux (overrides auto from paper cal).';
            else if (val === 'adjust_lref') hint.innerHTML = 'Lux to add or subtract (e.g. 100 or -50). Use <code>{step}</code> for Numeric Rocker widgets.';
            else if (val === 'set_zone5') hint.textContent = 'Zone V time in seconds (from bare-bulb test strip).';
            else if (val === 'adjust_zone5') hint.innerHTML = 'Seconds to add or subtract (e.g. 0.1 or -0.1). Use <code>{step}</code> for Numeric Rocker widgets.';
            else hint.textContent = '';
        }
        if (valInput) {
            if (val === 'set_lref') valInput.placeholder = 'e.g. 1847.3';
            else if (val === 'adjust_lref') valInput.placeholder = 'e.g. 100, -50, or {step}';
            else if (val === 'set_zone5') valInput.placeholder = 'e.g. 6.3';
            else if (val === 'adjust_zone5') valInput.placeholder = 'e.g. 0.1, -0.1, or {step}';
            else valInput.placeholder = '';
        }
    };

    // Show/hide print sub-fields based on the print action dropdown.
    window._actionEditorDarkroomPrintChanged = function(prefix) {
        var sel = document.getElementById(prefix + '-print-action');
        if (!sel) return;
        var val = sel.value;
        var valGrp = document.getElementById(prefix + '-print-value-group');
        if (valGrp) valGrp.style.display = (val === 'set_star') ? '' : 'none';
    };
})();
