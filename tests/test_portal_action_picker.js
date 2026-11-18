// tests/test_portal_action_picker.js — action-type picker host test
//
// Verifies the picker renders from the firmware-authored catalog: grouped
// type options, command options for multi-command types, and Shutter
// Tester's command-family -> command population. Also guards against the
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
assert.deepStrictEqual(setVariableCatalog && {
    group: setVariableCatalog.group,
    label: setVariableCatalog.label,
    nameBindable: setVariableCatalog.editor_fields[0].bindable === true,
    valueBindable: setVariableCatalog.editor_fields[1].bindable === true
}, { group: 'Variables', label: 'Set variable', nameBindable: false, valueBindable: true });

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

// A fixture standing in for GET /api/info?catalog=1's "catalog" array
// (see src/app/action_catalog.cpp). Presentation-only, mirrors real shapes.
const FIXTURE_CATALOG = [
    { type: 'screen', group: 'Navigation', label: 'Navigate to screen' },
    { type: 'back', group: 'Navigation', label: 'Navigate back' },
    { type: 'mqtt', group: 'Connectivity', label: 'Publish MQTT message' },
    { type: 'ha_service', group: 'Connectivity', label: 'Call Home Assistant service' },
    ...productionCatalogs.portal.filter(entry => entry.type === 'bthome'),
    { type: 'gamepad', group: 'Gamepad', label: 'Gamepad', button_count: 16 },
    {
        type: 'mouse_button', group: 'Mouse', label: 'Mouse button',
        commands: [{ id: 'left', label: 'Left' }, { id: 'right', label: 'Right' }, { id: 'middle', label: 'Middle' }],
        editor_fields: [{ name: 'button', label: 'Button', type: 'select', default: 'left', command_options: true }]
    },
    {
        type: 'volume', group: 'Audio', label: 'Volume',
        template_fields: { volume_value: { max: 15 } },
        commands: [{ id: 'set', label: 'Set volume' }, { id: 'adjust', label: 'Adjust volume' }],
        editor_fields: [
            { name: 'volume_mode', label: 'Command', type: 'select', command_options: true },
            { name: 'volume_value', label: 'Value (%)', type: 'text', bindable: true }
        ]
    },
    {
        type: 'brightness', group: 'Display', label: 'Brightness',
        commands: [{ id: 'set', label: 'Set brightness' }, { id: 'adjust', label: 'Adjust brightness' }],
        editor_fields: [
            { name: 'brightness_mode', label: 'Command', type: 'select', command_options: true },
            { name: 'brightness_value', label: 'Value (%)', type: 'text', bindable: true }
        ]
    },
    ...productionCatalogs.portal.filter(entry => entry.type === 'system'),
    ...voiceCatalogs.portal.filter(entry => entry.group === 'Voice notes'),
    {
        type: 'music', group: 'Audio', label: 'Music',
        commands: [{ id: 'play_pause', label: 'Play/Pause' }, { id: 'next', label: 'Next track' }],
        editor_fields: [{ name: 'music_command', label: 'Command', type: 'select', command_options: true }]
    },
    {
        type: 'timer', group: 'Timer', label: 'Timer',
        commands: ['toggle', 'start', 'stop', 'pause', 'resume', 'reset', 'set', 'adjust']
            .map(function(id) { return { id: id, label: id }; })
    },
    { type: 'delay', group: 'Timer', label: 'Delay', max_pending_actions: 3 },
    ...productionCatalogs.portal.filter(entry => ['alarm_tone', 'alarm_mp3'].includes(entry.type)),
    {
        type: 'shutter', group: 'Shutter Tester', label: 'Shutter tester',
        command_families: [
            {
                id: 'target_speed', label: 'Target speed', commands: [
                    { id: 'toggle_lock', label: 'Toggle lock' },
                    { id: 'set', label: 'Set target speed' },
                    { id: 'adjust', label: 'Adjust target speed' }
                ]
            },
            {
                id: 'session', label: 'Session', commands: [
                    { id: 'sess_toggle', label: 'Toggle start/stop' },
                    { id: 'sess_start', label: 'Start' }
                ]
            }
        ]
    }
];

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
vm.runInContext(fs.readFileSync('src/app/web/portal_action_editor_cycle_pad.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_action_editor_mqtt.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_action_editor_ha.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_action_editor_key.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_action_editor_sound_alert.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_action_editor_timer.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_action_editor_notify.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_action_editor_visual_alert.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_action_editor_delay.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_action_editor_gamepad.js', 'utf8'), context);
vm.runInContext(
    fs.readFileSync('src/app/device_classes/shutter_tester/web/portal_action_editor_shutter.js', 'utf8'),
    context
);

const keyEditor = context._actionEditorExtensions.find(extension => extension.type === 'key');
for (const type of ['key', 'ble_pair', 'mqtt']) {
    keyEditor.typeChanged('keyboard-test', type);
    assert.strictEqual(document.getElementById('keyboard-test-ble-hint').style.display,
        type === 'mqtt' ? 'none' : '');
}
FIXTURE_CATALOG.push({ type: 'key', group: 'Keyboard', label: 'Send keys' });
for (const type of ['key', 'ble_pair']) {
    keyEditor.typeChanged('keyboard-test', type);
    assert.strictEqual(document.getElementById('keyboard-test-ble-hint').style.display, 'none');
}
assert.strictEqual(document.getElementById('keyboard-test-key-group').style.display, 'none');
keyEditor.typeChanged('keyboard-test', 'key');
assert.strictEqual(document.getElementById('keyboard-test-key-group').style.display, '');
FIXTURE_CATALOG.pop();

// --- Group assembly from the fixture catalog ---
const optionsHtml = context.actionEditorTypeOptionsHTML();
assert(optionsHtml.includes('<optgroup label="Navigation">'));
assert(optionsHtml.includes('<option value="screen">Navigate to screen</option>'));
assert(optionsHtml.includes('<option value="back">Navigate back</option>'));
assert(optionsHtml.includes('<option value="delay">Delay</option>'));
assert(optionsHtml.includes('<optgroup label="Connectivity">'));
assert(optionsHtml.includes('<option value="mqtt">Publish MQTT message</option>'));
assert(optionsHtml.includes('<optgroup label="Mouse">'));
assert(optionsHtml.includes('<option value="mouse_button">Mouse button</option>'));
assert(optionsHtml.includes('<option value="gamepad">Gamepad</option>'));
for (const action of [
    { type: 'gamepad', control: 'button', button: 16, operation: 'tap' },
    { type: 'gamepad', control: 'hat', direction: 'left', operation: 'down' },
    { type: 'gamepad', control: 'trigger', trigger: 'right', operation: 'up' }
]) {
    context.actionEditorLoad('gamepad-test', action);
    assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild('gamepad-test'))), action);
}
context.actionEditorLoadGamepad('held-test', { control: 'hat', direction: 'up' });
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuildGamepad('held-test', true))),
    { type: 'gamepad', control: 'hat', direction: 'up', operation: 'down' });
context.actionEditorLoadGamepad('held-test', { control: 'button', button: 17 });
assert.throws(() => context.actionEditorBuildGamepad('held-test', true), /1-16/);
const mouseCommands = context.actionEditorCommandOptionsHTML('mouse_button');
for (const button of ['Left', 'Right', 'Middle']) {
    assert(mouseCommands.includes('<option value="' + button.toLowerCase() + '">' + button + '</option>'));
}
assert(optionsHtml.includes('<optgroup label="Shutter Tester">'));
assert(optionsHtml.includes('<option value="shutter">Shutter tester</option>'));

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
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild('legacy-pad'))), legacyVisual);
context.actionEditorLoad('legacy-host', legacyVisual);
assert.strictEqual(document.getElementById('legacy-host-type').value, 'visual_alert');
const mixedType = document.getElementById('mixed-host-type');
mixedType.setAttribute('data-operations', 'true');
mixedType.setAttribute('data-mixed', 'true');
const mixedLegacy = { type: 'visual_alert', op: 'stop', pattern: 'breathe' };
context.actionEditorLoad('mixed-host', mixedLegacy);
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild('mixed-host'))), mixedLegacy);
for (const action of [
    { op: 'screen.open', args: { target: 'pad_2' } },
    { op: 'wifi.station.set_enabled', args: { enabled: false } }
]) {
    context.actionEditorLoad('mixed-host', action);
    assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild('mixed-host'))), action);
}
context.actionEditorLoad('mixed-host', { op: 'unknown.operation', args: { value: 'preserve' } });
assert.throws(() => context.actionEditorBuild('mixed-host'), /unavailable/);
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
assert(operationMarkup.includes('for="operation-test-search">Search operations'));
assert(operationMarkup.includes('> Dynamic</label>'));
assert(operationMarkup.includes('Timer 1'));
assert(operationMarkup.includes('min="1" max="55000" value="1"'));
assert(operationMarkup.includes('Expected: true or false'));
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
context.actionEditorLoadOperationSounds('operation-test', pickerOperation)
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
const searchSelect = document.getElementById('search-test-type');
const searchInput = document.getElementById('search-test-search');
const navigationGroup = { tagName: 'OPTGROUP', label: 'Navigation', children: [] };
const wifiGroup = { tagName: 'OPTGROUP', label: 'Connectivity', children: [] };
const selectedOption = { value: '@screen.open', textContent: 'Navigate to screen', selected: true, parentElement: navigationGroup };
const wifiOption = { value: '@wifi.station.set_enabled', textContent: 'Set Wi-Fi station mode', selected: false, parentElement: wifiGroup };
navigationGroup.children.push(selectedOption);
wifiGroup.children.push(wifiOption);
searchSelect.options = [selectedOption, wifiOption];
searchSelect.children = [navigationGroup, wifiGroup];
searchSelect.value = selectedOption.value;
searchInput.value = 'station';
context.actionEditorFilterOperations('search-test');
assert.strictEqual(searchSelect.value, '@screen.open');
assert.strictEqual(selectedOption.hidden, false);
assert.strictEqual(wifiOption.hidden, false);
assert.strictEqual(document.getElementById('search-test-search-status').textContent, '1 matching operation');
searchInput.value = 'connectivity';
context.actionEditorFilterOperations('search-test');
assert.strictEqual(wifiGroup.hidden, false);
searchInput.value = 'no match';
context.actionEditorFilterOperations('search-test');
assert.strictEqual(wifiGroup.hidden, true);
assert.strictEqual(navigationGroup.hidden, false);
searchInput.value = '';
context.actionEditorFilterOperations('search-test');
assert.strictEqual(wifiGroup.hidden, false);
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

// --- Command population for a multi-command type ---
const volumeCommands = context.actionEditorCommandOptionsHTML('volume');
assert(volumeCommands.includes('<option value="set">Set volume</option>'));
assert(volumeCommands.includes('<option value="adjust">Adjust volume</option>'));

const brightnessCommands = context.actionEditorCommandOptionsHTML('brightness');
assert(brightnessCommands.includes('<option value="set">Set brightness</option>'));
assert(brightnessCommands.includes('<option value="adjust">Adjust brightness</option>'));

// --- Shutter Tester: command family -> command population ---
const familyOptions = context.actionEditorFamilyOptionsHTML('shutter');
assert(familyOptions.includes('<option value="target_speed">Target speed</option>'));
assert(familyOptions.includes('<option value="session">Session</option>'));

const targetSpeedCommands = context.actionEditorFamilyCommandOptionsHTML('shutter', 'target_speed');
assert(targetSpeedCommands.includes('<option value="toggle_lock">Toggle lock</option>'));
assert(!targetSpeedCommands.includes('sess_start'));

const sessionCommands = context.actionEditorFamilyCommandOptionsHTML('shutter', 'session');
assert(sessionCommands.includes('<option value="sess_start">Start</option>'));
assert(!sessionCommands.includes('toggle_lock'));

assert.strictEqual(context.actionEditorFamilyForCommand('shutter', 'sess_toggle'), 'session');
assert.strictEqual(context.actionEditorFamilyForCommand('shutter', 'toggle_lock'), 'target_speed');

// Selecting a family repopulates the command select for that family alone.
const prefix = 'picker';
const editorHtml = context.actionEditorHTML(prefix, '', {});
assert(editorHtml.includes('Up to 3 pausable actions can be pending device-wide at a time'));
assert(editorHtml.includes('picker-generic-volume-volume_mode'));
assert(editorHtml.includes('picker-generic-brightness-brightness_mode'));
assert(editorHtml.includes('picker-generic-brightness-brightness_value'));
assert(editorHtml.includes('picker-generic-system-system_command'));
assert(editorHtml.includes('picker-generic-music-music_command'));
assert(editorHtml.includes('picker-generic-mouse_button-button'));
assert(optionsHtml.includes('<optgroup label="Voice notes">'));
assert(!optionsHtml.includes('<option value="voice_note">'));
assert(!optionsHtml.includes('<option value="voice_note_del">'));
const recordCommands = context.actionEditorCommandOptionsHTML('vnote_record');
assert(recordCommands.includes('<option value="toggle">Toggle recording (start/save)</option>'));
assert(recordCommands.includes('<option value="save">Stop and save recording</option>'));
assert(!recordCommands.includes('process'));
context.actionEditorLoad(prefix, { type: 'vnote_record' });
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild(prefix))),
    { type: 'vnote_record', command: 'toggle' });
for (const command of ['toggle', 'start', 'pause', 'resume', 'save', 'discard']) {
    const action = { type: 'vnote_record', command };
    context.actionEditorLoad(prefix, action);
    assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild(prefix))), action);
}
context.actionEditorLoad(prefix, { type: 'vnote_process' });
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild(prefix))),
    { type: 'vnote_process' });
for (const type of ['vnote_load', 'vnote_delete']) {
    const action = { type, id: '[item:id]' };
    context.actionEditorLoad(prefix, action);
    assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild(prefix))), action);
}
for (const button of ['left', 'right', 'middle', undefined]) {
    context.actionEditorLoad(prefix, { type: 'mouse_button', button: button });
    const mouseAction = context.actionEditorBuild(prefix);
    assert.strictEqual(mouseAction.type, 'mouse_button');
    assert.strictEqual(mouseAction.button, button || 'left');
}
assert(!editorHtml.includes('picker-system-command'));
assert(!editorHtml.includes('picker-music-command'));
context.actionEditorLoad(prefix, { type: 'volume', volume_mode: 'adjust', volume_value: '{step}' });
const volumeAction = context.actionEditorBuild(prefix, '{step}');
assert.strictEqual(volumeAction.type, 'volume');
assert.strictEqual(volumeAction.volume_mode, 'adjust');
assert.strictEqual(volumeAction.volume_value, '{step}');
context.actionEditorLoad(prefix, { type: 'brightness', brightness_mode: 'adjust', brightness_value: '-10' });
const brightnessAction = context.actionEditorBuild(prefix);
assert.strictEqual(brightnessAction.type, 'brightness');
assert.strictEqual(brightnessAction.brightness_mode, 'adjust');
assert.strictEqual(brightnessAction.brightness_value, '-10');
context.actionEditorLoad(prefix, { type: 'system', system_command: 'wifi_connect' });
const systemAction = context.actionEditorBuild(prefix);
assert.strictEqual(systemAction.type, 'system');
assert.strictEqual(systemAction.system_command, 'wifi_connect');
assert(editorHtml.includes('Turn off Wi-Fi'));
context.actionEditorLoad(prefix, { type: 'system', system_command: 'wifi_off' });
assert.strictEqual(context.actionEditorBuild(prefix).system_command, 'wifi_off');
context.actionEditorLoad(prefix, { type: 'music', music_command: 'next' });
const musicAction = context.actionEditorBuild(prefix);
assert.strictEqual(musicAction.type, 'music');
assert.strictEqual(musicAction.music_command, 'next');
const familyEl = document.getElementById(prefix + '-shutter-family');
familyEl.value = 'session';
context.actionEditorShutterFamilyChanged(prefix);
const commandEl = document.getElementById(prefix + '-shutter-command');
assert(commandEl.innerHTML.includes('sess_start'));
assert(!commandEl.innerHTML.includes('toggle_lock'));

// Loading a persisted shutter action derives the family from its command.
context.actionEditorLoad(prefix, { type: 'shutter', shutter_command: 'sess_start' });
assert.strictEqual(familyEl.value, 'session');
assert(document.getElementById(prefix + '-shutter-command').innerHTML.includes('sess_start'));

const bthomeCatalog = productionCatalogs.portal.find(entry => entry.type === 'bthome');
assert(bthomeCatalog, 'BLE-capable firmware exposes the BTHome action');
const bthomeFields = context.actionEditorGenericFields('bthome');
assert.strictEqual(bthomeFields[0].options.length, 10);
assert.strictEqual(bthomeFields[1].type, 'select');
assert.deepStrictEqual(bthomeFields[1].options.map(option => option.id),
    ['press', 'double_press', 'long_press']);
context.actionEditorLoad(prefix, {type: 'bthome'});
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild(prefix))),
    {type: 'bthome', button: 1, event: 'press'});
context.actionEditorLoad(prefix, {type: 'bthome', button: 10, event: 'long_press'});
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild(prefix))),
    {type: 'bthome', button: 10, event: 'long_press'});

// Delay uses a bounded numeric duration and persists the flat JSON contract.
context.actionEditorLoad(prefix, { type: 'delay', duration_ms: 2500 });
assert.strictEqual(document.getElementById(prefix + '-delay-duration').value, 2500);
const delayAction = context.actionEditorBuild(prefix);
assert.strictEqual(delayAction.type, 'delay');
assert.strictEqual(delayAction.duration_ms, 2500);

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
const resetPrefix = 'reset-test';
const resetFields = ['-ha-entity', '-ha-service', '-ha-data', '-generic-volume-volume_value', '-timer-set-sec', '-timer-adjust-sec'].map(suffix => {
    const input = document.getElementById(resetPrefix + suffix);
    input.tagName = 'INPUT';
    input.defaultValue = '';
    input.defaultChecked = false;
    return input;
});
const resetCommand = document.getElementById(resetPrefix + '-generic-system-system_command');
resetCommand.options = [{ value: 'reboot', defaultSelected: false }, { value: 'wifi_connect', defaultSelected: false }];
Object.defineProperty(resetCommand, 'selectedIndex', { set(index) { this.value = this.options[index].value; } });
const resetCheckbox = new Element('INPUT');
resetCheckbox.defaultValue = 'on';
resetCheckbox.defaultChecked = true;
resetCheckbox.checked = false;
const resetSelected = new Element('SELECT');
resetSelected.options = [{ value: 'first', defaultSelected: false }, { value: 'configured', defaultSelected: true }];
Object.defineProperty(resetSelected, 'selectedIndex', { set(index) { this.value = this.options[index].value; } });
document.getElementById(resetPrefix + '-fields').querySelectorAll = () => resetFields.concat(resetCommand, resetCheckbox, resetSelected);
const previousHa = { type: 'ha_service', entity_id: 'light.previous', service: 'turn_on', data_json: '{"brightness_pct":80}' };
context.actionEditorLoad(resetPrefix, previousHa);
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild(resetPrefix))), previousHa);
document.getElementById(resetPrefix + '-type').value = 'mqtt';
context.actionEditorTypeChanged(resetPrefix);
document.getElementById(resetPrefix + '-type').value = 'ha_service';
context.actionEditorTypeChanged(resetPrefix);
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild(resetPrefix))), previousHa, 'type switching within a button preserves its draft');
context.actionEditorLoad(resetPrefix, {});
document.getElementById(resetPrefix + '-type').value = 'ha_service';
context.actionEditorTypeChanged(resetPrefix);
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild(resetPrefix))), {type:'ha_service'}, 'new buttons must not inherit HA settings');
context.actionEditorLoad(resetPrefix, {type:'volume', volume_mode:'set', volume_value:'73'});
document.getElementById(resetPrefix + '-timer-set-sec').value = '123';
document.getElementById(resetPrefix + '-timer-adjust-sec').value = '-45';
resetCommand.value = 'wifi_connect';
context.actionEditorLoad(resetPrefix, {type:'mqtt', topic:'new/button', payload:'fresh'});
document.getElementById(resetPrefix + '-type').value = 'volume';
context.actionEditorTypeChanged(resetPrefix);
assert.strictEqual(context.actionEditorBuild(resetPrefix).volume_value, undefined, 'different action types also clear inactive generic fields');
assert.strictEqual(document.getElementById(resetPrefix + '-timer-set-sec').value, '');
assert.strictEqual(document.getElementById(resetPrefix + '-timer-adjust-sec').value, '');
assert.strictEqual(resetCommand.value, 'reboot');
assert.strictEqual(resetSelected.value, 'configured');
assert.strictEqual(resetCheckbox.checked, true);
const specializedTypes = ['screen', 'cycle_pad', 'mqtt', 'key', 'sound_alert', 'timer', 'notify', 'visual_alert', 'delay', 'gamepad'];
const manifest = fs.readFileSync('src/app/web/portal.js.bundle', 'utf8');
for (const type of specializedTypes) {
    assert(!new RegExp("(?:type|action\\.type)\\s*===?\\s*['\"]" + type + "['\"]").test(editorSource), 'shared editor must not branch on ' + type);
    const filename = 'portal_action_editor_' + type + '.js';
    assert(manifest.indexOf(filename) > manifest.indexOf('portal_action_editor.js'), 'extension dependency order: ' + type);
    const extension = context._actionEditorExtensions.find(candidate => candidate.type === type);
    assert(extension && extension.groups && extension.load && extension.build && extension.typeChanged, 'complete ownership: ' + type);
}
for (const suffix of ['-timer-action', '-sound-alert-file', '-gamepad-control', '-cycle-pad-exclusions', '-notify-text', '-va-color', '-delay-duration', '-target', '-topic', '-sequence']) {
    assert(!editorSource.includes(suffix), 'shared editor must not own action controls: ' + suffix);
}
assert.deepStrictEqual(Array.from(context.actionEditorBindingSuffixes('mqtt')), ['-topic', '-payload']);
assert.deepStrictEqual(Array.from(context.actionEditorBindingSuffixes('volume')), ['-generic-volume-volume_value']);
assert(!context.actionEditorBindingSuffixes('delay').length);
let attachedBindings = [];
context.bindingAttachValidation = input => attachedBindings.push(input);
context.actionEditorLoad('binding-test', {type:'mqtt', topic:'[mqtt:topic]', payload:'{step}'});
context.actionEditorTypeChanged('binding-test');
assert.strictEqual(attachedBindings.length, 2, 'binding setup must be idempotent');
assert(attachedBindings.includes(document.getElementById('binding-test-topic')));
context.actionEditorLoad('binding-test', {type:'key', sequence:'ctrl+c'});
assert.strictEqual(attachedBindings.length, 3);
const unknownAction = {type:'future_action', custom:{nested:[1, 2]}, enabled:false};
context.actionEditorLoad('unknown-test', unknownAction);
assert.deepStrictEqual(context.actionEditorBuild('unknown-test'), unknownAction);
document.getElementById('unknown-test-type').value = 'back';
context.actionEditorTypeChanged('unknown-test');
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild('unknown-test'))), {type:'back'});
for (const invalid of ['', '0', '-1', '1.5', '55001', '[mqtt:duration]']) {
    context.actionEditorLoad(prefix, {type:'delay', duration_ms:1000});
    document.getElementById(prefix + '-delay-duration').value = invalid;
    assert.throws(() => context.actionEditorBuild(prefix), /Delay duration/);
}
for (const duration of [1, 55000]) {
    document.getElementById(prefix + '-delay-duration').value = String(duration);
    assert.strictEqual(context.actionEditorBuild(prefix).duration_ms, duration);
}

FIXTURE_CATALOG[0].alarm_hook_allowed = true;
context.actionEditorListRender('alarm-hooks', ['alarm-hook-1'], null, {actionOptions:{alarmHook:true}});
const alarmHtml = document.getElementById('alarm-hooks').innerHTML;
assert(alarmHtml.includes('<option value="screen">'));
assert(!alarmHtml.includes('<option value="delay">'));
assert(!alarmHtml.includes('<option value="key">'));
assert(!alarmHtml.includes('<option value="mqtt">'));
assert(alarmHtml.includes('data-alarm-hook="true"'));
const alarmType = document.getElementById('alarm-hook-1-type');
alarmType.setAttribute('data-alarm-hook', 'true');
for (const type of ['delay', 'mqtt', 'unknown_action']) {
    context.actionEditorEnsureUnsupportedOption(alarmType, type);
    assert.strictEqual(alarmType.options[alarmType.options.length - 1].disabled, true);
    context.actionEditorLoad('alarm-hook-1', {type});
    assert.throws(() => context.actionEditorBuild('alarm-hook-1'), /not allowed for alarm hooks/);
}
context.actionEditorLoad('alarm-hook-1', {type:'alarm_mp3', sound_alert_file:'wake-up'});
assert.strictEqual(context.actionEditorBuild('alarm-hook-1').type, 'alarm_mp3');
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
assert(mixedAlarm.includes('value="legacy_sync"'));
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
assert(context.actionEditorTypeOptionsHTML().includes('<option value="delay">'));
context.actionEditorLoad(prefix, {type:'alarm_tone', sound_alert_pattern:'1000:200 800'});
assert.strictEqual(context.actionEditorBuild(prefix).type, 'alarm_tone');
assert.strictEqual(context.actionEditorBuild(prefix).sound_alert_pattern, '1000:200 800');
context.actionEditorLoad(prefix, {type:'alarm_mp3', sound_alert_file:'missing-clip', sound_alert_volume:65});
assert.strictEqual(context.actionEditorBuild(prefix).type, 'alarm_mp3');
assert.strictEqual(context.actionEditorBuild(prefix).sound_alert_file, 'missing-clip');
assert.strictEqual(context.actionEditorBuild(prefix).sound_alert_volume, 65);
assert.strictEqual(context.actionEditorBuild(prefix).sound_alert_kind, undefined);
assert(context.actionEditorTypeOptionsHTML().includes('<option value="alarm_mp3">Loop MP3</option>'));
assert(!context.actionEditorHTML(prefix).includes('<option value="tone_loop">'));
context.actionEditorLoad(prefix, {type:'sound_alert', sound_alert_kind:'stop'});
const stopAudio = context.actionEditorBuild(prefix);
assert.strictEqual(stopAudio.sound_alert_kind, 'stop');
assert.strictEqual(stopAudio.sound_alert_file, undefined);
assert.strictEqual(stopAudio.sound_alert_pattern, undefined);
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
assert.deepStrictEqual(Object.keys(context.alarmConfigBuild()).sort(), ['auto_dismiss_minutes', 'lateness_minutes', 'on_ring', 'on_stop', 'snooze_minutes']);
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
    {type:'set_var', var_name:'counter', var_value:'[expr:[var:counter|0] + 1]'},
    {type:'mqtt', topic:'home/topic', payload:'[mqtt:value]'},
    {type:'key', sequence:'ctrl+c'},
    {type:'cycle_pad', direction:'previous', wrap:false, excluded_pads:'1,3'},
    {type:'sound_alert', sound_alert_kind:'tone', sound_alert_pattern:'1000:200 100', sound_alert_volume:50},
    {type:'sound_alert', sound_alert_kind:'mp3', sound_alert_file:'missing-sound', sound_alert_volume:60},
    {type:'sound_alert', sound_alert_kind:'stop'},
    {type:'notify', message:'[mqtt:message]\nSecond line', duration_ms:0},
    {type:'confirm', message:'Continue?', approve_text:'Yes', cancel_text:'No', timeout_ms:300000},
    {type:'confirm', message:'No deadline', approve_text:'Continue', cancel_text:'Cancel', timeout_ms:0},
    {type:'visual_alert', op:'stop', pattern:'blink', period_ms:900, intensity:75, duration_ms:5000},
    {type:'timer', timer_id:2, timer_command:'adjust', timer_value:'{step}'},
    {type:'delay', duration_ms:55000}
];
for (const action of roundTrips) {
    context.actionEditorLoad('round-trip', action);
    assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild('round-trip', '{step}'))), action, 'round-trip: ' + action.type);
}
context.actionEditorLoad('round-trip', {type:'sound_alert'});
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild('round-trip'))), {type:'sound_alert', sound_alert_kind:'tone', sound_alert_pattern:''});
context.actionEditorLoad('round-trip', {type:'notify', message:'Notification'});
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild('round-trip'))), {type:'notify', message:'Notification', duration_ms:0});
for (const type of ['notify', 'confirm']) {
    for (const initial of [{}, {type:'mqtt', message:'Unrelated content', duration_ms:5000}, {type}]) {
        context.actionEditorLoad('dialog-defaults', initial);
        document.getElementById('dialog-defaults-type').value = type;
        context.actionEditorTypeChanged('dialog-defaults');
        const action = context.actionEditorBuild('dialog-defaults');
        assert.strictEqual(action.message, 'Your message to show');
        assert.strictEqual(action[type === 'confirm' ? 'timeout_ms' : 'duration_ms'], 0);
        assert.strictEqual(document.getElementById('dialog-defaults-' + type + '-persistent').checked, true);
        assert.strictEqual(document.getElementById('dialog-defaults-' + type + '-seconds').disabled, true);
        if (type === 'confirm') {
            assert.strictEqual(action.approve_text, 'Continue');
            assert.strictEqual(action.cancel_text, 'Cancel');
        }
    }
    const timingField = type === 'confirm' ? 'timeout_ms' : 'duration_ms';
    const saved = {type, message:'Keep my message', [timingField]:61000};
    context.actionEditorLoad('dialog-defaults', saved);
    assert.strictEqual(context.actionEditorBuild('dialog-defaults')[timingField], 61000);
    assert.strictEqual(context.actionEditorBuild('dialog-defaults').message, saved.message);
    assert.strictEqual(document.getElementById('dialog-defaults-' + type + '-persistent').checked, false);
    assert.strictEqual(document.getElementById('dialog-defaults-' + type + '-seconds').disabled, false);
    context.actionEditorLoad('dialog-defaults', {...saved, message:''});
    assert.throws(() => context.actionEditorBuild('dialog-defaults'), /Dialog message is required/);
}
context.actionEditorLoad('round-trip', {type:'delay'});
assert.strictEqual(context.actionEditorBuild('round-trip').duration_ms, 1000);
context.actionEditorLoad('round-trip', {type:'visual_alert', op:'stop'});
assert.strictEqual(document.getElementById('round-trip-va-config-group').style.display, 'none');
context.actionEditorLoad('round-trip', {type:'sound_alert', sound_alert_kind:'mp3'});
assert.strictEqual(document.getElementById('round-trip-sound-alert-tone-group').style.display, 'none');
assert.strictEqual(document.getElementById('round-trip-sound-alert-mp3-group').style.display, '');
const productionOptions = context.actionEditorTypeOptionsHTML({alarmHook:true});
for (const entry of productionCatalogs.portal) {
    assert.strictEqual(productionOptions.includes('<option value="' + entry.type + '">'),
        entry.alarm_hook_allowed === true, 'actual hook eligibility: ' + entry.type);
}
for (const type of ['alarm', 'alarm_tone', 'alarm_mp3']) {
    assert(productionOptions.includes('<option value="' + type + '">'));
}
assert(productionOptions.includes('<option value="alarm_tone">Loop Tone</option>'));
assert(productionOptions.includes('<option value="alarm_mp3">Loop MP3</option>'));
assert(context.actionEditorCommandOptionsHTML('alarm').includes('<option value="cancel">Cancel</option>'));
assert(context.actionEditorCommandOptionsHTML('alarm').includes('<option value="weekday_enable">Enable weekday</option>'));
vm.runInContext(fs.readFileSync('src/app/web/portal_action_editor_alarm.js', 'utf8'), context);
const productionEditor = context.actionEditorHTML(prefix);
assert(productionEditor.includes('Repeats until stopped.'));
assert(productionEditor.includes('0 = use device volume; 1-100 overrides it.'));
assert(productionEditor.includes('<span id="' + prefix + '-generic-alarm-alarm_id-label">Alarm</span>'));
assert(productionEditor.includes('<option value="0">Active alarm</option>'));
document.getElementById(prefix + '-generic-alarm-alarm_command').value = 'set_time';
context.actionEditorAlarmChanged(prefix);
assert.strictEqual(document.getElementById(prefix + '-generic-alarm-alarm_value-label').textContent, 'Time (minutes since midnight)');
assert(document.getElementById(prefix + '-generic-alarm-alarm_value-help').textContent.includes('420 = 07:00'));
document.getElementById(prefix + '-generic-alarm-alarm_command').value = 'adjust_minutes';
context.actionEditorAlarmChanged(prefix);
assert.strictEqual(document.getElementById(prefix + '-generic-alarm-alarm_value-label').textContent, 'Adjustment (minutes)');
assert(document.getElementById(prefix + '-generic-alarm-alarm_value-help').textContent.includes('{step}'));
console.log('production portal/MCP action parity: PASS');
