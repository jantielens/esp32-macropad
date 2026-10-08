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

// Grouped <optgroup> markup for the type <select>, in catalog order.
function actionEditorTypeOptionsHTML(opts) {
    var order = [];
    var byGroup = {};
    actionEditorCatalog().forEach(function(entry) {
        if (opts && opts.alarmHook && entry.alarm_hook_allowed !== true) return;
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
    var entry = actionEditorCatalogEntry(type);
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
    h += '<select class="form-select form-select-sm action-type-select" id="' + prefix + '-type" data-alarm-hook="' + (opts.alarmHook === true) + '" onchange="actionEditorTypeChanged(\'' + prefix + '\')">';
    h += actionEditorTypeOptionsHTML(opts);
    h += '</select>';
    h += '<small id="' + prefix + '-context" class="action-context" style="display:none;"></small>';
    h += '</div>';
    h += actionEditorGenericFieldsHTML(prefix);
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
    actionEditorCatalog().forEach(function(entry) {
        var group = document.getElementById(prefix + '-generic-' + entry.type + '-group');
        if (group) group.style.display = entry.type === type ? '' : 'none';
    });
    actionEditorInitBindings(prefix, type);
    // Extension-contributed type-change hooks (e.g. shutter group visibility)
    _actionEditorExtensions.forEach(function(ext) { if (ext.typeChanged) ext.typeChanged(prefix, type); });
}

// Load an action object { type, target, topic, payload, sequence } into the form.
function actionEditorLoad(prefix, action) {
    if (!action) action = {};
    var el;
    el = document.getElementById(prefix + '-type');
    if (el) {
        el.value = action.type || '';
        // The catalog can refresh after this editor's HTML was rendered. In
        // that case the catalog recognizes the type but the existing select
        // has no matching option, leaving it at '(none)' while extension
        // fields still load. Restore the missing option before loading it.
        if (action.type && el.value !== action.type) {
            actionEditorEnsureUnsupportedOption(el, action.type);
            el.value = action.type;
        }
        if (action.type && !actionEditorCatalogEntry(action.type)) {
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
function actionEditorBuild(prefix) {
    var typeEl = document.getElementById(prefix + '-type');
    if (!typeEl) return {};
    var type = typeEl.value;
    if (!type) return {};
    var entry = actionEditorCatalogEntry(type);
    if (typeEl.getAttribute('data-alarm-hook') === 'true' && (!entry || entry.alarm_hook_allowed !== true)) {
        throw new Error('Action "' + type + '" is not allowed for alarm hooks. Replace or remove it.');
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
    return act;
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

function actionEditorWireFragment(prefixes) {
    if (typeof getDeviceInfo !== 'function') return Promise.resolve();
    return getDeviceInfo().then(function(info) {
        return Promise.all(_actionEditorExtensions.map(function(extension) {
            return extension.wireFragment ? extension.wireFragment(prefixes, info) : null;
        }));
    });
}
