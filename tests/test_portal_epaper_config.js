const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

function element(value = '') {
    return { value, disabled: false, listeners: {}, textContent: '',
        addEventListener(name, listener) { this.listeners[name] = listener; },
        checkValidity() { return true; },
        reportValidity() { this.reported = true; } };
}
const elements = {};
const modes = ['bw', 'grayscale'].map(value => Object.assign(element(value), { type: 'radio', checked: value === 'bw' }));
for (const id of ['epaper_vcom_mv', 'epaper-vcom-volts', 'epaper-vcom-default',
    'epaper-vcom-default-btn', 'epaper_bw_conversion', 'epaper_bw_threshold',
    'epaper-bw-threshold-range', 'epaper-bw-threshold-controls', 'epaper-refresh-save-btn']) {
    elements[id] = element();
}
let stored, submitted, pendingReboot;
const messages = [];
const context = {
    console, window: {
        __extra_config_fields: [],
        registerConfigFields(fields) { this.__extra_config_fields.push(...fields); }
    },
    API_CONFIG: '/api/config',
    document: {
        getElementById(id) { return elements[id] || null; },
        querySelectorAll(selector) {
            return selector.includes('input') && selector.includes('epaper_render_mode') ? modes : [];
        },
        querySelector(selector) {
            if (selector.includes('input') && selector.includes('epaper_render_mode')) {
                const value = selector.match(/\[value="([^"]+)"\]/);
                return modes.find(mode => value ? mode.value === value[1] : mode.checked) || null;
            }
            const match = selector.match(/^\[name="([^"]+)"\]$/);
            return match ? elements[match[1]] || null : null;
        }
    },
    showMessage(message, type) { messages.push({ message, type }); },
    setPendingReboot() { pendingReboot = true; },
    async fetch(url, options) {
        if (!options) {
            assert.strictEqual(url, '/api/config');
            return { ok: true, json: async () => stored };
        }
        assert.strictEqual(url, '/api/config?no_reboot=1');
        submitted = JSON.parse(options.body);
        stored = { ...stored, ...submitted };
        return { ok: true, json: async () => ({ success: true }) };
    }
};
vm.createContext(context);
for (const file of ['portal_config.js', 'portal_fragment_init.js', 'portal_epaper_refresh.js']) {
    vm.runInContext(fs.readFileSync('src/app/web/' + file, 'utf8'), context);
}
const settle = () => new Promise(resolve => setImmediate(resolve));

(async function () {
    async function load(vcom, boardDefault) {
        stored = { epaper_render_mode: 'bw', epaper_bw_conversion: 'threshold',
            epaper_bw_threshold: 128, epaper_vcom_mv: vcom, epaper_default_vcom_mv: boardDefault };
        submitted = null;
        pendingReboot = false;
        context.window.init_epaper_refresh_fragment();
        await settle();
    }
    async function save(volts) {
        elements['epaper-vcom-volts'].value = volts;
        elements['epaper-refresh-save-btn'].listeners.click();
        await settle();
    }
    await load(0, 0);
    assert.match(elements['epaper-vcom-default'].textContent, /no specified default/);
    await save('0');
    assert.strictEqual(submitted.epaper_vcom_mv, '0');
    assert.strictEqual(pendingReboot, false);

    // The same stored zero follows a different firmware's board default.
    for (const boardDefault of [1530, 1400]) {
        await load(0, boardDefault);
        assert.strictEqual(Number(elements['epaper-vcom-volts'].value), 0);
        assert(elements['epaper-vcom-default'].textContent.includes('-' + (boardDefault / 1000).toFixed(3)));
        await save('-1.531');
        assert.strictEqual(submitted.epaper_vcom_mv, '1531');
        assert.strictEqual(pendingReboot, true);
        await load(1531, boardDefault);
        assert.strictEqual(Number(elements['epaper-vcom-volts'].value), -1.531);
        await save('-1.531');
        assert.strictEqual(pendingReboot, false);
        elements['epaper-vcom-default-btn'].listeners.click();
        await save(elements['epaper-vcom-volts'].value);
        assert.strictEqual(submitted.epaper_vcom_mv, '0');
        assert.strictEqual(pendingReboot, true);
    }
    for (const [volts, magnitude] of [['0', '0'], ['-0.001', '1'], ['-5', '5000']]) {
        await load(0, 1530);
        await save(volts);
        assert.strictEqual(submitted.epaper_vcom_mv, magnitude);
    }
    await load(0, 1530);
    elements['epaper-vcom-volts'].checkValidity = () => false;
    await save('-1.5301');
    assert.strictEqual(submitted, null);
    assert.strictEqual(elements['epaper-vcom-volts'].reported, true);
    assert(!messages.some(message => message.type === 'error'));
    console.log('portal_epaper_config: PASS');
})().catch(error => { console.error(error); process.exitCode = 1; });
