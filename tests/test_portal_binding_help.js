const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

const fixture = JSON.parse(fs.readFileSync('tools/mock-data/binding-docs.json', 'utf8'));
const context = {
    console,
    document: { addEventListener() {} },
    fetch() {
        return Promise.resolve({ ok: true, json: () => Promise.resolve(fixture) });
    }
};
vm.createContext(context);
vm.runInContext(fs.readFileSync('src/app/web/portal_binding_validator.js', 'utf8'), context);
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
    console.log('portal binding help tests passed');
})().catch(error => {
    console.error(error);
    process.exit(1);
});
