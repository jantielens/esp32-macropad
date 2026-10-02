const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

class Element {
    constructor() {
        this.value = '';
        this.checked = false;
        this.textContent = '';
        this.style = { display: 'none' };
    }
}

const elements = new Map();
const document = {
    getElementById(id) {
        if (!elements.has(id)) elements.set(id, new Element());
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
    actionEditorBuild() { throw new Error('Timer Duration is invalid'); },
    padActionPrefixes(gesture) { return [gesture + '-0', gesture + '-1', gesture + '-2']; },
    actionEditorListBuild(prefixes) {
        return prefixes.map(function(p) { return context.actionEditorBuild(p); })
            .filter(function(a) { return a && a.type; });
    }
};

vm.createContext(context);
vm.runInContext(fs.readFileSync('src/app/web/portal_pad_dialog.js', 'utf8'), context);

assert.throws(() => context.padDialogOk(false), /Timer Duration/);
assert.strictEqual(context.padState.buttons, originalButtons);
assert.strictEqual(context.padState.buttons.length, 1);
assert.strictEqual(context.padState.buttons[0], originalButton);

context.actionEditorBuild = function() { return {}; };
context.padBuildIconId = function() { return ''; };
context.bindingValidateDialog = function() { return { valid: false, count: 1 }; };
context.showMessage = function() {};
context.padMarkDirty = function() {};
context.padRenderGrid = function() {};

context.padDialogOk(true);
assert.strictEqual(context.padState.buttons, originalButtons);
assert.strictEqual(context.padState.buttons.length, 1);
assert.strictEqual(context.padState.buttons[0], originalButton);
assert.strictEqual(document.getElementById('pad-edit-validation-error').style.display, '');
assert.match(document.getElementById('pad-edit-validation-error').textContent, /1 binding error/);

context.bindingValidateDialog = function() { return { valid: true, count: 0 }; };
context.padDialogOk(true);
assert.strictEqual(context.padState.buttons.length, 1);
assert.notStrictEqual(context.padState.buttons[0], originalButton);
assert.strictEqual(context.padState.buttons[0].col, 1);
assert.strictEqual(context.padState.buttons[0].row, 2);
assert.strictEqual(document.getElementById('pad-edit-validation-error').style.display, 'none');

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
context.padDialogOk(true);
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
context.padDialogOk(true);
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

context.padDialogOk(true);
assert.strictEqual(context.padState.buttons[0].bg_image_password_set, true);
assert.strictEqual(context.padState.buttons[0].bg_image_password, undefined);

document.getElementById('pad-edit-widget-type').value = 'mousepad';
document.getElementById('pad-edit-mousepad-sensitivity').value = '1.5';
context.actionEditorBuild = function() { throw new Error('Mousepad must not build button actions'); };
context.padDialogOk(true);
assert.strictEqual(context.padState.buttons[0].widget_type, 'mousepad');
assert.strictEqual(context.padState.buttons[0].widget_mousepad_sensitivity, 1.5);
assert.strictEqual(context.padState.buttons[0].widget_mousepad_acceleration, 0);
assert.strictEqual(context.padState.buttons[0].widget_mousepad_movement_threshold, 3);
assert.strictEqual(context.padState.buttons[0].actions, undefined);
assert.strictEqual(context.padState.buttons[0].lp_actions, undefined);
document.getElementById('pad-edit-mousepad-sensitivity').value = '10';
context.padDialogOk(true);
assert.strictEqual(context.padState.buttons[0].widget_mousepad_sensitivity, 5);
document.getElementById('pad-edit-mousepad-sensitivity').value = 'Infinity';
context.padDialogOk(true);
assert.strictEqual(context.padState.buttons[0].widget_mousepad_sensitivity, 1);

for (const [input, expected] of [['', 1], ['   ', 1], ['0', 0.1], ['-1', 0.1], ['NaN', 1]]) {
    document.getElementById('pad-edit-mousepad-sensitivity').value = input;
    context.padDialogOk(true);
    assert.strictEqual(context.padState.buttons[0].widget_mousepad_sensitivity, expected);
}

for (const [input, expected] of [['3', 3], ['10', 5], ['-1', 0], ['Infinity', 0], ['NaN', 0], ['', 0]]) {
    document.getElementById('pad-edit-mousepad-acceleration').value = input;
    context.padDialogOk(true);
    assert.strictEqual(context.padState.buttons[0].widget_mousepad_acceleration, expected);
}

for (const [input, expected] of [['0', 0], ['3', 3], ['6', 6], ['12', 12], ['20', 12],
    ['-1', 0], ['Infinity', 3], ['NaN', 3], ['', 3], ['   ', 3]]) {
    document.getElementById('pad-edit-mousepad-movement-threshold').value = input;
    context.padDialogOk(true);
    assert.strictEqual(context.padState.buttons[0].widget_mousepad_movement_threshold, expected);
}

document.getElementById('pad-edit-widget-type').value = 'scrollpad';
context.actionEditorBuild = function() { throw new Error('Scrollpad must not build button actions'); };
for (const axis of ['vertical', 'horizontal', 'invalid', '']) {
    for (const reverse of [false, true]) {
        document.getElementById('pad-edit-scrollpad-axis').value = axis;
        document.getElementById('pad-edit-scrollpad-sensitivity').value = '1.5';
        document.getElementById('pad-edit-scrollpad-reverse').checked = reverse;
        context.padDialogOk(true);
        const saved = context.padState.buttons[0];
        assert.strictEqual(saved.widget_type, 'scrollpad');
        assert.strictEqual(saved.widget_scrollpad_axis, axis === 'horizontal' ? axis : 'vertical');
        assert.strictEqual(saved.widget_scrollpad_sensitivity, 1.5);
        assert.strictEqual(saved.widget_scrollpad_inertia, 0);
        assert.strictEqual(saved.widget_scrollpad_reverse, reverse);
        assert.strictEqual(saved.widget_mousepad_sensitivity, undefined);
        assert.strictEqual(saved.widget_mousepad_acceleration, undefined);
        assert.strictEqual(saved.widget_mousepad_movement_threshold, undefined);
        assert.strictEqual(saved.actions, undefined);
        assert.strictEqual(saved.lp_actions, undefined);
    }
}
for (const [input, expected] of [['10', 5], ['0', 0.1], ['-1', 0.1], ['Infinity', 1], ['NaN', 1], ['', 1]]) {
    document.getElementById('pad-edit-scrollpad-sensitivity').value = input;
    context.padDialogOk(true);
    assert.strictEqual(context.padState.buttons[0].widget_scrollpad_sensitivity, expected);
}

for (const [input, expected] of [['3', 3], ['10', 5], ['-1', 0], ['Infinity', 0], ['NaN', 0], ['', 0]]) {
    document.getElementById('pad-edit-scrollpad-inertia').value = input;
    context.padDialogOk(true);
    assert.strictEqual(context.padState.buttons[0].widget_scrollpad_inertia, expected);
}

console.log('portal_pad_dialog_transaction: PASS');
