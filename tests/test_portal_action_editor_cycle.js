const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

class Element {
    constructor() {
        this.value = '';
        this.checked = false;
        this.style = { display: 'none' };
        this.dataset = {};
        this.selectedIndex = 0;
        this.attributes = {};
        var classes = new Set();
        this.classList = {
            add: function(c) { classes.add(c); },
            remove: function(c) { classes.delete(c); },
            contains: function(c) { return classes.has(c); }
        };
    }
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
    deviceInfoCache: {
        max_pads: 6,
        catalog: [{ type: 'cycle_pad', group: 'Navigation', label: 'Navigate pad sequence' }]
    },
    listInjectSyntheticScreenOption() {},
    padInitBindableColor() {},
    padSetBindableColor() {},
    padGetBindableColor() { return ''; }
};
vm.createContext(context);
vm.runInContext(fs.readFileSync('src/app/web/portal_action_editor.js', 'utf8'), context);

const prefix = 'cycle';
function element(suffix) { return document.getElementById(prefix + suffix); }
function load(exclusions) {
    context.actionEditorLoad(prefix, {
        type: 'cycle_pad', direction: 'previous', wrap: false, excluded_pads: exclusions
    });
}
function build(exclusions) {
    element('-type').value = 'cycle_pad';
    element('-cycle-pad-exclusions').value = exclusions;
    return JSON.parse(JSON.stringify(context.actionEditorBuild(prefix)));
}

const html = context.actionEditorHTML(prefix, '', {});
assert(html.includes('value="cycle_pad">Navigate pad sequence</option>'));
assert(html.includes(prefix + '-cycle-pad-direction'));
assert(html.includes(prefix + '-cycle-pad-wrap'));
assert(html.includes(prefix + '-cycle-pad-exclusions'));

load('5, 1,5,bad,99');
assert.strictEqual(element('-cycle-pad-direction').value, 'previous');
assert.strictEqual(element('-cycle-pad-wrap').checked, false);
assert.strictEqual(element('-cycle-pad-exclusions').value, '1,5');
assert.strictEqual(element('-cycle-pad-group').style.display, '');

element('-cycle-pad-direction').value = 'next';
element('-cycle-pad-wrap').checked = true;
assert.deepStrictEqual(build(''), { type: 'cycle_pad', direction: 'next', wrap: true });
assert.strictEqual(build(' 5 , 1, 5 ').excluded_pads, '1,5');
assert.strictEqual(build('1,bad,0,7,99').excluded_pads, '1');
assert.strictEqual(build('+2,-3,4').excluded_pads, '4');
assert.strictEqual(element('-cycle-pad-exclusions').value, '4');

context.deviceInfoCache = null;
assert.strictEqual(build('16,17').excluded_pads, '16');

console.log('portal_action_editor_cycle: PASS');