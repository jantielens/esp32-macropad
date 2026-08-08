// ============================================================================
// Scale/Brew Action Editor Extension
// ============================================================================
// Extends portal_action_editor.js with Scale Control and Brew Control actions.
// Self-registers into the action editor extension system on load.
// Only loaded on builds with HAS_SCALE enabled.

(function() {
    if (typeof _actionEditorExtensions === 'undefined') return;

    _actionEditorExtensions.push({
        // Extra form group HTML
        groups: function(prefix) {
            var h = '';
            // Scale — sub-command dropdown with conditional fields
            h += '<div id="' + prefix + '-scale-group" style="display:none;">';
            h += '<div class="form-group">';
            h += '<label for="' + prefix + '-scale-cmd">Command</label>';
            h += '<select id="' + prefix + '-scale-cmd" onchange="_actionEditorScaleChanged(\'' + prefix + '\')">';
            h += actionEditorCommandOptionsHTML('scale');
            h += '</select>';
            h += '</div>';
            h += '<div class="form-group" id="' + prefix + '-scale-delta-group" style="display:none;">';
            h += '<label for="' + prefix + '-scale-delta">Weight Change (g)</label>';
            h += '<input type="number" id="' + prefix + '-scale-delta" step="0.1" placeholder="e.g. 10, -10, 0.5, -0.5">';
            h += '<small>Grams to add or subtract from the calibration reference weight each tap.</small>';
            h += '</div>';
            h += '<div class="form-group" id="' + prefix + '-scale-set-group" style="display:none;">';
            h += '<label for="' + prefix + '-scale-set-value">Calibration Weight (g)</label>';
            h += '<input type="number" id="' + prefix + '-scale-set-value" min="1" step="0.1" placeholder="e.g. 251.5">';
            h += '<small>Set the calibration reference weight to this value (grams).</small>';
            h += '</div>';
            h += '</div>';
            // Brew — command dropdown (populated dynamically from /api/brew-templates)
            h += '<div id="' + prefix + '-brew-group" style="display:none;">';
            h += '<div class="form-group">';
            h += '<label for="' + prefix + '-brew-cmd">Command</label>';
            h += '<select id="' + prefix + '-brew-cmd">';
            h += '<option value="">Loading templates...</option>';
            h += '</select>';
            h += '<small>Use <strong>Set Template</strong> to select a brew recipe (pair with a Navigate action to go to your brew pad). Use <strong>Advance</strong> on the brew pad for a single button that handles the full cycle &mdash; pair its label with <code>[brew:next_label]</code>.</small>';
            h += '</div></div>';
            return h;
        },

        // Show/hide groups when type changes
        typeChanged: function(prefix, type) {
            var scaleGrp = document.getElementById(prefix + '-scale-group');
            if (scaleGrp) scaleGrp.style.display = (type === 'scale') ? '' : 'none';
            if (type === 'scale') _actionEditorScaleChanged(prefix);
            var brewGrp = document.getElementById(prefix + '-brew-group');
            if (brewGrp) brewGrp.style.display = (type === 'brew') ? '' : 'none';
            if (type === 'brew') {
                // load() runs before actionEditorTypeChanged() and may have stored a
                // pending value to preserve. Use it once, then fall back to default.
                var bc = document.getElementById(prefix + '-brew-cmd');
                var pending = bc && bc.getAttribute('data-pending-value');
                if (pending) {
                    bc.removeAttribute('data-pending-value');
                    _actionEditorPopulateBrewCmd(prefix, pending);
                } else {
                    _actionEditorPopulateBrewCmd(prefix, 'advance');
                }
            }
        },

        // Load action data into form fields
        load: function(prefix, action) {
            if (action.type === 'scale') {
                var cmd = action.scale_command || 'tare';
                var val = action.scale_value || '';
                var el = document.getElementById(prefix + '-scale-cmd');
                if (el) el.value = cmd;
                if (cmd === 'cal_weight') {
                    var sd = document.getElementById(prefix + '-scale-delta');
                    if (sd) sd.value = val;
                } else if (cmd === 'cal_weight_set') {
                    var sv = document.getElementById(prefix + '-scale-set-value');
                    if (sv) sv.value = val;
                }
            } else {
                var el = document.getElementById(prefix + '-scale-cmd');
                if (el) el.value = 'tare';
            }
            // Brew — reconstruct dropdown selection from typed fields.
            // The dropdown encodes set_template as "set_template:<name>";
            // all other commands are bare strings. Store the desired value as a
            // data-attr so typeChanged() (called immediately after load) picks it
            // up instead of overwriting with the default.
            if (action.type === 'brew') {
                var bc = document.getElementById(prefix + '-brew-cmd');
                if (bc) {
                    var cmd = action.brew_command || 'advance';
                    var sel = (cmd === 'set_template' && action.brew_value)
                        ? 'set_template:' + action.brew_value
                        : cmd;
                    bc.setAttribute('data-pending-value', sel);
                }
            }
        },

        // Build action data from form fields. Return object with fields, or null.
        build: function(prefix, type) {
            if (type === 'scale') {
                var act = {};
                var scmd = document.getElementById(prefix + '-scale-cmd');
                act.scale_command = scmd ? scmd.value : 'tare';
                if (act.scale_command === 'cal_weight') {
                    var sd = document.getElementById(prefix + '-scale-delta');
                    act.scale_value = (sd && sd.value !== '') ? sd.value.trim() : '0';
                } else if (act.scale_command === 'cal_weight_set') {
                    var sv = document.getElementById(prefix + '-scale-set-value');
                    act.scale_value = (sv && sv.value !== '') ? sv.value.trim() : '1';
                }
                return act;
            }
            if (type === 'brew') {
                var act = {};
                var bc = document.getElementById(prefix + '-brew-cmd');
                var raw = bc ? (bc.value || 'advance') : 'advance';
                // Dropdown encodes "set_template:<name>" — split into typed fields.
                var colon = raw.indexOf(':');
                if (colon !== -1) {
                    act.brew_command = raw.substring(0, colon);
                    act.brew_value = raw.substring(colon + 1);
                } else {
                    act.brew_command = raw;
                }
                return act;
            }
            return null;
        }
    });

    // --- Internal helpers (prefixed with _ to avoid namespace conflicts) ---

    function _actionEditorScaleChanged(prefix) {
        var sel = document.getElementById(prefix + '-scale-cmd');
        if (!sel) return;
        var cmd = sel.value;
        var deltaGrp = document.getElementById(prefix + '-scale-delta-group');
        var setGrp = document.getElementById(prefix + '-scale-set-group');
        if (deltaGrp) deltaGrp.style.display = (cmd === 'cal_weight') ? '' : 'none';
        if (setGrp) setGrp.style.display = (cmd === 'cal_weight_set') ? '' : 'none';
    }
    // Expose for onchange handler
    window._actionEditorScaleChanged = _actionEditorScaleChanged;

    // ---- Dynamic brew template dropdown ----

    var _brewTemplatesCache = null;

    function _actionEditorFetchBrewTemplates(callback) {
        if (_brewTemplatesCache) { callback(_brewTemplatesCache); return; }
        fetch('/api/brew-templates')
            .then(function(r) { return r.ok ? r.json() : []; })
            .then(function(data) {
                _brewTemplatesCache = Array.isArray(data) ? data : [];
                callback(_brewTemplatesCache);
            })
            .catch(function() { callback([]); });
    }

    function _actionEditorBuildBrewOptions(templates) {
        var h = '';
        h += '<optgroup label="Brew Control">';
        h += '<option value="advance">Advance \u2014 single button, full cycle (recommended)</option>';
        h += '<option value="start">Start \u2014 begin brew</option>';
        h += '<option value="next">Next \u2014 advance manual stage</option>';
        h += '<option value="stop">Stop \u2014 freeze timer &amp; save</option>';
        h += '<option value="reset">Reset \u2014 clear all state</option>';
        h += '<option value="tare">Tare \u2014 zero the scale</option>';
        h += '</optgroup>';
        if (templates.length > 0) {
            h += '<optgroup label="Set Template">';
            templates.forEach(function(t) {
                h += '<option value="set_template:' + t.name + '">' + (t.display_name || t.name) + '</option>';
            });
            h += '</optgroup>';
        }
        h += '<optgroup label="Set Template by Slot (binding)">';
        for (var i = 0; i < 16; i++) {
            h += '<option value="set_template:[brew:tpl_' + i + '_name]">Template slot ' + i + '</option>';
        }
        h += '</optgroup>';
        return h;
    }

    function _actionEditorPopulateBrewCmd(prefix, selectedValue) {
        var sel = document.getElementById(prefix + '-brew-cmd');
        if (!sel) return;
        _actionEditorFetchBrewTemplates(function(templates) {
            sel.innerHTML = _actionEditorBuildBrewOptions(templates);
            sel.value = selectedValue;
            if (sel.value !== selectedValue) {
                var opt = document.createElement('option');
                opt.value = selectedValue;
                if (selectedValue.indexOf('[') !== -1) {
                    opt.textContent = selectedValue + ' (binding)';
                } else {
                    opt.textContent = selectedValue + ' (unknown template)';
                }
                sel.insertBefore(opt, sel.firstChild);
                sel.value = selectedValue;
            }
        });
    }
    // Expose for internal use by extension hooks
    window._actionEditorPopulateBrewCmd = _actionEditorPopulateBrewCmd;

})();
