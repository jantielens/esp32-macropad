// ============================================================================
// Scale/Brew Action Editor Extension
// ============================================================================
// Extends portal_action_editor.js with Scale Control and Brew Control actions.
// Self-registers into the action editor extension system on load.
// Only loaded on builds with HAS_SCALE enabled.

(function() {
    if (typeof _actionEditorExtensions === 'undefined') return;

    _actionEditorExtensions.push({
        // Extra <option> tags for the type dropdown
        options: function() {
            return '<option value="scale">Scale Control</option>'
                 + '<option value="brew">Brew Control</option>';
        },

        // Extra form group HTML
        groups: function(prefix) {
            var h = '';
            // Scale — sub-command dropdown with conditional fields
            h += '<div id="' + prefix + '-scale-group" style="display:none;">';
            h += '<div class="form-group">';
            h += '<label for="' + prefix + '-scale-cmd">Scale Command</label>';
            h += '<select id="' + prefix + '-scale-cmd" onchange="_actionEditorScaleChanged(\'' + prefix + '\')">';
            h += '<option value="tare">Tare</option>';
            h += '<option value="calibrate">Calibrate</option>';
            h += '<option value="cal_weight">Cal Weight &plusmn;</option>';
            h += '<option value="cal_weight_set">Cal Weight Set</option>';
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
            h += '<label for="' + prefix + '-brew-cmd">Brew Command</label>';
            h += '<select id="' + prefix + '-brew-cmd">';
            h += '<option value="">Loading templates...</option>';
            h += '</select>';
            h += '<small>Use <strong>Set Template</strong> to select a brew recipe (pair with a Navigate action to go to your brew pad). Use <strong>Advance</strong> on the brew pad for a single button that handles the full cycle &mdash; pair its label with <code>[brew:next_label]</code>.</small>';
            h += '</div></div>';
            return h;
        },

        // Show/hide groups when type changes
        typeChanged: function(prefix, type, skipBrewPopulate) {
            var scaleGrp = document.getElementById(prefix + '-scale-group');
            if (scaleGrp) scaleGrp.style.display = (type === 'scale') ? '' : 'none';
            if (type === 'scale') _actionEditorScaleChanged(prefix);
            var brewGrp = document.getElementById(prefix + '-brew-group');
            if (brewGrp) brewGrp.style.display = (type === 'brew') ? '' : 'none';
            if (type === 'brew' && !skipBrewPopulate) _actionEditorPopulateBrewCmd(prefix, 'advance');
        },

        // Load action data into form fields
        load: function(prefix, action) {
            if (action.type === 'scale' && action.payload) {
                var sp = action.payload;
                var el = document.getElementById(prefix + '-scale-cmd');
                if (el) {
                    if (sp === 'tare' || sp === '') {
                        el.value = 'tare';
                    } else if (sp === 'calibrate') {
                        el.value = 'calibrate';
                    } else if (sp.indexOf('cal_weight_set:') === 0) {
                        el.value = 'cal_weight_set';
                        var sv = document.getElementById(prefix + '-scale-set-value');
                        if (sv) sv.value = sp.substring(15);
                    } else if (sp.indexOf('cal_weight:') === 0) {
                        el.value = 'cal_weight';
                        var sd = document.getElementById(prefix + '-scale-delta');
                        if (sd) sd.value = sp.substring(11);
                    }
                }
            } else {
                var el = document.getElementById(prefix + '-scale-cmd');
                if (el) el.value = 'tare';
            }
            // Brew command — populate dropdown dynamically, then set value
            var el = document.getElementById(prefix + '-brew-cmd');
            if (el) _actionEditorPopulateBrewCmd(prefix, action.payload || 'start');
        },

        // Build action data from form fields. Return object with fields, or null.
        build: function(prefix, type) {
            if (type === 'scale') {
                var act = {};
                var scmd = document.getElementById(prefix + '-scale-cmd');
                var scmdVal = scmd ? scmd.value : 'tare';
                if (scmdVal === 'cal_weight') {
                    var sd = document.getElementById(prefix + '-scale-delta');
                    act.payload = 'cal_weight:' + ((sd && sd.value !== '') ? sd.value.trim() : '0');
                } else if (scmdVal === 'cal_weight_set') {
                    var sv = document.getElementById(prefix + '-scale-set-value');
                    act.payload = 'cal_weight_set:' + ((sv && sv.value !== '') ? sv.value.trim() : '1');
                } else {
                    act.payload = scmdVal;
                }
                return act;
            }
            if (type === 'brew') {
                var act = {};
                var bc = document.getElementById(prefix + '-brew-cmd');
                if (bc) act.payload = bc.value || 'start';
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
