const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

const fixture = JSON.parse(fs.readFileSync('tools/mock-data/binding-docs.json', 'utf8'));
const context = {
    console,
    TextEncoder,
    AbortController,
    DOMException,
    setTimeout,
    clearTimeout,
    document: { addEventListener() {} },
    fetch() {
        return Promise.resolve({ ok: true, json: () => Promise.resolve(fixture) });
    }
};
vm.createContext(context);
vm.runInContext(fs.readFileSync('src/app/web/portal_binding_validator.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_binding_builder.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_binding_builder_ha.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_binding_help.js', 'utf8'), context);

const scheme = name => fixture.schemes.find(s => s.name === name);

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
helpElements['binding-docs-search'].value = 'temperature';
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
        mode: 'raw', decimals: 1, unit: '', fallback: '', detail: null,
        controller: null, timer: null, generation: generation + 1
    });
    pending[2]({ entities: [], more: false });
    await closingLookup;
    assert.match(suggestions.innerHTML, /^$/);
    console.log('portal binding help tests passed');
})().catch(error => {
    console.error(error);
    process.exit(1);
});
