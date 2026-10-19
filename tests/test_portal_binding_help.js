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
vm.runInContext(fs.readFileSync('src/app/web/portal_button_builder.js', 'utf8'), context);

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

const helpElements = {
    'binding-help-overlay': { style: {} },
    'binding-docs-nav': { innerHTML: '' },
    'binding-docs-body': { innerHTML: '', scrollTop: 0 },
    'binding-docs-search': { value: '', focus() {} },
    'binding-docs-count': { textContent: '' }
};
context.document.getElementById = id => helpElements[id] || null;
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
assert.match(helpElements['binding-docs-body'].innerHTML, /id="ha-binding-builder"/);
assert.match(helpElements['binding-docs-body'].innerHTML, /data-bh-view="ha">full syntax and examples/);
assert.ok(!helpElements['binding-docs-body'].innerHTML.includes('binding-docs-signature'));
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
    tagName: 'INPUT', type: 'text', isConnected: true
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
context.bindingHelpInsertText(shortcutField, '[ha:sensor.temperature]', true);
assert.strictEqual(shortcutField.value, 'Before [ha:sensor.temperature] after');
vm.runInContext('_bindingHelp.target = null; _bindingHelp.pointer = null', context);
context.showBindingHelp();
context.window.deviceConfig = haConfigured;
context.showBindingBuilder('ha');
assert.strictEqual(vm.runInContext('_bindingHelp.view', context), 'builder:ha');
assert.match(helpElements['binding-docs-body'].innerHTML, /id="ha-binding-builder"/);
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
    const temperature = { entity_id: 'sensor.living_room_temperature', state: '21.37', unit: '\u00b0C', paths: [] };
    const light = { entity_id: 'light.kitchen', state: 'on', unit: '', paths: [] };
    const buildSession = (detail, builder) => ({
        col: 2, row: 1, page: 0, detail, scenario: builder,
        options: { ...builder.defaults(detail), title: 'Living room' }
    });
    const valueSession = buildSession(temperature, valueBuilder);
    const generatedValue = context.padButtonBuilderBuild(valueSession);
    assert.deepStrictEqual(JSON.parse(JSON.stringify(generatedValue)), {
        col: 2, row: 1, label_top: 'Living room',
        label_center: '[ha:sensor.living_room_temperature;;%.1f]', label_bottom: '\u00b0C'
    });
    assert.strictEqual(valueBuilder.preview(temperature, valueSession.options).label_center, '21.4');
    assert.strictEqual(valueBuilder.preview({ ...temperature, state: 'unavailable' }, valueSession.options).label_center, '---');
    const textSession = buildSession({ ...temperature, state: 'Ready', unit: '' }, valueBuilder);
    assert.strictEqual(context.padButtonBuilderBuild(textSession).label_center, '[ha:sensor.living_room_temperature;;%s]');
    valueSession.options.decimals = 7;
    assert.throws(() => context.padButtonBuilderBuild(valueSession), /decimal/);
    valueSession.options.decimals = 1;
    valueSession.options.title = '\u00b0'.repeat(96);
    assert.throws(() => context.padButtonBuilderBuild(valueSession), /UTF-8/);
    valueSession.options.title = 'Living room';
    const lightSession = buildSession(light, toggleBuilder);
    const generatedLight = context.padButtonBuilderBuild(lightSession);
    assert.strictEqual(generatedLight.icon_id, 'mi_lightbulb');
    assert.strictEqual(generatedLight.label_top_style, 'color:#000000', 'Keep the title legible on light button backgrounds');
    assert.strictEqual(generatedLight.fg_color, '[expr:[ha:light.kitchen]=="on"?"#FFC857":"#64748B"|#EF6B73]');
    assert.deepStrictEqual(JSON.parse(JSON.stringify(generatedLight.actions)), [
        { type: 'ha_service', entity_id: 'light.kitchen', service: 'toggle' }
    ]);
    assert.strictEqual(toggleBuilder.available(temperature), false);
    assert.strictEqual(toggleBuilder.available(light), true);
    assert.strictEqual(toggleBuilder.available({ ...light, entity_id: 'binary_sensor.door' }), false);
    const switchDetail = { ...light, entity_id: 'switch.desk_fan', state: 'off' };
    assert.strictEqual(toggleBuilder.available(switchDetail), true);
    const switchSession = buildSession(switchDetail, toggleBuilder);
    const generatedSwitch = context.padButtonBuilderBuild(switchSession);
    assert.strictEqual(generatedSwitch.icon_id, 'mi_toggle_on');
    assert.strictEqual(generatedSwitch.fg_color, '[expr:[ha:switch.desk_fan]=="on"?"#FFC857":"#64748B"|#EF6B73]');
    assert.deepStrictEqual(JSON.parse(JSON.stringify(generatedSwitch.actions)), [
        { type: 'ha_service', entity_id: 'switch.desk_fan', service: 'toggle' }
    ]);
    const catalogEntry = context.actionEditorCatalogEntry;
    context.actionEditorCatalogEntry = () => null;
    assert.strictEqual(toggleBuilder.available(switchDetail), false);
    assert.strictEqual(toggleBuilder.available(light), false);
    context.actionEditorCatalogEntry = catalogEntry;
    const previewColors = ['on', 'off', 'unavailable'].map(state => toggleBuilder.preview(light, lightSession.options, state).fg_color);
    assert.strictEqual(new Set(previewColors).size, 3);
    const longLight = { ...light, entity_id: 'light.' + 'a'.repeat(41) };
    assert.ok(context.padButtonBuilderBuild(buildSession(longLight, toggleBuilder)));
    assert.throws(() => context.padButtonBuilderBuild(buildSession({ ...longLight, entity_id: longLight.entity_id + 'a' }, toggleBuilder)), /48/);
    lightSession.options.icon = 'not an icon';
    assert.throws(() => context.padButtonBuilderBuild(lightSession), /Symbol/);

    // Bar inputs stay raw; only the bottom label gets display formatting.
    context.deviceInfoCache = { widget_catalog: [{ type: 'bar_chart' }] };
    assert.strictEqual(barBuilder.available(temperature), true);
    for (const detail of [light, { ...temperature, state: 'unavailable' },
        { ...temperature, state_truncated: true }]) {
        assert.strictEqual(barBuilder.available(detail), false);
    }
    context.deviceInfoCache.widget_catalog = [];
    assert.strictEqual(barBuilder.available(temperature), false);
    context.deviceInfoCache.widget_catalog = [{ type: 'bar_chart' }];
    const barSession = buildSession(temperature, barBuilder);
    assert.strictEqual(barSession.options.min, 0);
    assert.strictEqual(barSession.options.max, 50);
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
    Object.assign(barSession.options, { min: '-10.5', max: '40.5', orientation: 'horizontal', decimals: 2 });
    const generatedBar = context.padButtonBuilderBuild(barSession);
    assert.deepStrictEqual(JSON.parse(JSON.stringify(generatedBar)), {
        col: 2, row: 1, label_top: 'Living room', widget_type: 'bar_chart',
        widget_data_binding: '[ha:sensor.living_room_temperature]',
        widget_bar_min: -10.5, widget_bar_max: 40.5,
        widget_orientation: 'horizontal', widget_bar_zero_centered: true,
        label_bottom: '[ha:sensor.living_room_temperature;;%.2f \u00b0C]'
    });
    assert.strictEqual(barBuilder.preview(temperature, barSession.options).label_bottom, '21.37 \u00b0C');
    for (const state of ['unknown', 'unavailable', '']) {
        assert.strictEqual(barBuilder.preview({ ...temperature, state }, barSession.options).label_bottom, '---');
    }
    const percentSession = buildSession({ ...temperature, unit: ' % ' }, barBuilder);
    const generatedPercentBar = context.padButtonBuilderBuild(percentSession);
    assert.strictEqual(generatedPercentBar.widget_bar_min, 0);
    assert.strictEqual(generatedPercentBar.widget_bar_max, 100);
    assert.strictEqual(generatedPercentBar.widget_orientation, 'vertical');
    assert.strictEqual(generatedPercentBar.widget_bar_zero_centered, false);
    assert.strictEqual(generatedPercentBar.label_bottom, '[ha:sensor.living_room_temperature;;%.1f %%]');
    assert.strictEqual(barBuilder.preview(temperature, percentSession.options).label_bottom, '21.4 %');
    for (const [min, max] of [['', 100], [0, ' '], ['12abc', 100], [0, 'Infinity']]) {
        Object.assign(barSession.options, { min, max });
        assert.throws(() => context.padButtonBuilderBuild(barSession), /minimum and maximum/);
    }
    for (const [min, max] of [[1, 1], [2, 1], [0, '1e99'], [1, 1.000000001], ['-3e38', '3e38']]) {
        Object.assign(barSession.options, { min, max });
        assert.throws(() => context.padButtonBuilderBuild(barSession), /greater than minimum/);
    }
    Object.assign(barSession.options, { min: 0, max: 0.5 });
    assert.strictEqual(context.padButtonBuilderBuild(barSession).widget_bar_max, 0.5);
    barSession.options.orientation = 'diagonal';
    assert.throws(() => context.padButtonBuilderBuild(barSession), /orientation/);
    barSession.options.orientation = 'vertical';
    barSession.options.decimals = 7;
    assert.throws(() => context.padButtonBuilderBuild(barSession), /decimal/);
    barSession.options.decimals = 1;
    barSession.options.unit = '[invalid]';
    assert.throws(() => context.padButtonBuilderBuild(barSession), /delimiters/);

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
    valueSession.detail = null;
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
        controller: null, timer: null, generation: generation + 1
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
    const buttonDialog = { querySelector: selector => ({
        '[data-bb-status]': buttonStatus, '[data-bb-retry]': buttonRetry,
        '#button-builder-search': buttonSearch, '[data-bb-matches]': matches
    })[selector] };
    context.testButtonSession = { dialog: buttonDialog };
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
    vm.runInContext('padButtonBuilderSession = null', context);
    console.log('portal binding help tests passed');
})().catch(error => {
    console.error(error);
    process.exit(1);
});
