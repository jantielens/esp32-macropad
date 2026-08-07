const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

class Element {
    constructor() {
        this.value = '';
        this.style = { display: 'none' };
        this.dataset = {};
        this.selectedIndex = 0;
        this.attributes = {};
        this.validationMessage = '';
        this.focused = false;
    }
    setCustomValidity(message) { this.validationMessage = message; }
    reportValidity() { return !this.validationMessage; }
    focus() { this.focused = true; }
    addEventListener() {}
    hasAttribute(name) { return Object.prototype.hasOwnProperty.call(this.attributes, name); }
    getAttribute(name) { return this.attributes[name]; }
    setAttribute(name, value) { this.attributes[name] = value; }
    removeAttribute(name) { delete this.attributes[name]; }
}

const elements = new Map();
const document = {
    getElementById(id) {
        if (!elements.has(id)) elements.set(id, new Element());
        return elements.get(id);
    }
};
const context = {
    console,
    document,
    showMessage() {},
    listInjectSyntheticScreenOption() {},
    padInitBindableColor() {},
    padSetBindableColor() {},
    padGetBindableColor() { return ''; }
};
vm.createContext(context);
vm.runInContext(fs.readFileSync('src/app/web/portal_binding_validator.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_action_editor.js', 'utf8'), context);

const prefix = 'test';
function element(suffix) { return document.getElementById(prefix + suffix); }
function setTimer(command, mode, duration) {
    element('-type').value = 'timer';
    element('-timer-action').value = '1:' + command;
    element('-timer-mode').value = mode || '';
    element('-timer-duration').value = duration || '';
}
function buildFails(message) {
    assert.throws(() => context.actionEditorBuild(prefix), new RegExp(message));
}

const html = context.actionEditorHTML(prefix, '', {});
assert(html.includes(prefix + '-timer-mode'));
assert(html.includes(prefix + '-timer-duration'));
assert(!html.toLowerCase().includes('lap'));

setTimer('start', 'down', '300');
context.actionEditorTimerChanged(prefix);
assert.strictEqual(element('-timer-mode-group').style.display, '');
assert.strictEqual(element('-timer-duration-group').style.display, '');
assert.deepStrictEqual(
    JSON.parse(JSON.stringify(context.actionEditorBuild(prefix))),
    { type: 'timer', timer_id: 1, timer_command: 'start', timer_mode: 'down', timer_value: '300' }
);

setTimer('toggle', 'up', 'stale');
context.actionEditorTimerChanged(prefix);
assert.strictEqual(element('-timer-duration-group').style.display, 'none');
assert.deepStrictEqual(
    JSON.parse(JSON.stringify(context.actionEditorBuild(prefix))),
    { type: 'timer', timer_id: 1, timer_command: 'toggle', timer_mode: 'up' }
);

setTimer('start', '', '');
buildFails('Mode');
assert(element('-timer-mode').focused);
setTimer('start', 'down', '');
buildFails('Duration');
setTimer('start', 'down', '0');
buildFails('Duration');
setTimer('start', 'down', '1.5');
buildFails('Duration');
setTimer('start', 'down', '4294968');
buildFails('Duration');
setTimer('start', 'down', 'bad[');
buildFails('Duration');
setTimer('start', 'down', '[junk]');
buildFails('Duration');
setTimer('start', 'down', '[unknown:value]');
buildFails('Duration');
setTimer('start', 'down', '[mqtt:duration');
buildFails('Duration');
setTimer('start', 'down', '[mqtt:duration]');
assert.strictEqual(context.actionEditorBuild(prefix).timer_value, '[mqtt:duration]');

context.actionEditorLoad(prefix, {
    type: 'timer', timer_id: 2, timer_command: 'toggle', timer_mode: 'down', timer_value: '45'
});
assert.strictEqual(element('-timer-action').value, '2:toggle');
assert.strictEqual(element('-timer-mode').value, 'down');
assert.strictEqual(element('-timer-duration').value, '45');
assert.strictEqual(element('-timer-duration-group').style.display, '');

setTimer('set', '', '');
context.actionEditorTimerChanged(prefix);
assert.strictEqual(element('-timer-mode-group').style.display, 'none');
assert.strictEqual(element('-timer-set-group').style.display, '');

console.log('timer_action_editor: PASS');
