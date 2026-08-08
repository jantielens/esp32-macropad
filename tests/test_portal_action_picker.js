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
    {
        type: 'volume', group: 'Audio', label: 'Volume',
        commands: [{ id: 'set', label: 'Set volume' }, { id: 'adjust', label: 'Adjust volume' }]
    },
    {
        type: 'timer', group: 'Timer', label: 'Timer',
        commands: ['toggle', 'start', 'stop', 'pause', 'resume', 'reset', 'set', 'adjust']
            .map(function(id) { return { id: id, label: id }; })
    },
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
vm.runInContext(
    fs.readFileSync('src/app/device_classes/shutter_tester/web/portal_action_editor_shutter.js', 'utf8'),
    context
);

// --- Group assembly from the fixture catalog ---
const optionsHtml = context.actionEditorTypeOptionsHTML();
assert(optionsHtml.includes('<optgroup label="Navigation">'));
assert(optionsHtml.includes('<option value="screen">Navigate to screen</option>'));
assert(optionsHtml.includes('<option value="back">Navigate back</option>'));
assert(optionsHtml.includes('<optgroup label="Connectivity">'));
assert(optionsHtml.includes('<option value="mqtt">Publish MQTT message</option>'));
assert(optionsHtml.includes('<optgroup label="Shutter Tester">'));
assert(optionsHtml.includes('<option value="shutter">Shutter tester</option>'));

// --- Command population for a multi-command type ---
const volumeCommands = context.actionEditorCommandOptionsHTML('volume');
assert(volumeCommands.includes('<option value="set">Set volume</option>'));
assert(volumeCommands.includes('<option value="adjust">Adjust volume</option>'));

// A type absent from this build's catalog yields no command options, not a crash.
assert.strictEqual(context.actionEditorCommandOptionsHTML('brightness'), '');

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
context.actionEditorHTML(prefix, '', {});
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

console.log('portal_action_picker: PASS');
