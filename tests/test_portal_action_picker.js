// tests/test_portal_action_picker.js — action-type picker host test
//
// Verifies the picker renders from the firmware-authored catalog: grouped
// type options and typed operation arguments. Also guards against the
// regression that broke the previous (discarded) implementation: a broad
// DOM sweep on action-type selects that also matched the unrelated pad
// editor's widget-type and icon-type selects.

const assert = require('assert');
const fs = require('fs');
const vm = require('vm');
const path = require('path');
const {execFileSync} = require('child_process');
const catalogBinary = process.env.ACTION_CATALOG_BINARY || path.resolve('build/host-tests/tests/test_action_parse');
const productionCatalogs = JSON.parse(execFileSync(catalogBinary, ['--catalog'], {encoding:'utf8'}));
const voiceCatalogs = JSON.parse(execFileSync(path.join(path.dirname(catalogBinary), 'test_action_parse_voice_notes'),
    ['--catalog'], {encoding:'utf8'}));
assert.deepStrictEqual(productionCatalogs.portal, productionCatalogs.mcp.map(entry => {
    const metadata = {...entry};
    delete metadata.fields;
    return metadata;
}));
const setVariableCatalog = productionCatalogs.portal.find(entry => entry.type === 'set_var');
const setVariableOperation = setVariableCatalog.operations.find(operation => operation.id === 'variable.set');
assert.deepStrictEqual(setVariableCatalog && {
    group: setVariableCatalog.group,
    label: setVariableCatalog.label,
    nameDynamic: setVariableOperation.arguments.find(argument => argument.name === 'name').dynamic,
    valueDynamic: setVariableOperation.arguments.find(argument => argument.name === 'value').dynamic
}, { group: 'Variables', label: 'Set variable', nameDynamic: true, valueDynamic: true });

class Element {
    constructor(tag) {
        this.tagName = tag || 'SELECT';
        this.value = '';
        this.innerHTML = '';
        this.style = { display: 'none' };
        this.dataset = {};
        this.attributes = {};
        this.options = []; // appendChild-based options only (unsupported-type test)
        this.children = [];
        var classes = new Set();
        this.classList = {
            add: function(c) { classes.add(c); },
            remove: function(c) { classes.delete(c); },
            contains: function(c) { return classes.has(c); }
        };
    }
    appendChild(opt) { this.options.push(opt); return opt; }
    remove(index) { this.options.splice(index, 1); }
    addEventListener() {}
    setCustomValidity() {}
    reportValidity() { return true; }
    focus() {}
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
    },
    createElement(tag) { return new Element(tag); }
};

const FIXTURE_CATALOG = productionCatalogs.portal.concat(
    voiceCatalogs.portal.filter(entry => entry.group === 'Voice notes'));

const context = {
    console,
    TextEncoder,
    document,
    escAttr(s) { return (s || '').replace(/&/g, '&amp;').replace(/"/g, '&quot;').replace(/</g, '&lt;').replace(/>/g, '&gt;'); },
    deviceInfoCache: { catalog: FIXTURE_CATALOG },
    listInjectSyntheticScreenOption() {},
    padInitBindableColor() {},
    padSetBindableColor() {},
    padGetBindableColor() { return ''; },
    bindingAttachValidation() {}
};
vm.createContext(context);
vm.runInContext(fs.readFileSync('src/app/web/portal_action_editor.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_action_editor_screen.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_action_editor_gamepad.js', 'utf8'), context);

context.actionEditorLoadGamepad('held-test', { control: 'hat', direction: 'up' });
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuildGamepad('held-test'))),
    { op: 'gamepad.hat.down', args: { direction: 'up' } });
context.actionEditorLoadGamepad('held-test', { control: 'button', button: 17 });
assert.throws(() => context.actionEditorBuildGamepad('held-test'), /1-16/);
// Operation editors use the firmware descriptor, preserve dynamic text, and
// keep the fixed timer choice independent of the dynamic seconds argument.
const originalCatalog = context.deviceInfoCache.catalog;
context.deviceInfoCache.catalog = productionCatalogs.portal.concat([{
    type: 'camera_capture', group: 'Camera', label: 'Save camera snapshot',
    operations: [{ id: 'camera.capture', label: 'Save camera snapshot', arguments: [{
        name: 'save_to', label: 'Save to', type: 'enum', dynamic: true,
        example: '[var:camera_destination]', control: 'select', max_length: 6,
        choices: ['both', 'latest', 'roll'], choice_labels: ['Latest and camera roll', 'Latest image', 'Camera roll image']
    }] }]
}, {
    type: 'test_picker', group: 'Test', label: 'Picker',
    operations: [{ id: 'test.pickers', label: 'Pick', arguments: [
        { name: 'button', label: 'Button', type: 'integer', min: 1, max: 16,
          dynamic: true, example: '[var:button]', control: 'gamepad_buttons' },
        { name: 'file', label: 'Sound', type: 'string', max_length: 63,
          dynamic: true, example: '[var:file]', control: 'sounds' }
    ] }]
}, {
    type: 'shutter', group: 'Shutter Tester', label: 'Shutter tester',
    operations: [{ id: 'shutter.target.set', label: 'Set target speed', arguments: [{
        name: 'speed', label: 'Target speed', type: 'enum', max_length: 8,
        dynamic: true, example: '[item:speed]', control: 'select', choices: ['1s', '1/125s']
    }] }]
}, {
    type: 'test_defaults', group: 'Test', label: 'Defaults',
    operations: [{ id: 'test.defaults', label: 'Defaults', arguments: [
        { name: 'on', label: 'On', type: 'boolean', default: true },
        { name: 'off', label: 'Off', type: 'boolean', default: false },
        { name: 'implicit', label: 'Implicit', type: 'boolean' }
    ] }]
}]);
context.validateBinding = value => ({ valid: !!value, errors: [{ message: 'Invalid binding' }] });
const operationOptions = context.actionEditorTypeOptionsHTML({ operations: true });
assert(operationOptions.includes('value="@screen.open">Navigate to screen'));
assert(operationOptions.includes('value="@shutter.target.set">Set target speed'));
assert(operationOptions.includes('value="@screen.back">Navigate back'));
assert(operationOptions.includes('value="@music.next">Next track'));
assert(operationOptions.includes('value="@timer.adjust">Adjust timer'));
assert(operationOptions.includes('value="@wifi.station.set_enabled">Set Wi-Fi station mode'));
assert(operationOptions.includes('value="@wifi.ap.set_enabled">Set access point'));
assert(operationOptions.includes('value="@wifi.radio.turn_off">Turn off Wi-Fi'));
assert(operationOptions.includes('value="@delay.wait">Delay'));
const mixedOptions = context.actionEditorTypeOptionsHTML({ operations: true, mixed: true });
assert(mixedOptions.includes('value="@screen.open">Navigate to screen'));
assert(!mixedOptions.includes('value="screen">Navigate to screen'));
assert(mixedOptions.includes('value="@visual_alert.start">'));
const legacyVisual = { type: 'visual_alert', op: 'show', color: 'ff0000' };
document.getElementById('legacy-pad-type').setAttribute('data-operations', 'true');
context.actionEditorLoad('legacy-pad', legacyVisual);
assert.throws(() => context.actionEditorBuild('legacy-pad'), /Legacy actions are not supported/);
context.actionEditorLoad('legacy-host', legacyVisual);
assert.throws(() => context.actionEditorBuild('legacy-host'), /Legacy actions are not supported/);
const mixedType = document.getElementById('mixed-host-type');
mixedType.setAttribute('data-operations', 'true');
mixedType.setAttribute('data-mixed', 'true');
const mixedLegacy = { type: 'visual_alert', op: 'stop', pattern: 'breathe' };
context.actionEditorLoad('mixed-host', mixedLegacy);
assert.throws(() => context.actionEditorBuild('mixed-host'), /Legacy actions are not supported/);
for (const action of [
    { op: 'screen.open', args: { target: 'pad_2' } },
    { op: 'wifi.station.set_enabled', args: { enabled: false } }
]) {
    context.actionEditorLoad('mixed-host', action);
    assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild('mixed-host'))), action);
}
context.actionEditorLoad('mixed-host', { op: 'unknown.operation', args: { value: 'preserve' } });
assert.throws(() => context.actionEditorBuild('mixed-host'), /unavailable/);
const restoredSelect = document.getElementById('restored-type');
let restoredValue = '';
Object.defineProperty(restoredSelect, 'value', {
    get() { return restoredValue; },
    set(value) { restoredValue = restoredSelect.options.some(option => option.value === value) ? value : ''; }
});
context.actionEditorLoad('restored', { op: 'unknown.operation', args: {} });
assert.strictEqual(restoredSelect.value, '@unknown.operation');
assert.strictEqual(restoredSelect.options[0].disabled, true);
assert.match(restoredSelect.options[0].textContent, /unsupported by this build/);
assert.strictEqual(document.getElementById('restored-remove').hidden, false);
assert.throws(() => context.actionEditorBuild('restored'), /unavailable/);
context.actionEditorLoad('restored', { op: 'screen.back', args: {} });
assert.strictEqual(restoredSelect.value, '@screen.back');
assert.strictEqual(restoredSelect.options[1].disabled, false);
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild('restored'))),
    { op: 'screen.back', args: {} });
context.actionEditorLoad('restored', {});
assert.strictEqual(restoredSelect.value, '');
assert.strictEqual(document.getElementById('restored-remove').hidden, true);
const mockCatalog = JSON.parse(fs.readFileSync('tools/mock-data/pad-editor.json', 'utf8')).catalog;
const catalogsToCheck = [productionCatalogs.portal, voiceCatalogs.portal];
if (process.env.ACTION_CATALOG_FULL_BINARY) {
    catalogsToCheck.push(JSON.parse(execFileSync(process.env.ACTION_CATALOG_FULL_BINARY, ['--catalog'], {encoding:'utf8'})).portal);
}
for (const catalog of catalogsToCheck) {
    for (const entry of catalog) {
        assert.deepStrictEqual(mockCatalog.find(item => item.type === entry.type), entry);
    }
}
const operationMarkup = context.actionEditorHTML('operation-test', '', { operations: true });
assert(operationMarkup.includes('data-search-placeholder="Search actions..."'));
assert(!operationMarkup.includes('operation-test-generic-volume'));
assert(!operationMarkup.includes('operation-test-shutter-group'));
assert(operationMarkup.includes('> Dynamic</label>'));
assert(operationMarkup.includes('Timer 1'));
assert(operationMarkup.includes('min="1" max="55000" value="1"'));
assert(operationMarkup.includes('Expected: true or false'));
assert(operationMarkup.includes('<option value="1/125s">1/125s</option>'));
for (const speed of ['1/125s', { $template: '[item:speed]' }]) {
    const action = { op: 'shutter.target.set', args: { speed } };
    context.actionEditorLoad('operation-test', action);
    assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild('operation-test'))), action);
}
const cycleWrap = context.actionEditorOperation('pad.cycle').arguments.find(argument => argument.name === 'wrap');
assert.strictEqual(cycleWrap.default, true);
const cycleWrapOptions = operationMarkup.match(/<select[^>]*id="operation-test-op-pad\.cycle-wrap-fixed"[^>]*>(.*?)<\/select>/)[1];
assert(cycleWrapOptions.startsWith('<option value="true" selected>'));
const defaultMarkup = name => operationMarkup.match(new RegExp('<select[^>]*id="operation-test-op-test.defaults-' + name + '-fixed"[^>]*>(.*?)</select>'))[1];
assert(defaultMarkup('on').includes('<option value="true" selected>'));
assert(defaultMarkup('off').includes('<option value="false" selected>'));
assert(!defaultMarkup('implicit').includes(' selected'));
assert(operationMarkup.includes('Optional comma-separated 1-based pad numbers.'));
assert(operationMarkup.includes('List ID on the active pad.'));
const defaultPrefix = 'boolean-defaults';
const defaultInputs = ['on', 'off', 'implicit'].map((name, index) => {
    const input = document.getElementById(defaultPrefix + '-op-test.defaults-' + name + '-fixed');
    input.options = [
        {value:'true', defaultSelected:index === 0},
        {value:'false', defaultSelected:index === 1}
    ];
    Object.defineProperty(input, 'selectedIndex', {set(i) { this.value = this.options[i].value; }});
    return input;
});
document.getElementById(defaultPrefix + '-fields').querySelectorAll = () => defaultInputs;
context.actionEditorLoad(defaultPrefix, {op:'test.defaults', args:{on:false, off:true, implicit:false}});
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild(defaultPrefix))),
    {op:'test.defaults', args:{on:false, off:true, implicit:false}});
context.actionEditorLoad(defaultPrefix, {});
document.getElementById(defaultPrefix + '-type').value = '@test.defaults';
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild(defaultPrefix))),
    {op:'test.defaults', args:{on:true, off:false, implicit:true}});
assert(operationMarkup.includes('<option value="roll">Camera roll image</option>'));
assert(operationMarkup.includes('Expected: both, latest, roll.'));
assert(operationMarkup.includes('&quot;aan&quot;'));
assert(operationMarkup.includes('<textarea class="form-control form-control-sm" id="operation-test-op-dialog.confirm-message-fixed"'));
assert(operationMarkup.includes('operation-test-op-dialog.confirm-timeout_ms-zero'));
assert(operationMarkup.includes('No timeout'));
for (const [id, value] of [['dialog.notify-duration_ms', 3000], ['dialog.confirm-timeout_ms', 10000]]) {
    const base = 'operation-test-op-' + id;
    const input = operationMarkup.match(new RegExp('<input[^>]*value="' + value + '"[^>]*id="' + base + '-fixed"[^>]*>'))[0];
    const zero = operationMarkup.match(new RegExp('<input[^>]*id="' + base + '-zero"[^>]*>'))[0];
    assert(!input.includes(' disabled'));
    assert(!zero.includes(' checked'));
}
assert(operationMarkup.includes('operation-test-op-visual_alert.start-color-color-wrap'));
assert(operationMarkup.includes('value="Continue"'));
assert(operationMarkup.includes('<option value="16">Button 16</option>'));
const bthomeOperation = context.actionEditorOperation('bthome.send');
assert(bthomeOperation);
const bthomeButton = bthomeOperation.arguments.find(argument => argument.name === 'button');
assert.strictEqual(bthomeButton.control, 'bthome_buttons');
const bthomeMarkup = operationMarkup.match(/<select[^>]*id="operation-test-op-bthome.send-button-fixed"[^>]*>(.*?)<\/select>/)[1];
assert(bthomeMarkup.startsWith('<option value="1">Button 1</option>'));
assert(bthomeMarkup.endsWith('<option value="10">Button 10</option>'));
const haAction = { op: 'ha.service.call', args: {
    entity_id: 'light.living_room', service: 'turn_on', data_json: '{"brightness_pct":80}'
} };
context.actionEditorLoad('operation-test', haAction);
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild('operation-test'))), haAction);
for (const invalid of ['[1]', 'null', '{"brightness_pct":}']) {
    document.getElementById('operation-test-op-ha.service.call-data_json-fixed').value = invalid;
    assert.throws(() => context.actionEditorBuild('operation-test'), /JSON object/);
}
document.getElementById('operation-test-op-ha.service.call-data_json-fixed').value = '';
assert.strictEqual(context.actionEditorBuild('operation-test').args.data_json, '');
haAction.args.data_json = { $template: '[var:service_data]' };
context.actionEditorLoad('operation-test', haAction);
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild('operation-test'))), haAction);
for (const button of [1, 10]) {
    const saved = { op: 'bthome.send', args: { button, event: 'press' } };
    context.actionEditorLoad('operation-test', saved);
    assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild('operation-test'))), saved);
}
document.getElementById('operation-test-op-bthome.send-button-fixed').value = '11';
assert.throws(() => context.actionEditorBuild('operation-test'), /whole number/);
assert(operationMarkup.includes('Select a sound'));
const longKeys = { op: 'keyboard.send', args: { sequence: 'a'.repeat(255) } };
context.actionEditorLoad('operation-test', longKeys);
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild('operation-test'))), longKeys);
document.getElementById('operation-test-op-keyboard.send-sequence-fixed').value += 'a';
assert.throws(() => context.actionEditorBuild('operation-test'), /allowed length/);
context.actionEditorLoad('operation-test', { op: 'keyboard.send', args: {
    sequence: { $template: 'a'.repeat(192) }
} });
assert.throws(() => context.actionEditorBuild('operation-test'), /under 192 bytes/);
const literalText = {op: 'keyboard.type', args: {text: 'é'.repeat(1024), allow_controls: false}};
context.actionEditorLoad('operation-test', literalText);
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild('operation-test'))), literalText);
document.getElementById('operation-test-op-keyboard.type-text-fixed').value += 'a';
assert.throws(() => context.actionEditorBuild('operation-test'), /allowed length/);
const soundInput = document.getElementById('operation-test-op-test.pickers-file-fixed');
soundInput.appendChild({ value: '', textContent: 'Select a sound' });
const pickerAction = { op: 'test.pickers', args: { button: 16, file: 'missing.mp3' } };
context.actionEditorLoad('operation-test', pickerAction);
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild('operation-test'))), pickerAction);
context.actionEditorUpdateSounds(soundInput, ['bell.mp3']);
assert.deepStrictEqual(soundInput.options.map(option => option.value), ['', 'bell.mp3', 'missing.mp3']);
assert.strictEqual(soundInput.value, 'missing.mp3');
context.fetch = url => {
    assert.strictEqual(url, '/api/sounds/list');
    return Promise.resolve({ ok: true, json: () => Promise.resolve(['chime.mp3']) });
};
const pickerOperation = context.actionEditorOperation('test.pickers');
const soundPickerTest = context.actionEditorLoadOperationSounds('operation-test', pickerOperation)
    .then(() => {
        assert.deepStrictEqual(soundInput.options.map(option => option.value), ['', 'chime.mp3', 'missing.mp3']);
        assert.strictEqual(soundInput.value, 'missing.mp3');
        context.fetch = () => Promise.resolve({ ok: false, status: 503 });
        return context.actionEditorLoadOperationSounds('operation-test', pickerOperation);
    }).then(() => {
        const message = document.getElementById('operation-test-op-test.pickers-file-load-error');
        assert.match(message.textContent, /Could not load sounds/);
        context.fetch = () => Promise.resolve({ ok: true, json: () => Promise.resolve(['bell.mp3']) });
        return context.actionEditorLoadOperationSounds('operation-test', pickerOperation);
    }).then(() => {
        assert.strictEqual(document.getElementById('operation-test-op-test.pickers-file-load-error').textContent, '');
    }).catch(error => { console.error(error); process.exitCode = 1; });
for (const action of [
    { op: 'pad.cycle', args: { direction: 'previous', wrap: false, excluded_pads: '1,3' } },
    { op: 'pad.cycle', args: { direction: 'next', wrap: true, excluded_pads: { $template: '[var:excluded_pads]' } } },
    { op: 'list.navigate', args: { target: 'notes', command: 'next_page' } },
    { op: 'dialog.notify', args: { message: 'First\nSecond', duration_ms: 0 } },
    { op: 'dialog.confirm', args: { message: 'Proceed?', approve_text: 'Yes', cancel_text: 'No', timeout_ms: 1000 } },
    { op: 'visual_alert.start', args: { color: '#abc', pattern: 'blink', period_ms: 800, intensity: 80, duration_ms: 0 } }
]) {
    context.actionEditorLoad('operation-test', action);
    assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild('operation-test'))), action);
}
context.actionEditorLoad('operation-test', {op:'pad.cycle', args:{direction:'next', wrap:false, excluded_pads:' 1, 3 '}});
assert.strictEqual(context.actionEditorBuild('operation-test').args.excluded_pads, ' 1, 3 ');
for (const value of ['0', '17', '1,', '1,abc']) {
    document.getElementById('operation-test-op-pad.cycle-excluded_pads-fixed').value = value;
    assert.throws(() => context.actionEditorBuild('operation-test'), /comma-separated pad numbers/);
}
context.actionEditorLoad('operation-test', {op:'list.navigate', args:{target:'list-1_A', command:'next_page'}});
assert.strictEqual(context.actionEditorBuild('operation-test').args.target, 'list-1_A');
document.getElementById('operation-test-op-list.navigate-target-fixed').value = 'bad.id';
assert.throws(() => context.actionEditorBuild('operation-test'), /List ID/);
context.actionEditorLoad('operation-test', {op:'list.navigate', args:{target:{$template:'[var:list_id]'}, command:'next_page'}});
assert.strictEqual(context.actionEditorBuild('operation-test').args.target.$template, '[var:list_id]');
const dialogTiming = 'operation-test-op-dialog.confirm-timeout_ms';
context.actionEditorLoad('operation-test', { op: 'dialog.confirm', args: {
    message: 'Proceed?', approve_text: 'Yes', cancel_text: 'No', timeout_ms: 0
} });
assert.strictEqual(document.getElementById(dialogTiming + '-zero').checked, true);
assert.strictEqual(document.getElementById(dialogTiming + '-fixed').disabled, true);
document.getElementById(dialogTiming + '-zero').checked = false;
context.actionEditorOperationTimingChanged('operation-test', 'dialog.confirm', 'timeout_ms');
document.getElementById(dialogTiming + '-fixed').value = '999';
assert.throws(() => context.actionEditorBuild('operation-test'), /0 or a whole number from 1000/);
document.getElementById(dialogTiming + '-fixed').value = '1000';
assert.strictEqual(context.actionEditorBuild('operation-test').args.timeout_ms, 1000);
const alertColor = 'operation-test-op-visual_alert.start-color-fixed';
context.actionEditorLoad('operation-test', { op: 'visual_alert.start', args: {
    color: '', pattern: 'breathe', period_ms: 0, intensity: 0, duration_ms: 0
} });
assert.strictEqual(context.actionEditorBuild('operation-test').args.color, '');
document.getElementById(alertColor).value = 'invalid';
assert.throws(() => context.actionEditorBuild('operation-test'), /hex color/);
const clearSelect = document.getElementById('clear-test-type');
clearSelect.value = '@screen.open';
let clearChanges = 0;
context.Event = class { constructor(type, options) { this.type = type; this.bubbles = options.bubbles; } };
clearSelect.dispatchEvent = event => {
    assert.strictEqual(event.type, 'change');
    assert.strictEqual(event.bubbles, true);
    clearChanges++;
    context.actionEditorTypeChanged('clear-test');
};
context.actionEditorClear('clear-test');
assert.strictEqual(clearSelect.value, '');
assert.strictEqual(clearChanges, 1);
assert.strictEqual(document.getElementById('clear-test-remove').hidden, true);
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild('clear-test'))), {});
for (const action of [
    { op: 'screen.back', args: {} },
    { op: 'device.restart', args: {} },
    { op: 'display.brightness.save', args: {} },
    { op: 'audio.volume.save', args: {} },
    { op: 'music.next', args: {} },
    { op: 'camera.capture', args: { save_to: 'roll' } },
    { op: 'camera.capture', args: { save_to: { $template: '[var:camera_destination]' } } },
    { op: 'variable.set', args: { name: 'counter', value: '' } },
    { op: 'variable.set', args: { name: 'counter', value: '[var:counter]' } },
    { op: 'variable.set', args: { name: 'counter', value: { $template: '[var:counter]' } } },
    { op: 'display.brightness.set', args: { percent: 60 } },
    { op: 'display.brightness.adjust', args: { percent: { $template: '[event:step]' } } },
    { op: 'audio.volume.set', args: { percent: 40 } },
    { op: 'audio.volume.adjust', args: { percent: { $template: '[event:step]' } } }
]) {
    context.actionEditorLoad('operation-test', action);
    assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild('operation-test'))), action);
}
context.actionEditorLoad('operation-test', { op: 'camera.capture', args: { save_to: 'roll' } });
document.getElementById('operation-test-op-camera.capture-save_to-fixed').value = 'invalid';
assert.throws(() => context.actionEditorBuild('operation-test'), /available choice/);
const wifiAction = { op: 'wifi.station.set_enabled', args: { enabled: false } };
context.actionEditorLoad('operation-test', wifiAction);
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild('operation-test'))), wifiAction);
const wifiBase = 'operation-test-op-wifi.station.set_enabled-enabled';
document.getElementById(wifiBase + '-dynamic').checked = true;
context.actionEditorOperationModeChanged('operation-test', 'wifi.station.set_enabled', 'enabled');
assert.strictEqual(document.getElementById(wifiBase + '-template').value, 'false');
document.getElementById(wifiBase + '-template').value = '[event:value]';
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild('operation-test'))),
    { op: 'wifi.station.set_enabled', args: { enabled: { $template: '[event:value]' } } });
for (const action of [
    { op: 'wifi.ap.set_enabled', args: { enabled: true } },
    { op: 'wifi.radio.turn_off', args: {} }
]) {
    context.actionEditorLoad('operation-test', action);
    assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild('operation-test'))), action);
}
const operation = { op: 'timer.adjust', args: { timer: 2, seconds: { $template: '[expr:[event:step]*2]' } } };
context.actionEditorLoad('operation-test', operation);
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild('operation-test'))), operation);
const timerBase = 'operation-test-op-timer.adjust-timer';
document.getElementById(timerBase + '-dynamic').checked = true;
context.actionEditorOperationModeChanged('operation-test', 'timer.adjust', 'timer');
assert.strictEqual(document.getElementById(timerBase + '-template').value, '2');
document.getElementById(timerBase + '-template').value = '[var:timer]';
document.getElementById(timerBase + '-dynamic').checked = false;
context.actionEditorOperationModeChanged('operation-test', 'timer.adjust', 'timer');
document.getElementById(timerBase + '-dynamic').checked = true;
context.actionEditorOperationModeChanged('operation-test', 'timer.adjust', 'timer');
assert.strictEqual(document.getElementById(timerBase + '-template').value, '[var:timer]');
document.getElementById(timerBase + '-dynamic').checked = false;
document.getElementById(timerBase + '-fixed').value = '3';
context.actionEditorOperationFixedChanged('operation-test', 'timer.adjust', 'timer');
document.getElementById(timerBase + '-dynamic').checked = true;
context.actionEditorOperationModeChanged('operation-test', 'timer.adjust', 'timer');
assert.strictEqual(document.getElementById(timerBase + '-template').value, '3');
document.getElementById(timerBase + '-dynamic').checked = false;
const screenBase = 'operation-test-op-screen.open-target';
document.getElementById(screenBase + '-fixed').value = 'pad_2';
document.getElementById(screenBase + '-dynamic').checked = true;
context.actionEditorOperationModeChanged('operation-test', 'screen.open', 'target');
assert.strictEqual(document.getElementById(screenBase + '-template').value, 'pad_2');
const screenSelect = document.getElementById('pending-screen-op-screen.open-target-fixed');
const screenTemplate = document.getElementById('pending-screen-op-screen.open-target-template');
screenSelect.setAttribute('data-pending-value', 'pad_missing');
screenTemplate.setAttribute('data-dynamic-waiting', '1');
context.actionEditorPopulateScreens(['pending-screen'], [{id:'pad_0', name:'Home'}]);
assert.strictEqual(screenSelect.value, 'pad_missing');
assert(screenSelect.options.some(option => option.value === 'pad_missing' && option.textContent.includes('unavailable')));
assert.strictEqual(screenTemplate.value, 'pad_missing');
const savedDynamic = { op: 'timer.adjust', args: { timer: { $template: '[var:timer]' }, seconds: 5 } };
context.actionEditorLoad('operation-test', savedDynamic);
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild('operation-test'))), savedDynamic);
context.actionEditorLoad('operation-test', operation);
const secondsBase = 'operation-test-op-timer.adjust-seconds';
assert.strictEqual(document.getElementById(secondsBase + '-fixed-group').style.display, 'none');
assert.strictEqual(document.getElementById(secondsBase + '-dynamic-group').style.display, '');
assert.strictEqual(document.getElementById(secondsBase + '-label').htmlFor, secondsBase + '-template');
document.getElementById(secondsBase + '-fixed').value = '42';
document.getElementById(secondsBase + '-dynamic').checked = false;
context.actionEditorOperationRefresh('operation-test', 'timer.adjust');
assert.strictEqual(document.getElementById(secondsBase + '-fixed-group').style.display, '');
assert.strictEqual(document.getElementById(secondsBase + '-dynamic-group').style.display, 'none');
assert.strictEqual(context.actionEditorBuild('operation-test').args.seconds, 42);
document.getElementById(secondsBase + '-dynamic').checked = true;
context.actionEditorOperationRefresh('operation-test', 'timer.adjust');
assert.strictEqual(context.actionEditorBuild('operation-test').args.seconds.$template, '[expr:[event:step]*2]');
document.getElementById('operation-test-op-timer.adjust-seconds-template').value = '';
assert.throws(() => context.actionEditorBuild('operation-test'), /dynamic value/);
context.deviceInfoCache.catalog = originalCatalog;

// --- Regression guard: no broad DOM sweep that could catch unrelated selects ---
// The discarded prior implementation used
// document.querySelectorAll('select[id$="-type"]') to refresh every action
// picker after /api/info resolved. That pattern also matched the pad editor's
// unrelated widget-type and icon-type selects and wiped them. This design has
// no such sweep at all: the catalog is read once, synchronously, at HTML
// generation time. Guard against it recurring.
const editorSource = fs.readFileSync('src/app/web/portal_action_editor.js', 'utf8');
assert(!editorSource.includes('document.querySelectorAll'));
assert(editorSource.includes('action-type-select'));
const alarmType = document.getElementById('alarm-hook-1-type');
alarmType.setAttribute('data-alarm-hook', 'true');
const savedCatalog = context.deviceInfoCache.catalog;
context.deviceInfoCache.catalog = [
    { type: 'sync_owner', group: 'Test', label: 'Synchronous legacy', alarm_hook_allowed: true, operations: [
        { id: 'test.pause', label: 'Pausable', alarm_hook_allowed: false, arguments: [] },
        { id: 'test.unknown', label: 'Unknown', alarm_hook_allowed: false, arguments: [] }
    ] },
    { type: 'pause_owner', group: 'Test', label: 'Pausable legacy', alarm_hook_allowed: false, operations: [
        { id: 'test.sync', label: 'Synchronous', alarm_hook_allowed: true, arguments: [] },
        { id: 'test.missing', label: 'Missing metadata', arguments: [] }
    ] },
    { type: 'legacy_sync', group: 'Test', label: 'Legacy', alarm_hook_allowed: true }
];
const mixedAlarm = context.actionEditorTypeOptionsHTML({alarmHook:true, operations:true, mixed:true});
assert(mixedAlarm.includes('value="@test.sync"'));
assert(!mixedAlarm.includes('value="@test.pause"'));
assert(!mixedAlarm.includes('value="@test.unknown"'));
assert(!mixedAlarm.includes('value="@test.missing"'));
assert(!mixedAlarm.includes('value="legacy_sync"'));
assert(!mixedAlarm.includes('value="sync_owner"'));
assert(!mixedAlarm.includes('value="pause_owner"'));
alarmType.setAttribute('data-operations', 'true');
alarmType.setAttribute('data-mixed', 'true');
context.actionEditorLoad('alarm-hook-1', {op:'test.sync', args:{}});
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild('alarm-hook-1'))), {op:'test.sync', args:{}});
for (const id of ['test.pause', 'test.unknown', 'test.missing']) {
    context.actionEditorEnsureUnsupportedOption(alarmType, '@' + id);
    assert.strictEqual(alarmType.options[alarmType.options.length - 1].disabled, true);
    context.actionEditorLoad('alarm-hook-1', {op:id, args:{}});
    assert.throws(() => context.actionEditorBuild('alarm-hook-1'), /not allowed for alarm hooks/);
}
context.deviceInfoCache.catalog = savedCatalog;
context.window = {};
vm.runInContext(fs.readFileSync('src/app/web/portal_alarms.js', 'utf8'), context);
document.getElementById('alarm-time').value = '07:30';
document.getElementById('alarm-enabled').checked = true;
for (let day = 0; day < 7; day++) document.getElementById('alarm-day-' + day).checked = false;
const alarmForm = document.getElementById('alarm-config-form');
alarmForm.dataset.section = 'schedule';
alarmForm.dataset.onceLocal = '2026-10-08 07:30';
context.alarmScheduleSummary();
assert.strictEqual(document.getElementById('alarm-repeat-summary').textContent, 'Only once: next valid 07:30, then automatically disabled. Snooze can still ring again within that session.');
assert.strictEqual(document.getElementById('alarm-once-target').hidden, false);
assert.strictEqual(document.getElementById('alarm-once-target').textContent, 'Scheduled: 2026-10-08 07:30 (device timezone).');
const pendingOnceText = 'Saving enabled one-shot settings schedules the next occurrence, usually today or tomorrow. Unchanged settings keep the saved occurrence.';
alarmForm.dataset.dirty = 'true';
context.alarmScheduleSummary();
assert.strictEqual(document.getElementById('alarm-once-target').textContent, pendingOnceText);
assert.strictEqual(document.getElementById('alarm-once-target').hidden, false);
alarmForm.dataset.dirty = 'false';
alarmForm.dataset.onceLocal = '';
context.alarmScheduleSummary();
assert.strictEqual(document.getElementById('alarm-once-target').textContent, pendingOnceText);
assert.strictEqual(document.getElementById('alarm-once-target').hidden, false);
document.getElementById('alarm-day-1').checked = true;
document.getElementById('alarm-day-5').checked = true;
context.alarmScheduleSummary();
assert.strictEqual(document.getElementById('alarm-repeat-summary').textContent, 'Weekly: Mon, Fri at 07:30.');
assert.strictEqual(document.getElementById('alarm-once-target').hidden, true);
document.getElementById('alarm-enabled').checked = false;
context.alarmScheduleSummary();
assert(document.getElementById('alarm-repeat-summary').textContent.startsWith('Disabled. Enable and save to schedule. '));
assert.strictEqual(document.getElementById('alarm-once-target').hidden, true);
const scheduleFragment = fs.readFileSync('src/app/web/alarm-schedule.fragment.html', 'utf8');
const behaviorFragment = fs.readFileSync('src/app/web/alarm-behavior.fragment.html', 'utf8');
const alarmFragment = scheduleFragment + behaviorFragment;
assert(!scheduleFragment.includes('alarm-snooze-minutes'));
assert(!scheduleFragment.includes('alarm-ring-editors'));
assert(!behaviorFragment.includes('alarm-time'));
assert.deepStrictEqual(Object.keys(context.alarmConfigBuild()).sort(), ['enabled', 'hour', 'minute', 'weekdays']);
alarmForm.dataset.section = 'behavior';
assert.deepStrictEqual(Object.keys(context.alarmConfigBuild()).sort(), ['action_format_version', 'auto_dismiss_minutes', 'lateness_minutes', 'on_ring', 'on_stop', 'snooze_minutes']);
for (const label of ['Repeat days', 'Snooze duration (minutes)', 'Auto-dismiss after (minutes)',
    'When ringing starts', 'When ringing stops', 'Maximum alarm lateness (minutes)']) {
    assert(alarmFragment.includes(label));
}
const bindingDocs = JSON.parse(fs.readFileSync('tools/mock-data/binding-docs.json', 'utf8'));
const alarmDocs = bindingDocs.schemes.find(scheme => scheme.name === 'alarm');
assert(alarmDocs, 'alarm scheme documented');
for (const key of ['1_next_seconds', '1_next_ring_seconds', '1_dismiss_seconds']) {
    assert(alarmDocs.key_docs.some(doc => doc.key === key && doc.desc), key);
}
console.log('portal_action_picker: PASS');
context.deviceInfoCache.catalog = productionCatalogs.portal;
const roundTrips = [
    {op:'variable.set', args:{name:'counter', value:{$template:'[expr:[var:counter|0] + 1]'}}},
    {op:'keyboard.send', args:{sequence:'ctrl+c'}},
    {op:'timer.adjust', args:{timer:2, seconds:{$template:'[event:step]'}}},
    {op:'delay.wait', args:{duration_ms:55000}}
];
for (const action of roundTrips) {
    context.actionEditorLoad('round-trip', action);
    assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild('round-trip'))), action, 'round-trip: ' + action.op);
}
const productionOptions = context.actionEditorTypeOptionsHTML({alarmHook:true});
for (const entry of productionCatalogs.portal) {
    for (const operation of entry.operations || []) {
        assert.strictEqual(productionOptions.includes('<option value="@' + operation.id + '">'),
            operation.alarm_hook_allowed === true, 'actual hook eligibility: ' + operation.id);
    }
}
console.log('production portal/MCP action parity: PASS');

async function testBrewTemplateLoading() {
    const operation = { id: 'brew.select', arguments: [{ name: 'template', control: 'brew_templates' }] };
    const base = 'brew-test-op-brew.select-template';
    const select = document.getElementById(base + '-fixed');
    const template = document.getElementById(base + '-template');
    select.appendChild = function(option) {
        this.options.push(option);
        if (this.options.length === 1 && !this.value) this.value = option.value;
    };
    context.fetch = () => Promise.resolve({
        ok: true, json: () => Promise.resolve([{ name: 'espresso', display_name: 'Espresso' }])
    });
    context.actionEditorOperationFixedChanged('brew-test', operation.id, 'template');
    assert.strictEqual(template.value, '');
    await context.actionEditorLoadBrewTemplates('brew-test', operation);
    assert.strictEqual(select.value, 'espresso');
    assert.strictEqual(template.value, 'espresso');

    select.setAttribute('data-pending-value', 'missing');
    template.value = '';
    template.removeAttribute('data-dynamic-initialized');
    select.value = '';
    context.actionEditorOperationFixedChanged('brew-test', operation.id, 'template');
    await context.actionEditorLoadBrewTemplates('brew-test', operation);
    assert.strictEqual(select.value, 'missing');
    assert.strictEqual(template.value, 'missing');
    assert(select.options.some(option => option.value === 'missing' && option.textContent.includes('unavailable')));

    context.fetch = () => Promise.resolve({ ok: false, status: 503 });
    await context.actionEditorLoadBrewTemplates('brew-test', operation);
    assert(document.getElementById(base + '-load-error').textContent.includes('Could not load'));
}
soundPickerTest.then(testBrewTemplateLoading).catch(error => { console.error(error); process.exitCode = 1; });
