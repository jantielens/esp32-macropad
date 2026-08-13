// tests/test_portal_action_list.js — shared action-list controller host test
//
// Verifies actionEditorListRender/Load/Build/SetLabels: default vs
// host-supplied slot labels, the collapsed "Add action" placeholder for an
// empty slot vs. the host label once populated, empty-slot omission on save
// (not just trailing empties), and configured-order preservation.

const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

class Element {
    constructor(tag) {
        this.tagName = tag || 'DIV';
        this.value = '';
        this.open = false;
        this.style = { display: 'none' };
        this.dataset = {};
        this.attributes = {};
        this.textContent = '';
        var classes = new Set();
        this.classList = {
            add: function(c) { classes.add(c); },
            remove: function(c) { classes.delete(c); },
            contains: function(c) { return classes.has(c); }
        };
        this._summary = null;
        this._innerHTML = '';
    }
    addEventListener() {}
    hasAttribute(name) { return Object.prototype.hasOwnProperty.call(this.attributes, name); }
    getAttribute(name) { return this.attributes[name]; }
    setAttribute(name, value) { this.attributes[name] = value; }
    removeAttribute(name) { delete this.attributes[name]; }
    querySelector(sel) { return sel === 'summary' ? this._summary : null; }
    get innerHTML() { return this._innerHTML; }
    set innerHTML(html) {
        this._innerHTML = html;
        // Mirror what a real DOM parser would produce for the markup
        // actionEditorListRender() emits: a fully configured slot element per
        // "<details class=... id="X-group" data-slot-label="Y"><summary>Z</summary>".
        var slotRe = /<details class="([^"]*)" id="([\w-]+)-group" data-slot-label="([^"]*)">\s*<summary>([^<]*)<\/summary>/g;
        var m;
        while ((m = slotRe.exec(html))) {
            var el = getOrCreate(m[2] + '-group', 'DETAILS');
            m[1].split(' ').forEach(function(c) { el.classList.add(c); });
            el.dataset.slotLabel = m[3];
            el.open = false;
            var summary = new Element('SUMMARY');
            summary.textContent = m[4];
            el._summary = summary;
        }
        // Register each slot's type select so actionEditorLoad/Build can use it.
        var typeRe = /id="([\w-]+)-type"/g;
        while ((m = typeRe.exec(html))) getOrCreate(m[1] + '-type', 'SELECT');
    }
}

const elements = new Map();
function getOrCreate(id, tag) {
    if (!elements.has(id)) elements.set(id, new Element(tag));
    return elements.get(id);
}

const document = {
    getElementById(id) { return getOrCreate(id); },
    createElement(tag) { return new Element(tag); }
};

const context = {
    console,
    document,
    deviceInfoCache: {
        catalog: [
            { type: 'mqtt', group: 'Connectivity', label: 'Publish MQTT message' },
            { type: 'screen', group: 'Navigation', label: 'Navigate to screen' }
        ]
    },
    listInjectSyntheticScreenOption() {},
    padInitBindableColor() {},
    padSetBindableColor() {},
    padGetBindableColor() { return ''; }
};
vm.createContext(context);
vm.runInContext(fs.readFileSync('src/app/web/portal_action_editor.js', 'utf8'), context);

// --- Default slot labels ---
context.actionEditorListRender('container-a', ['a0', 'a1', 'a2']);
for (var i = 0; i < 3; i++) {
    var group = document.getElementById('a' + i + '-group');
    assert(group.classList.contains('action-list-slot'));
    assert.strictEqual(group.dataset.slotLabel, 'Action ' + (i + 1));
    assert.strictEqual(group._summary.textContent, 'Add action');
    assert.strictEqual(group.open, false);
}

// --- Host-supplied slot labels ---
context.actionEditorListRender('container-b', ['b0', 'b1', 'b2'], ['Left action 1', 'Left action 2', 'Left action 3']);
assert.strictEqual(document.getElementById('b0-group').dataset.slotLabel, 'Left action 1');
assert.strictEqual(document.getElementById('b1-group').dataset.slotLabel, 'Left action 2');
assert.strictEqual(document.getElementById('b1-group')._summary.textContent, 'Add left action');

// --- Loading a slot reveals the host label and opens the slot; an empty
//     slot stays collapsed with its own "Add <slot>" placeholder ---
context.actionEditorListLoad(['b0', 'b1', 'b2'], [{ type: 'mqtt', topic: 't', payload: 'p' }]);
assert.strictEqual(document.getElementById('b0-group')._summary.textContent, 'Left action 1');
assert.strictEqual(document.getElementById('b0-group').open, true);
assert.strictEqual(document.getElementById('b1-group')._summary.textContent, 'Add left action');
assert.strictEqual(document.getElementById('b1-group').open, false);

// --- actionEditorListSetLabels updates an already-populated slot's visible
//     summary immediately, and a still-empty slot's placeholder tracks the
//     new label too ---
context.actionEditorListSetLabels(['b0', 'b1', 'b2'], ['Up action 1', 'Up action 2', 'Up action 3']);
assert.strictEqual(document.getElementById('b0-group').dataset.slotLabel, 'Up action 1');
assert.strictEqual(document.getElementById('b0-group')._summary.textContent, 'Up action 1');
assert.strictEqual(document.getElementById('b1-group')._summary.textContent, 'Add up action');

// --- Empty-slot omission (not just trailing) + order preservation ---
context.actionEditorListRender('container-c', ['c0', 'c1', 'c2']);
context.actionEditorListLoad(['c0', 'c1', 'c2'], [
    { type: 'mqtt', topic: 'first', payload: '' },
    {}, // empty middle slot
    { type: 'screen', target: 'pad_2' }
]);
var built = context.actionEditorListBuild(['c0', 'c1', 'c2']);
assert.strictEqual(built.length, 2);
assert.strictEqual(built[0].type, 'mqtt');
assert.strictEqual(built[0].topic, 'first');
assert.strictEqual(built[1].type, 'screen');
assert.strictEqual(built[1].target, 'pad_2');

// Clearing a slot back to "(none)" collapses it back to its "Add <slot>"
// placeholder without requiring a re-render.
document.getElementById('c0-type').value = '';
context.actionEditorTypeChanged('c0');
assert.strictEqual(document.getElementById('c0-group')._summary.textContent, 'Add action');

console.log('portal_action_list: PASS');
