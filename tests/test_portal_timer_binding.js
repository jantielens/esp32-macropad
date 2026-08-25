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

for (const token of ['[health:cpu_core_0]', '[health:cpu_core_1]']) {
    assert.strictEqual(context.validateBinding(token).valid, true, token);
}

const invalid = context.validateBinding('[timer:1_unknown]');
assert.strictEqual(invalid.valid, false);
assert.match(invalid.errors[0].message, /_target/);

console.log('portal_timer_binding: PASS');