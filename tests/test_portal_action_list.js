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
        this.options = [];
        this.children = [];
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
    TextEncoder,
    deviceInfoCache: {
        catalog: [
            { type: 'mqtt', group: 'Connectivity', operations: [
                {id:'mqtt.publish', label:'Publish MQTT message', arguments:[
                    {name:'topic', label:'Topic', type:'string', max_length:64},
                    {name:'payload', label:'Payload', type:'string', max_length:64, allow_empty:true}
                ]}
            ] },
            { type: 'screen', group: 'Navigation', operations: [
                {id:'screen.open', label:'Navigate to screen', arguments:[
                    {name:'target', label:'Target', type:'string', max_length:31}
                ]}
            ] }
        ]
    },
    padInitBindableColor() {},
    escAttr(value) { return value || ''; },
    padSetBindableColor() {},
    padGetBindableColor() { return ''; }
};
vm.createContext(context);
vm.runInContext(fs.readFileSync('src/app/web/portal_action_editor.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_action_editor_screen.js', 'utf8'), context);

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
context.actionEditorListLoad(['b0', 'b1', 'b2'], [{ op: 'mqtt.publish', args: {topic:'t', payload:'p'} }]);
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
    { op: 'mqtt.publish', args: {topic:'first', payload:''} },
    {}, // empty middle slot
    { op: 'screen.open', args: {target:'pad_2'} }
]);
var built = context.actionEditorListBuild(['c0', 'c1', 'c2']);
assert.strictEqual(built.length, 2);
assert.strictEqual(built[0].op, 'mqtt.publish');
assert.strictEqual(built[0].args.topic, 'first');
assert.strictEqual(built[1].op, 'screen.open');
assert.strictEqual(built[1].args.target, 'pad_2');

// Clearing a slot back to "(none)" collapses it back to its "Add <slot>"
// placeholder without requiring a re-render.
document.getElementById('c0-type').value = '';
context.actionEditorTypeChanged('c0');
assert.strictEqual(document.getElementById('c0-group')._summary.textContent, 'Add action');

console.log('portal_action_list: PASS');

async function checkIndependentBootSaves() {
    const nodes = new Map();
    for (const id of ['boot-appearance-settings', 'boot-logo-settings', 'system-screens-save',
        'boot-appearance-font-size', 'boot-appearance-text-color', 'boot-appearance-background-color',
        'boot-logo-status', 'boot-logo-preview-wrap', 'boot-logo-preview']) {
        nodes.set(id, new Element());
    }
    const requests = [];
    const messages = [];
    let fail = false;
    let reboot = false;
    const ctx = {
        console: { error() {} },
        document: { getElementById: id => nodes.get(id) || null },
        actionEditorSlotPrefixes: context.actionEditorSlotPrefixes,
        actionEditorListBuild: () => [{ op: 'screen.back', args: {} }],
        actionEditorListLoad() {},
        actionEditorHandleError: () => false,
        padInitBindableColor() {},
        setPendingReboot() { reboot = true; },
        showMessage: (text, kind) => messages.push({ text, kind }),
        fetch: async (url, options) => {
            requests.push({ url, options });
            return {
                ok: !fail,
                json: async () => options ? { success: !fail, message: 'Save failed' } :
                    url.includes('boot-actions') ? { action_format_version: 1, actions: [] } : {
                    caps: { boot_appearance: true, boot_logo: false },
                    boot_appearance_font_size: 18,
                    boot_appearance_text_color: 4294967295,
                    boot_appearance_background_color: 0,
                    boot_appearance_default_text_color: 0xFFFFFF,
                    boot_appearance_default_background_color: 0
                }
            };
        }
    };
    vm.createContext(ctx);
    vm.runInContext(fs.readFileSync('src/app/web/portal_config_actions.js', 'utf8'), ctx);
    await ctx.saveBootActions();
    assert.strictEqual(requests.length, 0);
    await ctx.loadBootActions();
    requests.length = 0;
    await ctx.saveBootActions();
    assert.strictEqual(requests.length, 1);
    assert.strictEqual(requests[0].url, '/api/component/boot-actions/config');
    assert.deepStrictEqual(JSON.parse(requests[0].options.body), {
        action_format_version: 1,
        actions: [{ op: 'screen.back', args: {} }]
    });
    await ctx.bootAppearanceInit();
    assert.strictEqual(nodes.get('system-screens-save').disabled, false);
    requests.length = 0;
    await ctx.saveSystemScreenAppearance();
    assert.strictEqual(requests.length, 1);
    assert.strictEqual(requests[0].url, '/api/config?no_reboot=1');
    assert.deepStrictEqual(JSON.parse(requests[0].options.body), {
        boot_appearance_font_size: 18,
        boot_appearance_text_color: 4294967295,
        boot_appearance_background_color: 0
    });
    assert(reboot);
    fail = true;
    reboot = false;
    await ctx.saveSystemScreenAppearance();
    assert.strictEqual(messages.at(-1).kind, 'error');
    assert(!reboot);
    assert.strictEqual(nodes.get('system-screens-save').disabled, false);
    await ctx.bootAppearanceInit();
    assert.strictEqual(nodes.get('system-screens-save').disabled, true);
    const before = requests.length;
    await ctx.saveSystemScreenAppearance();
    assert.strictEqual(requests.length, before, 'failed loads must not overwrite saved appearance');
    console.log('independent boot actions / appearance saves: PASS');
}

async function checkSwipeActions() {
    const node = new Element();
    const loaded = {};
    const first = {
        action_format_version: 1,
        swipe_left: { op: 'screen.open', args: { target: 'pad_2' } },
        swipe_right: { op: 'screen.back', args: {} },
        swipe_up: { op: 'shutter.session.start', args: { camera: '' } },
        swipe_down: {}
    };
    const requests = [];
    const options = [];
    const ctx = {
        console,
        document: { getElementById: id => id === 'swipe-editors' ? node : null },
        actionEditorSlotPrefixes: context.actionEditorSlotPrefixes,
        getDeviceInfo: async () => {},
        actionEditorHTML: (prefix, label, opts) => {
            options.push({ prefix, operations: opts.operations, mixed: opts.mixed });
            return context.actionEditorHTML(prefix, label, opts);
        },
        actionEditorLoad: (prefix, action) => { loaded[prefix] = action; },
        actionEditorBuild: prefix => loaded[prefix],
        showMessage() {},
        actionEditorHandleError: () => false,
        fetch: async (url, request) => {
            requests.push({ url, request });
            return { ok: true, json: async () => first };
        }
    };
    vm.createContext(ctx);
    vm.runInContext(fs.readFileSync('src/app/web/portal_config_actions.js', 'utf8'), ctx);
    const screen = context.deviceInfoCache.catalog.find(entry => entry.type === 'screen');
    const previousOperations = screen.operations;
    context.escAttr = value => String(value || '').replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/</g, '&lt;');
    screen.operations = [
        { id: 'screen.open', label: 'Open screen', arguments: [
            { name: 'target', label: 'Screen', type: 'screen', dynamic: true, max_length: 31 }
        ] },
        { id: 'screen.back', label: 'Back', arguments: [] }
    ];
    context.deviceInfoCache.catalog.push({ type: 'shutter', group: 'Shutter', label: 'Shutter' });
    await ctx.swipeInitEditors();
    screen.operations = previousOperations;
    context.deviceInfoCache.catalog.pop();
    assert.deepStrictEqual(options.map(x => x.prefix),
        ['swipe-left', 'swipe-right', 'swipe-up', 'swipe-down']);
    assert(options.every(x => x.operations && !x.mixed));
    assert(node.innerHTML.includes('Search operations'));
    assert(node.innerHTML.includes('value="@screen.open"'));
    assert(node.innerHTML.includes('value="@screen.back"'));
    assert(!node.innerHTML.includes('value="shutter"'));
    await ctx.loadSwipeActions();
    await ctx.saveSwipeActions();
    assert.strictEqual(requests[1].url, '/api/component/swipe-actions/config');
    assert.deepStrictEqual(JSON.parse(requests[1].request.body), first);
}

async function checkTriggerHosts() {
    const nodes = new Map();
    const getNode = id => {
        if (!nodes.has(id)) nodes.set(id, new Element());
        return nodes.get(id);
    };
    getNode('hw-button-editors').querySelectorAll = () => [{}];
    getNode('mqtt-trigger-editors').querySelectorAll = () => [{}];
    const drafts = new Map();
    const rendered = [];
    const requests = [];
    const tap = { op: 'screen.open', args: { target: { $template: '[var:target]' } } };
    const hold = { op: 'screen.back', args: {} };
    const mqtt = { op: 'screen.back', args: {} };
    const ctx = {
        console,
        document: { getElementById: getNode },
        actionEditorSlotPrefixes: context.actionEditorSlotPrefixes,
        getDeviceInfo: async () => ({ has_display: true }),
        actionEditorListRender: (container, prefixes, labels, options) => {
            rendered.push({ container, options: options.actionOptions });
            const html = context.actionEditorHTML(prefixes[0], '', options.actionOptions);
            assert(html.includes('Search operations'));
        },
        actionEditorListLoad: (prefixes, actions) => drafts.set(prefixes[0], actions),
        actionEditorListBuild: prefixes => drafts.get(prefixes[0]) || [],
        actionEditorWireFragment() {},
        actionEditorHandleError: () => false,
        showMessage() {},
        fetch: async (url, request) => {
            requests.push({ url, request });
            return { ok: true, json: async () => url.includes('hw-buttons')
                ? { action_format_version: 1, buttons: [{ label: 'Key', pin: 1, tap_actions: [tap], hold_actions: [hold] }] }
                : { max: 1, triggers: [{ topic: 'event', value: 'on', actions: [mqtt] }] } };
        }
    };
    vm.createContext(ctx);
    vm.runInContext(fs.readFileSync('src/app/web/portal_config_actions.js', 'utf8'), ctx);
    await ctx.initHwButtons();
    await ctx.initMqttTriggers();
    assert.deepStrictEqual(rendered.map(entry => entry.container),
        ['hwbtn-1-tap-editors', 'hwbtn-1-hold-editors', 'mqtttrig-1-editors']);
    assert(rendered.every(entry => entry.options.operations));
    assert(rendered.every(entry => !entry.options.mixed));
    await ctx.saveHwButtons();
    await ctx.saveMqttTriggers();
    assert.deepStrictEqual(JSON.parse(requests[2].request.body),
        { action_format_version: 1, buttons: [{ tap_actions: [tap], hold_actions: [hold] }] });
    assert.deepStrictEqual(JSON.parse(requests[3].request.body),
        { action_format_version: 1, triggers: [{ topic: 'event', value: 'on', actions: [mqtt] }] });
}

async function checkTimerLoadFailure() {
    const button = new Element();
    const messages = [];
    const requests = [];
    let ok = false;
    const ctx = {
        console: {error() {}},
        document: {getElementById: id => id === 'timer-config-save' ? button : null},
        actionEditorSlotPrefixes: context.actionEditorSlotPrefixes,
        actionEditorListLoad() {},
        actionEditorListBuild: () => [],
        actionEditorHandleError: () => false,
        showMessage: (text, kind) => messages.push({text, kind}),
        fetch: async (url, options) => {
            requests.push({url, options});
            return {ok, json: async () => ({action_format_version: 1})};
        }
    };
    vm.createContext(ctx);
    vm.runInContext(fs.readFileSync('src/app/web/portal_config_actions.js', 'utf8'), ctx);
    await ctx.loadTimerConfig();
    assert(button.disabled);
    assert.strictEqual(messages.at(-1).kind, 'error');
    await ctx.saveTimerConfig();
    assert.strictEqual(requests.length, 1);
    ok = true;
    await ctx.loadTimerConfig();
    assert(!button.disabled);
    await ctx.saveTimerConfig();
    assert.strictEqual(requests.length, 3);
    assert.strictEqual(requests[2].options.method, 'POST');
    assert.strictEqual(JSON.parse(requests[2].options.body).action_format_version, 1);
}

async function checkShutterSessionActions() {
    const requests = [];
    const drafts = new Map();
    const messages = [];
    let available = false;
    const started = [{ op: 'shutter.session.start', args: { camera: { $template: '[var:camera]' } } }];
    const completed = [{ op: 'screen.back', args: {} }];
    const ctx = {
        window: {}, console: { error() {} },
        actionEditorSlotPrefixes: context.actionEditorSlotPrefixes,
        actionEditorListLoad: (prefixes, actions) => drafts.set(prefixes[0], actions),
        actionEditorListBuild: prefixes => drafts.get(prefixes[0]) || [],
        actionEditorHandleError: () => false,
        showMessage: (text, kind) => messages.push({ text, kind }),
        fetch: async (url, options) => {
            requests.push({ url, options });
            return { ok: available, json: async () => available
                ? { action_format_version: 1, save_start_actions: started, save_complete_actions: completed }
                : { error: 'unavailable' } };
        },
    };
    vm.createContext(ctx);
    vm.runInContext(fs.readFileSync('src/app/device_classes/shutter_tester/web/portal_shutter_config.js', 'utf8'), ctx);
    await ctx.loadShutterSessionActions();
    await ctx.saveShutterSessionActions();
    assert.strictEqual(requests.length, 1, 'A failed load must not overwrite session actions');
    assert(messages.some(message => message.kind === 'error'));
    available = true;
    await ctx.loadShutterSessionActions();
    await ctx.saveShutterSessionActions();
    assert.deepStrictEqual(JSON.parse(requests[2].options.body), {
        action_format_version: 1, save_start_actions: started, save_complete_actions: completed,
    });
}

Promise.all([checkIndependentBootSaves(), checkSwipeActions(), checkTriggerHosts(), checkTimerLoadFailure(), checkShutterSessionActions()]).catch(error => {
    console.error(error);
    process.exitCode = 1;
});
