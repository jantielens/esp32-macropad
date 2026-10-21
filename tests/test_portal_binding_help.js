const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

const fixture = JSON.parse(fs.readFileSync('tools/mock-data/binding-docs.json', 'utf8'));
const listeners = [];
const context = {
    console,
    TextEncoder,
    AbortController,
    DOMException,
    setTimeout,
    clearTimeout,
    window: { deviceConfig: {} },
    document: {
        readyState: 'loading',
        addEventListener(type, handler, capture) { listeners.push({ type, handler, capture }); }
    },
    fetch() {
        return Promise.resolve({ ok: true, json: () => Promise.resolve(fixture) });
    }
};
vm.createContext(context);
vm.runInContext(fs.readFileSync('src/app/web/portal_binding_validator.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_binding_help.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_binding_builder.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_binding_builder_ha.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_button_templates.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_button_builder.js', 'utf8'), context);
const haButtonBuilderProvider = vm.runInContext('haButtonBuilderProvider', context);

const scheme = name => fixture.schemes.find(s => s.name === name);
const haConfigured = { caps: { ha_live: true }, ha_url: 'http://ha.local:8123', ha_token_set: true };
assert.strictEqual(context.bindingBuilderShortcuts(haConfigured)[0].label, 'HA');
assert.strictEqual(context.bindingBuilderShortcuts({ ...haConfigured, connected: false })[0].id, 'ha');
for (const config of [{}, { ...haConfigured, caps: {} }, { ...haConfigured, ha_url: ' ' },
    { ...haConfigured, ha_token_set: false }]) {
    assert.strictEqual(context.bindingBuilderShortcuts(config).length, 0);
}

assert.ok(context.bindingHelpKeyMatches('#_state', '12_state'));
assert.ok(!context.bindingHelpKeyMatches('#_state', 'x_state'));
assert.ok(context.bindingHelpKeyMatches('*.selected', 'fixture.selected'));
assert.ok(!context.bindingHelpKeyMatches('*.selected', 'a.b.selected'));
assert.strictEqual(context.bindingHelpDisplayKey('seg_time:#'), 'seg_time:N');

assert.strictEqual(context.bindingHelpKeyToken(scheme('timer'), '#_state'), '[timer:1_state]');
assert.strictEqual(context.bindingHelpKeyToken(scheme('list'), '*.selected'), '[list:fixture.selected]');
assert.strictEqual(context.bindingHelpKeyToken(scheme('health'), 'cpu'), '[health:cpu]');
assert.strictEqual(context.bindingHelpSignature(scheme('mqtt')), '[mqtt:topic;path?;format?]');

const categories = context.bindingHelpCategories(fixture.schemes).map(entry => entry[0]);
assert.strictEqual(JSON.stringify(categories.slice(0, 4)), '["Data","Device","Time","Logic"]');

// Key names outrank description matches: "ble" finds ble_* keys before "table".
const results = context.bindingHelpSearch(fixture.schemes, 'ble');
const health = results.find(group => group.title === 'health');
assert.ok(health, 'health has ble matches');
assert.match(health.hits[0].doc.key, /^ble_/);
assert.ok(health.hits.findIndex(hit => hit.doc && hit.doc.key === 'table') >
          health.hits.findIndex(hit => hit.doc && hit.doc.key === 'ble_status'));
assert.ok(context.bindingHelpSearch(fixture.schemes, '%V').some(group => group.title === 'Format strings'));
assert.ok(context.bindingHelpSearch(fixture.schemes, 'threshold').some(group => group.title === 'expr'));
assert.strictEqual(context.bindingHelpSearch(fixture.schemes, 'zzzz-no-match').length, 0);

// Generated tokens must respect native buffer bounds and printf escaping.
assert.strictEqual(context.bindingBuilderFormat('raw', 1, ' %'), '');
assert.strictEqual(context.bindingBuilderUnitSuffix(' \u00b0C '), ' \u00b0C');
assert.strictEqual(context.bindingBuilderUnitSuffix('%'), ' %');
assert.strictEqual(context.bindingBuilderUnitSuffix('  '), '');
assert.strictEqual(context.bindingBuilderFormat('number', 2, context.bindingBuilderUnitSuffix(' % ')), '%.2f %%');
assert.strictEqual(context.bindingBuilderSample('21.37', 'number', 1, context.bindingBuilderUnitSuffix(' \u00b0C ')), '21.4 \u00b0C');
assert.strictEqual(context.bindingBuilderFormat('number', 1, ' %'), '%.1f %%');
assert.strictEqual(context.bindingBuilderFormat('text', 1, ' watts'), '%s watts');
assert.strictEqual(context.bindingBuilderToken('ha', 'sensor.temp', '', '%.1f C', 'Offline'),
    '[ha:sensor.temp;;%.1f C|Offline]');
assert.strictEqual(context.bindingBuilderToken('mqtt', 'house/temp', 'value', '', ''),
    '[mqtt:house/temp;value]');
assert.strictEqual(context.bindingBuilderToken('ha', 'light.kitchen', 'attributes.brightness', '%s', ''),
    '[ha:light.kitchen;attributes.brightness;%s]');
for (const invalid of ['a;b', 'a|b', 'a[b', 'a]b']) {
    assert.throws(() => context.bindingBuilderFormat('number', 1, invalid), /delimiters/);
    assert.throws(() => context.bindingBuilderToken('ha', 'sensor.temp', invalid, '', ''), /delimiters/);
    assert.throws(() => context.bindingBuilderToken('ha', 'sensor.temp', '', '', invalid), /delimiters/);
}
assert.throws(() => context.bindingBuilderFormat('number', 7, ''), /decimal/);
assert.throws(() => context.bindingBuilderFormat('text', 0, '\u00b0'.repeat(32)), /64 UTF-8/);
assert.throws(() => context.bindingBuilderToken('ha', 'sensor.temp', '', '', 'x'.repeat(180)), /192 UTF-8/);
assert.strictEqual(context.bindingBuilderSample('21.37', 'number', 1, ' C'), '21.4 C');
assert.strictEqual(context.bindingBuilderSample('0', 'number', 0, ' %'), '0 %');
assert.strictEqual(context.bindingBuilderSample('on', 'number', 1, ' C'), 'on');
assert.strictEqual(context.bindingBuilderSample('', 'number', 1, ' C'), '');
assert.strictEqual(context.bindingBuilderSample(null, 'raw', 1, ''), null);
for (const value of ['0', '-12.5', ' 2e3 ']) assert.ok(context.bindingBuilderIsNumeric(value));
for (const value of ['', ' ', '23abc', '0x10', 'unknown', 'unavailable', 'Infinity', '1e999']) {
    assert.ok(!context.bindingBuilderIsNumeric(value));
}
function builderState() {
    return JSON.parse(vm.runInContext('JSON.stringify(_haBuilder)', context));
}
function prepareEntity() {
    context.haBindingBuilderReset();
    vm.runInContext("_haBuilder.entity = 'sensor.temp'", context);
}
for (const value of ['21.37', '0', '-2.5']) {
    prepareEntity();
    assert.strictEqual(context._haBuilderSuggestFormat({ state: value, unit: ' C ' }), '');
    assert.strictEqual(builderState().mode, 'number');
    assert.strictEqual(builderState().decimals, 1);
    assert.strictEqual(builderState().unit, 'C');
}
for (const detail of [
    { state: 'on', unit: 'C' }, { state: 'unavailable', unit: 'C' },
    { state: '123', state_truncated: true, unit: 'C' }
]) {
    prepareEntity();
    context._haBuilderSuggestFormat(detail);
    assert.strictEqual(builderState().mode, 'raw');
    assert.strictEqual(builderState().unit, '');
}
prepareEntity();
context._haBuilderSuggestFormat({ state: '12' });
assert.strictEqual(builderState().mode, 'number');
assert.strictEqual(builderState().unit, '');
prepareEntity();
assert.match(context._haBuilderSuggestFormat({ state: '12', unit: '[invalid]' }), /skipped/);
assert.strictEqual(builderState().mode, 'raw');
assert.strictEqual(builderState().unit, '');
prepareEntity();
vm.runInContext("_haBuilder.fallback = 'x'.repeat(170)", context);
assert.match(context._haBuilderSuggestFormat({ state: '12', unit: 'C' }), /skipped/);
assert.doesNotThrow(() => context.bindingBuilderToken('ha', 'sensor.temp', '', '',
    builderState().fallback));
prepareEntity();
vm.runInContext("_haBuilder.path = 'attributes.battery'", context);
context._haBuilderSuggestFormat({ state: '12', unit: 'C' });
assert.strictEqual(builderState().mode, 'raw');
context.haBindingBuilderReset();
assert.ok(context.haBindingValidEntity('sensor.temperature'));
assert.ok(!context.haBindingValidEntity('sensor.temperature;path'));
assert.ok(!context.haBindingValidEntity('sensor.' + 'x'.repeat(89)));

function field(value, start, end) {
    const events = [];
    return {
        value, selectionStart: start, selectionEnd: end, events,
        setSelectionRange(a, b) { this.selectionStart = a; this.selectionEnd = b; },
        dispatchEvent(event) { events.push(event.type); }
    };
}
context.Event = class { constructor(type) { this.type = type; } };
const focused = field('CPU  %', 4, 4);
context.bindingHelpInsertText(focused, '[health:cpu]', true);
assert.strictEqual(focused.value, 'CPU [health:cpu] %');
assert.deepStrictEqual(focused.events, ['input', 'change']);
const unfocused = field('Temp', 0, 0);
context.bindingHelpInsertText(unfocused, '[time:%H:%M]', false);
assert.strictEqual(unfocused.value, 'Temp [time:%H:%M]');
const empty = field('', 0, 0);
context.bindingHelpInsertText(empty, '[timer:1]', false);
assert.strictEqual(empty.value, '[timer:1]');

// Use the same field metadata as validation, not input names or widget-specific guesses.
const rawField = { addEventListener() {} };
context.bindingAttachValidation(rawField, { isWidgetBinding: true });
context.testTarget = rawField;
vm.runInContext('_bindingHelp.target = testTarget', context);
prepareEntity();
assert.strictEqual(builderState().rawOnly, true);
context._haBuilderSuggestFormat({ state: '21.37', unit: 'C' });
assert.strictEqual(builderState().mode, 'raw');
assert.strictEqual(builderState().unit, '');

const builderElements = new Map();
const builderRoot = {
    querySelector(selector) {
        if (!builderElements.has(selector)) builderElements.set(selector, {
            value: '', hidden: false, innerHTML: '', classList: { toggle() {} }
        });
        return builderElements.get(selector);
    },
    querySelectorAll(selector) { return [this.querySelector(selector)]; }
};
vm.runInContext(`Object.assign(_haBuilder, {
    mode: 'number', unit: 'C', detail: {
        state: '21.37', paths: [{ path: 'attributes.battery', value: '87.65' }]
    }
})`, context);
context._haBuilderUpdate(builderRoot);
assert.match(builderRoot.querySelector('#ha-builder-output').innerHTML,
    /<code>\[ha:sensor.temp\]<\/code>/);
assert.match(builderRoot.querySelector('#ha-builder-output').innerHTML,
    /<output aria-live="polite">21\.37<\/output>/);
assert.strictEqual(builderRoot.querySelector('[data-ha-format-choice]').hidden, true);
assert.strictEqual(builderRoot.querySelector('[data-ha-number]').hidden, true);
assert.strictEqual(builderRoot.querySelector('[data-ha-formatted]').hidden, true);
vm.runInContext("_haBuilder.path = 'attributes.battery'; _haBuilder.fallback = '0'", context);
context._haBuilderUpdate(builderRoot);
assert.match(builderRoot.querySelector('#ha-builder-output').innerHTML,
    /<code>\[ha:sensor.temp;attributes.battery\|0\]<\/code>/);
assert.match(builderRoot.querySelector('#ha-builder-output').innerHTML,
    /<output aria-live="polite">87\.65<\/output>/);

context.bindingAttachValidation(rawField);
prepareEntity();
assert.strictEqual(builderState().rawOnly, false);
context._haBuilderSuggestFormat({ state: '21.37', unit: 'C' });
context._haBuilderUpdate(builderRoot);
assert.match(builderRoot.querySelector('#ha-builder-output').innerHTML,
    /<code>\[ha:sensor.temp;;%\.1f C\]<\/code>/);
assert.strictEqual(builderRoot.querySelector('[data-ha-format-choice]').hidden, false);
vm.runInContext('_bindingHelp.target = null', context);
prepareEntity();
assert.strictEqual(builderState().rawOnly, false);
delete context.testTarget;

const helpElements = {
    'binding-help-overlay': { style: {}, classList: { toggle() {} } },
    'binding-docs-nav': { innerHTML: '' },
    'binding-docs-body': { innerHTML: '', scrollTop: 0 },
    'binding-docs-search': { value: '', focus() {} },
    'binding-docs-count': { textContent: '' }
};
context.document.getElementById = id => helpElements[id] || null;
context.document.querySelector = () => null;
context.document.querySelectorAll = () => [];
context.testHelpSchemes = fixture.schemes;
vm.runInContext("_bindingHelp.schemes = testHelpSchemes; _bindingHelp.view = 'builder:ha'", context);
context._bhRenderNav();
assert.match(helpElements['binding-docs-nav'].innerHTML, /Binding builders/);
assert.match(helpElements['binding-docs-nav'].innerHTML, /data-bh-view="builder:ha">Home Assistant/);
const navHTML = helpElements['binding-docs-nav'].innerHTML;
assert.ok(navHTML.indexOf('<h6>Guides</h6>') < navHTML.indexOf('<h6>Binding builders</h6>'));
assert.ok(navHTML.indexOf('<h6>Binding builders</h6>') < navHTML.indexOf('<h6>Data</h6>'));
context._bhRender();
assert.match(helpElements['binding-docs-body'].innerHTML, /binding-source-picker/);
assert.match(helpElements['binding-docs-body'].innerHTML, /data-bb-provider-picker/);
assert.ok(!helpElements['binding-docs-body'].innerHTML.includes('binding-docs-signature'));
context.window.deviceConfig = { caps: { ha_live: false } };
assert.match(haButtonBuilderProvider.availability(context.window.deviceConfig),
    /Home Assistant live lookup is not available on this device/);
context.window.deviceConfig = {};
const haReference = context._bhRenderScheme(scheme('ha'));
assert.ok(!haReference.includes('id="ha-binding-builder"'));
assert.match(haReference, /data-bh-view="builder:ha">Open Home Assistant builder/);
for (const status of ['idle', '', 'Invalid token']) {
    const haStatus = { ...scheme('ha'), status };
    const warning = status === 'Invalid token';
    assert.strictEqual(context.bindingHelpHasStatusWarning(haStatus), warning);
    const reference = context._bhRenderScheme(haStatus);
    assert.strictEqual(reference.includes('until this is resolved'), warning);
    if (status === 'idle') assert.match(reference, /no entities are currently subscribed/);
    context.testHelpSchemes = [haStatus];
    vm.runInContext('_bindingHelp.schemes = testHelpSchemes', context);
    context._bhRenderNav();
    assert.strictEqual(helpElements['binding-docs-nav'].innerHTML.includes('binding-docs-dot'), warning);
}
assert.strictEqual(context.bindingHelpHasStatusWarning({ name: 'mqtt', status: 'idle' }), true);
let referenceClicks = 0;
const referenceTrigger = { click() { referenceClicks++; } };
const referenceKeydown = listeners.find(listener => listener.type === 'keydown' && !listener.capture).handler;
for (const key of ['Enter', ' ', 'Tab']) {
    let prevented = false;
    referenceKeydown({
        key, target: { closest() { return referenceTrigger; } },
        preventDefault() { prevented = true; }
    });
    assert.strictEqual(prevented, key !== 'Tab');
}
assert.strictEqual(referenceClicks, 2);
context.testHelpSchemes = fixture.schemes;
vm.runInContext('_bindingHelp.schemes = testHelpSchemes', context);
helpElements['binding-docs-search'].value = 'temperature';
context.showBindingHelp();
assert.strictEqual(vm.runInContext('_bindingHelp.view', context), 'guide:basics');
const shortcutField = Object.assign(field('Before selected after', 7, 15), {
    tagName: 'INPUT', type: 'text', isConnected: true, addEventListener() {}
});
context.window.deviceConfig = haConfigured;
helpElements['shortcut-field'] = shortcutField;
context.document.activeElement = shortcutField;
const shortcut = {
    dataset: { bindingBuilder: 'ha' },
    closest(selector) { return selector === 'label' ? { htmlFor: 'shortcut-field' } : this; },
    matches(selector) { return selector === '.binding-builder-shortcut'; }
};
listeners.find(listener => listener.type === 'pointerdown' && listener.capture).handler({ target: shortcut });
let preventedLabelFocus = false;
listeners.find(listener => listener.type === 'click' && listener.capture).handler({
    target: shortcut, preventDefault() { preventedLabelFocus = true; }
});
assert.ok(preventedLabelFocus);
assert.strictEqual(vm.runInContext('_bindingHelp.view', context), 'builder:ha');
assert.strictEqual(vm.runInContext('_bindingHelp.targetFocused', context), true);
assert.strictEqual(builderState().rawOnly, false);
context.bindingAttachValidation(shortcutField, { isWidgetBinding: true });
listeners.find(listener => listener.type === 'click' && listener.capture).handler({
    target: shortcut, preventDefault() {}
});
assert.strictEqual(builderState().rawOnly, true);
context.showBindingHelp();
context.showBindingBuilder('ha');
assert.strictEqual(builderState().rawOnly, true);
context.bindingHelpInsertText(shortcutField, '[ha:sensor.temperature]', true);
assert.strictEqual(shortcutField.value, 'Before [ha:sensor.temperature] after');
vm.runInContext('_bindingHelp.target = null; _bindingHelp.pointer = null', context);
context.showBindingHelp();
context.window.deviceConfig = haConfigured;
context.showBindingBuilder('ha');
assert.strictEqual(vm.runInContext('_bindingHelp.view', context), 'builder:ha');
assert.strictEqual(builderState().rawOnly, false);
assert.match(helpElements['binding-docs-body'].innerHTML, /binding-source-picker/);
context.showBindingHelp();
assert.strictEqual(vm.runInContext('_bindingHelp.view', context), 'guide:basics');
assert.strictEqual(helpElements['binding-docs-search'].value, '');
assert.match(helpElements['binding-docs-body'].innerHTML, /<h4>Basics<\/h4>/);
assert.match(helpElements['binding-docs-body'].innerHTML, /data-bh-view="builder:ha">Home Assistant/);
vm.runInContext("_bindingHelp.view = 'ha'", context);
context.showBindingHelp();
assert.strictEqual(vm.runInContext('_bindingHelp.view', context), 'guide:basics');
context.testHelpSchemes = fixture.schemes.filter(s => s.name !== 'ha');
vm.runInContext('_bindingHelp.schemes = testHelpSchemes', context);
context._bhRenderNav();
context._bhRender();
assert.ok(!helpElements['binding-docs-nav'].innerHTML.includes('Binding builders'));
assert.ok(!helpElements['binding-docs-body'].innerHTML.includes('id="ha-binding-builder"'));
assert.ok(!context._bhRenderGuide('basics').includes('data-bh-view="builder:ha"'));
vm.runInContext("_bindingHelp.schemes = null; _bindingHelp.view = 'guide:basics'", context);
delete context.testHelpSchemes;

// Every documented example must pass the portal validator too.
(async function() {
    await context.bindingLoadSchema();
    // Whole-button generation must produce ordinary, valid bindings and literal service actions.
    context.actionEditorCatalogEntry = type => type === 'ha_service' ? {} : null;
    context.padGetEffectiveDefault = () => '#000000';
    context.padColorNormalizeLiteral = color => color;
    const builders = vm.runInContext('padButtonBuilders', context);
    const valueBuilder = builders.find(builder => builder.id === 'value');
    const toggleBuilder = builders.find(builder => builder.id === 'toggle');
    const barBuilder = builders.find(builder => builder.id === 'bar');
    const gaugeBuilder = builders.find(builder => builder.id === 'gauge');
    const historyBuilder = builders.find(builder => builder.id === 'history');
    const temperature = { entity_id: 'sensor.living_room_temperature', state: '21.37', unit: '\u00b0C', paths: [] };
    const light = { entity_id: 'light.kitchen', state: 'on', unit: '', paths: [] };
    const makeCandidate = (detail, name = 'Living room') => context.haButtonBuilderCandidate(detail, name);
    const makeSource = (builder, detail, name = 'Living room', index = 0, previous) =>
        context.padButtonBuilderSource(builder, makeCandidate(detail, name), index, previous);
    const makeDraft = (detail, options = {}) => ({
        sources: [makeSource(null, detail)], options, previewState: detail.state
    });
    const buildSession = (detail, builder) => {
        const session = { col: 2, row: 1, page: 0, draft: makeDraft(detail) };
        context.padButtonBuilderSetScenario(session, builder);
        return session;
    };
    const temperatureCandidate = makeCandidate(temperature);
    assert.strictEqual(temperatureCandidate.sourceId, temperature.entity_id);
    assert.strictEqual(temperatureCandidate.sample, temperature.state);
    assert.strictEqual(temperatureCandidate.capabilities.numericWidget, true);
    assert.strictEqual(Object.hasOwn(temperatureCandidate, 'entity_id'), false,
        'Shared candidates do not expose HA-specific identity fields');
    assert.strictEqual(haButtonBuilderProvider.buildBinding(temperatureCandidate, { format: '%.1f' }),
        '[ha:sensor.living_room_temperature;;%.1f]');
    const valueSession = buildSession(temperature, valueBuilder);
    const generatedValue = context.padButtonBuilderBuild(valueSession);
    assert.deepStrictEqual(JSON.parse(JSON.stringify(generatedValue)), {
        col: 2, row: 1, label_top: 'Living room',
        label_center: '[ha:sensor.living_room_temperature;;%.1f]', label_bottom: '\u00b0C'
    });
    assert.strictEqual(valueBuilder.preview(valueSession.draft).label_center, '21.4');
    assert.strictEqual(valueBuilder.preview(makeDraft({ ...temperature, state: 'unavailable' }, valueSession.draft.options)).label_center, '---');
    const textSession = buildSession({ ...temperature, state: 'Ready', unit: '' }, valueBuilder);
    assert.strictEqual(context.padButtonBuilderBuild(textSession).label_center, '[ha:sensor.living_room_temperature;;%s]');
    assert.ok(!textSession.fields.some(field => field.name === 'decimals'));
    for (const decimals of ['', ' ', null, undefined, false, 'NaN', 'Infinity', '-1', '1.5', '7']) {
        valueSession.draft.options.decimals = decimals;
        assert.throws(() => context.padButtonBuilderBuild(valueSession), /decimal/);
    }
    for (const decimals of ['0', '6']) {
        valueSession.draft.options.decimals = decimals;
        assert.ok(context.padButtonBuilderBuild(valueSession).label_center.includes('%.' + decimals + 'f'));
    }
    const switchingSession = buildSession(temperature, valueBuilder);
    switchingSession.draft.options.title = 'My temperature';
    switchingSession.draft.options.unit = 'custom unit';
    context.padButtonBuilderSetScenario(switchingSession, historyBuilder, switchingSession.draft.options.title);
    assert.strictEqual(switchingSession.draft.options.title, 'My temperature');
    assert.strictEqual(switchingSession.draft.sources[0].options.unit, temperature.unit);
    switchingSession.draft.sources[0].options.color = '#123456';
    switchingSession.draft.sources[0].options.unit = 'degrees';
    context.padButtonBuilderSetScenario(switchingSession, valueBuilder, switchingSession.draft.options.title);
    assert.strictEqual(switchingSession.draft.options.title, 'My temperature');
    assert.ok(!Object.hasOwn(switchingSession.draft.options, 'color'));
    assert.ok(!Object.hasOwn(switchingSession.draft.options, 'window'));
    context.padButtonBuilderSetScenario(switchingSession, historyBuilder, switchingSession.draft.options.title);
    const switchedBack = context.padButtonBuilderBuild(switchingSession);
    assert.strictEqual(switchedBack.widget_sparkline_current_label, '[ha:sensor.living_room_temperature;;%.1f degrees]');
    assert.strictEqual(switchedBack.widget_sparkline_line_color, '#123456');

    // A new numeric knob needs only a descriptor and output mapping.
    let fieldEvaluations = 0;
    const steppedBuilder = {
        fields() {
            fieldEvaluations++;
            return [{ name: 'level', label: 'Level', type: 'number', default: 0.3, min: 0.1, max: 0.9, step: 0.2 }];
        },
        build: ({ options }) => ({ label_center: String(Number(options.level)) })
    };
    const steppedSession = buildSession(temperature, steppedBuilder);
    assert.strictEqual(context.padButtonBuilderBuild(steppedSession).label_center, '0.3');
    for (const level of ['0.1', '0.7', '0.9']) {
        steppedSession.draft.options.level = level;
        assert.strictEqual(context.padButtonBuilderBuild(steppedSession).label_center, level);
    }
    for (const level of ['', '0', '0.2', '1', '1e999']) {
        steppedSession.draft.options.level = level;
        assert.throws(() => context.padButtonBuilderBuild(steppedSession), /level/);
    }
    assert.strictEqual(fieldEvaluations, 1, 'Defaults, validation, and rendering share one field definition');

    // Another multi-value scenario can accept text and own its formatting and output limits.
    const textPairBuilder = {
        maxSources: 2,
        available: () => true,
        fields: () => [],
        sourceFields: () => [{ name: 'unit', label: 'Suffix', type: 'text', default: 'state', maxlength: 60 }],
        validateSource({ candidate }) {
            if (!['on', 'off'].includes(candidate.sample)) throw new Error('Choose an on/off entity.');
        },
        build({ sources }) {
            return { label_center: sources.map(source => source.candidate.sample).join(' / '),
                state_caption: sources[0].options.unit };
        },
        validateOutput(draft, button) {
            context.padButtonBuilderValidateText(button.state_caption, 8);
        }
    };
    const textPairSession = buildSession(light, textPairBuilder);
    assert.strictEqual(textPairSession.draft.sources[0].options.unit, 'state', 'The shell does not infer units');
    textPairSession.draft.sources[0].options.unit = 'custom';
    const replacementText = makeSource(textPairBuilder,
        { ...light, entity_id: 'switch.example', unit: '%' }, 'Example', 0, textPairSession.draft.sources[0]);
    assert.strictEqual(replacementText.options.unit, 'custom', 'Replacement policy belongs to the scenario');
    assert.strictEqual(context.padButtonBuilderBuild(textPairSession).label_center, 'on');
    textPairSession.draft.sources[0].options.unit = 'too long!';
    assert.throws(() => context.padButtonBuilderBuild(textPairSession), /8 UTF-8 byte/, 'Scenario-only output fields are validated');
    textPairSession.draft.sources[0].options.unit = 'custom';
    valueSession.draft.options.decimals = 7;
    assert.throws(() => context.padButtonBuilderBuild(valueSession), /decimal/);
    valueSession.draft.options.decimals = 1;
    valueSession.draft.options.title = '\u00b0'.repeat(96);
    assert.throws(() => context.padButtonBuilderBuild(valueSession), /UTF-8/);
    valueSession.draft.options.title = 'Living room';
    const lightSession = buildSession(light, toggleBuilder);
    const generatedLight = context.padButtonBuilderBuild(lightSession);
    assert.strictEqual(generatedLight.icon_id, 'mi_lightbulb');
    assert.strictEqual(generatedLight.label_top_style, 'color:#000000', 'Keep the title legible on light button backgrounds');
    assert.strictEqual(generatedLight.fg_color, '[expr:[ha:light.kitchen]=="on"?"#FFC857":"#64748B"|#EF6B73]');
    assert.deepStrictEqual(JSON.parse(JSON.stringify(generatedLight.actions)), [
        { type: 'ha_service', entity_id: 'light.kitchen', service: 'toggle' }
    ]);
    assert.strictEqual(toggleBuilder.available(makeDraft(temperature)), false);
    assert.strictEqual(toggleBuilder.available(makeDraft(light)), true);
    assert.strictEqual(toggleBuilder.available(makeDraft({ ...light, entity_id: 'binary_sensor.door' })), false);
    const switchDetail = { ...light, entity_id: 'switch.desk_fan', state: 'off' };
    assert.strictEqual(toggleBuilder.available(makeDraft(switchDetail)), true);
    const switchSession = buildSession(switchDetail, toggleBuilder);
    const generatedSwitch = context.padButtonBuilderBuild(switchSession);
    assert.strictEqual(generatedSwitch.icon_id, 'mi_toggle_on');
    assert.strictEqual(generatedSwitch.fg_color, '[expr:[ha:switch.desk_fan]=="on"?"#FFC857":"#64748B"|#EF6B73]');
    assert.deepStrictEqual(JSON.parse(JSON.stringify(generatedSwitch.actions)), [
        { type: 'ha_service', entity_id: 'switch.desk_fan', service: 'toggle' }
    ]);
    const catalogEntry = context.actionEditorCatalogEntry;
    context.actionEditorCatalogEntry = () => null;
    assert.strictEqual(toggleBuilder.available(makeDraft(switchDetail)), false);
    assert.strictEqual(toggleBuilder.available(makeDraft(light)), false);
    context.actionEditorCatalogEntry = catalogEntry;
    const previewColors = ['on', 'off', 'unavailable'].map(previewState =>
        toggleBuilder.preview({ ...lightSession.draft, previewState }).fg_color);
    assert.strictEqual(new Set(previewColors).size, 3);
    const longLight = { ...light, entity_id: 'light.' + 'a'.repeat(41) };
    assert.ok(context.padButtonBuilderBuild(buildSession(longLight, toggleBuilder)));
    assert.throws(() => context.padButtonBuilderBuild(buildSession({ ...longLight, entity_id: longLight.entity_id + 'a' }, toggleBuilder)), /48/);
    lightSession.draft.options.icon = 'not an icon';
    assert.throws(() => context.padButtonBuilderBuild(lightSession), /Symbol/);
    lightSession.draft.options.icon = '';
    assert.throws(() => context.padButtonBuilderBuild(lightSession), /symbol/i);
    lightSession.draft.options.icon = 'lightbulb';
    for (const color of ['red', '#fff', '[ha:light.kitchen]', '']) {
        lightSession.draft.options.on = color;
        assert.throws(() => context.padButtonBuilderBuild(lightSession), /colors/);
    }
    lightSession.draft.options.on = '#aAbBcC';
    assert.ok(context.padButtonBuilderBuild(lightSession).fg_color.includes('#aAbBcC'));
    valueSession.draft.options.unit = 'x'.repeat(61);
    assert.throws(() => context.padButtonBuilderBuild(valueSession), /unit/);
    valueSession.draft.options.unit = 'x'.repeat(60);
    assert.strictEqual(context.padButtonBuilderBuild(valueSession).label_bottom.length, 60);
    valueSession.draft.options.unit = temperature.unit;

    context.deviceInfoCache = { widget_catalog: [{ type: 'sparkline' }] };
    assert.strictEqual(historyBuilder.available(makeDraft(temperature)), true);
    assert.strictEqual(historyBuilder.available(makeDraft({ ...temperature, state: 'unavailable' })), true);
    for (const detail of [light, { ...temperature, state: 'unavailable', unit: '' },
        { ...temperature, state_truncated: true }]) {
        assert.strictEqual(historyBuilder.available(makeDraft(detail)), false);
    }
    context.deviceInfoCache.widget_catalog = [];
    assert.strictEqual(historyBuilder.available(makeDraft(temperature)), false);
    context.deviceInfoCache.widget_catalog = [{ type: 'sparkline' }];
    context.window.__device_caps = { ha_history: true };
    const historySession = buildSession(temperature, historyBuilder);
    assert.deepStrictEqual(JSON.parse(JSON.stringify(context.padButtonBuilderBuild(historySession))), {
        col: 2, row: 1, label_top: '', label_center: 'Living room', label_center_style: 'align:left',
        widget_type: 'sparkline', widget_data_binding: '[ha:sensor.living_room_temperature]',
        widget_sparkline_current_label: '[ha:sensor.living_room_temperature;;%.1f \u00b0C]',
        widget_sparkline_line_color: '#4CAF50', widget_sparkline_line_width: 2,
        widget_sparkline_current_dot: 5, widget_sparkline_smooth: 0,
        widget_sparkline_unified_scale: true, widget_sparkline_window: 86400, widget_sparkline_label_width: 100,
        widget_sparkline_slots: 288, widget_sparkline_ha_entity: temperature.entity_id,
        widget_sparkline_ha_stat: 'mean'
    });
    for (const [window, slots, caption] of [
        ['3600', 12, 'Last 1 hour.'], ['18000', 60, 'Last 5 hours.'], ['21600', 72, 'Last 6 hours.'],
        ['86400', 288, 'Last 24 hours.'], ['604800', 1024, 'Last 7 days.']
    ]) {
        historySession.draft.options.window = window;
        const button = context.padButtonBuilderBuild(historySession);
        assert.strictEqual(button.widget_sparkline_window, Number(window));
        assert.strictEqual(button.widget_sparkline_slots, slots);
        assert.ok(historyBuilder.previewNote(historySession.draft).startsWith(caption));
        assert.match(historyBuilder.previewNote(historySession.draft), /Illustrative trend, not actual history/);
    }
    const unknownUnitHistory = buildSession({ ...temperature, unit: undefined }, historyBuilder);
    assert.strictEqual(context.padButtonBuilderBuild(unknownUnitHistory).widget_sparkline_unified_scale,
        false, 'Automatic scaling requires a known provider-reported unit');
    historySession.draft.options.window = '0';
    assert.throws(() => context.padButtonBuilderBuild(historySession), /time range/);
    historySession.draft.options.window = '86400';
    historySession.draft.sources[0].options.color = 'red';
    assert.throws(() => context.padButtonBuilderBuild(historySession), /line color/);
    Object.assign(historySession.draft.sources[0].options, { color: '#2196F3', unit: '%', decimals: 2 });
    assert.strictEqual(context.padButtonBuilderBuild(historySession).widget_sparkline_current_label, '[ha:sensor.living_room_temperature;;%.2f %%]');
    historySession.draft.sources[0].options.decimals = 7;
    assert.throws(() => context.padButtonBuilderBuild(historySession), /decimal/);
    historySession.draft.sources[0].options.decimals = 1;
    const humidity = { entity_id: 'sensor.living_room_humidity', state: '54.2', unit: '%', paths: [] };
    historySession.draft.sources.push(makeSource(historyBuilder, humidity, 'Humidity', 1));
    Object.assign(historySession.draft.options, { window: '18000', interval: 60 });
    let multi = context.padButtonBuilderBuild(historySession);
    assert.strictEqual(multi.widget_sparkline_slots, 300);
    assert.strictEqual(multi.widget_sparkline_unified_scale, false, 'Automatic scales use entity units, not display overrides');
    assert.strictEqual(multi.widget_data_binding_2, '[ha:sensor.living_room_humidity]');
    assert.strictEqual(multi.widget_sparkline_current_label_2, '[ha:sensor.living_room_humidity;;%.1f %%]');
    assert.strictEqual(multi.widget_sparkline_line_color_2, '#2196F3');
    assert.strictEqual(multi.widget_sparkline_ha_entity_2, humidity.entity_id);
    assert.strictEqual(multi.widget_sparkline_ha_stat_2, 'mean');
    assert.match(historyBuilder.previewNote(historySession.draft), /60 seconds \(300 points per line\)/);
    historySession.draft.options.scale = 'shared';
    assert.strictEqual(context.padButtonBuilderBuild(historySession).widget_sparkline_unified_scale, true);
    historySession.draft.options.scale = 'independent';
    assert.strictEqual(context.padButtonBuilderBuild(historySession).widget_sparkline_unified_scale, false);
    historySession.draft.options.interval = 0.1;
    assert.strictEqual(context.padButtonBuilderBuild(historySession).widget_sparkline_slots, 1024);
    historySession.draft.options.interval = '';
    assert.throws(() => context.padButtonBuilderBuild(historySession), /sample interval/);
    historySession.draft.options.interval = 60;
    historySession.draft.sources[1].options.unit = '\u00b0'.repeat(30);
    assert.throws(() => context.padButtonBuilderBuild(historySession), /format|63 UTF-8 byte/);
    historySession.draft.sources[1].options.unit = '%';
    historySession.draft.sources[1].candidate = makeCandidate({
        ...humidity, entity_id: 'sensor.' + 'h'.repeat(48)
    }, 'Humidity');
    assert.throws(() => context.padButtonBuilderBuild(historySession), /63 UTF-8 byte/, 'Secondary current-value labels must fit firmware buffers');
    historySession.draft.sources[1].candidate = makeCandidate(humidity, 'Humidity');
    historySession.draft.sources.push(makeSource(historyBuilder,
        { ...temperature, entity_id: 'sensor.bedroom_temperature' }, 'Bedroom', 2));
    multi = context.padButtonBuilderBuild(historySession);
    assert.strictEqual(multi.widget_data_binding_3, '[ha:sensor.bedroom_temperature]');
    assert.strictEqual(multi.widget_sparkline_ha_entity_3, 'sensor.bedroom_temperature');
    historySession.draft.sources.push(historySession.draft.sources[2]);
    assert.throws(() => context.padButtonBuilderBuild(historySession), /between 1 and 3/);
    historySession.draft.sources.pop();
    historySession.draft.sources.splice(1, 1);
    multi = context.padButtonBuilderBuild(historySession);
    assert.strictEqual(multi.widget_data_binding_2, '[ha:sensor.bedroom_temperature]', 'Removing a source compacts numbered fields');
    assert.strictEqual(multi.widget_data_binding_3, undefined);
    historySession.draft.options.scale = 'auto';
    assert.strictEqual(context.padButtonBuilderBuild(historySession).widget_sparkline_unified_scale, true);
    const replacement = makeSource(historyBuilder, humidity, 'Humidity', 0, historySession.draft.sources[0]);
    assert.strictEqual(replacement.options.color, '#2196F3', 'Changing an entity preserves styling');
    assert.strictEqual(replacement.options.decimals, 1);
    assert.strictEqual(replacement.options.unit, '%', 'Changing an entity updates its unit');
    for (const caps of [undefined, {}, { ha_history: false }]) {
        context.window.__device_caps = caps;
        const button = context.padButtonBuilderBuild(historySession);
        assert.strictEqual(button.widget_sparkline_ha_entity, undefined);
        assert.strictEqual(button.widget_sparkline_ha_stat, undefined);
        assert.strictEqual(button.widget_sparkline_ha_entity_2, undefined);
        assert.strictEqual(button.widget_sparkline_ha_stat_2, undefined);
        assert.strictEqual(button.widget_data_binding, '[ha:sensor.living_room_temperature]');
    }

    // Bar inputs stay raw; only the bottom label gets display formatting.
    context.deviceInfoCache = { widget_catalog: [{ type: 'bar_chart' }] };
    assert.strictEqual(barBuilder.available(makeDraft(temperature)), true);
    for (const detail of [light, { ...temperature, state: 'unavailable' },
        { ...temperature, state_truncated: true }]) {
        assert.strictEqual(barBuilder.available(makeDraft(detail)), false);
    }
    context.deviceInfoCache.widget_catalog = [];
    assert.strictEqual(barBuilder.available(makeDraft(temperature)), false);
    context.deviceInfoCache.widget_catalog = [{ type: 'bar_chart' }];
    const barSession = buildSession(temperature, barBuilder);
    assert.strictEqual(barSession.draft.options.min, 0);
    assert.strictEqual(barSession.draft.options.max, 50);
    assert.strictEqual(context.padButtonBuilderBuild(barSession).widget_bar_max, 50);
    for (const [state, min, max] of [
        ['1200', 0, 5000], ['-12', -50, 50], ['0', 0, 100],
        ['0.012', 0, 0.05], ['-0.012', -0.05, 0.05], ['25', 0, 50]
    ]) {
        const suggested = buildSession({ ...temperature, state }, barBuilder);
        const button = context.padButtonBuilderBuild(suggested);
        assert.strictEqual(button.widget_bar_min, min);
        assert.strictEqual(button.widget_bar_max, max);
        assert.strictEqual(button.widget_bar_zero_centered, min < 0);
        assert.ok(Number(state) >= min && Number(state) < max);
    }
    Object.assign(barSession.draft.options, { min: '-10.5', max: '40.5', orientation: 'horizontal', decimals: 2 });
    const generatedBar = context.padButtonBuilderBuild(barSession);
    assert.deepStrictEqual(JSON.parse(JSON.stringify(generatedBar)), {
        col: 2, row: 1, label_top: 'Living room', widget_type: 'bar_chart',
        widget_data_binding: '[ha:sensor.living_room_temperature]',
        widget_bar_min: -10.5, widget_bar_max: 40.5,
        widget_orientation: 'horizontal', widget_bar_zero_centered: true,
        label_bottom: '[ha:sensor.living_room_temperature;;%.2f \u00b0C]'
    });
    assert.strictEqual(barBuilder.preview(barSession.draft).label_bottom, '21.37 \u00b0C');
    for (const state of ['unknown', 'unavailable', '']) {
        assert.strictEqual(barBuilder.preview(makeDraft({ ...temperature, state }, barSession.draft.options)).label_bottom, '---');
    }
    const percentSession = buildSession({ ...temperature, unit: ' % ' }, barBuilder);
    const generatedPercentBar = context.padButtonBuilderBuild(percentSession);
    assert.strictEqual(generatedPercentBar.widget_bar_min, 0);
    assert.strictEqual(generatedPercentBar.widget_bar_max, 100);
    assert.strictEqual(generatedPercentBar.widget_orientation, 'vertical');
    assert.strictEqual(generatedPercentBar.widget_bar_zero_centered, false);
    assert.strictEqual(generatedPercentBar.label_bottom, '[ha:sensor.living_room_temperature;;%.1f %%]');
    assert.strictEqual(barBuilder.preview(percentSession.draft).label_bottom, '21.4 %');
    for (const [min, max] of [['', 100], [0, ' '], ['12abc', 100], [0, 'Infinity']]) {
        Object.assign(barSession.draft.options, { min, max });
        assert.throws(() => context.padButtonBuilderBuild(barSession), /minimum and maximum/);
    }
    for (const [min, max] of [[1, 1], [2, 1], [0, '1e99'], [1, 1.000000001], ['-3e38', '3e38']]) {
        Object.assign(barSession.draft.options, { min, max });
        assert.throws(() => context.padButtonBuilderBuild(barSession), /greater than minimum/);
    }
    Object.assign(barSession.draft.options, { min: 0, max: 0.5 });
    assert.strictEqual(context.padButtonBuilderBuild(barSession).widget_bar_max, 0.5);
    barSession.draft.options.orientation = 'diagonal';
    assert.throws(() => context.padButtonBuilderBuild(barSession), /orientation/);
    barSession.draft.options.orientation = 'vertical';
    barSession.draft.options.decimals = 7;
    assert.throws(() => context.padButtonBuilderBuild(barSession), /decimal/);
    barSession.draft.options.decimals = 1;
    barSession.draft.options.unit = '[invalid]';
    assert.throws(() => context.padButtonBuilderBuild(barSession), /delimiters/);

    context.PAD_FIRMWARE_DEFAULTS = { widget_series_color: '#4CAF50' };
    context.deviceInfoCache = { widget_catalog: [{ type: 'gauge' }] };
    assert.strictEqual(gaugeBuilder.available(makeDraft(temperature)), true);
    for (const detail of [light, { ...temperature, state: 'unavailable' },
        { ...temperature, state: 'unknown' }, { ...temperature, state_truncated: true }]) {
        assert.strictEqual(gaugeBuilder.available(makeDraft(detail)), false);
    }
    context.deviceInfoCache = null;
    assert.strictEqual(gaugeBuilder.available(makeDraft(temperature)), false);
    context.deviceInfoCache = { widget_catalog: [{ type: 'bar_chart' }] };
    assert.strictEqual(gaugeBuilder.available(makeDraft(temperature)), false);
    const gaugeSession = buildSession(temperature, gaugeBuilder);
    const generatedGauge = context.padButtonBuilderBuild(gaugeSession);
    assert.deepStrictEqual(JSON.parse(JSON.stringify(generatedGauge)), {
        col: 2, row: 1, label_top: 'Living room', widget_type: 'gauge',
        widget_data_binding: '[ha:sensor.living_room_temperature]',
        widget_gauge_min: 0, widget_gauge_max: 50,
        widget_gauge_degrees: 180, widget_gauge_start_angle: 180,
        widget_gauge_show_needle: true, widget_gauge_ticks: 5, widget_arc_color: '#4CAF50',
        label_bottom: '[ha:sensor.living_room_temperature;;%.1f \u00b0C]'
    });
    for (const [state, unit, min, max] of [
        ['54.2', ' % ', 0, 100], ['-12', 'W', -50, 50], ['0', '', 0, 100], ['0.012', 'A', 0, 0.05]
    ]) {
        const button = context.padButtonBuilderBuild(buildSession({ ...temperature, state, unit }, gaugeBuilder));
        assert.strictEqual(button.widget_gauge_min, min);
        assert.strictEqual(button.widget_gauge_max, max);
    }
    Object.assign(gaugeSession.draft.options, { min: '-10.5', max: '40.5', decimals: 2, color: '#2196F3', unit: '%' });
    const customGauge = context.padButtonBuilderBuild(gaugeSession);
    assert.strictEqual(customGauge.widget_gauge_min, -10.5);
    assert.strictEqual(customGauge.widget_gauge_max, 40.5);
    assert.strictEqual(customGauge.widget_arc_color, '#2196F3');
    assert.strictEqual(customGauge.widget_data_binding, '[ha:sensor.living_room_temperature]');
    assert.strictEqual(customGauge.label_bottom, '[ha:sensor.living_room_temperature;;%.2f %%]');
    assert.strictEqual(gaugeBuilder.preview(gaugeSession.draft).label_bottom, '21.37 %');
    for (const state of ['unknown', 'unavailable', '']) {
        assert.strictEqual(gaugeBuilder.preview(makeDraft({ ...temperature, state }, gaugeSession.draft.options)).label_bottom, '---');
    }
    for (const [min, max] of [['', 100], ['12abc', 100], [1, 1], [2, 1],
        [0, '1e99'], [1, 1.000000001], ['-3e38', '3e38']]) {
        Object.assign(gaugeSession.draft.options, { min, max });
        assert.throws(() => context.padButtonBuilderBuild(gaugeSession), /minimum/);
    }
    Object.assign(gaugeSession.draft.options, { min: 0, max: 100, color: 'red' });
    assert.throws(() => context.padButtonBuilderBuild(gaugeSession), /arc color/);
    gaugeSession.draft.options.color = '#4CAF50';
    gaugeSession.draft.options.decimals = 7;
    assert.throws(() => context.padButtonBuilderBuild(gaugeSession), /decimal/);
    gaugeSession.draft.options.decimals = 1;
    gaugeSession.draft.options.unit = '[invalid]';
    assert.throws(() => context.padButtonBuilderBuild(gaugeSession), /delimiters/);

    // Creation is draft-only and cannot overwrite local or inherited content.
    context.padState = { cols: 4, rows: 3, page: 0, buttons: [], templateButtons: [] };
    context.padWorkspace = { errors: new Map() };
    context.padBuildOccupancySet = () => new Set(context.padState.buttons.flatMap(button =>
        Array.from({ length: button.col_span || 1 }, (_, col) => (button.col + col) + ',' + button.row)));
    context.padCanPlaceTemplateButton = () => true;
    assert.strictEqual(context.padButtonBuilderCanCreate(2, 1), true);
    context.padState.buttons.push({ col: 1, row: 1, col_span: 2 });
    assert.strictEqual(context.padButtonBuilderCanCreate(2, 1), false);
    context.padState.buttons = [];
    context.padState.templateButtons = [{ col: 1, row: 1, col_span: 2 }];
    assert.strictEqual(context.padButtonBuilderCanCreate(2, 1), false);
    context.padState.templateButtons = [];
    context.padWorkspace.errors.set('2,1', new Error('Unfinished manual edit'));
    assert.strictEqual(context.padButtonBuilderCanCreate(2, 1), false);
    context.padWorkspace.errors.clear();
    const validation = {};
    valueSession.dialog = { querySelector: () => validation };
    context.testButtonSession = valueSession;
    vm.runInContext('padButtonBuilderSession = testButtonSession', context);
    const originalClose = context.padButtonBuilderClose;
    let dirty = 0, opened = 0, closed = 0;
    context.padWorkspaceClearButton = () => {};
    context.padMarkDirty = () => dirty++;
    context.padDialogOpen = () => opened++;
    context.padButtonBuilderClose = () => closed++;
    context.showMessage = () => {};
    context.padButtonBuilderCreate();
    assert.strictEqual(context.padState.buttons.length, 1);
    assert.strictEqual(dirty, 1);
    assert.strictEqual(opened, 1);
    assert.strictEqual(closed, 1);
    assert.deepStrictEqual(JSON.parse(JSON.stringify(context.padState.buttons[0])), JSON.parse(JSON.stringify(generatedValue)));
    context.padButtonBuilderCreate();
    assert.strictEqual(context.padState.buttons.length, 1);
    assert.match(validation.textContent, /no longer empty/);
    context.padState.buttons = [];
    context.padState.page = 1;
    context.padButtonBuilderCreate();
    assert.strictEqual(context.padState.buttons.length, 0);
    assert.match(validation.textContent, /no longer empty/);
    context.padState.page = 0;
    valueSession.draft.sources = [];
    context.padButtonBuilderCreate();
    assert.strictEqual(context.padState.buttons.length, 0);
    assert.match(validation.textContent, /load its details/);
    context.padButtonBuilderClose = originalClose;
    vm.runInContext('padButtonBuilderSession = null', context);

    for (const s of fixture.schemes) {
        const codes = (s.examples || []).map(example => example.code)
            .concat(((s.reference || {}).rows || []).map(row => row.example).filter(Boolean));
        for (const code of codes) {
            const result = context.validateBinding(code, {});
            // Dynamic keys (list providers) differ per build; their doc pattern must still match.
            const dynamicOnly = result.errors.every(error => {
                const match = /^Unknown binding key "([^"]+)"$/.exec(error.message);
                return match && (s.key_docs || []).some(doc =>
                    doc.key.includes('*') && context.bindingHelpKeyMatches(doc.key, match[1]));
            });
            assert.ok(result.valid || dynamicOnly, s.name + ': ' + code + ' -> ' + JSON.stringify(result.errors));
        }
    }
    const responses = [
        { status: 409, ok: false, payload: { error: 'Busy' } },
        { status: 202, ok: true, payload: { job: 7 } },
        { status: 202, ok: true, payload: { job: 7 } },
        { status: 200, ok: true, payload: { entities: [], more: false } }
    ];
    const urls = [];
    context.fetch = async url => {
        urls.push(url);
        const response = responses.shift();
        return { ...response, json: async () => response.payload };
    };
    const signal = new AbortController().signal;
    const lookup = await context.haBindingLookup('query=temperature', signal);
    assert.strictEqual(lookup.more, false);
    assert.strictEqual(responses.length, 0);
    assert.ok(urls.slice(2).every(url => url.endsWith('job=7')));
    context.fetch = async () => ({ ok: false, status: 403, json: async () => ({ error: 'Administrator token required; enter an ID manually.' }) });
    await assert.rejects(context.haBindingLookup('query=test', signal), /Administrator token required/);
    context.fetch = async () => ({ ok: false, status: 401, json: async () => ({ error: 'Invalid token' }) });
    await assert.rejects(context.haBindingLookup('entity=sensor.temp', signal), /Invalid token/);
    context.fetch = async () => ({ ok: true, status: 202, json: async () => ({}) });
    await assert.rejects(context.haBindingLookup('query=test', signal), /Invalid HA lookup job/);
    const cancelled = new AbortController();
    cancelled.abort();
    await assert.rejects(context.haBindingLookup('query=test', cancelled.signal), { name: 'AbortError' });

    // A late response must not overwrite a newer query, even if abort loses a race.
    const status = { textContent: '', dataset: {} };
    const suggestions = { innerHTML: '' };
    const root = { isConnected: true, querySelector: selector =>
        selector === '#ha-builder-status' ? status : suggestions };
    const pending = [];
    context.haBindingLookup = () => new Promise(resolve => pending.push(resolve));
    vm.runInContext("_haBuilder.query = 'older'", context);
    const oldLookup = context._haBuilderLookup(root, false);
    vm.runInContext("_haBuilder.query = 'newer'", context);
    const newLookup = context._haBuilderLookup(root, false);
    pending[1]({ entities: [{ entity_id: 'sensor.newer', name: '<New>' }], more: false });
    await newLookup;
    assert.match(suggestions.innerHTML, /sensor\.newer/);
    assert.match(suggestions.innerHTML, /&lt;New&gt;/);
    pending[0]({ entities: [{ entity_id: 'sensor.older', name: 'Old' }], more: false });
    await oldLookup;
    assert.ok(!suggestions.innerHTML.includes('sensor.older'));
    const closingLookup = context._haBuilderLookup(root, false);
    const closingSignal = vm.runInContext('_haBuilder.controller.signal', context);
    const generation = vm.runInContext('_haBuilder.generation', context);
    vm.runInContext(`Object.assign(_haBuilder, {
        entity: 'sensor.newer', name: 'Newer', manual: true, path: 'attributes.battery',
        customPath: true, mode: 'number', decimals: 4, unit: '%', fallback: 'Offline',
        detail: { state: '12', paths: [] }
    })`, context);
    context.haBindingBuilderReset();
    assert.ok(closingSignal.aborted);
    assert.deepStrictEqual(JSON.parse(vm.runInContext('JSON.stringify(_haBuilder)', context)), {
        query: '', entity: '', name: '', manual: false, path: '', customPath: false,
        mode: 'raw', decimals: 1, unit: '', fallback: '', formattingEdited: false, detail: null,
        rawOnly: false, controller: null, timer: null, generation: generation + 1
    });
    pending[2]({ entities: [], more: false });
    await closingLookup;
    assert.match(suggestions.innerHTML, /^$/);

    // Initial details suggest formatting; refreshes and in-flight edits preserve choices.
    const update = context._haBuilderUpdate;
    const paths = context._haBuilderPaths;
    context._haBuilderUpdate = () => {};
    context._haBuilderPaths = () => {};
    prepareEntity();
    const detail = { entity_id: 'sensor.temp', state: '21.37', unit: 'C', paths: [] };
    context.haBindingLookup = async () => detail;
    await context._haBuilderLookup(root, true, true);
    assert.strictEqual(builderState().mode, 'number');
    vm.runInContext("Object.assign(_haBuilder, { mode: 'text', decimals: 3, unit: '', formattingEdited: true })", context);
    await context._haBuilderLookup(root, true);
    assert.strictEqual(builderState().mode, 'text');
    assert.strictEqual(builderState().decimals, 3);
    assert.strictEqual(builderState().unit, '');
    prepareEntity();
    let resolveDetail;
    context.haBindingLookup = () => new Promise(resolve => { resolveDetail = resolve; });
    const initialDetails = context._haBuilderLookup(root, true, true);
    vm.runInContext("Object.assign(_haBuilder, { mode: 'text', unit: 'custom', formattingEdited: true })", context);
    resolveDetail(detail);
    await initialDetails;
    assert.strictEqual(builderState().mode, 'text');
    assert.strictEqual(builderState().unit, 'custom');
    context._haBuilderUpdate = update;
    context._haBuilderPaths = paths;

    // Button-builder lookup must ignore superseded responses and failures.
    const buttonStatus = { dataset: {} };
    const buttonRetry = {};
    const buttonSearch = { value: 'older' };
    const matches = { replaceChildren() {} };
    const buttonDialog = { dataset: {}, querySelectorAll: () => [], querySelector: selector => ({
        '[data-bb-status]': buttonStatus, '[data-bb-retry]': buttonRetry,
        '#button-builder-search': buttonSearch, '[data-bb-matches]': matches
    })[selector] };
    context.testButtonSession = { dialog: buttonDialog, provider: haButtonBuilderProvider };
    vm.runInContext('padButtonBuilderSession = testButtonSession', context);
    const buttonPending = [];
    context.haBindingLookup = () => new Promise((resolve, reject) => buttonPending.push({ resolve, reject }));
    const older = context.padButtonBuilderLookup(false);
    buttonSearch.value = 'newer';
    const newer = context.padButtonBuilderLookup(false);
    buttonPending[1].resolve({ entities: [], more: false });
    await newer;
    const newerStatus = buttonStatus.textContent;
    buttonPending[0].reject(new Error('Old failure'));
    await older;
    assert.strictEqual(buttonStatus.textContent, newerStatus);
    assert.strictEqual(buttonRetry.hidden, true);
    const stopped = context.padButtonBuilderLookup(false);
    context.padButtonBuilderStopLookup();
    buttonPending[2].resolve({ entities: [], more: false });
    await stopped;
    assert.notStrictEqual(buttonStatus.textContent, 'No matching entities. Try another name or enter an exact entity ID.');

    // Going back to source selection supersedes any pending lookup.
    context.deviceInfoCache = { widget_catalog: [{ type: 'sparkline' }] };
    const sourceSession = buildSession(temperature, historyBuilder);
    sourceSession.provider = haButtonBuilderProvider;
    sourceSession.draft.options.title = 'Leefruimte';
    sourceSession.draft.options.window = '18000';
    sourceSession.draft.sources[0].options.decimals = 2;
    sourceSession.dialog = buttonDialog;
    const sourceNodes = Object.fromEntries(['source', 'design', 'create', 'name', 'id', 'state',
        'selection', 'choices']
        .map(name => ['[data-bb-' + name + ']', {}]));
    sourceNodes['[data-bb-choices] input:checked'] = { focus() {} };
    sourceNodes['#button-builder-title'] = { textContent: 'Customize your button' };
    const originalQuery = buttonDialog.querySelector;
    buttonDialog.querySelector = selector => sourceNodes[selector] || originalQuery(selector);
    buttonSearch.focus = () => {};
    context.padColorPopoverClose = () => {};
    const renderFields = context.padButtonBuilderRenderFields;
    const renderChoices = context.padButtonBuilderRenderChoices;
    context.padButtonBuilderRenderFields = () => {};
    context.padButtonBuilderRenderChoices = () => {};
    context.testButtonSession = sourceSession;
    vm.runInContext('padButtonBuilderSession = testButtonSession', context);
    const beforeSelection = JSON.stringify(context.padButtonBuilderBuild(sourceSession));
    context.padButtonBuilderPickSource(1);
    assert.strictEqual(sourceNodes['#button-builder-title'].textContent, 'Build a button from...');
    buttonSearch.value = humidity.entity_id;
    const cancelledSource = context.padButtonBuilderLookup(true);
    const cancelledSourceSignal = sourceSession.controller.signal;
    context.padButtonBuilderPickSource(1);
    assert.strictEqual(cancelledSourceSignal.aborted, true);
    buttonPending[3].resolve(humidity);
    await cancelledSource;
    assert.strictEqual(JSON.stringify(context.padButtonBuilderBuild(sourceSession)), beforeSelection);
    assert.strictEqual(sourceNodes['[data-bb-design]'].hidden, true);
    assert.strictEqual(sourceNodes['[data-bb-source]'].hidden, false);
    context.padButtonBuilderPickSource(1);
    buttonSearch.value = light.entity_id;
    context.haBindingLookup = async () => light;
    await context.padButtonBuilderLookup(true);
    assert.match(buttonStatus.textContent, /numeric/);
    assert.strictEqual(sourceSession.draft.sources.length, 1);
    buttonSearch.value = humidity.entity_id;
    context.haBindingLookup = async () => humidity;
    await context.padButtonBuilderLookup(true);
    assert.strictEqual(sourceSession.draft.sources.length, 2);
    assert.strictEqual(sourceSession.draft.sources[0].options.decimals, 2);
    assert.strictEqual(sourceSession.draft.options.title, 'Leefruimte');
    assert.strictEqual(sourceSession.draft.options.window, '18000');
    assert.strictEqual(sourceSession.sourceIndex, undefined);
    assert.strictEqual(sourceNodes['#button-builder-title'].textContent, 'Customize your button');
    context.padButtonBuilderPickSource(0);
    buttonSearch.value = humidity.entity_id;
    await context.padButtonBuilderLookup(true);
    assert.match(buttonStatus.textContent, /already included/);
    assert.strictEqual(sourceSession.draft.sources[0].candidate.sourceId, temperature.entity_id);
    const bedroom = { ...temperature, entity_id: 'sensor.bedroom_temperature' };
    buttonSearch.value = bedroom.entity_id;
    context.haBindingLookup = async () => bedroom;
    await context.padButtonBuilderLookup(true);
    assert.strictEqual(sourceSession.draft.sources[0].candidate.sourceId, bedroom.entity_id);
    assert.strictEqual(valueBuilder.build({ ...sourceSession.draft, options: { decimals: 1, unit: '' } }).label_center,
        '[ha:sensor.bedroom_temperature;;%.1f]', 'Other scenarios derive the changed primary entity from the same source');
    assert.strictEqual(sourceSession.draft.sources[0].options.decimals, 2);
    textPairSession.dialog = buttonDialog;
    textPairSession.provider = haButtonBuilderProvider;
    context.testButtonSession = textPairSession;
    vm.runInContext('padButtonBuilderSession = testButtonSession', context);
    context.padButtonBuilderPickSource(1);
    buttonSearch.value = switchDetail.entity_id;
    context.haBindingLookup = async () => switchDetail;
    await context.padButtonBuilderLookup(true);
    assert.strictEqual(textPairSession.draft.sources.length, 2);
    assert.strictEqual(context.padButtonBuilderBuild(textPairSession).label_center, 'on / off',
        'The shared picker and build path support nonnumeric sources');
    context.padButtonBuilderPickSource(1);
    buttonSearch.value = temperature.entity_id;
    context.haBindingLookup = async () => temperature;
    await context.padButtonBuilderLookup(true);
    assert.match(buttonStatus.textContent, /on\/off entity/);
    assert.strictEqual(textPairSession.draft.sources[1].candidate.sourceId, switchDetail.entity_id);
    context.padButtonBuilderStopLookup();
    context.padButtonBuilderRenderFields = renderFields;
    context.padButtonBuilderRenderChoices = renderChoices;
    vm.runInContext('padButtonBuilderSession = null', context);

    const previewStatus = { hidden: true, dataset: {}, textContent: '' };
    const previewResult = {
        isConnected: true,
        querySelector: selector => selector === '[data-binding-preview-result]' ? previewStatus : null
    };
    let previewRequest;
    context.fetch = async (url, options) => {
        previewRequest = { url, options };
        return { ok: true, json: async () => ({ resolved: [{ value: '73' }] }) };
    };
    await context.bindingBuilderPreviewBinding('[health:cpu;%ld]', previewResult);
    assert.strictEqual(previewRequest.url, '/api/pad/resolve');
    assert.strictEqual(previewRequest.options.method, 'POST');
    assert.deepStrictEqual(JSON.parse(previewRequest.options.body), { bindings: ['[health:cpu;%ld]'] });
    assert.strictEqual(previewStatus.textContent, 'Device result: 73');

    context.fetch = async () => ({ ok: false, status: 503, json: async () => ({ error: 'Busy, retry.' }) });
    await context.bindingBuilderPreviewBinding('[health:cpu]', previewResult);
    assert.strictEqual(previewStatus.dataset.kind, 'error');
    assert.match(previewStatus.textContent, /Busy, retry/);

    context.fetch = async () => ({ ok: true, json: async () => fixture });
    await context._bhLoad();
    vm.runInContext(fs.readFileSync('src/app/web/portal_binding_builder_catalog.js', 'utf8'), context);
    vm.runInContext(fs.readFileSync('src/app/web/portal_binding_builder_mqtt.js', 'utf8'), context);
    const catalogProvider = vm.runInContext('catalogBindingProvider', context);
    const mqttProvider = vm.runInContext('mqttBindingProvider', context);
    const catalogKeys = await catalogProvider.list('');
    for (const scheme of fixture.schemes) {
        if (scheme.free_form && scheme.reference?.formats && scheme.reference.rows?.length) {
            assert.ok(catalogKeys.some(candidate => candidate.reference.scheme === scheme.name),
                scheme.name + ' format presets');
            continue;
        }
        for (const key of scheme.free_form ? [''] : scheme.keys) {
            assert.ok(catalogKeys.some(candidate => candidate.reference.scheme === scheme.name &&
                candidate.reference.params[0] === (key || undefined)), scheme.name + ':' + key);
        }
    }
    const timePreset = catalogKeys.find(candidate => candidate.displayName === 'Time — 24-hour clock');
    assert.ok(timePreset, 'catalog exposes complete time presets');
    assert.strictEqual(timePreset.sourceId, 'time:%H:%M');
    assert.strictEqual(timePreset.exampleValue, '14:05');
    assert.strictEqual(JSON.stringify(context.catalogBindingDefaultReference(
        { scheme: 'time', params: [], fallback: '' })),
        JSON.stringify({ scheme: 'time', params: ['%H:%M'], fallback: '' }));
    assert.strictEqual(context.catalogBindingCandidate(
        { scheme: 'health', params: ['cpu'], fallback: '' }).exampleValue, '42');
    const inlineCatalog = context.catalogBindingFormHTML(false, true);
    assert.ok(!inlineCatalog.includes('data-catalog-search'), 'unified selection has only one search box');
    assert.ok(!inlineCatalog.includes('>Scheme<'), 'selected scheme is not selected a second time');
    assert.ok(inlineCatalog.includes('data-catalog-change'), 'selected binding can return to search');
    assert.ok((await catalogProvider.list('core 0')).some(candidate => candidate.sourceId === 'health:cpu_core_0'));
    assert.strictEqual(context.bindingBuilderForScheme('time').id, 'catalog');
    assert.strictEqual(context.bindingBuilderForScheme('ha').id, 'ha');
    assert.strictEqual(context.bindingBuilderForScheme('mqtt').id, 'mqtt');
    const catalogReference = (name, ...params) => ({ scheme: name, params, fallback: '' });
    const catalogToken = (name, ...params) => context.catalogBindingToken(catalogReference(name, ...params));
    assert.strictEqual(catalogToken('health', 'cpu_core_0'), '[health:cpu_core_0]');
    assert.strictEqual(catalogToken('music', 'title'), '[music:title]');
    assert.strictEqual(catalogToken('time', '%H:%M', 'Europe/Amsterdam'), '[time:%H:%M;Europe/Amsterdam]');
    assert.strictEqual(catalogToken('mqtt', 'room/state', '', '%.1f'), '[mqtt:room/state;;%.1f]');
    assert.strictEqual(catalogToken('mqtt', 'room/state', '', ''), '[mqtt:room/state]');
    assert.strictEqual(context.catalogBindingToken(catalogReference('health', 'cpu'), { isWidgetBinding: true }),
        '[health:cpu]');
    assert.throws(() => context.catalogBindingToken(catalogReference('health', 'cpu', '%ld'),
        { isWidgetBinding: true }), /Widget data bindings/);
    assert.strictEqual(catalogToken('expr', '[mqtt:room/state;watts|0] > 0 ? "A;B|C" : "off"', '%.1f'),
        '[expr:[mqtt:room/state;watts|0] > 0 ? "A;B|C" : "off";%.1f]');
    assert.deepStrictEqual(JSON.parse(JSON.stringify(context.catalogBindingReference('[mqtt:room/state;;%.1f|none]'))),
        { scheme: 'mqtt', params: ['room/state', '', '%.1f'], fallback: 'none' });
    assert.throws(() => catalogToken('music', 'missing'), /registered key/);
    assert.throws(() => catalogToken('time', ''), /Enter format/);
    assert.throws(() => catalogToken('time', '%H:%M;injected'), /top-level/);
    assert.throws(() => catalogToken('expr', '[health:cpu'), /brackets/);
    assert.throws(() => catalogToken('time', 'é'.repeat(93)), /192 UTF-8/);
    assert.throws(() => context.catalogBindingToken({ ...catalogReference('music', 'title'), fallback: 'bad|fallback' }),
        /fallback/);
    const requiredScheme = {
        name: 'test_multi', min_params: 2, max_params: 3, widget_max_params: 2,
        format_param: 2, free_form: true, params: [
            { name: 'source', desc: 'Source', required: true },
            { name: 'field', desc: 'Field', required: true },
            { name: 'format', desc: 'Format', required: false }
        ]
    };
    vm.runInContext('_bindingHelp.schemes', context).push(requiredScheme);
    context.bindingRegisterSchema(fixture.schemes);
    assert.throws(() => catalogToken('test_multi', 'source'), /required parameters/);
    assert.throws(() => catalogToken('test_multi', 'source', ''), /Enter field/);
    assert.strictEqual(catalogToken('test_multi', 'source', 'field', ''), '[test_multi:source;field]');
    delete requiredScheme.params;
    assert.ok(context.catalogBindingSchemes().some(scheme => scheme.name === 'test_multi'),
        'Missing parameter documentation or authoring metadata must not hide a registered scheme');
    assert.ok((await catalogProvider.list('test_multi')).some(candidate => candidate.reference.scheme === 'test_multi'));
    assert.strictEqual(catalogToken('test_multi', 'source', 'field'), '[test_multi:source;field]');
    vm.runInContext('_bindingHelp.schemes', context).pop();
    context.bindingRegisterSchema(fixture.schemes);
    const unpreviewed = context.catalogBindingCandidate(catalogReference('health', 'cpu'));
    assert.strictEqual(unpreviewed.capabilities.numericWidget, false);
    assert.strictEqual(unpreviewed.acquisitionState, 'not_required');
    assert.strictEqual(unpreviewed.exampleValue, '42', 'catalog examples do not count as live samples');
    assert.notStrictEqual(context.catalogBindingCandidate(catalogReference('time', '%H')).sourceId,
        context.catalogBindingCandidate(catalogReference('time', '%M')).sourceId);

    let providerRequests = [];
    context.fetch = async (url, options) => {
        providerRequests.push({ url, options });
        return { ok: true, status: 200, json: async () => ({ resolved: [{ value: '73' }] }) };
    };
    const cpu = await catalogProvider.inspect(catalogReference('health', 'cpu'), new AbortController().signal);
    assert.strictEqual(cpu.acquisitionState, 'received');
    assert.strictEqual(cpu.sample, '73');
    assert.strictEqual(catalogProvider.buildBinding(cpu, { mode: 'integer' }), '[health:cpu;%ld]');
    assert.strictEqual(providerRequests[0].url, '/api/pad/resolve');
    assert.deepStrictEqual(JSON.parse(providerRequests[0].options.body),
        { bindings: ['[health:cpu]'] });
    context.fetch = async () => ({
        ok: true, status: 200, json: async () => ({ resolved: [{ value: '?' }] })
    });
    const unavailable = await catalogProvider.inspect(catalogReference('health', 'wifi_ssid'), new AbortController().signal);
    assert.strictEqual(unavailable.acquisitionState, 'unavailable');
    assert.strictEqual(Object.hasOwn(unavailable, 'sample'), false);
    assert.strictEqual(catalogProvider.buildBinding(unavailable, { mode: 'raw' }), '[health:wifi_ssid]');

    context.fetch = async (url, options) => {
        providerRequests.push({ url, options });
        return { ok: true, json: async () => ({ resolved: [{ value: '73' }] }) };
    };
    const generic = await catalogProvider.inspect(catalogReference('health', 'cpu_core_0'), undefined, 'pad_2');
    assert.strictEqual(generic.sample, '73');
    assert.strictEqual(generic.valueKind, 'integer', 'CPU cores now declare their numeric type');
    assert.strictEqual(generic.capabilities.numericWidget, true);
    assert.strictEqual(generic.capabilities.liveSampling, true);
    assert.deepStrictEqual(JSON.parse(providerRequests.at(-1).options.body),
        { bindings: ['[health:cpu_core_0]'], screen: 'pad_2' });
    const formatted = await catalogProvider.inspect(catalogReference('health', 'cpu', '%ld'));
    assert.strictEqual(formatted.capabilities.numericWidget, false, 'Formatted tokens cannot become widget inputs');
    const clearedFormat = await catalogProvider.inspect(catalogReference('health', 'cpu', ''));
    assert.strictEqual(clearedFormat.capabilities.numericWidget, true, 'Cleared optional formats restore raw numeric eligibility');
    const fallbackCandidate = await catalogProvider.inspect({ ...catalogReference('health', 'cpu'), fallback: '0' });
    assert.strictEqual(fallbackCandidate.capabilities.numericWidget, false, 'A fallback is not live numeric evidence');
    context.fetch = async () => ({ ok: true, json: async () => ({ resolved: [{ value: '1'.repeat(127) }] }) });
    const catalogTruncated = await catalogProvider.inspect(catalogReference('health', 'cpu'));
    assert.strictEqual(catalogTruncated.sampleTruncated, true);
    assert.strictEqual(catalogTruncated.capabilities.numericWidget, false);
    context.fetch = async () => ({ ok: false, status: 503, json: async () => ({ error: 'Device busy' }) });
    await assert.rejects(catalogProvider.inspect(catalogReference('time', '%H')), /Device busy/);
    context.fetch = async () => ({ ok: true, json: async () => ({ resolved: [] }) });
    await assert.rejects(catalogProvider.inspect(catalogReference('time', '%H')), /invalid binding preview/);

    const mqttResponse = (status, payload, ok = true) => ({
        ok, status, json: async () => payload
    });
    const mqttInspect = async (payload, topic = 'sensors/room/state') => {
        const requests = [];
        const responses = [
            mqttResponse(202, { handle: 77, status: 'queued' }),
            mqttResponse(200, payload),
            mqttResponse(202, { handle: 77, status: 'released' })
        ];
        context.fetch = async (url, options = {}) => {
            requests.push({ url, options });
            return responses.shift();
        };
        const candidate = await mqttProvider.inspect(
            { topic }, new AbortController().signal);
        return { candidate, requests };
    };
    assert.strictEqual(context.mqttBindingTopicValid('sensors/room/state'), true);
    for (const topic of ['', 'sensors/#', 'sensors/+', '\ud800']) {
        assert.strictEqual(context.mqttBindingTopicValid(topic), false);
    }
    assert.strictEqual(context.mqttBindingTopicValid('x'.repeat(128)), false);
    const mqttReceived = await mqttInspect({
        status: 'received', received: true, cached: true, truncated: false,
        sample: '{"temperature":21.37,"active":true,"nested":{"value":"ok"}}'
    });
    assert.strictEqual(mqttReceived.candidate.cached, true);
    assert.deepStrictEqual(Array.from(mqttReceived.candidate.fields, item => item.path),
        ['', 'temperature', 'active', 'nested.value']);
    const selectedTemperature = context.mqttBindingBuilderSelectedField(
        mqttReceived.candidate, 'temperature');
    assert.strictEqual(selectedTemperature.sample, '21.37');
    assert.strictEqual(mqttProvider.buildBinding(selectedTemperature, {
        path: 'temperature', mode: 'number', decimals: 1, unit: '\u00b0C'
    }), '[mqtt:sensors/room/state;temperature;%.1f \u00b0C]');
    await mqttProvider.dispose(mqttReceived.candidate);
    assert.strictEqual(mqttReceived.requests[2].options.method, 'DELETE');

    const longScalar = await mqttInspect({
        status: 'received', received: true, truncated: false,
        sample: JSON.stringify({ description: 'x'.repeat(128) })
    });
    const selectedLongScalar = context.mqttBindingBuilderSelectedField(
        longScalar.candidate, 'description');
    assert.strictEqual(selectedLongScalar.sampleTruncated, true);
    assert.strictEqual(selectedLongScalar.capabilities.numericWidget, false);

    for (const sample of ['', '---', 'ERR:mock']) {
        const result = await mqttInspect({
            status: 'received', received: true, truncated: false, sample
        });
        assert.strictEqual(result.candidate.acquisitionState, 'received');
        assert.strictEqual(result.candidate.sample, sample);
    }
    const malformed = await mqttInspect({
        status: 'received', received: true, truncated: false, sample: '{"temperature":'
    });
    assert.strictEqual(malformed.candidate.malformedJson, true);
    assert.deepStrictEqual(Array.from(malformed.candidate.fields, item => item.path), ['']);
    const truncated = await mqttInspect({
        status: 'received', received: true, truncated: true, sample: 'x'.repeat(2047)
    });
    assert.strictEqual(truncated.candidate.payloadTruncated, true);
    assert.deepStrictEqual(Array.from(truncated.candidate.fields, item => item.path), ['']);

    const cancellation = new AbortController();
    let cancelReleased = false;
    context.fetch = async (url, options = {}) => {
        if (options.method === 'POST') return mqttResponse(202, { handle: 78 }, true);
        if (options.method === 'DELETE') {
            cancelReleased = true;
            return mqttResponse(202, { handle: 78, status: 'released' }, true);
        }
        cancellation.abort();
        throw new DOMException('Inspection cancelled', 'AbortError');
    };
    await assert.rejects(
        mqttProvider.inspect({ topic: 'sensors/cancel' }, cancellation.signal),
        { name: 'AbortError' });
    assert.strictEqual(cancelReleased, true);

    // Additional button sources reuse acquisition and enforce numeric evidence per selected value.
    const catalogButtons = vm.runInContext('catalogButtonBuilderProvider', context);
    const mqttButtons = vm.runInContext('mqttButtonBuilderProvider', context);
    assert.strictEqual(catalogButtons.listOnOpen, undefined, 'catalog does not show every entry on open');
    assert.deepStrictEqual(JSON.parse(JSON.stringify((await catalogButtons.search('he')).candidates)), [],
        'catalog waits for three characters before suggesting entries');
    assert.ok((await catalogButtons.search('hea')).candidates.length,
        'catalog searches after three characters');
    const candidateSession = (candidate, builder) => {
        const session = { col: 2, row: 1, page: 0, draft: {
            sources: [context.padButtonBuilderSource(null, candidate, 0)], options: {}, previewState: candidate.sample
        } };
        context.padButtonBuilderSetScenario(session, builder);
        return session;
    };
    const runtime = { widget_catalog: [{ type: 'sparkline' }, { type: 'bar_chart' }, { type: 'gauge' }] };
    context.deviceInfoCache = runtime;
    const attributeParent = context.haButtonBuilderCandidate({
        entity_id: 'light.desk', state: 'on', unit: '%',
        paths: [{ path: 'attributes.brightness', value: '128' }]
    }, 'Desk light');
    const brightness = haButtonBuilderProvider.selectField(attributeParent, 'attributes.brightness');
    assert.strictEqual(brightness.sample, '128');
    assert.strictEqual(brightness.unit, undefined);
    assert.strictEqual(brightness.capabilities.numericWidget, true);
    assert.strictEqual(brightness.capabilities.control, false);
    assert.strictEqual(brightness.capabilities.historyBackfill, false);
    assert.strictEqual(context.bindingSourceSelection(brightness).mode, 'number');
    assert.strictEqual(context.bindingSourceSelection(attributeParent).mode, 'raw');
    assert.strictEqual(haButtonBuilderProvider.buildBinding(brightness),
        '[ha:light.desk;attributes.brightness]');
    const unknownAttribute = haButtonBuilderProvider.selectField(attributeParent, 'attributes.missing');
    assert.strictEqual(unknownAttribute.capabilities.numericWidget, false);
    assert.strictEqual(unknownAttribute.acquisitionState, 'unavailable');
    const unavailableAttribute = haButtonBuilderProvider.selectField(context.haButtonBuilderCandidate({
        entity_id: 'light.desk', state: 'unavailable',
        paths: [{ path: 'attributes.brightness', value: '128' }]
    }), 'attributes.brightness');
    assert.strictEqual(unavailableAttribute.acquisitionState, 'unavailable');
    assert.strictEqual(unavailableAttribute.capabilities.numericWidget, false,
        'Cached attributes cannot qualify for numeric widgets while their entity is unavailable');
    assert.throws(() => haButtonBuilderProvider.selectField(attributeParent, 'attributes.bad;key'), /dotted path/);
    const selectedBrightness = { ...brightness, selection: {
        mode: 'number', decimals: '2', unit: 'level', fallback: 'Offline'
    } };
    assert.strictEqual(context.bindingSourceToken(selectedBrightness, selectedBrightness.selection),
        '[ha:light.desk;attributes.brightness;%.2f level|Offline]');
    assert.strictEqual(context.bindingSourceToken(selectedBrightness, selectedBrightness.selection, true),
        '[ha:light.desk;attributes.brightness|Offline]');
    for (const builder of [valueBuilder, barBuilder, gaugeBuilder, historyBuilder]) {
        const session = candidateSession(selectedBrightness, builder);
        const button = context.padButtonBuilderBuild(session);
        const label = button.label_center?.includes('[ha:') ? button.label_center :
            button.label_bottom || button.widget_sparkline_current_label;
        assert.match(label, /\[ha:light.desk;attributes\.brightness;%\.2f(?: level)?\|Offline\]/);
        if (builder !== valueBuilder) {
            assert.strictEqual(button.widget_data_binding, '[ha:light.desk;attributes.brightness]',
                'Display formatting and fallback must not alter the numeric widget input');
        }
        assert.ok(!session.fields.some(field => ['decimals', 'unit'].includes(field.name)),
            'Selected display options are not requested again in button customization');
    }
    const selectedCpu = { ...cpu, selection: { mode: 'integer', decimals: 1, unit: '%', fallback: 'Offline' } };
    assert.strictEqual(context.bindingSourceToken(selectedCpu, selectedCpu.selection), '[health:cpu;%ld|Offline] %');
    assert.strictEqual(context.bindingSourceToken(selectedCpu, selectedCpu.selection, true), '[health:cpu|Offline]');
    assert.strictEqual(context.bindingSourceSample(selectedCpu, selectedCpu.selection), '73 %');
    assert.throws(() => context.bindingSourceToken(selectedCpu, { ...selectedCpu.selection, unit: '[bad]' }), /delimiters/);
    const selectedMqtt = { ...selectedTemperature, selection: {
        mode: 'number', decimals: 2, unit: 'C', fallback: 'Offline'
    } };
    assert.strictEqual(context.bindingSourceToken(selectedMqtt, selectedMqtt.selection),
        '[mqtt:sensors/room/state;temperature;%.2f C|Offline]');
    assert.strictEqual(context.bindingSourceSelection(selectedTemperature).mode, 'number');
    assert.strictEqual(context.bindingSourceSelection({
        ...selectedTemperature, sample: '0'
    }).mode, 'number', 'Zero is numeric');
    assert.strictEqual(context.bindingSourceSelection({
        ...selectedTemperature, sample: '', valueKind: 'text'
    }).mode, 'raw', 'An empty payload is not numeric');
    assert.strictEqual(context.bindingSourceSelection({
        ...selectedTemperature, selection: { mode: 'raw', decimals: 2, unit: 'C', fallback: '' }
    }).mode, 'raw', 'An explicit display choice is preserved');
    for (const sample of ['', '0', 'unavailable']) {
        assert.strictEqual(context.bindingSourceSample({
            ...selectedMqtt, sample, acquisitionState: 'received'
        }, { mode: 'raw', decimals: 1, unit: '', fallback: 'Offline' }), sample,
        'Received MQTT text must not be confused with a missing sample');
    }
    assert.strictEqual(context.bindingSourceSample({
        ...selectedMqtt, acquisitionState: 'unavailable'
    }, { mode: 'raw', decimals: 1, unit: '', fallback: 'Offline' }), 'Offline');
    const sampleController = new AbortController();
    context.fetch = async () => { throw new Error('Authoring must not require a live device read'); };
    const sampleCpu = await catalogButtons.prepare(catalogReference('health', 'cpu'), sampleController.signal);
    assert.strictEqual(sampleCpu.acquisitionState, 'not_required');
    assert.strictEqual(sampleCpu.sample, '42');
    assert.strictEqual(sampleCpu.sampleIsExample, true);
    assert.strictEqual(sampleCpu.capabilities.numericWidget, true);
    assert.strictEqual(context.bindingSourceSelection(sampleCpu).mode, 'integer');
    const sensor = catalogButtons.prepare(catalogReference('sensor', 'temperature'));
    assert.strictEqual(sensor.valueKind, 'number');
    assert.strictEqual(sensor.sample, '21.37');
    assert.strictEqual(sensor.unit, '\u00b0C');
    assert.strictEqual(sensor.capabilities.numericWidget, true);
    assert.strictEqual(sensor.capabilities.liveSampling, true);
    const sensorDisplay = context.bindingSourceSelection(sensor);
    assert.strictEqual(sensorDisplay.mode, 'number');
    assert.strictEqual(context.bindingSourceToken(sensor, sensorDisplay), '[sensor:temperature;%.1f] \u00b0C');
    assert.strictEqual(context.bindingSourceSample(sensor, sensorDisplay), '21.4 \u00b0C');
    assert.strictEqual(context.bindingSourceToken(sensor, sensorDisplay, true), '[sensor:temperature]');
    const preformattedSensor = catalogButtons.prepare(catalogReference('sensor', 'temperature', '%.3f'));
    const preformattedDisplay = context.bindingSourceSelection(preformattedSensor);
    assert.strictEqual(preformattedDisplay.mode, 'raw', 'A supplied native format is not overwritten by defaults');
    assert.strictEqual(context.bindingSourceToken(preformattedSensor, preformattedDisplay),
        '[sensor:temperature;%.3f] \u00b0C');
    for (const builder of [barBuilder, gaugeBuilder, historyBuilder]) {
        const session = candidateSession({ ...sensor, selection: sensorDisplay }, builder);
        assert.strictEqual(builder.available(session.draft, runtime), true);
        const button = context.padButtonBuilderBuild(session);
        assert.strictEqual(button.widget_data_binding, '[sensor:temperature]');
        assert.ok(!JSON.stringify(button).includes('21.37'), 'Illustrative samples are never saved as runtime values');
    }
    const clock = catalogButtons.prepare(timePreset.reference);
    assert.strictEqual(clock.sample, '14:05');
    assert.strictEqual(clock.capabilities.numericWidget, false,
        'Example output does not grant undeclared numeric capabilities');
    for (const scheme of fixture.schemes.filter(scheme =>
        !['ha', 'mqtt', 'expr', 'pad'].includes(scheme.name))) {
        for (const key of scheme.free_form ? [] : scheme.keys) {
            const candidate = catalogButtons.prepare(catalogReference(scheme.name, key));
            assert.strictEqual(typeof candidate.sample, 'string', scheme.name + ':' + key + ' sample');
            assert.strictEqual(candidate.sampleIsExample, true);
            assert.ok(['integer', 'number', 'text'].includes(candidate.valueKind));
        }
    }
    const elapsed = catalogButtons.prepare(catalogReference('music', 'elapsed_s'));
    assert.strictEqual(elapsed.unit, 's');
    assert.strictEqual(context.bindingSourceSelection(elapsed).mode, 'integer');
    assert.strictEqual(context.bindingSourceToken(elapsed, context.bindingSourceSelection(elapsed)),
        '[music:elapsed_s] s', 'Raw integer resolvers do not receive an invented printf argument');
    const duration = catalogButtons.prepare(catalogReference('timer', '1'));
    assert.strictEqual(duration.sample, '245.3');
    assert.strictEqual(duration.unit, 's');
    assert.strictEqual(duration.capabilities.numericWidget, true);
    assert.deepStrictEqual(Array.from(duration.formatModes), ['raw'],
        'Duration syntax must not be advertised as printf formatting');
    const formattedDuration = catalogButtons.prepare(catalogReference('timer', '1', 'mm:ss'));
    assert.strictEqual(formattedDuration.sample, '4:05');
    assert.strictEqual(formattedDuration.valueKind, 'text');
    assert.strictEqual(formattedDuration.unit, '');
    assert.strictEqual(formattedDuration.capabilities.numericWidget, false);
    const activityAge = catalogButtons.prepare(catalogReference('net', 'mqtt_rx', 'age'));
    assert.strictEqual(activityAge.sample, '125');
    assert.strictEqual(activityAge.valueKind, 'integer');
    assert.strictEqual(activityAge.unit, 'ms', 'Age variants do not inherit unitless activity metadata');
    const customClock = catalogButtons.prepare(catalogReference('time', '%H'));
    assert.strictEqual(customClock.sample, undefined, 'Custom free-form formats do not inherit a preset sample');
    const structuralScheme = {
        name: 'test_structural', min_params: 1, max_params: 2, widget_max_params: 1,
        format_param: 1, free_form: true,
        key_docs: [{ key: 'sensor_#_ms', desc: 'Sensor duration' }],
        authoring: { keys: [{ key: 'sensor_#_ms', kind: 'number', unit: 'ms',
            example_value: '1.020', formats: ['raw', 'number'],
            capabilities: ['display', 'numeric_widget', 'live_sampling'], read_only: true }] }
    };
    vm.runInContext('_bindingHelp.schemes', context).push(structuralScheme);
    context.bindingRegisterSchema(fixture.schemes);
    const structuralResults = await catalogProvider.list('Sensor duration');
    assert.ok(structuralResults.some(candidate => candidate.sourceId === 'test_structural:sensor_1_ms'));
    const structuralValue = catalogButtons.prepare(catalogReference('test_structural', 'sensor_3_ms'));
    assert.strictEqual(structuralValue.sample, '1.020');
    assert.strictEqual(structuralValue.unit, 'ms');
    assert.strictEqual(structuralValue.capabilities.numericWidget, true);
    assert.strictEqual(context.bindingSourceToken(structuralValue, context.bindingSourceSelection(structuralValue)),
        '[test_structural:sensor_3_ms;%.1f] ms');
    vm.runInContext('_bindingHelp.schemes', context).pop();
    context.bindingRegisterSchema(fixture.schemes);
    sampleController.abort();
    assert.throws(() => catalogButtons.prepare(catalogReference('health', 'cpu'), sampleController.signal),
        /cancelled/);
    const pickerNodes = new Map();
    const pickerRoot = { querySelectorAll: () => providerInputs, querySelector(selector) {
        if (!pickerNodes.has(selector)) pickerNodes.set(selector, {
            hidden: true, value: '', dataset: {}, firstChild: { textContent: '' }, focus() {}, replaceChildren() {},
            setAttribute(name) { if (name === 'hidden') this.hidden = true; },
            removeAttribute(name) { if (name === 'hidden') this.hidden = false; }
        });
        return pickerNodes.get(selector);
    } };
    const providerInputs = [{ value: 'catalog' }, { value: 'mqtt' }, { value: 'ha' }];
    const pickerSession = { dialog: pickerRoot, picker: true, config: { caps: { mqtt: true }, mqtt_host: 'broker' } };
    context.padButtonBuilderChooseProvider('mqtt', pickerSession);
    assert.strictEqual(pickerRoot.querySelector('[data-bb-catalog-search-panel]').hidden, false,
        'Changing provider restores the input after a catalog selection');
    assert.strictEqual(pickerRoot.querySelector('[data-bb-use]').hidden, false);
    assert.strictEqual(pickerRoot.querySelector('[data-bb-picked]').hidden, true);
    assert.deepStrictEqual(providerInputs.map(input => input.checked), [false, true, false]);
    context.padButtonBuilderChooseProvider('catalog', pickerSession);
    assert.strictEqual(pickerRoot.querySelector('[data-bb-use]').hidden, true,
        'Searchable sources do not need an extra confirmation button');
    assert.strictEqual(pickerRoot.querySelector('[data-bb-catalog-search-panel]').hidden, false);
    assert.deepStrictEqual(providerInputs.map(input => input.checked), [true, false, false]);
    const configuredSources = { ...haConfigured, caps: { ha_live: true, mqtt: true }, mqtt_host: 'broker' };
    assert.deepStrictEqual(Array.from(context.bindingSourceProviders(configuredSources), item => item.id).sort(),
        ['catalog', 'ha', 'mqtt']);
    const unconfiguredSources = { caps: { ha_live: true, mqtt: true } };
    assert.deepStrictEqual(Array.from(context.bindingSourceProviders(unconfiguredSources), item => item.id).sort(),
        ['catalog', 'ha', 'mqtt'], 'Unconfigured integrations remain selectable');
    assert.strictEqual((context.bindingSourceProviderOptions(unconfiguredSources, 'test-source')
        .match(/Setup needed/g) || []).length, 2);
    assert.ok(!context.bindingSourceProviderOptions(configuredSources, 'test-source').includes('Setup needed'));
    assert.deepStrictEqual(Array.from(context.bindingSourceProviders({ caps: {} }), item => item.id), ['catalog'],
        'Unsupported integrations are omitted rather than offering unusable settings');
    pickerSession.config = unconfiguredSources;
    context.padButtonBuilderChooseProvider('ha', pickerSession, false);
    assert.strictEqual(pickerRoot.querySelector('[data-bb-source]').hidden, true);
    assert.strictEqual(pickerRoot.querySelector('[data-bb-setup]').hidden, false);
    assert.match(pickerRoot.querySelector('[data-bb-setup]').firstChild.textContent, /Configure Home Assistant/);
    assert.strictEqual(pickerRoot.querySelector('[data-bb-settings]').href, '#ha-discovery');
    context.padButtonBuilderChooseProvider('mqtt', pickerSession, false);
    assert.strictEqual(pickerRoot.querySelector('[data-bb-settings]').href, '#mqtt');
    context.padButtonBuilderStopLookup(pickerSession);
    pickerSession.disposed = true;
    assert.strictEqual(context.bindingSourceSessionActive(pickerSession), false);
    const cpuExample = context.catalogBindingCandidate(catalogReference('health', 'cpu'));
    const cpuExampleSession = candidateSession(cpuExample, valueBuilder);
    assert.strictEqual(catalogButtons.canEnableNumeric(cpuExample), true,
        'declared numeric metadata can offer a live check without treating the example as evidence');
    assert.strictEqual(valueBuilder.preview(cpuExampleSession.draft).label_center, '42');
    assert.strictEqual(context.padButtonBuilderCandidateValueText(cpuExample), '',
        'example is rendered in the button preview, not repeated as current state');
    assert.match(catalogButtons.previewNote(cpuExample), /Illustrative sample preview/);
    assert.strictEqual(barBuilder.available(candidateSession(cpuExample, barBuilder).draft, runtime), false,
        'Representative examples never qualify as numeric evidence');
    const textExample = context.catalogBindingCandidate(catalogReference('health', 'wifi_ssid'));
    assert.strictEqual(catalogButtons.canEnableNumeric(textExample), false,
        'text bindings do not offer a live numeric check');
    const originalRenderSelection = context.padButtonBuilderRenderSelection;
    const originalRenderChoices = context.padButtonBuilderRenderChoices;
    const originalRenderFields = context.padButtonBuilderRenderFields;
    const originalShowStatus = context.padButtonBuilderStatus;
    const liveCheckStatuses = [];
    context.padButtonBuilderRenderSelection = () => {};
    context.padButtonBuilderRenderChoices = () => {};
    context.padButtonBuilderRenderFields = () => {};
    context.padButtonBuilderStatus = (message, error) => liveCheckStatuses.push({ message, error });
    const liveCheckSession = {
        page: 0, scenario: valueBuilder, controller: null, timer: null,
        dialog: { querySelector: () => null },
        draft: { sources: [context.padButtonBuilderSource(valueBuilder, cpuExample, 0)],
            options: {}, previewState: cpuExample.exampleValue }
    };
    context.testLiveCheckSession = liveCheckSession;
    vm.runInContext('padButtonBuilderSession = testLiveCheckSession', context);
    context.fetch = async () => mqttResponse(200, { resolved: [{ value: '23' }] });
    await context.padButtonBuilderReadCurrentValue();
    assert.strictEqual(liveCheckSession.draft.sources[0].candidate.sample, '23');
    assert.strictEqual(liveCheckSession.draft.sources[0].candidate.capabilities.numericWidget, true);
    assert.strictEqual(liveCheckSession.draft.previewState, '23');
    assert.strictEqual(barBuilder.available(liveCheckSession.draft, runtime), true,
        'A live numeric read enables numeric layouts after an illustrative sample alone did not');
    assert.deepStrictEqual(liveCheckStatuses.map(item => item.message),
        ['Reading the current device value...', '']);
    context.padButtonBuilderRenderSelection = originalRenderSelection;
    context.padButtonBuilderRenderChoices = originalRenderChoices;
    context.padButtonBuilderRenderFields = originalRenderFields;
    context.padButtonBuilderStatus = originalShowStatus;
    vm.runInContext('padButtonBuilderSession = null', context);
    const cpuSession = candidateSession(cpu, valueBuilder);
    assert.strictEqual(context.padButtonBuilderBuild(cpuSession).label_center, '[health:cpu;%ld]');
    assert.strictEqual(cpuSession.fields.some(field => field.name === 'decimals'), false);
    assert.strictEqual(valueBuilder.preview(cpuSession.draft).label_center, '73');
    for (const builder of [barBuilder, gaugeBuilder, historyBuilder]) {
        const session = candidateSession(cpu, builder);
        const button = context.padButtonBuilderBuild(session);
        assert.strictEqual(builder.available(session.draft, runtime), true);
        assert.strictEqual(button.widget_data_binding, '[health:cpu]');
        assert.strictEqual(button.label_bottom || button.widget_sparkline_current_label, '[health:cpu;%ld] %');
        assert.ok(!JSON.stringify(button).includes('%.'));
        assert.strictEqual((builder.sourceFields ? builder.sourceFields(session.draft.sources[0], 0) : session.fields)
            .some(field => field.name === 'decimals'), false);
    }
    assert.strictEqual(context.padButtonBuilderBuild(candidateSession(unavailable, valueBuilder)).label_center,
        '[health:wifi_ssid]');
    assert.strictEqual(valueBuilder.preview(candidateSession(unavailable, valueBuilder).draft).label_center, '---');
    for (const sample of ['?', 'true', '', '12abc', 'Infinity']) {
        context.fetch = async () => mqttResponse(200, { resolved: [{ value: sample }] });
        const candidate = await catalogButtons.inspect(catalogReference('health', 'cpu'), new AbortController().signal);
        assert.strictEqual(candidate.capabilities.numericWidget, false);
        assert.strictEqual(historyBuilder.available(candidateSession(candidate, valueBuilder).draft, runtime), false);
    }
    cpuSession.draft.options.unit = 'custom';
    assert.strictEqual(context.padButtonBuilderBuild(cpuSession).label_bottom, 'custom');
    assert.strictEqual(catalogButtons.buildLabel(cpu, { unit: '%;bad' }, false), '[health:cpu;%ld]');
    assert.throws(() => catalogButtons.buildLabel(cpu, { unit: '[ha:sensor.bad]' }, true), /delimiters/);
    assert.strictEqual(context.padButtonBuilderBuild(candidateSession(generic, valueBuilder)).label_center,
        '[health:cpu_core_0;%ld]');
    assert.strictEqual(historyBuilder.available(candidateSession(generic, valueBuilder).draft, runtime), true);

    const acquiredButton = async (sample, extra = {}, topic = 'sensors/room') => {
        const requests = [];
        context.fetch = async (url, options = {}) => {
            requests.push({ url, options });
            if (options.method === 'POST') return mqttResponse(202, { handle: 91 });
            if (options.method === 'DELETE') return mqttResponse(202, { status: 'release_pending' });
            return mqttResponse(200, { status: 'received', received: true, truncated: false, sample, ...extra });
        };
        const candidate = await mqttButtons.inspect({ topic }, new AbortController().signal);
        assert.strictEqual(requests.filter(request => request.options.method === 'DELETE').length, 1,
            'Button acquisition releases immediately, before selection or draft creation');
        assert.strictEqual(candidate.reference.handle, undefined);
        await mqttButtons.dispose(candidate);
        assert.strictEqual(requests.length, 3, 'Draft lifetime cannot retain or re-release a preview handle');
        return candidate;
    };
    for (const sample of ['0', '', '---', 'ERR:mock', 'unavailable', 'true', '12abc', '1e999', '{}', '[]']) {
        const acquired = await acquiredButton(sample);
        const selected = mqttButtons.selectField(acquired, '');
        const session = candidateSession(selected, valueBuilder);
        assert.strictEqual(selected.sample, sample);
        assert.strictEqual(Object.hasOwn(selected, 'unit'), false, 'MQTT never invents a unit');
        for (const builder of [barBuilder, gaugeBuilder, historyBuilder]) {
            assert.strictEqual(builder.available(session.draft, runtime), sample === '0');
        }
        assert.strictEqual(toggleBuilder.available(session.draft), false);
        assert.strictEqual(valueBuilder.preview(session.draft).label_center, sample === '0' ? '0.0' : sample);
        assert.ok(!JSON.stringify(context.padButtonBuilderBuild(session)).includes('ha_entity'));
    }
    const objectSample = await acquiredButton('{"temperature":21.37,"active":true,"custom":0,"text":"12abc"}');
    const mqttTemperature = mqttButtons.selectField(objectSample, 'temperature');
    assert.strictEqual(mqttButtons.buildBinding(mqttTemperature), '[mqtt:sensors/room;temperature]');
    assert.strictEqual(context.padButtonBuilderBuild(candidateSession(mqttTemperature, barBuilder)).label_bottom,
        '[mqtt:sensors/room;temperature;%.1f]');
    for (const path of ['active', 'text']) {
        const selected = mqttButtons.selectField(objectSample, path);
        assert.strictEqual(historyBuilder.available(candidateSession(selected, valueBuilder).draft, runtime), false);
    }
    assert.strictEqual(mqttButtons.selectField(objectSample, 'custom').sample, '0');
    const manualSample = await acquiredButton('{"room":{"sensor value":12,"\u6e29\u5ea6":0}}');
    assert.strictEqual(mqttButtons.selectField(manualSample, '.room..sensor value.').sample, '12');
    assert.strictEqual(mqttButtons.selectField(manualSample, 'room.\u6e29\u5ea6').sample, '0');
    assert.strictEqual(mqttButtons.selectField(objectSample, '.').reference.path, '');
    assert.throws(() => mqttButtons.selectField(objectSample, 'missing'), /scalar value/);
    assert.throws(() => mqttButtons.selectField(objectSample, 'x'.repeat(48)), /47 UTF-8/);
    assert.throws(() => mqttButtons.selectField(objectSample, 'temperature;bad'), /dotted scalar/);
    const bounded = await acquiredButton(JSON.stringify(Object.fromEntries(
        Array.from({ length: 34 }, (_, index) => ['v' + index, index]))));
    assert.strictEqual(bounded.fields.length, 33);
    assert.strictEqual(mqttButtons.selectField(bounded, 'v33').sample, '33', 'Manual paths work beyond the suggestion cap');
    const oversized = await acquiredButton('x'.repeat(128));
    assert.throws(() => mqttButtons.selectField(oversized, ''), /127 UTF-8 bytes/);
    const truncatedButton = await acquiredButton('123', { truncated: true });
    assert.throws(() => mqttButtons.selectField(truncatedButton, ''), /127 UTF-8 bytes/);
    assert.throws(() => mqttButtons.selectField(truncatedButton, 'value'), /complete JSON/);
    const malformedButton = await acquiredButton('{"value":');
    assert.strictEqual(mqttButtons.selectField(malformedButton, '').sample, '{"value":');
    assert.throws(() => mqttButtons.selectField(malformedButton, 'value'), /complete JSON/);

    // Per-series tokens and backfill are independent of ordering and display-unit overrides.
    context.window.__device_caps = { ha_history: true };
    const mixed = candidateSession(makeCandidate(temperature), historyBuilder);
    mixed.draft.sources.push(context.padButtonBuilderSource(historyBuilder, mqttTemperature, 1),
        context.padButtonBuilderSource(historyBuilder, cpu, 2));
    let mixedButton = context.padButtonBuilderBuild(mixed);
    assert.strictEqual(mixedButton.widget_data_binding, '[ha:sensor.living_room_temperature]');
    assert.strictEqual(mixedButton.widget_data_binding_2, '[mqtt:sensors/room;temperature]');
    assert.strictEqual(mixedButton.widget_data_binding_3, '[health:cpu]');
    assert.strictEqual(mixedButton.widget_sparkline_current_label_2, '[mqtt:sensors/room;temperature;%.1f]');
    assert.strictEqual(mixedButton.widget_sparkline_current_label_3, '[health:cpu;%ld] %');
    assert.strictEqual(mixedButton.widget_sparkline_ha_entity, temperature.entity_id);
    assert.strictEqual(mixedButton.widget_sparkline_unified_scale, false);
    for (const suffix of ['_2', '_3']) {
        assert.strictEqual(Object.hasOwn(mixedButton, 'widget_sparkline_ha_entity' + suffix), false);
        assert.strictEqual(Object.hasOwn(mixedButton, 'widget_sparkline_ha_stat' + suffix), false);
    }
    mixed.draft.sources.forEach(source => { source.options.unit = '%'; });
    assert.strictEqual(context.padButtonBuilderBuild(mixed).widget_sparkline_unified_scale, false);
    mixed.draft.options.scale = 'shared';
    assert.strictEqual(context.padButtonBuilderBuild(mixed).widget_sparkline_unified_scale, true);
    mixed.draft.options.scale = 'independent';
    assert.strictEqual(context.padButtonBuilderBuild(mixed).widget_sparkline_unified_scale, false);
    mixed.draft.options.scale = 'auto';
    mixed.draft.sources = [
        context.padButtonBuilderSource(historyBuilder, cpu, 0),
        makeSource(historyBuilder, humidity, 'Humidity', 1)
    ];
    mixedButton = context.padButtonBuilderBuild(mixed);
    assert.strictEqual(mixedButton.widget_sparkline_unified_scale, true, 'Known matching HA/built-in units share a scale');
    assert.strictEqual(Object.hasOwn(mixedButton, 'widget_sparkline_ha_entity'), false);
    assert.strictEqual(mixedButton.widget_sparkline_ha_entity_2, humidity.entity_id);
    const noDecimalSource = context.padButtonBuilderSource(historyBuilder, cpu, 0,
        { candidate: mqttTemperature, options: { decimals: 6, unit: 'leaked', color: '#123456', entity_id: 'bad' } });
    assert.deepStrictEqual(JSON.parse(JSON.stringify(noDecimalSource.options)), { unit: '%', color: '#123456' });
    const backToMqtt = context.padButtonBuilderSource(historyBuilder, mqttTemperature, 0, noDecimalSource);
    assert.strictEqual(backToMqtt.options.unit, '');
    assert.strictEqual(backToMqtt.options.decimals, 1);
    const longTopic = await acquiredButton('0', {}, 's'.repeat(60));
    assert.throws(() => context.padButtonBuilderBuild(candidateSession(mqttButtons.selectField(longTopic, ''), historyBuilder)),
        /63 UTF-8 byte/);
    assert.throws(() => mqttButtons.buildBinding(mqttTemperature, { format: 'x'.repeat(24) }), /24 UTF-8/);

    // Failed, canceled, and superseded acquisitions cannot mutate the prior valid draft.
    context.padButtonBuilderRenderFields = () => {};
    context.padButtonBuilderRenderChoices = () => {};
    const acquisitionSession = candidateSession(cpu, historyBuilder);
    acquisitionSession.provider = mqttButtons;
    acquisitionSession.dialog = buttonDialog;
    context.testButtonSession = acquisitionSession;
    vm.runInContext('padButtonBuilderSession = testButtonSession', context);
    const priorDraft = JSON.stringify(context.padButtonBuilderBuild(acquisitionSession));
    for (const error of ['timeout', 'subscribe_failed', 'expired', 'broker_disconnected']) {
        context.padButtonBuilderPickSource(1);
        buttonSearch.value = 'sensors/failure';
        let deleted = false;
        context.fetch = async (url, options = {}) => {
            if (options.method === 'POST') return mqttResponse(202, { handle: 92 });
            if (options.method === 'DELETE') {
                deleted = true;
                return mqttResponse(202, { status: 'release_pending' });
            }
            return mqttResponse(200, { status: 'failed', error });
        };
        await context.padButtonBuilderLookup(true);
        assert.strictEqual(deleted, true);
        assert.strictEqual(JSON.stringify(context.padButtonBuilderBuild(acquisitionSession)), priorDraft);
        assert.strictEqual(sourceNodes['[data-bb-create]'].disabled, true);
        assert.strictEqual(buttonStatus.dataset.kind, 'error');
    }
    context.padButtonBuilderPickSource(1);
    buttonSearch.value = 'sensors/late';
    let resolveLate;
    let lateReleased = 0;
    context.fetch = async (url, options = {}) => {
        if (options.method === 'POST') return mqttResponse(202, { handle: 93 });
        if (options.method === 'DELETE') {
            lateReleased++;
            return mqttResponse(202, { status: 'release_pending' });
        }
        return new Promise(resolve => { resolveLate = resolve; });
    };
    const lateAcquisition = context.padButtonBuilderLookup(true);
    await new Promise(resolve => setImmediate(resolve));
    context.padButtonBuilderPickSource(1);
    resolveLate(mqttResponse(200, { status: 'received', received: true, sample: '0' }));
    await lateAcquisition;
    assert.strictEqual(lateReleased, 1);
    assert.strictEqual(acquisitionSession.pendingCandidate, undefined);
    assert.strictEqual(JSON.stringify(context.padButtonBuilderBuild(acquisitionSession)), priorDraft);
    context.padButtonBuilderPickSource(1);
    buttonSearch.value = 'sensors/cleanup';
    for (const status of [202, 503]) {
        context.fetch = async (url, options = {}) => {
            if (options.method === 'POST') return mqttResponse(202, { handle: 94 });
            if (options.method === 'DELETE') {
                return mqttResponse(status, { status: 'cleanup_failed', error: 'unsubscribe_failed' }, status === 202);
            }
            return mqttResponse(200, { status: 'received', received: true, sample: '0' });
        };
        await context.padButtonBuilderLookup(true);
        assert.strictEqual(acquisitionSession.pendingCandidate, undefined);
        assert.strictEqual(JSON.stringify(context.padButtonBuilderBuild(acquisitionSession)), priorDraft);
        assert.strictEqual(buttonStatus.dataset.kind, 'error');
    }
    context.padButtonBuilderStopLookup();
    assert.throws(() => context.padButtonBuilderAcceptCandidate(acquisitionSession,
        mqttButtons.selectField(objectSample, 'active')), /numeric/);
    assert.strictEqual(JSON.stringify(context.padButtonBuilderBuild(acquisitionSession)), priorDraft);
    context.padButtonBuilderAcceptCandidate(acquisitionSession, mqttTemperature);
    assert.strictEqual(acquisitionSession.draft.sources.length, 2);
    assert.strictEqual(acquisitionSession.draft.sources[1].options.unit, '');
    assert.strictEqual(acquisitionSession.sourceIndex, undefined);
    const controlReplacement = candidateSession(makeCandidate(light), toggleBuilder);
    controlReplacement.provider = catalogButtons;
    controlReplacement.dialog = buttonDialog;
    controlReplacement.sourceIndex = 0;
    context.testButtonSession = controlReplacement;
    vm.runInContext('padButtonBuilderSession = testButtonSession', context);
    context.padButtonBuilderAcceptCandidate(controlReplacement, cpu);
    const withoutControl = context.padButtonBuilderBuild(controlReplacement);
    assert.strictEqual(controlReplacement.scenario.id, 'value');
    assert.strictEqual(withoutControl.label_center, '[health:cpu;%ld]');
    for (const key of ['actions', 'fg_color', 'icon_id', 'widget_sparkline_ha_entity']) {
        assert.strictEqual(Object.hasOwn(withoutControl, key), false);
    }
    assert.deepStrictEqual(Object.keys(controlReplacement.draft.options).sort(), ['title', 'unit']);

    // Catalog source selection configures parameters without acquiring a sample first.
    const parameters = { hidden: true, setAttribute() { this.hidden = true; } };
    sourceNodes['[data-bb-parameters]'] = parameters;
    const configureSelection = catalogButtons.configureSelection;
    let configuredReference;
    let chooseCatalog;
    let stoppedConfigurations = 0;
    catalogButtons.configureSelection = (root, reference, onUse, screen) => {
        assert.strictEqual(root, parameters);
        assert.strictEqual(screen, 'pad_0');
        configuredReference = reference;
        chooseCatalog = onUse;
        return () => { stoppedConfigurations++; };
    };
    context.fetch = () => { throw new Error('Catalog selection must not fetch without an explicit preview'); };
    controlReplacement.sourceIndex = 0;
    const musicReference = catalogReference('music', 'title');
    await context.padButtonBuilderLookup(true, { reference: musicReference });
    assert.strictEqual(configuredReference, musicReference);
    assert.strictEqual(parameters.hidden, false);
    assert.strictEqual(context.padButtonBuilderBuild(controlReplacement).label_center, '[health:cpu;%ld]');
    context.padButtonBuilderStopLookup();
    assert.strictEqual(stoppedConfigurations, 1);
    assert.strictEqual(parameters.hidden, true);
    await context.padButtonBuilderLookup(true, { reference: musicReference });
    chooseCatalog(context.catalogBindingCandidate(musicReference));
    assert.strictEqual(stoppedConfigurations, 2);
    assert.strictEqual(parameters.hidden, true);
    assert.strictEqual(context.padButtonBuilderBuild(controlReplacement).label_center, '[music:title]');
    catalogButtons.configureSelection = configureSelection;
    context.padButtonBuilderRenderFields = renderFields;
    context.padButtonBuilderRenderChoices = renderChoices;
    vm.runInContext('padButtonBuilderSession = null', context);

    console.log('portal binding help tests passed');
})().catch(error => {
    console.error(error);
    process.exit(1);
});
