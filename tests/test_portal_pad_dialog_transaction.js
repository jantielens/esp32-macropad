const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

class Element {
    constructor(id) {
        this.id = id;
        this.value = '';
        this.checked = false;
        this.textContent = '';
        this.style = { display: 'none', setProperty(name, value) { this[name] = value; } };
        this.dataset = {};
        this.attributes = {};
        const classes = new Set();
        this.classList = {
            contains: name => classes.has(name),
            remove: name => classes.delete(name),
            toggle(name, enabled) { if (enabled) classes.add(name); else classes.delete(name); }
        };
    }
    setAttribute(name, value) { this.attributes[name] = value; }
    removeAttribute(name) { delete this.attributes[name]; }
}

const elements = new Map();
const document = {
    getElementById(id) {
        if (!elements.has(id)) elements.set(id, new Element(id));
        return elements.get(id);
    }
};

const originalButton = { col: 1, row: 2, label_center: 'Keep me' };
const originalButtons = [originalButton];
const context = {
    console,
    document,
    MAX_ACTIONS: 3,
    padState: { editCol: 1, editRow: 2, buttons: originalButtons },
    padLabelFromInput() { return ''; },
    padGetBindableColor() { return ''; },
    padGetEffectiveDefault() { return ''; },
    padFillBuild() {},
    actionEditorBuild() { throw new Error('Timer Duration is invalid'); },
    padActionPrefixes(gesture) { return [gesture + '-0', gesture + '-1', gesture + '-2']; },
    actionEditorListBuild(prefixes) {
        return prefixes.map(function(p) { return context.actionEditorBuild(p); })
            .filter(function(a) { return a && a.type; });
    }
};

vm.createContext(context);
vm.runInContext(fs.readFileSync('src/app/web/portal_widget_editor.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_widget_table.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_widget_gamepad_button.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_widget_rocker.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_widget_numericrocker.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_widget_inputs.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_widget_list.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_widget_camera.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_pad_dialog.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_pad_workspace.js', 'utf8'), context);
vm.runInContext('padWorkspace = { selected: true, loading: false, forms: new Map(), errors: new Map() };', context);
context.padWorkspaceCapture = function() {};
context.padWorkspaceSummaries = function() {};
context.padWorkspaceRefresh = function() {};
context.padMarkDirty = function() {};
context.padRenderGrid = function() {};

context.padWorkspaceEdit();
assert.match(document.getElementById('pad-edit-validation-message').textContent, /Timer Duration/);
assert.strictEqual(context.padState.buttons, originalButtons);
assert.strictEqual(context.padState.buttons.length, 1);
assert.strictEqual(context.padState.buttons[0], originalButton);

const validationErrors = vm.runInContext('padWorkspace.errors', context);
const errorCount = validationErrors.size;
assert.strictEqual(document.getElementById('pad-action-controls').inert, true);
context.padDialogDismissValidationError();
assert.strictEqual(document.getElementById('pad-edit-validation-error').style.display, 'none');
assert.strictEqual(document.getElementById('pad-action-controls').inert, false);
assert.strictEqual(validationErrors.size, errorCount);
assert.match(document.getElementById('pad-edit-validation-message').textContent, /Timer Duration/);
context.padWorkspaceEdit();
assert.strictEqual(document.getElementById('pad-edit-validation-error').style.display, '');
assert.strictEqual(context.padState.buttons[0], originalButton);

context.actionEditorBuild = function() { return {}; };
context.padBuildIconId = function() { return ''; };
context.bindingValidateDialog = function() { return { valid: false, count: 1 }; };
context.showMessage = function() {};
context.padMarkDirty = function() {};
context.padRenderGrid = function() {};

context.padWorkspaceEdit();
assert.strictEqual(context.padState.buttons, originalButtons);
assert.strictEqual(context.padState.buttons.length, 1);
assert.strictEqual(context.padState.buttons[0], originalButton);
assert.strictEqual(document.getElementById('pad-edit-validation-error').style.display, '');
assert.match(document.getElementById('pad-edit-validation-message').textContent, /1 binding error/);

context.bindingValidateDialog = function() { return { valid: true, count: 0 }; };
context.padWorkspaceEdit();
assert.strictEqual(context.padState.buttons.length, 1);
assert.notStrictEqual(context.padState.buttons[0], originalButton);
assert.strictEqual(context.padState.buttons[0].col, 1);
assert.strictEqual(context.padState.buttons[0].row, 2);
assert.strictEqual(document.getElementById('pad-edit-validation-error').style.display, 'none');

context.actionEditorLoadGamepad = function() {};
context.actionEditorLoad = function() {};
context.PAD_FIRMWARE_DEFAULTS = { widget_indicator_color: '#FFFFFF' };
context.padSetBindableColor = function(id, value, fallback) { document.getElementById(id).value = value || fallback; };
context.padWidgetEditorsLoad({ widget_type: 'table', widget_table_scrollable: false });
assert.strictEqual(document.getElementById('pad-edit-table-scrollable').value, 'false');
document.getElementById('pad-edit-widget-type').value = 'table';
document.getElementById('pad-edit-table-scrollable').value = 'false';
document.getElementById('pad-edit-table-data-binding').value = ' [health:table] ';
document.getElementById('pad-edit-table-style').value = ' font_size:14 ';
context.padWorkspaceEdit();
assert.strictEqual(context.padState.buttons[0].widget_data_binding, '[health:table]');
assert.strictEqual(context.padState.buttons[0].widget_table_style, 'font_size:14');
assert.strictEqual(context.padState.buttons[0].widget_table_scrollable, false);
document.getElementById('pad-edit-table-data-binding').value = '';
document.getElementById('pad-edit-table-style').value = '';
context.padWorkspaceEdit();
assert.strictEqual(context.padState.buttons[0].widget_data_binding, undefined);
assert.strictEqual(context.padState.buttons[0].widget_table_style, undefined);
document.getElementById('pad-edit-widget-type').value = '';

const numericInput = document.getElementById('pad-edit-icon-scale');
numericInput.style.display = '';
numericInput.value = '251';
numericInput.validity = { valid: false };
numericInput.validationMessage = 'Maximum is 250';
const editorOverlay = document.getElementById('pad-edit-overlay');
editorOverlay.querySelectorAll = () => [numericInput];
editorOverlay.querySelector = () => ({ textContent: 'Icon scale' });
context.padWorkspaceEdit();
assert.strictEqual(numericInput.classList.contains('pad-field-error'), true);
assert.strictEqual(numericInput.attributes['aria-invalid'], 'true');
assert.match(document.getElementById('pad-edit-validation-message').textContent, /Icon scale/);
context.padDialogDismissValidationError();
assert.strictEqual(numericInput.classList.contains('pad-field-error'), true, 'Dismissing the overlay must retain field highlighting');
numericInput.value = '100';
numericInput.validity.valid = true;
context.padWorkspaceEdit();
assert.strictEqual(numericInput.classList.contains('pad-field-error'), false);
assert.strictEqual(numericInput.attributes['aria-invalid'], undefined);
assert.strictEqual(document.getElementById('pad-edit-validation-error').style.display, 'none');
delete editorOverlay.querySelectorAll;
delete editorOverlay.querySelector;

document.getElementById('pad-edit-label-center-style').value = 'font:my-font;font_size:300;align:right';
context.padWorkspaceEdit();
assert.strictEqual(context.padState.buttons[0].label_center_style, 'font:my-font;font_size:300;align:right');
document.getElementById('pad-edit-label-center-style').value = '';
context.padWorkspaceEdit();
assert.strictEqual(context.padState.buttons[0].label_center_style, undefined);

const savedCredentialButton = {
    col: 1,
    row: 2,
    bg_image_url: 'https://camera.example/stream',
    bg_image_password_set: true
};
document.getElementById('pad-edit-widget-type').value = 'gamepad_stick';
document.getElementById('pad-edit-gamepad-stick').value = 'right';
document.getElementById('pad-edit-gamepad-center').value = 'floating';
document.getElementById('pad-edit-gamepad-dead-zone').value = '20';
document.getElementById('pad-edit-gamepad-invert-x').checked = true;
context.actionEditorBuild = function() { throw new Error('Joystick must not dispatch actions'); };
context.padWorkspaceEdit();
assert.strictEqual(context.padState.buttons[0].widget_gamepad_stick, 'right');
assert.strictEqual(context.padState.buttons[0].widget_gamepad_center, 'floating');
assert.strictEqual(context.padState.buttons[0].widget_gamepad_dead_zone, .2);
assert.strictEqual(context.padState.buttons[0].widget_gamepad_invert_x, true);
assert.strictEqual(context.padState.buttons[0].actions, undefined);
document.getElementById('pad-edit-widget-type').value = 'gamepad_button';
context.actionEditorBuildGamepad = function(prefix, held) {
    assert.strictEqual(held, true);
    return { type: 'gamepad', control: 'trigger', trigger: 'left', operation: 'down' };
};
context.padWorkspaceEdit();
assert.strictEqual(context.padState.buttons[0].actions.length, 1);
assert.strictEqual(context.padState.buttons[0].actions[0].trigger, 'left');
assert.strictEqual(context.padState.buttons[0].lp_actions, undefined);
document.getElementById('pad-edit-widget-type').value = '';
context.actionEditorBuild = function() { return {}; };
context.padState.buttons = [savedCredentialButton];
context.padFindButton = function(col, row) {
    return context.padState.buttons.find(function(button) {
        return button.col === col && button.row === row;
    });
};
document.getElementById('pad-edit-bg-image-url').value = savedCredentialButton.bg_image_url;
document.getElementById('pad-edit-bg-image-user').value = '';
document.getElementById('pad-edit-bg-image-password').value = '';
document.getElementById('pad-edit-bg-image-interval').value = '0';
document.getElementById('pad-edit-bg-image-letterbox').checked = false;

context.padWorkspaceEdit();
assert.strictEqual(context.padState.buttons[0].bg_image_password_set, true);
assert.strictEqual(context.padState.buttons[0].bg_image_password, undefined);

document.getElementById('pad-edit-widget-type').value = 'mousepad';
document.getElementById('pad-edit-mousepad-sensitivity').value = '1.5';
context.actionEditorBuild = function() { throw new Error('Mousepad must not build button actions'); };
context.padWorkspaceEdit();
assert.strictEqual(context.padState.buttons[0].widget_type, 'mousepad');
assert.strictEqual(context.padState.buttons[0].widget_mousepad_sensitivity, 1.5);
assert.strictEqual(context.padState.buttons[0].widget_mousepad_acceleration, 0);
assert.strictEqual(context.padState.buttons[0].widget_mousepad_movement_threshold, 3);
assert.strictEqual(context.padState.buttons[0].widget_mousepad_inertia, 0);
assert.strictEqual(context.padState.buttons[0].widget_mousepad_reverse, false);
assert.strictEqual(context.padState.buttons[0].widget_mousepad_buttons, false);
assert.strictEqual(context.padState.buttons[0].widget_disable_pad_swipes, false);
assert.strictEqual(context.padState.buttons[0].widget_mousepad_back, false);
assert.strictEqual(context.padState.buttons[0].widget_mousepad_back_size, 44);
document.getElementById('pad-edit-disable-pad-swipes').checked = true;
document.getElementById('pad-edit-mousepad-back').checked = true;
document.getElementById('pad-edit-mousepad-back-size').value = '88';
context.padWorkspaceEdit();
assert.strictEqual(context.padState.buttons[0].widget_disable_pad_swipes, true);
assert.strictEqual(context.padState.buttons[0].widget_mousepad_back, true);
assert.strictEqual(context.padState.buttons[0].widget_mousepad_back_size, 88);
for (const widgetType of ['scrollpad', 'gamepad_stick', 'gamepad_button']) {
    document.getElementById('pad-edit-widget-type').value = widgetType;
    context.padWorkspaceEdit();
    assert.strictEqual(context.padState.buttons[0].widget_disable_pad_swipes, true);
    assert.strictEqual(context.padState.buttons[0].widget_mousepad_back, undefined);
    assert.strictEqual(context.padState.buttons[0].widget_mousepad_back_size, undefined);
    document.getElementById('pad-edit-disable-pad-swipes').checked = false;
    context.padWorkspaceEdit();
    assert.strictEqual(context.padState.buttons[0].widget_disable_pad_swipes, false);
    document.getElementById('pad-edit-disable-pad-swipes').checked = true;
}
document.getElementById('pad-edit-widget-type').value = '';
context.actionEditorBuild = function() { return {}; };
context.padWorkspaceEdit();
assert.strictEqual(context.padState.buttons[0].widget_disable_pad_swipes, undefined);
document.getElementById('pad-edit-widget-type').value = 'mousepad';
document.getElementById('pad-edit-disable-pad-swipes').checked = false;
document.getElementById('pad-edit-mousepad-back').checked = false;
assert.strictEqual(context.padState.buttons[0].actions, undefined);
assert.strictEqual(context.padState.buttons[0].lp_actions, undefined);
document.getElementById('pad-edit-mousepad-buttons').checked = true;
context.padWorkspaceEdit();
assert.strictEqual(context.padState.buttons[0].widget_mousepad_buttons, true);
document.getElementById('pad-edit-mousepad-buttons').checked = false;
context.padWorkspaceEdit();
assert.strictEqual(context.padState.buttons[0].widget_mousepad_buttons, false);
for (const [name, raw] of [['sensitivity', '10'], ['back-size', '88.5']]) {
    const input = document.getElementById('pad-edit-mousepad-' + name);
    const previous = context.padState.buttons[0];
    input.value = raw;
    input.validity = { valid: false };
    input.validationMessage = 'Invalid numeric value';
    context.padWorkspaceEdit();
    assert.strictEqual(context.padState.buttons[0], previous);
    assert.strictEqual(input.value, raw);
    input.validity.valid = true;
    input.value = '';
    context.padWorkspaceEdit();
}
assert.strictEqual(context.padState.buttons[0].widget_mousepad_sensitivity, 1);
assert.strictEqual(context.padState.buttons[0].widget_mousepad_back_size, 44);

document.getElementById('pad-edit-widget-type').value = 'scrollpad';
context.actionEditorBuild = function() { throw new Error('Scrollpad must not build button actions'); };
for (const axis of ['vertical', 'horizontal']) {
    for (const reverse of [false, true]) {
        document.getElementById('pad-edit-scrollpad-axis').value = axis;
        document.getElementById('pad-edit-scrollpad-sensitivity').value = '1.5';
        document.getElementById('pad-edit-scrollpad-reverse').checked = reverse;
        context.padWorkspaceEdit();
        const saved = context.padState.buttons[0];
        assert.strictEqual(saved.widget_type, 'scrollpad');
        assert.strictEqual(saved.widget_scrollpad_axis, axis === 'horizontal' ? axis : 'vertical');
        assert.strictEqual(saved.widget_scrollpad_sensitivity, 1.5);
        assert.strictEqual(saved.widget_scrollpad_inertia, 0);
        assert.strictEqual(saved.widget_scrollpad_reverse, reverse);
        assert.strictEqual(saved.widget_mousepad_sensitivity, undefined);
        assert.strictEqual(saved.widget_mousepad_acceleration, undefined);
        assert.strictEqual(saved.widget_mousepad_movement_threshold, undefined);
        assert.strictEqual(saved.widget_mousepad_inertia, undefined);
        assert.strictEqual(saved.widget_mousepad_reverse, undefined);
        assert.strictEqual(saved.widget_mousepad_buttons, undefined);
        assert.strictEqual(saved.actions, undefined);
        assert.strictEqual(saved.lp_actions, undefined);
    }
}
for (const [input, expected] of [['0.1', 0.1], ['5', 5], ['', 1]]) {
    document.getElementById('pad-edit-scrollpad-sensitivity').value = input;
    context.padWorkspaceEdit();
    assert.strictEqual(context.padState.buttons[0].widget_scrollpad_sensitivity, expected);
}

for (const [input, expected] of [['3', 3], ['5', 5], ['', 0]]) {
    document.getElementById('pad-edit-scrollpad-inertia').value = input;
    context.padWorkspaceEdit();
    assert.strictEqual(context.padState.buttons[0].widget_scrollpad_inertia, expected);
}

context.window = { __padMaxlenHintWired: true };
vm.runInContext(fs.readFileSync('src/app/web/portal_pad_defaults.js', 'utf8'), context);
const bindingName = document.getElementById('pad-binding-name-0');
bindingName.value = '123bad';
assert.strictEqual(context.padValidateBindingNameInput(bindingName), false);
assert.strictEqual(bindingName.classList.contains('binding-error'), true);
assert.strictEqual(bindingName.attributes['aria-invalid'], 'true');
bindingName.classList.toggle('pad-field-error', true);
bindingName.value = 'temperature';
assert.strictEqual(context.padValidateBindingNameInput(bindingName), true);
assert.strictEqual(bindingName.classList.contains('binding-error'), false);
assert.strictEqual(bindingName.classList.contains('pad-field-error'), false);
assert.strictEqual(bindingName.attributes['aria-invalid'], undefined);

vm.runInContext(fs.readFileSync('src/app/web/portal_pad_icons.js', 'utf8'), context);
const buildFill = context.padFillBuild;
vm.runInContext(fs.readFileSync('src/app/web/portal_pad_colors.js', 'utf8'), context);
context.padFillBuild = buildFill;
vm.runInContext(fs.readFileSync('src/app/web/portal_widget_gauge.js', 'utf8'), context);
context.PAD_FIRMWARE_DEFAULTS = {
    widget_series_color: '#123456', widget_series_color_2: '#234567',
    widget_series_color_3: '#345678', widget_series_color_4: '#456789',
    widget_track_color: '#111111', widget_indicator_color: '#ffffff',
    widget_tick_color: '#808080', widget_zone_color: '#ff5722'
};
vm.runInContext('PAD_FIRMWARE_DEFAULTS = globalThis.PAD_FIRMWARE_DEFAULTS;', context);
context.padSetBindableColor = (id, value) => { document.getElementById(id).value = value; };
context.padGetBindableColor = id => document.getElementById(id).value;
context.actionEditorBuild = () => ({});
const gaugeEditor = context.padWidgetEditor('gauge');
function disclosureGroup(editor, id, keys, open = false) {
    const inputs = editor.fields.filter(field => keys.includes(field.key)).map(field => document.getElementById(field.id));
    return { element: { id, contains: element => inputs.includes(element) }, open };
}
gaugeEditor.groups = [
    disclosureGroup(gaugeEditor, 'pad-edit-gauge-source-1', [], true),
    disclosureGroup(gaugeEditor, 'pad-edit-gauge-source-2', ['widget_data_binding_2', 'widget_arc_color_2']),
    disclosureGroup(gaugeEditor, 'pad-edit-gauge-source-3', ['widget_data_binding_3']),
    disclosureGroup(gaugeEditor, 'pad-edit-gauge-needle', ['widget_gauge_show_needle']),
    disclosureGroup(gaugeEditor, 'pad-edit-gauge-tick-marks', ['widget_gauge_ticks']),
    disclosureGroup(gaugeEditor, 'pad-edit-gauge-target', ['widget_gauge_marker_value'])
];
context.padWidgetEditorsLoad({ widget_type: 'gauge', widget_arc_color_2: '#234567', widget_gauge_ticks: '5' });
assert.deepStrictEqual(gaugeEditor.groups.map(group => group.element.open), [true, false, false, false, false, false], 'Explicit defaults must not expand optional groups');
context.padWidgetEditorsLoad({ widget_type: 'gauge', widget_data_binding_2: ' [pad:source] ', widget_gauge_marker_value: 0,
    widget_gauge_show_needle: false, widget_gauge_ticks: 0 });
assert.deepStrictEqual(gaugeEditor.groups.map(group => group.element.open), [true, true, false, true, true, true], 'Data, zero targets, and changed false/zero defaults expand their groups');
gaugeEditor.groups[1].element.open = false;
context.padWidgetEditorsRefresh('gauge');
assert.strictEqual(gaugeEditor.groups[1].element.open, false, 'Refresh must not override a manual disclosure choice');
context.padWidgetEditorsLoad({ widget_type: 'table' });
assert.deepStrictEqual(gaugeEditor.groups.map(group => group.element.open), [true, false, false, false, false, false], 'Another button must not inherit configured disclosure');
const gaugeScale = disclosureGroup(gaugeEditor, 'pad-edit-gauge-scale', ['widget_gauge_min', 'widget_gauge_max']);
const gaugeLayout = disclosureGroup(gaugeEditor, 'pad-edit-gauge-layout', ['widget_gauge_degrees']);
gaugeEditor.groups.push(gaugeScale, gaugeLayout);
context.padWidgetEditorsLoad({ widget_type: 'gauge', widget_gauge_min: '0', widget_gauge_max: 100,
    widget_track_outline_width: 0, widget_track_outline_color: '#000000' });
assert.strictEqual(gaugeScale.element.open, false);
assert.strictEqual(gaugeLayout.element.open, false);
context.padWidgetEditorsLoad({ widget_type: 'gauge', widget_gauge_min: '[pad:minimum]', widget_track_outline_width: 2 });
assert.strictEqual(gaugeScale.element.open, true, 'A bound scale opens without a widget-specific shared-code branch');
assert.strictEqual(gaugeLayout.element.open, true, 'Moved track controls use owner-declared section membership');
context.padWidgetEditorsLoad({
    widget_type: 'gauge', widget_gauge_show_needle: false, widget_gauge_needle_width: 7,
    widget_gauge_ticks: 0, widget_gauge_tick_width: 3, widget_gauge_marker_zone_deg: 19,
    widget_gauge_marker_value: 0, widget_gauge_min: '[pad:minimum]',
    widget_data_binding_3: '[pad:source]', widget_gauge_start_label_3: 'Third\nring'
});
document.getElementById('pad-edit-widget-type').value = 'gauge';
context.padWorkspaceEdit();
let gauge = context.padState.buttons[0];
assert.strictEqual(gauge.widget_gauge_show_needle, false);
assert.strictEqual(gauge.widget_gauge_needle_width, 7);
assert.strictEqual(gauge.widget_gauge_ticks, 0);
assert.strictEqual(gauge.widget_gauge_tick_width, 3);
assert.strictEqual(gauge.widget_gauge_marker_zone_deg, 19);
assert.strictEqual(gauge.widget_gauge_marker_value, '0');
assert.strictEqual(gauge.widget_gauge_min, '[pad:minimum]');
assert.strictEqual(gauge.widget_data_binding_3, '[pad:source]');
assert.strictEqual(gauge.widget_gauge_start_label_3, 'Third\nring');
assert.strictEqual(gauge.widget_arc_color_4, '#456789');
const needleWidth = document.getElementById('pad-edit-gauge-needle-width');
needleWidth.value = '11';
needleWidth.validity = { valid: false };
needleWidth.validationMessage = 'Maximum is 10';
context.padWorkspaceEdit();
assert.strictEqual(context.padState.buttons[0], gauge, 'Invalid disabled styles must not replace the last valid button');
assert.strictEqual(needleWidth.value, '11', 'Keep explicit invalid input raw');
assert.match(validationErrors.get('1,2').message, /Maximum is 10/);
needleWidth.value = '7';
needleWidth.validity.valid = true;
document.getElementById('pad-edit-gauge-min').value = '12oops';
context.padWorkspaceEdit();
assert.strictEqual(context.padState.buttons[0], gauge, 'Reject partially numeric text instead of accepting a parsed prefix');
document.getElementById('pad-edit-gauge-min').value = '0x10';
context.padWorkspaceEdit();
assert.strictEqual(context.padState.buttons[0], gauge, 'Reject text whose whole-number and prefix parsers disagree');
document.getElementById('pad-edit-gauge-min').value = '';
context.padWorkspaceEdit();
assert.strictEqual(context.padState.buttons[0].widget_gauge_min, 0);
assert.strictEqual(validationErrors.size, 0);
document.getElementById('pad-edit-widget-type').value = 'table';
document.getElementById('pad-edit-gauge-min').value = 'unrelated invalid draft';
context.padWorkspaceEdit();
assert.strictEqual(context.padState.buttons[0].widget_type, 'table', 'Inactive Gauge fields must not block Table');

const gamepadEditor = context.padWidgetEditor('gamepad_button');
assert.strictEqual(gamepadEditor.available({ has_touch: true, has_usb_hid: true }), true);
assert.strictEqual(gamepadEditor.available({ has_touch: true, has_usb_hid: false }), false);
document.getElementById('pad-edit-widget-type').value = 'gamepad_button';
gamepadEditor.supported = false;
const unsupported = context.padDialogBuildButton();
assert.strictEqual(unsupported.widget_type, undefined);
assert.strictEqual(unsupported.actions, undefined);
assert.strictEqual(unsupported.lp_actions, undefined);

context.padWidgetEditorsLoad({ widget_type: 'numericrocker', widget_numericrocker_small_step: 0.25 });
document.getElementById('pad-edit-widget-type').value = 'numericrocker';
context.actionEditorBuild = prefix => prefix === 'pad-edit-nr-adjust'
    ? { type: 'mqtt', payload: '{step}' } : { type: 'screen', target: 'info' };
let numericRocker = context.padDialogBuildButton();
assert.strictEqual(numericRocker.widget_numericrocker_small_step, 0.25);
assert.strictEqual(numericRocker.widget_numericrocker_action.payload, '{step}');
assert.strictEqual(numericRocker.actions.length, 3);
assert.strictEqual(numericRocker.lp_actions, undefined);
context.actionEditorBuild = () => ({});
numericRocker = context.padDialogBuildButton();
assert.strictEqual(numericRocker.widget_numericrocker_action, undefined);
const opacity = document.getElementById('pad-edit-numericrocker-opacity');
opacity.value = '80.5';
opacity.validity = { valid: false };
opacity.validationMessage = 'Enter a whole number';
assert.throws(() => context.padDialogBuildButton(), /whole number/);
assert.strictEqual(opacity.value, '80.5');
opacity.validity.valid = true;
context.padWidgetEditorsLoad({ widget_type: 'rocker', widget_rocker_axis: 'horizontal' });
document.getElementById('pad-edit-widget-type').value = 'rocker';
assert.deepStrictEqual(Array.from(context.padWidgetEditor('rocker').actions.labels()), ['Left action', 'Right action']);
context.actionEditorBuild = () => ({ type: 'screen', target: 'info' });
const rocker = context.padDialogBuildButton();
assert.strictEqual(rocker.widget_rocker_axis, 'horizontal');
assert.strictEqual(rocker.actions.length, 3);
assert.strictEqual(rocker.lp_actions.length, 3);

vm.runInContext(fs.readFileSync('src/app/web/portal_widget_bar_chart.js', 'utf8'), context);
const barEditor = context.padWidgetEditor('bar_chart');
barEditor.groups = [
    disclosureGroup(barEditor, 'pad-edit-bar-source-2', ['widget_bar_label_2']),
    disclosureGroup(barEditor, 'pad-edit-bar-source-3', ['widget_data_binding_3']),
    disclosureGroup(barEditor, 'pad-edit-bar-gridlines', ['widget_bar_tick_width']),
    disclosureGroup(barEditor, 'pad-edit-bar-target', ['widget_bar_marker_value'])
];
context.padWidgetEditorsLoad({ widget_type: 'bar_chart', widget_bar_ticks: 0, widget_bar_tick_width: 4,
    widget_bar_marker_value: '0', widget_bar_label_2: 'Grid', widget_bar_track_radius: 0 });
assert.deepStrictEqual(barEditor.groups.map(group => group.element.open), [true, false, true, true], 'Bar labels, customized gridlines, and zero targets open their groups');
document.getElementById('pad-edit-widget-type').value = 'bar_chart';
const barChart = context.padDialogBuildButton();
assert.strictEqual(barChart.widget_bar_ticks, 0);
assert.strictEqual(barChart.widget_bar_tick_width, 4);
assert.strictEqual(barChart.widget_bar_marker_value, '0');
assert.strictEqual(barChart.widget_bar_label_2, 'Grid');
assert.strictEqual(barChart.widget_bar_track_radius, 0);
assert.strictEqual(barChart.widget_bar_min, 0);
assert.strictEqual(barChart.widget_bar_max, 3);
const barLayout = disclosureGroup(barEditor, 'pad-edit-bar-layout', ['widget_orientation']);
barEditor.groups.push(barLayout);
context.padWidgetEditorsLoad({ widget_type: 'bar_chart', widget_bar_pattern_3: 'none', widget_bar_bg_pattern_color: '#000000',
    widget_bar_track_radius: '4', widget_orientation: 'vertical' });
assert.strictEqual(barEditor.groups[1].element.open, false);
assert.strictEqual(barLayout.element.open, false, 'Explicit custom-control defaults must not open layout');
context.padWidgetEditorsLoad({ widget_type: 'bar_chart', widget_bar_pattern_3: 'dots', widget_bar_bg_pattern_color: '#123456' });
assert.strictEqual(barEditor.groups[1].element.open, true, 'Pattern-only configuration opens an optional source');
assert.strictEqual(barLayout.element.open, true, 'Pattern color alone opens its owning section');

vm.runInContext(fs.readFileSync('src/app/web/portal_widget_sparkline.js', 'utf8'), context);
const sparklineEditor = context.padWidgetEditor('sparkline');
sparklineEditor.groups = [
    disclosureGroup(sparklineEditor, 'pad-edit-sparkline-source-2', ['widget_data_binding_2']),
    disclosureGroup(sparklineEditor, 'pad-edit-sparkline-source-3', ['widget_data_binding_3']),
    disclosureGroup(sparklineEditor, 'pad-edit-sparkline-minimum-marker', ['widget_sparkline_marker_size_min']),
    disclosureGroup(sparklineEditor, 'pad-edit-sparkline-maximum-marker', ['widget_sparkline_max_fmt'])
];
context.padWidgetEditorsLoad({ widget_type: 'sparkline', widget_data_binding_2: '[pad:second]',
    widget_sparkline_marker_size_min: 4, widget_sparkline_max_fmt: 'High %.1f' });
assert.deepStrictEqual(sparklineEditor.groups.map(group => group.element.open), [true, false, true, true], 'Sparkline sources and customized markers open on load');
context.padWidgetEditorsLoad({ widget_type: 'sparkline', widget_sparkline_ref_1_color: '#123456' });
assert.deepStrictEqual(sparklineEditor.groups.map(group => group.element.open), [false, false, false, false], 'Unrelated reference styling must not open source or marker groups');
const samplingGroup = disclosureGroup(sparklineEditor, 'pad-edit-sparkline-sampling', []);
const markerParent = disclosureGroup(sparklineEditor, 'pad-edit-sparkline-markers', []);
markerParent.element.contains = element => sparklineEditor.groups.slice(2, 4).some(group => group.element === element || group.element.contains(element));
sparklineEditor.groups.push(samplingGroup, markerParent);
context.padWidgetEditorsLoad({ widget_type: 'sparkline', widget_sparkline_ha_stat_2: 'mean',
    widget_sparkline_window: '300', widget_sparkline_slots: 60, widget_data_binding_3: '  ' });
assert.ok(sparklineEditor.groups.every(group => !group.element.open), 'Blank sources and explicit custom defaults leave optional sections closed');
context.padWidgetEditorsLoad({ widget_type: 'sparkline', widget_sparkline_ha_entity_2: 'sensor.temperature',
    widget_sparkline_slots: 120, widget_sparkline_marker_size_min: 4 });
assert.strictEqual(sparklineEditor.groups[0].element.open, true, 'Owner-declared history settings participate in disclosure');
assert.strictEqual(samplingGroup.element.open, true, 'Changed sampling opens its section');
assert.strictEqual(markerParent.element.open, true, 'A configured child opens its containing group');
context.padWidgetEditorsLoad({ widget_type: 'sparkline', widget_sparkline_ref_1_color: '#123456' });
document.getElementById('pad-edit-sparkline-window').value = '7';
document.getElementById('pad-edit-sparkline-window-unit').value = '86400';
document.getElementById('pad-edit-sparkline-interval').value = '0.1';
document.getElementById('pad-edit-sparkline-interval-unit').value = '1';
document.getElementById('pad-edit-widget-type').value = 'sparkline';
const sparkline = context.padDialogBuildButton();
assert.strictEqual(sparkline.widget_sparkline_window, 604800);
assert.strictEqual(sparkline.widget_sparkline_slots, 1024);
assert.strictEqual(sparkline.widget_sparkline_min, undefined);
assert.strictEqual(sparkline.widget_sparkline_max_label_color, undefined);
assert.strictEqual(sparkline.widget_sparkline_ref_1_color, '#123456');
document.getElementById('pad-edit-sparkline-interval').value = '0.099';
assert.throws(() => context.padWidgetEditorBuild({}, context.padWidgetEditor('sparkline')), /100 milliseconds/);
assert.strictEqual(document.getElementById('pad-edit-sparkline-interval').value, '0.099');
document.getElementById('pad-edit-sparkline-interval').value = '0.1';
context.window.__device_caps = { ha_history: true };
document.getElementById('pad-edit-sparkline-ha-entity').value = 'sensor.energy';
assert.throws(() => context.padDialogBuildButton(), /requires a live data binding/);
document.getElementById('pad-edit-sparkline-data-binding').value = '[mqtt:energy]';
document.getElementById('pad-edit-sparkline-ha-stat').value = 'sum';
assert.strictEqual(context.padDialogBuildButton().widget_sparkline_ha_stat, 'sum');
document.getElementById('pad-edit-sparkline-ha-entity').value = '';
assert.strictEqual(context.padDialogBuildButton().widget_sparkline_ha_entity, undefined);
document.getElementById('pad-edit-sparkline-ha-entity').value = 'sensor.energy';
context.window.__device_caps.ha_history = false;
assert.strictEqual(context.padDialogBuildButton().widget_sparkline_ha_entity, undefined);
document.getElementById('pad-edit-sparkline-window').value = '9';
document.getElementById('pad-edit-sparkline-window-unit').value = '1';
assert.throws(() => context.padDialogBuildButton(), /10 seconds and 7 days/);

vm.runInContext(fs.readFileSync('src/app/web/portal_widget_waveform.js', 'utf8'), context);
const waveformEditor = context.padWidgetEditor('waveform');
waveformEditor.groups = [disclosureGroup(waveformEditor, 'pad-edit-waveform-section', waveformEditor.fields.map(field => field.key))];
context.padWidgetEditorsLoad({ widget_type: 'waveform', widget_waveform_color_1: '#4caf50' });
assert.strictEqual(waveformEditor.groups[0].element.open, false, 'Literal color case alone does not count as customization');
context.padWidgetEditorsLoad({ widget_type: 'waveform', widget_waveform_show_threshold: false });
assert.strictEqual(waveformEditor.groups[0].element.open, true, 'A disabled default-on feature still counts as customization');
const cameraEditor = context.padWidgetEditor('camera_preview');
cameraEditor.groups = [disclosureGroup(cameraEditor, 'pad-edit-camera-preview-section', ['widget_camera_scale'])];
context.padWidgetEditorsLoad({ widget_type: 'camera_preview', widget_camera_scale: 'cover' });
assert.strictEqual(cameraEditor.groups[0].element.open, false);
context.padWidgetEditorsLoad({ widget_type: 'camera_preview', widget_camera_scale: 'letterbox' });
assert.strictEqual(cameraEditor.groups[0].element.open, true);
assert.strictEqual(waveformEditor.available({ widget_catalog: [{ type: 'waveform' }] }), true);
assert.strictEqual(waveformEditor.available({ widget_catalog: [{ type: 'gauge' }] }), false);
assert.strictEqual(waveformEditor.available(null), false);
context.padWidgetEditorsLoad({ widget_type: 'waveform', widget_waveform_color_1: '[pad:color]',
    widget_waveform_color_4: '#123456', widget_waveform_line_width: 5, widget_waveform_sensor: 4,
    widget_waveform_show_threshold: false, widget_waveform_show_trigger: false,
    widget_waveform_show_target: false, widget_waveform_invert_y: true });
document.getElementById('pad-edit-widget-type').value = 'waveform';
const waveform = context.padDialogBuildButton();
assert.strictEqual(waveform.widget_waveform_color_1, '[pad:color]');
assert.strictEqual(waveform.widget_waveform_color_4, '#123456');
assert.strictEqual(waveform.widget_waveform_threshold_color, '#FF9800');
assert.strictEqual(waveform.widget_waveform_trigger_color, '#FFFFFF');
assert.strictEqual(waveform.widget_waveform_line_width, 5);
assert.strictEqual(waveform.widget_waveform_sensor, 4);
assert.strictEqual(waveform.widget_waveform_show_threshold, false);
assert.strictEqual(waveform.widget_waveform_show_trigger, false);
assert.strictEqual(waveform.widget_waveform_show_target, false);
assert.strictEqual(waveform.widget_waveform_invert_y, true);
const waveformWidth = document.getElementById('pad-edit-waveform-line-width');
waveformWidth.value = '2.5';
waveformWidth.validity = { valid: false };
waveformWidth.validationMessage = 'Enter a whole number';
assert.throws(() => context.padDialogBuildButton(), /whole number/);
assert.strictEqual(waveformWidth.value, '2.5');
waveformWidth.validity.valid = true;
waveformWidth.value = '';
assert.strictEqual(context.padDialogBuildButton().widget_waveform_line_width, 2);
context.padWidgetEditorsLoad({ widget_type: 'waveform' });
assert.strictEqual(context.padDialogBuildButton().widget_waveform_show_threshold, true);
assert.strictEqual(context.padDialogBuildButton().widget_waveform_sensor, 0);

async function checkSourceColors() {
    document.querySelector = () => null;
    const border = id => document.getElementById(id).style['--pad-source-color'];
    for (const [type, colorKey, groupPrefix, sourceCount] of [
        ['gauge', 'widget_arc_color', 'pad-edit-gauge-source-', 4],
        ['bar_chart', 'widget_bar_color', 'pad-edit-bar-source-', 4],
        ['sparkline', 'widget_sparkline_line_color', 'pad-edit-sparkline-source-', 3]
    ]) {
        document.getElementById('pad-edit-widget-type').value = type;
        context.padWidgetEditorsLoad({ widget_type: type });
        for (let source = 1; source <= sourceCount; source++) {
            const suffix = source === 1 ? '' : '_' + source;
            const field = context.padWidgetEditor(type).fields.find(field => field.key === colorKey + suffix);
            const group = document.getElementById(groupPrefix + source);
            group.open = false;
            const fallback = context.PAD_FIRMWARE_DEFAULTS['widget_series_color' + suffix];
            assert.strictEqual(border(group.id), fallback.toUpperCase());
            const input = document.getElementById(field.id);
            input.value = '#aBc';
            context.padPvSetChip(field.id);
            assert.strictEqual(border(group.id), '#AABBCC', 'Literal edits normalize without needing a preview chip');
            input.value = '[pad:color]';
            context.padPvSetChip(field.id, '#fedcba');
            assert.strictEqual(border(group.id), '#FEDCBA', 'Resolved binding colors reach their owning source');
            for (const result of ['', '---', 'not a color']) {
                context.padPvSetChip(field.id, result);
                assert.strictEqual(border(group.id), fallback.toUpperCase());
            }
            context.padPvSetChip(field.id, '#fedcba', 'loading');
            assert.strictEqual(border(group.id), fallback.toUpperCase());
            input.value = '';
            context.padPvSetChip(field.id);
            assert.strictEqual(border(group.id), fallback.toUpperCase());
            assert.strictEqual(group.open, false, 'Color updates preserve collapsed sections');
        }
    }
    context.padWidgetEditorsLoad({ widget_type: 'bar_chart', widget_bar_color: '#123abc',
        widget_bar_pattern: 'dots', widget_bar_pattern_color: '#ffffff' });
    document.getElementById('pad-edit-widget-bar-color-pattern-color').value = '#000000';
    context.padPvSetChip('pad-edit-widget-bar-color-pattern-color', '#ffffff');
    assert.strictEqual(border('pad-edit-bar-source-1'), '#123ABC', 'Pattern color must not replace the base-color accent');

    const pending = [];
    context.padPvMount = () => {};
    context.fetch = (url, options) => new Promise((resolve, reject) => {
        assert.strictEqual(url, '/api/pad/resolve');
        pending.push({ resolve, reject, body: JSON.parse(options.body) });
    });
    const settle = () => new Promise(resolve => setImmediate(resolve));
    document.getElementById('pad-edit-widget-type').value = 'gauge';
    context.padWidgetEditorsLoad({ widget_type: 'gauge', widget_arc_color: '[pad:first]' });
    const input = document.getElementById('pad-edit-gauge-arc-color');
    context.padPvRefreshAll(0);
    assert.strictEqual(pending.length, 1, 'Accents reuse the existing batch request');
    input.value = '[pad:second]';
    context.padPvRefreshAll(0);
    pending[0].resolve({ ok: true, json: () => ({ button: { widget_arc_color: '#ff0000' } }) });
    await settle();
    assert.strictEqual(border('pad-edit-gauge-source-1'), '#123456', 'Changed expressions reject old results');
    pending[1].resolve({ ok: true, json: () => ({ button: { widget_arc_color: '#00ff00' } }) });
    await settle();
    assert.strictEqual(border('pad-edit-gauge-source-1'), '#00FF00');
    input.value = '[pad:first]';
    context.padPvRefreshAll(0);
    input.value = '[pad:second]';
    context.padPvRefreshAll(0);
    pending[3].resolve({ ok: true, json: () => ({ button: { widget_arc_color: '#00ff00' } }) });
    await settle();
    pending[2].reject(new Error('Old request failed'));
    await settle();
    assert.strictEqual(border('pad-edit-gauge-source-1'), '#00FF00', 'Stale failures cannot reset newer results');
    context.padPvRefreshField(input.id, 0);
    pending[4].resolve({ ok: true, json: () => ({ resolved: [{ value: '#abcdef' }] }) });
    await settle();
    assert.strictEqual(border('pad-edit-gauge-source-1'), '#ABCDEF', 'Click-to-refresh also updates the accent');
    context.padPvRefreshAll(0);
    context.padState.editCol++;
    context.padWidgetEditorsLoad({ widget_type: 'gauge', widget_arc_color: '[pad:second]' });
    pending[5].resolve({ ok: true, json: () => ({ button: { widget_arc_color: '#ff0000' } }) });
    await settle();
    assert.strictEqual(border('pad-edit-gauge-source-1'), '#123456', 'Switching buttons rejects old results');
}

checkSourceColors().then(() => console.log('portal_pad_dialog_transaction: PASS')).catch(error => {
    console.error(error);
    process.exitCode = 1;
});
