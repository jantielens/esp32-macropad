// ============================================================================
// Shared Action Editor — reusable UI component for ButtonAction editing
// ============================================================================
// Catalog-backed operation fields and reusable action-list plumbing.

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
                if (argument.control === 'hidden') {
                    html += '<input type="hidden" id="' + id + '-fixed" value="' + escAttr(String(argument.default)) + '">';
                    return;
                }
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
                } else if (argument.control === 'brew_templates') {
                    html += '<select class="form-select form-select-sm" id="' + id + '-fixed"' + fixedChanged + '><option value="">Loading templates...</option></select>';
                    html += '<small class="text-danger" id="' + id + '-load-error" role="alert"></small>';
                } else if (argument.type === 'boolean' && argument.control === 'checkbox') {
                    html += '<input type="checkbox" class="form-check-input" id="' + id + '-fixed"' +
                        (argument.default === true ? ' checked' : '') + '>';
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
                        html += '<input type="number" step="' + (argument.type === 'number' ? 'any' : '1') + '" min="' + argument.min + '" max="' + argument.max + '" value="' + initial + '" class="form-control form-control-sm" id="' + id + '-fixed">';
                    }
                }
                html += '</div>';
                if (argument.description)
                    html += '<small class="form-text">' + escAttr(argument.description) + '</small>';
                if (argument.dynamic) {
                    html += '<div id="' + id + '-dynamic-group" style="display:none;"><input type="text" class="form-control form-control-sm binding-mono" id="' + id + '-template" placeholder="' + escAttr(argument.example) + '">';
                    var expected = argument.control === 'screens' ? 'screen ID'
                        : argument.type === 'boolean' ? 'true or false'
                        : argument.type === 'enum' ? (argument.choices || []).map(escAttr).join(', ')
                        : argument.type === 'string' ? 'text' : argument.type === 'number' ? 'number' : 'whole number';
                    html += '<small>Expected: ' + expected + '. Example: <code>' + escAttr(argument.example) + '</code></small></div>';
                }
                html += '</div>';
            });
            html += '</div>';
        });
    });
    return html;
}

// @portal-if IS_COFFEE_SCALE
function actionEditorLoadBrewTemplates(prefix, operation) {
    var fields = operation.arguments.filter(function(argument) { return argument.control === 'brew_templates'; });
    if (!fields.length || typeof fetch !== 'function') return;
    var selects = fields.map(function(argument) {
        var id = prefix + '-op-' + operation.id + '-' + argument.name + '-fixed';
        return { id: id, element: document.getElementById(id) };
    });
    return fetch('/api/brew-templates').then(function(response) {
        if (!response.ok) throw new Error('HTTP ' + response.status);
        return response.json();
    }).then(function(templates) {
        if (!Array.isArray(templates)) throw new Error('Invalid template list');
        selects.forEach(function(select) {
            var element = select.element;
            if (!element || document.getElementById(select.id) !== element) return;
            var selected = element.getAttribute('data-pending-value') || element.value;
            while (element.options.length) element.remove(0);
            templates.forEach(function(template) {
                if (!template || typeof template.name !== 'string' || !template.name) return;
                var option = document.createElement('option');
                option.value = template.name;
                option.textContent = template.display_name || template.name;
                element.appendChild(option);
            });
            if (selected && !Array.from(element.options).some(function(option) { return option.value === selected; })) {
                var missing = document.createElement('option');
                missing.value = selected;
                missing.textContent = selected + ' (unavailable)';
                element.appendChild(missing);
            }
            if (selected) element.value = selected;
            element.removeAttribute('data-pending-value');
            var template = document.getElementById(select.id.replace(/-fixed$/, '-template'));
            if (template && template.hasAttribute('data-dynamic-waiting')) {
                if (!template.value && element.value) {
                    template.value = element.value;
                    template.setAttribute('data-dynamic-initialized', '1');
                }
                template.removeAttribute('data-dynamic-waiting');
            }
            var error = document.getElementById(select.id.replace(/-fixed$/, '-load-error'));
            if (error) error.textContent = element.options.length ? '' : 'No brew templates are available.';
        });
    }).catch(function(error) {
        console.error('Cannot load brew templates:', error);
        selects.forEach(function(select) {
            if (document.getElementById(select.id) !== select.element) return;
            var message = document.getElementById(select.id.replace(/-fixed$/, '-load-error'));
            if (message) message.textContent = 'Could not load brew templates. Reopen this action to retry.';
        });
    });
}
// @portal-endif

// @portal-if HAS_SOUND_PLAYER
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

function actionEditorPopulateSounds(prefixes, sounds) {
    if (!Array.isArray(sounds)) return;
    prefixes.forEach(function(prefix) {
        actionEditorCatalog().forEach(function(entry) {
            (entry.operations || []).forEach(function(operation) {
                operation.arguments.forEach(function(argument) {
                    if (argument.control === 'sounds')
                        actionEditorUpdateSounds(document.getElementById(prefix + '-op-' + operation.id + '-' + argument.name + '-fixed'), sounds);
                });
            });
        });
    });
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

// @portal-endif

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
    var value = fixed.value || fixed.getAttribute('data-pending-value');
    if (!value) {
        template.setAttribute('data-dynamic-waiting', '1');
        return;
    }
    template.value = value;
    template.removeAttribute('data-dynamic-waiting');
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
        if (input && argument.control === 'checkbox' && !(dynamic && dynamic.checked))
            value = String(input.checked);
        var error = '';
        if (dynamic && dynamic.checked) {
            var checked = validateBinding(value, { requireKnownScheme: true });
            if (!value || new TextEncoder().encode(value).length >= 192) error = 'Enter a dynamic value under 192 bytes.';
            else if (!checked.valid) error = checked.errors[0].message;
            args[argument.name] = { '$template': value };
        } else if (argument.type === 'number') {
            if (!value.trim() || !Number.isFinite(Number(value)) ||
                Number(value) < argument.min || Number(value) > argument.max)
                error = 'Enter a number from ' + argument.min + ' to ' + argument.max + '.';
            args[argument.name] = Number(value);
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
        (entry.operations || []).forEach(function(operation) {
            if (!opts || !opts.alarmHook || operation.alarm_hook_allowed === true)
                choices.push('<option value="@' + operation.id + '">' + operation.label + '</option>');
        });
        if (!choices.length) return;
        if (!byGroup[entry.group]) { byGroup[entry.group] = []; order.push(entry.group); }
        byGroup[entry.group].push(choices.join(''));
    });
    var html = '<option value="">Choose an action...</option>';
    order.forEach(function(group) {
        html += '<optgroup label="' + group + '">';
        html += byGroup[group].join('');
        html += '</optgroup>';
    });
    return html;
}

function actionEditorClear(prefix) {
    var select = document.getElementById(prefix + '-type');
    if (!select) return;
    select.value = '';
    select.dispatchEvent(new Event('change', { bubbles: true }));
    if (select._cddToggle) select._cddToggle.focus();
}

// Give the <select> a disabled placeholder option for an unsupported type so
// el.value = type actually sticks (a <select> silently ignores an unknown value).
function actionEditorEnsureUnsupportedOption(select, type) {
    if (!select || !type) return;
    for (var i = 0; i < select.options.length; i++) {
        if (select.options[i].value === type) return;
    }
    var opt = document.createElement('option');
    opt.value = type;
    var entry = type.charAt(0) === '@' ? actionEditorOperation(type.slice(1)) : null;
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
    h += '<div class="d-flex justify-content-between align-items-center gap-2 mb-1">';
    h += '<label class="form-label mb-0" for="' + prefix + '-type">' + (label || 'Action') + '</label>';
    h += '<button type="button" class="btn btn-sm action-remove" id="' + prefix + '-remove" aria-label="Remove action" title="Remove action" hidden onclick="actionEditorClear(\'' + prefix + '\')"><span class="portal-icon" aria-hidden="true">delete</span><span class="portal-icon-fallback">Remove action</span></button></div>';
    h += '<select class="form-select form-select-sm action-type-select" id="' + prefix + '-type" data-search-placeholder="Search actions..." data-alarm-hook="' + (opts.alarmHook === true) + '" onchange="actionEditorTypeChanged(\'' + prefix + '\')">';
    h += actionEditorTypeOptionsHTML(opts);
    h += '</select>';
    h += '<small id="' + prefix + '-context" class="action-context" style="display:none;"></small>';
    h += '</div>';
    h += '<div id="' + prefix + '-fields">';
    h += actionEditorOperationHTML(prefix);
    return h + '</div>';
}

// Show/hide sub-groups when the action type dropdown changes.
function actionEditorTypeChanged(prefix) {
    var typeEl = document.getElementById(prefix + '-type');
    if (!typeEl) return;
    var type = typeEl.value;
    if (typeof dropdownRefresh === 'function') dropdownRefresh(typeEl);
    var remove = document.getElementById(prefix + '-remove');
    if (remove) remove.hidden = !type;
    var contextEl = document.getElementById(prefix + '-context');
    if (contextEl) {
        var operation = type.charAt(0) === '@' ? actionEditorOperation(type.slice(1)) : null;
        var owner = operation && actionEditorCatalog().find(function(entry) {
            return (entry.operations || []).includes(operation);
        });
        contextEl.textContent = operation && owner ? (owner.group + ' / ' + operation.label) : '';
        contextEl.style.display = operation && owner ? '' : 'none';
    }
    actionEditorListRefreshSlot(prefix);
    actionEditorCatalog().forEach(function(entry) {
        (entry.operations || []).forEach(function(operation) {
            var operationGroup = document.getElementById(prefix + '-op-' + operation.id + '-group');
            if (operationGroup) operationGroup.style.display = type === '@' + operation.id ? '' : 'none';
            if (type === '@' + operation.id) operation.arguments.forEach(function(argument) {
                if (argument.control !== 'color') return;
                var base = prefix + '-op-' + operation.id + '-' + argument.name;
                var wrap = document.getElementById(base + '-color-wrap');
                if (wrap) padInitBindableColor(wrap);
            });
            // @portal-if HAS_SOUND_PLAYER
            if (type === '@' + operation.id) actionEditorLoadOperationSounds(prefix, operation);
            // @portal-endif
            // @portal-if IS_COFFEE_SCALE
            if (type === '@' + operation.id) actionEditorLoadBrewTemplates(prefix, operation);
            // @portal-endif
            actionEditorOperationRefresh(prefix, operation.id);
        });
    });
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
        input.removeAttribute('data-dynamic-waiting');
        input.removeAttribute('data-pending-value');
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
                    } else if (!dynamic && argument.control === 'checkbox') input.checked = value === true;
                    else input.value = dynamic ? value.$template : String(value);
                    if (dynamic) input.setAttribute('data-dynamic-initialized', '1');
                    if (!dynamic && input.tagName === 'SELECT' && input.value !== String(value)) {
                        if (argument.control === 'screens' && input.hasAttribute('data-screens-loaded')) {
                            var missingScreen = document.createElement('option');
                            missingScreen.value = String(value);
                            missingScreen.textContent = String(value) + ' (unavailable)';
                            input.appendChild(missingScreen);
                            input.value = String(value);
                        } else input.setAttribute('data-pending-value', String(value));
                    }
                    if (!dynamic && argument.control === 'color' && input.dispatchEvent)
                        input.dispatchEvent(new Event('input', { bubbles: true }));
                }
            });
        }
    }
    actionEditorTypeChanged(prefix);
}

// Build an action object from the form. Returns {} if type is empty.
function actionEditorBuild(prefix) {
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
    throw Object.assign(new Error('Legacy actions are not supported. Replace or remove this action.'), { fieldIds: [prefix + '-type'] });
}

function actionEditorHandleError(error) {
    if (!error.fieldIds || !error.fieldIds.length) return false;
    var input = document.getElementById(error.fieldIds[0]);
    if (input) {
        for (var parent = input.parentElement; parent; parent = parent.parentElement) {
            if (parent.tagName === 'DETAILS') parent.open = true;
        }
        if (typeof bindingShowError === 'function') bindingShowError(input, error.message);
        if (input._cddToggle) input._cddToggle.focus();
        else input.focus();
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
function actionEditorListBuild(prefixes) {
    return prefixes.map(function(prefix) { return actionEditorBuild(prefix); })
        .filter(function(action) { return !!action.op; });
}

function actionEditorWireFragment(prefixes) {
    // @portal-if HAS_DISPLAY
    if (typeof getDeviceInfo !== 'function') return Promise.resolve();
    return getDeviceInfo().then(function(info) {
        actionEditorPopulateScreens(prefixes, info && info.available_screens);
    });
    // @portal-endif
    return Promise.resolve();
}
