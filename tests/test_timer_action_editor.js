const assert = require('assert');
const fs = require('fs');
const vm = require('vm');
const path = require('path');
const {execFileSync} = require('child_process');

const binary = process.env.ACTION_CATALOG_BINARY || path.resolve('build/host-tests/tests/test_action_parse');
const catalog = JSON.parse(execFileSync(binary, ['--catalog'], {encoding:'utf8'})).portal;
const elements = new Map();
const document = {
    getElementById(id) {
        if (!elements.has(id)) elements.set(id, {
            id, value:'', checked:false, style:{}, options:[], children:[], attributes:{},
            classList:{contains() { return false; }},
            getAttribute(name) { return this.attributes[name]; },
            setAttribute(name, value) { this.attributes[name] = value; },
            removeAttribute(name) { delete this.attributes[name]; },
            hasAttribute(name) { return Object.hasOwn(this.attributes, name); },
            setCustomValidity(message) { this.error = message; }
        });
        return elements.get(id);
    },
    createElement() { return {}; }
};
const context = {console, document, TextEncoder, deviceInfoCache:{catalog},
    escAttr(value) { return value || ''; },
    validateBinding(value) { return {valid:value === '[mqtt:duration]', errors:[{message:'Invalid binding'}]}; }
};
vm.createContext(context);
vm.runInContext(fs.readFileSync('src/app/web/portal_action_editor.js', 'utf8'), context);

const prefix = 'timer-test';
const action = {op:'timer.start_down', args:{timer:2, seconds:45}};
context.actionEditorLoad(prefix, action);
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild(prefix))), action);
const duration = document.getElementById(prefix + '-op-timer.start_down-seconds-fixed');
for (const value of ['', '0', '-1', '1.5', '4294968']) {
    duration.value = value;
    assert.throws(() => context.actionEditorBuild(prefix), /whole number/);
}
duration.value = '1';
assert.strictEqual(context.actionEditorBuild(prefix).args.seconds, 1);
duration.value = '4294967';
assert.strictEqual(context.actionEditorBuild(prefix).args.seconds, 4294967);
document.getElementById(prefix + '-op-timer.start_down-seconds-dynamic').checked = true;
const template = document.getElementById(prefix + '-op-timer.start_down-seconds-template');
template.value = '[mqtt:duration';
assert.throws(() => context.actionEditorBuild(prefix), /Invalid binding/);
template.value = '[mqtt:duration]';
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild(prefix).args.seconds)),
    {$template:'[mqtt:duration]'});
console.log('timer_action_editor: PASS');
