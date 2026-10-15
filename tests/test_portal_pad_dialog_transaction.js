const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

class Element {
    constructor(id) {
        this.id = id;
        this.value = '';
        this.checked = false;
        this.textContent = '';
        this.style = { display: 'none' };
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
context.padWidgetEditorsLoad({ widget_type: 'bar_chart', widget_bar_ticks: 0, widget_bar_tick_width: 4,
    widget_bar_marker_value: '0', widget_bar_label_2: 'Grid', widget_bar_track_radius: 0 });
document.getElementById('pad-edit-widget-type').value = 'bar_chart';
const barChart = context.padDialogBuildButton();
assert.strictEqual(barChart.widget_bar_ticks, 0);
assert.strictEqual(barChart.widget_bar_tick_width, 4);
assert.strictEqual(barChart.widget_bar_marker_value, '0');
assert.strictEqual(barChart.widget_bar_label_2, 'Grid');
assert.strictEqual(barChart.widget_bar_track_radius, 0);
assert.strictEqual(barChart.widget_bar_min, 0);
assert.strictEqual(barChart.widget_bar_max, 3);

vm.runInContext(fs.readFileSync('src/app/web/portal_widget_sparkline.js', 'utf8'), context);
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

console.log('portal_pad_dialog_transaction: PASS');
