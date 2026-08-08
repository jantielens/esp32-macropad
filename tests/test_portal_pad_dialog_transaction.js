const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

class Element {
    constructor() {
        this.value = '';
        this.checked = false;
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

context.bindingValidateDialog = function() { return { valid: true, count: 0 }; };
context.padDialogOk(true);
assert.strictEqual(context.padState.buttons.length, 1);
assert.notStrictEqual(context.padState.buttons[0], originalButton);
assert.strictEqual(context.padState.buttons[0].col, 1);
assert.strictEqual(context.padState.buttons[0].row, 2);

console.log('portal_pad_dialog_transaction: PASS');
