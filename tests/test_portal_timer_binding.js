const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

const context = { console };
vm.createContext(context);
vm.runInContext(fs.readFileSync('src/app/web/portal_binding_validator.js', 'utf8'), context);

for (const token of [
    '[timer:1]',
    '[timer:1;ss]',
    '[timer:1_state]',
    '[timer:1_mode]',
    '[timer:1_expired]',
    '[timer:1_target]'
]) {
    assert.strictEqual(context.validateBinding(token).valid, true, token);
}

const invalid = context.validateBinding('[timer:1_unknown]');
assert.strictEqual(invalid.valid, false);
assert.match(invalid.errors[0].message, /_target/);

for (const token of ['[stt:status]', '[stt:text]']) {
    assert.strictEqual(context.validateBinding(token).valid, true, token);
}

const invalidStt = context.validateBinding('[stt:result]');
assert.strictEqual(invalidStt.valid, false);
assert.match(invalidStt.errors[0].message, /speech-to-text key/);

console.log('portal_timer_binding: PASS');