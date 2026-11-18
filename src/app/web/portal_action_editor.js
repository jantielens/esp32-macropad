// ============================================================================
// Shared Action Editor — reusable UI component for ButtonAction editing
// ============================================================================
// Catalog-backed fields, extension dispatch, and reusable action-list plumbing.

// Extension modules (e.g. portal_action_editor_shutter.js) can register into
// _actionEditorExtensions to add action types without modifying this file.
// Extensions provide groups, typeChanged, load, build, optional wireFragment,
// and bindingSuffixes/colorSuffixes declarations. Type-select
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

function actionEditorOperation(id) {
    var match = null;
    actionEditorCatalog().forEach(function(entry) {
        (entry.operations || []).forEach(function(operation) {
            if (operation.id === id) match = operation;
        });
    });
    return match;
}

function actionEditorOperationHTML(prefix) {
    var html = '';
    actionEditorCatalog().forEach(function(entry) {
        (entry.operations || []).forEach(function(operation) {
            var base = prefix + '-op-' + operation.id;
            html += '<div id="' + base + '-group" style="display:none;">';
            operation.arguments.forEach(function(argument) {
                var id = base + '-' + argument.name;
                html += '<div class="form-group"><div class="d-flex flex-wrap justify-content-between align-items-center gap-2 mb-1">';
                html += '<label class="form-label mb-0" id="' + id + '-label" for="' + id + '-fixed">' + argument.label + '</label>';
                if (argument.dynamic) {
                    html += '<label class="d-inline-flex align-items-center gap-1"><input type="checkbox" id="' + id + '-dynamic" aria-label="Dynamic ' + argument.label + '" onchange="actionEditorOperationModeChanged(\'' + prefix + '\',\'' + operation.id + '\',\'' + argument.name + '\')"> Dynamic</label>';
                }
                html += '</div><div id="' + id + '-fixed-group">';
                var fixedChanged = ' onchange="actionEditorOperationFixedChanged(\'' + prefix + '\',\'' + operation.id + '\',\'' + argument.name + '\')"';
                if (argument.control === 'screens') {
                    html += '<select class="form-select form-select-sm" id="' + id + '-fixed"' + fixedChanged + '><option value="">Select a screen</option></select>';
                } else if (argument.control === 'timers') {
                    html += '<select class="form-select form-select-sm" id="' + id + '-fixed"' + fixedChanged + '>';
                    for (var n = argument.min; n <= argument.max; n++)
                        html += '<option value="' + n + '">Timer ' + n + '</option>';
                    html += '</select>';
                } else if (argument.control === 'gamepad_buttons' || argument.control === 'bthome_buttons') {
                    html += '<select class="form-select form-select-sm" id="' + id + '-fixed"' + fixedChanged + '>';
                    for (var button = argument.min; button <= argument.max; button++)
                        html += '<option value="' + button + '">Button ' + button + '</option>';
                    html += '</select>';
                } else if (argument.control === 'sounds') {
                    html += '<select class="form-select form-select-sm" id="' + id + '-fixed"' + fixedChanged + '><option value="">Select a sound</option></select>';
                    html += '<small class="text-danger" id="' + id + '-load-error" role="alert"></small>';
                } else if (argument.type === 'boolean') {
                    html += '<select class="form-select form-select-sm" id="' + id + '-fixed"' + fixedChanged + '>';
                    html += '<option value="true"' + (argument.default === true ? ' selected' : '') + '>Enabled</option>';
                    html += '<option value="false"' + (argument.default === false ? ' selected' : '') + '>Disabled</option></select>';
                } else if (argument.type === 'enum') {
                    html += '<select class="form-select form-select-sm" id="' + id + '-fixed"' + fixedChanged + '>';
                    (argument.choices || []).forEach(function(value, index) {
                        var text = argument.choice_labels ? argument.choice_labels[index] : value;
                        html += '<option value="' + escAttr(value) + '">' + escAttr(text) + '</option>';
                    });
                    html += '</select>';
                } else if (argument.type === 'string') {
                    var defaultText = escAttr(argument.default || '');
                    if (argument.control === 'multiline') {
                        html += '<textarea class="form-control form-control-sm" id="' + id + '-fixed" rows="3">' + defaultText + '</textarea>';
                    } else if (argument.control === 'color') {
                        html += '<div class="bindable-color" id="' + id + '-color-wrap"><div class="bc-swatch"></div>';
                        html += '<input type="text" class="bc-input" id="' + id + '-fixed" value="' + defaultText + '" placeholder="#ff0000"></div>';
                    } else if (argument.control === 'pad_exclusions') {
                        html += '<input type="text" class="form-control form-control-sm" id="' + id + '-fixed" value="' + defaultText + '" placeholder="e.g. 2, 5, 8">';
                        html += '<small>Optional comma-separated 1-based pad numbers.</small>';
                    } else if (argument.control === 'list_ids') {
                        html += '<input type="text" class="form-control form-control-sm" id="' + id + '-fixed" value="' + defaultText + '" placeholder="e.g. list_1">';
                        html += '<small>List ID on the active pad.</small>';
                    } else {
                        html += '<input type="text" class="form-control form-control-sm" id="' + id + '-fixed" value="' + defaultText + '">';
                    }
                } else {
                    var initial = argument.default === undefined ? Math.min(argument.max, Math.max(argument.min, 0)) : argument.default;
                    if (argument.control === 'timing' && argument.nonzero_min) {
                        html += '<input type="number" step="1" min="' + argument.nonzero_min + '" max="' + argument.max + '" value="' + (initial || argument.nonzero_min) + '" class="form-control form-control-sm" id="' + id + '-fixed"' + (initial === 0 ? ' disabled' : '') + '>';
                        html += '<label><input type="checkbox" id="' + id + '-zero"' + (initial === 0 ? ' checked' : '') + ' onchange="actionEditorOperationTimingChanged(\'' + prefix + '\',\'' + operation.id + '\',\'' + argument.name + '\')"> ' + escAttr(argument.zero_label || 'No timeout') + '</label>';
                    } else {
                        html += '<input type="number" step="1" min="' + argument.min + '" max="' + argument.max + '" value="' + initial + '" class="form-control form-control-sm" id="' + id + '-fixed">';
                    }
                }
                html += '</div>';
                if (argument.dynamic) {
                    html += '<div id="' + id + '-dynamic-group" style="display:none;"><input type="text" class="form-control form-control-sm binding-mono" id="' + id + '-template" placeholder="' + escAttr(argument.example) + '">';
                    var expected = argument.control === 'screens' ? 'screen ID'
                        : argument.type === 'boolean' ? 'true or false'
                        : argument.type === 'enum' ? (argument.choices || []).map(escAttr).join(', ')
                        : argument.type === 'string' ? 'text' : 'whole number';
                    html += '<small>Expected: ' + expected + '. Example: <code>' + escAttr(argument.example) + '</code></small></div>';
                }
                html += '</div>';
            });
            html += '</div>';
        });
    });
    return html;
}

function actionEditorUpdateSounds(select, sounds) {
    if (!select || !Array.isArray(sounds)) return;
    var selected = select.value;
    while (select.options.length > 1) select.remove(1);
    var names = sounds.slice();
    if (selected && !names.includes(selected)) names.push(selected);
    names.forEach(function(name) {
        var option = document.createElement('option');
        option.value = name;
        option.textContent = name;
        select.appendChild(option);
    });
    select.value = selected;
}

function actionEditorLoadOperationSounds(prefix, operation) {
    var fields = operation.arguments.filter(function(argument) { return argument.control === 'sounds'; });
    if (!fields.length || typeof fetch !== 'function') return;
    var selects = fields.map(function(argument) {
        var id = prefix + '-op-' + operation.id + '-' + argument.name + '-fixed';
        return { id: id, element: document.getElementById(id) };
    });
    return fetch('/api/sounds/list').then(function(response) {
            if (!response.ok) throw new Error('HTTP ' + response.status);
            return response.json();
        })
        .then(function(sounds) {
            if (!Array.isArray(sounds)) throw new Error('Invalid sound list');
            selects.forEach(function(select) {
                if (document.getElementById(select.id) === select.element) {
                    actionEditorUpdateSounds(select.element, sounds);
                    var error = document.getElementById(select.id.replace(/-fixed$/, '-load-error'));
                    if (error) error.textContent = '';
                }
            });
        }).catch(function(error) {
            console.error('Cannot load sounds:', error);
            selects.forEach(function(select) {
                if (document.getElementById(select.id) !== select.element) return;
                var message = document.getElementById(select.id.replace(/-fixed$/, '-load-error'));
                if (message) message.textContent = 'Could not load sounds. Reopen this action to retry.';
            });
        });
}

function actionEditorOperationTimingChanged(prefix, id, name) {
    var base = prefix + '-op-' + id + '-' + name;
    var input = document.getElementById(base + '-fixed');
    var zero = document.getElementById(base + '-zero');
    if (input && zero) input.disabled = zero.checked;
}

function actionEditorOperationFixedChanged(prefix, id, name) {
    var base = prefix + '-op-' + id + '-' + name;
    var fixed = document.getElementById(base + '-fixed');
    var template = document.getElementById(base + '-template');
    if (!fixed || !template) return;
    template.value = fixed.value;
    template.setAttribute('data-dynamic-initialized', '1');
}

function actionEditorOperationModeChanged(prefix, id, name) {
    var base = prefix + '-op-' + id + '-' + name;
    var toggle = document.getElementById(base + '-dynamic');
    var fixed = document.getElementById(base + '-fixed');
    var template = document.getElementById(base + '-template');
    if (toggle && toggle.checked && fixed && fixed.tagName === 'SELECT' && template &&
        !template.hasAttribute('data-dynamic-initialized')) {
        actionEditorOperationFixedChanged(prefix, id, name);
    }
    actionEditorOperationRefresh(prefix, id);
}

function actionEditorOperationRefresh(prefix, id) {
    var operation = actionEditorOperation(id);
    if (!operation) return;
    operation.arguments.forEach(function(argument) {
        var base = prefix + '-op-' + id + '-' + argument.name;
        var toggle = document.getElementById(base + '-dynamic');
        var fixed = document.getElementById(base + '-fixed-group');
        var group = document.getElementById(base + '-dynamic-group');
        var label = document.getElementById(base + '-label');
        if (fixed) fixed.style.display = toggle && toggle.checked ? 'none' : '';
        if (group) group.style.display = toggle && toggle.checked ? '' : 'none';
        if (label) label.htmlFor = base + (toggle && toggle.checked ? '-template' : '-fixed');
        if (argument.control === 'timing')
            actionEditorOperationTimingChanged(prefix, operation.id, argument.name);
    });
}

function actionEditorBuildOperation(prefix, operation) {
    var args = {};
    operation.arguments.forEach(function(argument) {
        var base = prefix + '-op-' + operation.id + '-' + argument.name;
        var dynamic = document.getElementById(base + '-dynamic');
        var input = document.getElementById(base + (dynamic && dynamic.checked ? '-template' : '-fixed'));
        var value = input ? input.value : '';
        var error = '';
        if (dynamic && dynamic.checked) {
            var checked = validateBinding(value, { requireKnownScheme: true });
            if (!value || new TextEncoder().encode(value).length >= 192) error = 'Enter a dynamic value under 192 bytes.';
            else if (!checked.valid) error = checked.errors[0].message;
            args[argument.name] = { '$template': value };
        } else if (argument.type === 'integer') {
            var zero = argument.control === 'timing' && document.getElementById(base + '-zero');
            if (zero && zero.checked) value = '0';
            if (!/^-?\d+$/.test(value) || Number(value) < argument.min || Number(value) > argument.max ||
                (argument.nonzero_min && Number(value) !== 0 && Number(value) < argument.nonzero_min))
                error = argument.nonzero_min ? 'Enter 0 or a whole number from ' + argument.nonzero_min + ' to ' + argument.max + '.'
                    : 'Enter a whole number from ' + argument.min + ' to ' + argument.max + '.';
            args[argument.name] = Number(value);
        } else if (argument.type === 'boolean') {
            if (value !== 'true' && value !== 'false') error = 'Select enabled or disabled.';
            args[argument.name] = value === 'true';
        } else if (argument.type === 'string') {
            if ((!value && !argument.allow_empty) || new TextEncoder().encode(value).length > argument.max_length)
                error = 'Enter text within the allowed length.';
            else if (argument.control === 'variable' && !/^[A-Za-z0-9_.-]+$/.test(value))
                error = 'Use letters, numbers, underscores, hyphens, or periods.';
            else if (argument.control === 'list_ids' && !/^[A-Za-z0-9_-]+$/.test(value))
                error = 'Use letters, numbers, underscores, or hyphens for the List ID.';
            else if (argument.control === 'pad_exclusions' && value) {
                var maxPads = typeof deviceInfoCache !== 'undefined' && deviceInfoCache
                    ? Number(deviceInfoCache.max_pads) : 0;
                if (!Number.isFinite(maxPads) || maxPads < 1) maxPads = Math.floor(argument.max_length / 3);
                maxPads = Math.floor(maxPads);
                var pads = value.split(',');
                if (pads.some(function(pad) { return !/^[ \t\r\n\v\f]*\d+[ \t\r\n\v\f]*$/.test(pad) || Number(pad) < 1 || Number(pad) > maxPads; }))
                    error = 'Enter comma-separated pad numbers from 1 to ' + maxPads + '.';
            }
            else if (argument.control === 'color' && value &&
                     !/^(?:#|0x)?(?:[0-9a-f]{3}|[0-9a-f]{6})[ \t\r\n]*$/i.test(value))
                error = 'Enter a hex color.';
            else if (argument.control === 'json_object' && value) {
                try {
                    var parsed = JSON.parse(value);
                    if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed))
                        error = 'Enter a JSON object.';
                } catch (_) {
                    error = 'Enter a JSON object.';
                }
            }
            args[argument.name] = value;
        } else if (argument.type === 'enum') {
            if (!(argument.choices || []).includes(value)) error = 'Select an available choice.';
            args[argument.name] = value;
        } else {
            if (!value || new TextEncoder().encode(value).length > argument.max_length) error = 'Select a screen.';
            args[argument.name] = value;
        }
        if (input && input.setCustomValidity) input.setCustomValidity(error);
        if (error) throw Object.assign(new Error(error), { fieldIds: [input.id] });
    });
    return { op: operation.id, args: args };
}

// Grouped <optgroup> markup for the type <select>, in catalog order.
function actionEditorTypeOptionsHTML(opts) {
    var order = [];
    var byGroup = {};
    actionEditorCatalog().forEach(function(entry) {
        var choices = [];
        if ((!opts || !opts.operations || (opts.mixed && !(entry.operations || []).length)) &&
            (!opts || !opts.alarmHook || entry.alarm_hook_allowed === true))
            choices.push('<option value="' + entry.type + '">' + entry.label + '</option>');
        if (opts && opts.operations) (entry.operations || []).forEach(function(operation) {
            if (!opts.alarmHook || operation.alarm_hook_allowed === true)
                choices.push('<option value="@' + operation.id + '">' + operation.label + '</option>');
        });
        if (!choices.length) return;
        if (!byGroup[entry.group]) { byGroup[entry.group] = []; order.push(entry.group); }
        byGroup[entry.group].push(choices.join(''));
    });
    var html = '<option value="">(none)</option>';
    order.forEach(function(group) {
        html += '<optgroup label="' + group + '">';
        html += byGroup[group].join('');
        html += '</optgroup>';
    });
    return html;
}

function actionEditorFilterOperations(prefix) {
    var search = document.getElementById(prefix + '-search');
    var select = document.getElementById(prefix + '-type');
    if (!search || !select) return;
    var query = search.value.trim().toLowerCase();
    var count = 0;
    Array.from(select.options).forEach(function(option) {
        var group = option.parentElement;
        var groupLabel = group && group.tagName === 'OPTGROUP' ? group.label : '';
        var match = !query || (option.textContent + ' ' + option.value + ' ' + groupLabel).toLowerCase().includes(query);
        if (option.value && match) count++;
        option.hidden = !!option.value && !match && !option.selected;
    });
    Array.from(select.children).forEach(function(group) {
        if (group.tagName === 'OPTGROUP') {
            group.hidden = !Array.from(group.children).some(function(option) { return !option.hidden; });
        }
    });
    var status = document.getElementById(prefix + '-search-status');
    var noun = select.getAttribute('data-mixed') === 'true' ? ' action' : ' operation';
    if (status) status.textContent = query ? count + ' matching' + noun + (count === 1 ? '' : 's') : '';
    if (typeof dropdownRefresh === 'function') dropdownRefresh(select);
}

// <option> tags for a multi-command type's Command selector. '' for a
// direct action (no commands) or a type absent from this build's catalog.
function actionEditorCommandOptionsHTML(type, selected) {
    var entry = actionEditorCatalogEntry(type);
    if (!entry || !entry.commands) return '';
    return entry.commands.map(function(c) {
        return '<option value="' + c.id + '"' + (selected !== undefined && String(c.id) === String(selected) ? ' selected' : '') + '>' + c.label + '</option>';
    }).join('');
}

// Render only the small common field vocabulary. Actions that need conditional
// controls or visual previews keep their explicit editor extension.
function actionEditorGenericFieldsHTML(prefix) {
    var html = '';
    actionEditorCatalog().forEach(function(entry) {
        var fields = actionEditorGenericFields(entry.type);
        if (!fields.length) return;
        html += '<div id="' + prefix + '-generic-' + entry.type + '-group" style="display:none;">';
        fields.forEach(function(field) {
            var id = prefix + '-generic-' + entry.type + '-' + field.name;
            html += '<div class="form-group">';
            if (field.type === 'toggle') {
                html += '<label><input type="checkbox" id="' + id + '"> ' + field.label + '</label>';
            } else {
                html += '<label class="form-label" for="' + id + '">' + field.label;
                if (field.bindable) html += ' <span class="fx-hint" onclick="showBindingHelp()">fx</span>';
                html += '</label>';
                if (field.type === 'select') {
                    html += '<select class="form-select form-select-sm" id="' + id + '">';
                    if (field.command_options) html += actionEditorCommandOptionsHTML(entry.type, field.default);
                    else if (field.options_source === 'sounds') html += '<option value="">(none)</option>';
                    else if (field.options) html += field.options.map(function(option) {
                        return '<option value="' + option.id + '"' + (field.default !== undefined && String(option.id) === String(field.default) ? ' selected' : '') + '>' + option.label + '</option>';
                    }).join('');
                    html += '</select>';
                } else {
                    var inputType = field.type === 'number' ? 'number' : 'text';
                    html += '<input type="' + inputType + '" class="form-control form-control-sm" id="' + id + '"';
                    if (field.min !== undefined) html += ' min="' + Number(field.min) + '"';
                    if (field.max !== undefined) html += ' max="' + Number(field.max) + '"';
                    html += '>';
                }
            }
            if (field.help) html += '<small>' + field.help + '</small>';
            html += '</div>';
        });
        html += '</div>';
    });
    return html;
}

function actionEditorGenericFields(type) {
    if (_actionEditorExtensions.some(function(ext) { return ext.type === type; })) return [];
    var entry = actionEditorCatalogEntry(type);
    return entry && entry.editor_fields ? entry.editor_fields : [];
}

function actionEditorSetGenericFields(prefix, type, action) {
    actionEditorGenericFields(type).forEach(function(field) {
        var el = document.getElementById(prefix + '-generic-' + type + '-' + field.name);
        if (!el) return;
        if (field.type === 'toggle') el.checked = !!action[field.name];
        else {
            var value = action[field.name] === undefined ? (field.default === undefined ? '' : field.default) : action[field.name];
            if (field.options_source === 'sounds' && value && !Array.from(el.options).some(function(option) { return option.value === value; })) {
                var option = document.createElement('option');
                option.value = value;
                option.textContent = value;
                el.appendChild(option);
            }
            el.value = value;
        }
    });
}

function actionEditorBuildGenericFields(prefix, type, action) {
    actionEditorGenericFields(type).forEach(function(field) {
        var el = document.getElementById(prefix + '-generic-' + type + '-' + field.name);
        if (!el) return;
        if (field.type === 'toggle') {
            action[field.name] = el.checked;
        } else if (el.value !== '') {
            action[field.name] = field.type === 'number' || field.numeric ? Number(el.value) : el.value.trim();
        }
    });
}

function actionEditorBindingSuffixes(type) {
    var extension = _actionEditorExtensions.find(function(candidate) { return candidate.type === type; });
    var suffixes = extension ? (extension.bindingSuffixes || []).slice() : [];
    if (extension) (extension.colorSuffixes || []).forEach(function(suffix) {
        suffixes.push(suffix.replace(/-wrap$/, ''));
    });
    actionEditorGenericFields(type).forEach(function(field) {
        if (field.bindable) suffixes.push('-generic-' + type + '-' + field.name);
    });
    return suffixes;
}

function actionEditorInitBindings(prefix, type) {
    var extension = _actionEditorExtensions.find(function(candidate) { return candidate.type === type; });
    if (extension) (extension.colorSuffixes || []).forEach(function(suffix) {
        var wrap = document.getElementById(prefix + suffix);
        if (wrap) padInitBindableColor(wrap);
    });
    actionEditorBindingSuffixes(type).forEach(function(suffix) {
        var input = document.getElementById(prefix + suffix);
        if (input && !input.dataset.bcBind && typeof bindingAttachValidation === 'function') {
            input.dataset.bcBind = '1';
            bindingAttachValidation(input);
        }
    });
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
    var entry = type.charAt(0) === '@' ? actionEditorOperation(type.slice(1)) : actionEditorCatalogEntry(type);
    var disallowed = select.getAttribute('data-alarm-hook') === 'true' && (!entry || entry.alarm_hook_allowed !== true);
    opt.textContent = disallowed ? type + ' (not allowed for alarm hooks)'
        : entry ? entry.label : type + ' (unsupported by this build)';
    opt.disabled = !entry || disallowed;
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
    if (opts.operations) {
        h += '<label class="form-label" for="' + prefix + '-search">Search ' + (opts.mixed ? 'actions' : 'operations') + '</label>';
        h += '<input type="search" class="form-control form-control-sm action-operation-search" id="' + prefix + '-search" aria-controls="' + prefix + '-type" aria-describedby="' + prefix + '-search-status" oninput="actionEditorFilterOperations(\'' + prefix + '\'); event.stopPropagation()" onchange="event.stopPropagation()">';
        h += '<small id="' + prefix + '-search-status" role="status" aria-live="polite"></small>';
    }
    h += '<select class="form-select form-select-sm action-type-select" id="' + prefix + '-type" data-alarm-hook="' + (opts.alarmHook === true) + '" data-operations="' + (opts.operations === true) + '" data-mixed="' + (opts.mixed === true) + '" onchange="actionEditorTypeChanged(\'' + prefix + '\')">';
    h += actionEditorTypeOptionsHTML(opts);
    h += '</select>';
    h += '<small id="' + prefix + '-context" class="action-context" style="display:none;"></small>';
    h += '</div>';
    h += '<div id="' + prefix + '-fields">';
    h += actionEditorGenericFieldsHTML(prefix);
    if (opts.operations) h += actionEditorOperationHTML(prefix);
    // Extension-contributed groups (e.g. shutter command UI on shutter-tester builds)
    _actionEditorExtensions.forEach(function(ext) { if (ext.groups) h += ext.groups(prefix, opts); });
    return h + '</div>';
}

// Show/hide sub-groups when the action type dropdown changes.
function actionEditorTypeChanged(prefix) {
    var typeEl = document.getElementById(prefix + '-type');
    if (!typeEl) return;
    var type = typeEl.value;
    if (typeEl.getAttribute('data-operations') === 'true') actionEditorFilterOperations(prefix);
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
    actionEditorCatalog().forEach(function(entry) {
        var group = document.getElementById(prefix + '-generic-' + entry.type + '-group');
        if (group) group.style.display = entry.type === type ? '' : 'none';
        (entry.operations || []).forEach(function(operation) {
            var operationGroup = document.getElementById(prefix + '-op-' + operation.id + '-group');
            if (operationGroup) operationGroup.style.display = type === '@' + operation.id ? '' : 'none';
            if (type === '@' + operation.id) operation.arguments.forEach(function(argument) {
                if (argument.control !== 'color') return;
                var base = prefix + '-op-' + operation.id + '-' + argument.name;
                var wrap = document.getElementById(base + '-color-wrap');
                if (wrap) padInitBindableColor(wrap);
            });
            if (type === '@' + operation.id) actionEditorLoadOperationSounds(prefix, operation);
            actionEditorOperationRefresh(prefix, operation.id);
        });
    });
    actionEditorInitBindings(prefix, type);
    // Extension-contributed type-change hooks (e.g. shutter group visibility)
    _actionEditorExtensions.forEach(function(ext) { if (ext.typeChanged) ext.typeChanged(prefix, type); });
}

// Load an action object { type, target, topic, payload, sequence } into the form.
function actionEditorLoad(prefix, action) {
    if (!action) action = {};
    var fields = document.getElementById(prefix + '-fields');
    if (fields && fields.querySelectorAll) fields.querySelectorAll('input, select, textarea').forEach(function(input) {
        if (input.tagName === 'SELECT') {
            var options = Array.from(input.options);
            var selected = options.findIndex(function(option) { return option.defaultSelected; });
            input.selectedIndex = selected < 0 ? 0 : selected;
        } else {
            input.value = input.defaultValue;
            input.checked = input.defaultChecked;
        }
        input.removeAttribute('data-dynamic-initialized');
        if (input.setCustomValidity) input.setCustomValidity('');
    });
    var el;
    el = document.getElementById(prefix + '-type');
    if (el) {
        var isOperation = !action.type && action.op;
        el.value = isOperation ? '@' + action.op : (action.type || '');
        if (isOperation && el.value !== '@' + action.op) {
            actionEditorEnsureUnsupportedOption(el, '@' + action.op);
            el.value = '@' + action.op;
        }
        // The catalog can refresh after this editor's HTML was rendered. In
        // that case the catalog recognizes the type but the existing select
        // has no matching option, leaving it at '(none)' while extension
        // fields still load. Restore the missing option before loading it.
        if (action.type && el.value !== action.type) {
            actionEditorEnsureUnsupportedOption(el, action.type);
            el.value = action.type;
        }
        if (isOperation) {
            var operation = actionEditorOperation(action.op);
            if (operation) operation.arguments.forEach(function(argument) {
                var base = prefix + '-op-' + operation.id + '-' + argument.name;
                var value = (action.args || {})[argument.name];
                var dynamic = value && typeof value === 'object' && typeof value.$template === 'string';
                var toggle = document.getElementById(base + '-dynamic');
                var input = document.getElementById(base + (dynamic ? '-template' : '-fixed'));
                if (toggle) toggle.checked = dynamic;
                if (input && value !== undefined) {
                    if (!dynamic && argument.control === 'sounds' && value &&
                        !Array.from(input.options).some(function(option) { return option.value === String(value); })) {
                        var sound = document.createElement('option');
                        sound.value = String(value);
                        sound.textContent = String(value);
                        input.appendChild(sound);
                    }
                    if (!dynamic && argument.control === 'timing' && argument.nonzero_min) {
                        var zero = document.getElementById(base + '-zero');
                        if (zero) zero.checked = value === 0;
                        input.value = value === 0 ? String(argument.nonzero_min) : String(value);
                        actionEditorOperationTimingChanged(prefix, operation.id, argument.name);
                    } else input.value = dynamic ? value.$template : String(value);
                    if (dynamic) input.setAttribute('data-dynamic-initialized', '1');
                    if (!dynamic && input.tagName === 'SELECT' && input.value !== String(value))
                        input.setAttribute('data-pending-value', String(value));
                    if (!dynamic && argument.control === 'color' && input.dispatchEvent)
                        input.dispatchEvent(new Event('input', { bubbles: true }));
                }
            });
        }
        var legacyEntry = actionEditorCatalogEntry(action.type);
        if (action.type && (!legacyEntry || (el.getAttribute('data-operations') === 'true' &&
            (el.getAttribute('data-mixed') !== 'true' || (legacyEntry.operations || []).length)))) {
            _actionEditorUnsupported[prefix] = action;
        } else {
            delete _actionEditorUnsupported[prefix];
        }
    }
    actionEditorSetGenericFields(prefix, action.type || '', action);
    // Extension-contributed load hooks (e.g. shutter field population)
    _actionEditorExtensions.forEach(function(ext) { if (ext.load) ext.load(prefix, action); });
    actionEditorTypeChanged(prefix);
}

// Build an action object from the form. Returns {} if type is empty.
function actionEditorBuild(prefix, eventToken, eventValues) {
    var typeEl = document.getElementById(prefix + '-type');
    if (!typeEl) return {};
    var type = typeEl.value;
    if (!type) return {};
    if (type.charAt(0) === '@') {
        var operation = actionEditorOperation(type.slice(1));
        if (!operation) throw Object.assign(new Error('Operation is unavailable.'), { fieldIds: [prefix + '-type'] });
        if (typeEl.getAttribute('data-alarm-hook') === 'true' && operation.alarm_hook_allowed !== true)
            throw Object.assign(new Error('Operation "' + type.slice(1) + '" is not allowed for alarm hooks. Replace or remove it.'), { fieldIds: [prefix + '-type'] });
        return actionEditorBuildOperation(prefix, operation);
    }
    var entry = actionEditorCatalogEntry(type);
    if (typeEl.getAttribute('data-alarm-hook') === 'true' && (!entry || entry.alarm_hook_allowed !== true)) {
        throw Object.assign(new Error('Action "' + type + '" is not allowed for alarm hooks. Replace or remove it.'), { fieldIds: [prefix + '-type'] });
    }
    // Round-trip a persisted action whose type this build's catalog does not
    // contain — its fields have no editor, so save it back exactly as loaded.
    if (_actionEditorUnsupported[prefix] && _actionEditorUnsupported[prefix].type === type) {
        return _actionEditorUnsupported[prefix];
    }
    var act = { type: type };
    actionEditorBuildGenericFields(prefix, type, act);
    // Extension-contributed build hooks (e.g. shutter merges shutter_command/shutter_value).
    _actionEditorExtensions.forEach(function(ext) {
        if (ext.build) {
            var extra = ext.build(prefix, type);
            if (extra) { for (var k in extra) act[k] = extra[k]; }
        }
    });
    actionEditorValidateEventTokens(act, eventToken, prefix, eventValues);
    return act;
}

function actionEditorValidateEventTokens(action, eventToken, prefix, eventValues) {
    const entry = actionEditorCatalogEntry(action.type) || {};
    const fields = entry.template_fields || {};
    const fail = (field, message) => {
        throw Object.assign(new Error(field + ': ' + message), { fieldIds: [prefix + '-type'] });
    };
    const candidates = eventValues === undefined ? [null] : (eventValues.length ? eventValues : ['value']);
    for (const candidate of candidates) {
        const normalized = { ...action };
        for (const [name, value] of Object.entries(action)) {
            if (/\{(?:value|step)\}/.test(name)) fail(name, 'event tokens are not valid in field names');
            if (typeof value !== 'string') continue;
            let expanded = value;
            for (const token of ['{value}', '{step}']) {
                if (!value.includes(token)) continue;
                if (token !== eventToken)
                    fail(name, token + ' is unavailable here' + (eventToken ? '; use ' + eventToken : ''));
                if (!fields[name]) fail(name, 'this field does not support event tokens');
                expanded = value.replaceAll(token, candidate === null ? '-999999.9999' : candidate);
                normalized[name] = value.replaceAll(token, candidate === null ? '0' : candidate);
            }
            if (fields[name] && new TextEncoder().encode(expanded).length > fields[name].max)
                fail(name, 'expanded value exceeds ' + fields[name].max + ' bytes');
        }
        _actionEditorExtensions.forEach(extension => {
            if (extension.validate) extension.validate(normalized, prefix);
        });
    }
}

function actionEditorHandleError(error) {
    if (!error.fieldIds || !error.fieldIds.length) return false;
    var input = document.getElementById(error.fieldIds[0]);
    if (input) {
        for (var parent = input.parentElement; parent; parent = parent.parentElement) {
            if (parent.tagName === 'DETAILS') parent.open = true;
        }
        if (typeof bindingShowError === 'function') bindingShowError(input, error.message);
        input.focus();
    }
    showMessage(error.message, 'error');
    return true;
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
function actionEditorListBuild(prefixes, eventToken, eventValues) {
    return prefixes.map(function(prefix) { return actionEditorBuild(prefix, eventToken, eventValues); })
        .filter(function(action) { return !!(action.type || action.op); });
}

function actionEditorWireFragment(prefixes) {
    if (typeof getDeviceInfo !== 'function') return Promise.resolve();
    return getDeviceInfo().then(function(info) {
        return Promise.all(_actionEditorExtensions.map(function(extension) {
            return extension.wireFragment ? extension.wireFragment(prefixes, info) : null;
        }));
    });
}
