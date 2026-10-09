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
assert.deepStrictEqual(productionCatalogs.portal, productionCatalogs.mcp.map(entry => {
    const metadata = {...entry};
    delete metadata.fields;
    return metadata;
}));

class Element {
    constructor(tag) {
        this.tagName = tag || 'SELECT';
        this.value = '';
        this.innerHTML = '';
        this.style = { display: 'none' };
        this.dataset = {};
        this.attributes = {};
        this.options = []; // appendChild-based options only (unsupported-type test)
        var classes = new Set();
        this.classList = {
            add: function(c) { classes.add(c); },
            remove: function(c) { classes.delete(c); },
            contains: function(c) { return classes.has(c); }
        };
    }
    appendChild(opt) { this.options.push(opt); return opt; }
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
    { type: 'gamepad', group: 'Gamepad', label: 'Gamepad', button_count: 16 },
    {
        type: 'mouse_button', group: 'Mouse', label: 'Mouse button',
        commands: [{ id: 'left', label: 'Left' }, { id: 'right', label: 'Right' }, { id: 'middle', label: 'Middle' }],
        editor_fields: [{ name: 'button', label: 'Button', type: 'select', default: 'left', command_options: true }]
    },
    {
        type: 'volume', group: 'Audio', label: 'Volume',
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
    {
        type: 'system', group: 'Device', label: 'Device command',
        commands: [{ id: 'reboot', label: 'Restart device' }, { id: 'wifi_reconnect', label: 'Reconnect Wi-Fi' }],
        editor_fields: [{ name: 'system_command', label: 'Command', type: 'select', command_options: true }]
    },
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
    document,
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
for (const button of ['left', 'right', 'middle', undefined]) {
    context.actionEditorLoad(prefix, { type: 'mouse_button', button: button });
    const mouseAction = context.actionEditorBuild(prefix);
    assert.strictEqual(mouseAction.type, 'mouse_button');
    assert.strictEqual(mouseAction.button, button || 'left');
}
assert(!editorHtml.includes('picker-system-command'));
assert(!editorHtml.includes('picker-music-command'));
context.actionEditorLoad(prefix, { type: 'volume', volume_mode: 'adjust', volume_value: '{step}' });
const volumeAction = context.actionEditorBuild(prefix);
assert.strictEqual(volumeAction.type, 'volume');
assert.strictEqual(volumeAction.volume_mode, 'adjust');
assert.strictEqual(volumeAction.volume_value, '{step}');
context.actionEditorLoad(prefix, { type: 'brightness', brightness_mode: 'adjust', brightness_value: '-10' });
const brightnessAction = context.actionEditorBuild(prefix);
assert.strictEqual(brightnessAction.type, 'brightness');
assert.strictEqual(brightnessAction.brightness_mode, 'adjust');
assert.strictEqual(brightnessAction.brightness_value, '-10');
context.actionEditorLoad(prefix, { type: 'system', system_command: 'wifi_reconnect' });
const systemAction = context.actionEditorBuild(prefix);
assert.strictEqual(systemAction.type, 'system');
assert.strictEqual(systemAction.system_command, 'wifi_reconnect');
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
assert(!editorSource.includes('querySelectorAll'));
assert(editorSource.includes('action-type-select'));
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
    {type:'mqtt', topic:'home/topic', payload:'[mqtt:value]'},
    {type:'key', sequence:'ctrl+c'},
    {type:'cycle_pad', direction:'previous', wrap:false, excluded_pads:'1,3'},
    {type:'sound_alert', sound_alert_kind:'tone', sound_alert_pattern:'1000:200 100', sound_alert_volume:50},
    {type:'sound_alert', sound_alert_kind:'mp3', sound_alert_file:'missing-sound', sound_alert_volume:60},
    {type:'sound_alert', sound_alert_kind:'stop'},
    {type:'notify', notify_text:'[mqtt:message]', notify_duration_ms:'0', notify_location:'center', notify_opacity:75, notify_font_size:20},
    {type:'visual_alert', op:'stop', pattern:'blink', period_ms:900, intensity:75, duration_ms:5000},
    {type:'timer', timer_id:2, timer_command:'adjust', timer_value:'{step}'},
    {type:'delay', duration_ms:55000}
];
for (const action of roundTrips) {
    context.actionEditorLoad('round-trip', action);
    assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild('round-trip'))), action, 'round-trip: ' + action.type);
}
context.actionEditorLoad('round-trip', {type:'sound_alert'});
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild('round-trip'))), {type:'sound_alert', sound_alert_kind:'tone', sound_alert_pattern:''});
context.actionEditorLoad('round-trip', {type:'notify'});
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild('round-trip'))), {type:'notify', notify_text:'', notify_duration_ms:'3000', notify_location:'bottom'});
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
