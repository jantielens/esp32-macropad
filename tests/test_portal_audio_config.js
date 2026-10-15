const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

const elements = {
    'audio-section': { style: {} },
    'amplifier-section': { style: {} },
    audio_volume: { value: '', disabled: false },
    audio_volume_value: { textContent: '' },
    audio_idle_mute: { type: 'checkbox', checked: true, disabled: true },
    audio_wake_delay_ms: { value: '', disabled: true }
};
let stored;
let submitted;
let pendingReboot = false;
const messages = [];
const context = {
    console,
    window: {},
    API_CONFIG: '/api/config',
    document: {
        getElementById(id) { return elements[id] || null; },
        querySelectorAll() { return []; },
        querySelector(selector) {
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
        stored = Object.assign({}, stored, submitted);
        return { ok: true, json: async () => ({ success: true }) };
    }
};
vm.createContext(context);
for (const file of ['portal_config.js', 'portal_fragment_init.js']) {
    vm.runInContext(fs.readFileSync('src/app/web/' + file, 'utf8'), context);
}

(async function () {
    stored = { audio_volume: 70, audio_idle_mute: true, audio_wake_delay_ms: 120,
        caps: { audio_amplifier_mute: true } };
    await context.loadConfig();
    assert.strictEqual(elements.audio_idle_mute.checked, true);
    assert.strictEqual(Number(elements.audio_wake_delay_ms.value), 120);
    assert.strictEqual(elements['amplifier-section'].style.display, '');
    assert.strictEqual(elements.audio_wake_delay_ms.disabled, false);
    assert.deepStrictEqual(messages, []);

    for (const value of [0, 35, 120, 1000]) {
        elements.audio_wake_delay_ms.value = String(value);
        submitted = null;
        await context.saveFragmentConfig(false);
        assert.deepStrictEqual(submitted, { audio_volume: 70, audio_idle_mute: true,
            audio_wake_delay_ms: String(value) });
        await context.loadConfig();
        assert.strictEqual(Number(elements.audio_wake_delay_ms.value), value);
        assert.strictEqual(pendingReboot, false);
    }

    elements.audio_idle_mute.checked = false;
    context.updateAmplifierSettings();
    assert.strictEqual(elements.audio_wake_delay_ms.disabled, true);
    await context.saveFragmentConfig(false);
    assert.strictEqual(submitted.audio_idle_mute, false);
    assert.strictEqual(submitted.audio_wake_delay_ms, undefined);
    await context.loadConfig();
    assert.strictEqual(elements.audio_idle_mute.checked, false);
    assert.strictEqual(Number(elements.audio_wake_delay_ms.value), 1000);
    elements.audio_idle_mute.checked = true;
    context.updateAmplifierSettings();
    assert.strictEqual(elements.audio_wake_delay_ms.disabled, false);
    assert.strictEqual(Number(elements.audio_wake_delay_ms.value), 1000);

    for (const value of [-1, 1001, 65536, 1.5, '', ' ', '12junk', '1e2', null, false, {}, []]) {
        assert.strictEqual(context.validateConfig({ audio_wake_delay_ms: value }).valid, false, String(value));
    }
    elements.audio_wake_delay_ms.value = '-1';
    submitted = null;
    await context.saveFragmentConfig(false);
    assert.strictEqual(submitted, null);
    assert.strictEqual(messages.pop().type, 'error');

    for (const caps of [{ audio_amplifier_mute: false }, {}]) {
        stored = { audio_volume: 70, caps };
        await context.loadConfig();
        assert.strictEqual(elements['amplifier-section'].style.display, 'none');
        assert.strictEqual(elements.audio_idle_mute.disabled, true);
        assert.strictEqual(elements.audio_wake_delay_ms.disabled, true);
        await context.saveFragmentConfig(false);
        assert.deepStrictEqual(submitted, { audio_volume: 70 });
    }
    delete elements['amplifier-section'];
    await context.loadConfig();
    assert(!messages.some(message => message.type === 'error'));
    console.log('portal_audio_config: PASS');
})().catch(error => { console.error(error); process.exit(1); });