const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

const context = {
    console,
    fetch() {
        return Promise.resolve({
            ok: true,
            json() {
                return Promise.resolve({
                    schemes: [
                        {
                            name: 'timer', min_params: 1, max_params: 2,
                            widget_max_params: 1, format_param: 1,
                            validation_mode: 0, free_form: false,
                            keys: [
                                '1', '1_state', '1_mode', '1_expired', '1_target',
                                '2', '2_state', '2_mode', '2_expired', '2_target',
                                '3', '3_state', '3_mode', '3_expired', '3_target'
                            ]
                        },
                        {
                            name: 'health', min_params: 1, max_params: 2,
                            widget_max_params: 1, format_param: 1,
                            validation_mode: 0, free_form: false,
                            keys: ['cpu_core_0', 'cpu_core_1']
                        }
                    ]
                });
            }
        });
    }
};
vm.createContext(context);
vm.runInContext(fs.readFileSync('src/app/web/portal_binding_validator.js', 'utf8'), context);

(async function() {
    await context.bindingLoadSchema();

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
    assert.match(invalid.errors[0].message, /Unknown binding key/);

    console.log('portal_timer_binding: PASS');
})().catch(function(error) {
    console.error(error);
    process.exitCode = 1;
});