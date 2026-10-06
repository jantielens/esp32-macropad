const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

const fragment = fs.readFileSync('src/app/web/screensaver.fragment.html', 'utf8');
const elements = {};
function makeElement(type = '') {
    let value = '';
    return {
        type, disabled: false, checked: false, hidden: false,
        textContent: '', style: {}, dataset: {}, options: [], listeners: {},
        get value() { return value; },
        set value(next) { value = String(next); },
        get selectedIndex() { return this.options.findIndex(option => option.value === value); },
        appendChild(option) { this.options.push(option); },
        addEventListener(event, handler) { this.listeners[event] = handler; }
    };
}
for (const match of fragment.matchAll(/<[^>]+id="([^"]+)"[^>]*>/g)) {
    const type = match[0].match(/type="([^"]+)"/);
    elements[match[1]] = makeElement(type ? type[1] : '');
}
elements.idle_screen_pad.options.push({ value: '', textContent: 'None' });
let stored;
let submitted;
let loads = 0;
const messages = [];
const context = {
    console, window: {}, API_CONFIG: '/api/config', deviceInfoCache: null,
    document: {
        getElementById(id) { return elements[id] || null; },
        createElement() { return makeElement(); },
        querySelectorAll() { return []; },
        querySelector(selector) {
            const match = selector.match(/^\[name="([^"]+)"\]$/);
            return match ? elements[match[1]] || null : null;
        }
    },
    showMessage(message, type) { messages.push({ message, type }); },
    getDeviceInfo: async () => ({ available_screens: [
        { id: 'info', name: 'Device Info' },
        { id: 'pad_0', name: 'Living Room' }, { id: 'pad_1', name: 'Clock' }
    ] }),
    async fetch(url, options) {
        if (!options) {
            assert.strictEqual(url, '/api/config');
            ++loads;
            return { ok: true, json: async () => stored };
        }
        assert.strictEqual(url, '/api/config?no_reboot=1');
        assert.strictEqual(options.method, 'POST');
        submitted = JSON.parse(options.body);
        Object.assign(stored, submitted);
        return { ok: true, json: async () => ({ success: true }) };
    }
};
vm.createContext(context);
for (const file of ['portal_config.js', 'portal_fragment_init.js']) {
    vm.runInContext(fs.readFileSync('src/app/web/' + file, 'utf8'), context);
}

(async function () {
    for (const keepPanelAwake of [false, true]) {
        stored = {
            screen_saver_backlight_only: false, screen_saver_keeps_panel_awake: keepPanelAwake,
            screen_saver_enabled: true, screen_saver_timeout_seconds: 300,
            screen_saver_fade_out_ms: 800, screen_saver_fade_in_ms: 400,
            screen_saver_wake_on_touch: true, screen_saver_wake_binding: '[mqtt:presence]',
            idle_screen_enabled: true, idle_screen_timeout_seconds: 60, idle_screen_pad: 'pad_1',
            caps: { display: true, touch: true }
        };
        elements.idle_screen_pad.options.splice(1);
        await context.window.init_screensaver_fragment();
        await context.loadConfig();
        assert.deepStrictEqual(elements.idle_screen_pad.options.map(option => option.value), ['', 'pad_0', 'pad_1']);
        assert.strictEqual(elements.idle_screen_pad.value, 'pad_1');
        assert.strictEqual(elements['screensaver-timeline-idle-title'].textContent, 'Clock');
        assert.strictEqual(elements['screensaver-timeline-rail'].dataset.stageCount, 2);
        assert.strictEqual(elements['screensaver-sleep-heading'].textContent, keepPanelAwake ? 'Backlight Off' : 'Display Sleep');
        assert.strictEqual(elements['screensaver-timeline-sleep-title'].textContent, keepPanelAwake ? 'Backlight off' : 'Display off');
        for (const field of ['screen_saver_enabled', 'screen_saver_fade_out_ms', 'screen_saver_fade_in_ms']) {
            assert.strictEqual(elements[field].disabled, false);
            assert.strictEqual(elements[field].hidden, false);
        }
        assert.strictEqual(elements.screen_saver_fade_out_ms.value, '800');
        assert.strictEqual(elements.screen_saver_fade_in_ms.value, '400');
        elements.screen_saver_fade_out_ms.value = '1500';
        elements.screen_saver_fade_in_ms.value = '750';
        elements.screen_saver_enabled.checked = false;
        context.window.screensaverTimelineUpdate();
        assert.strictEqual(elements['screensaver-timeline-sleep'].hidden, true);
        assert.strictEqual(elements['screensaver-timeline-rail'].dataset.stageCount, 1);
        await context.saveFragmentConfig(false);
        assert.strictEqual(submitted.screen_saver_enabled, false);
        assert.strictEqual(submitted.screen_saver_fade_out_ms, '1500');
        assert.strictEqual(submitted.screen_saver_fade_in_ms, '750');
        assert.strictEqual(submitted.screen_saver_timeout_seconds, '300');
        assert.strictEqual(submitted.idle_screen_pad, 'pad_1');
        assert.strictEqual(submitted.screen_saver_wake_on_touch, true);
        assert.strictEqual(submitted.screen_saver_wake_binding, '[mqtt:presence]');
        assert.strictEqual(submitted.screen_saver_keeps_panel_awake, undefined);
        await context.loadConfig();
        assert.strictEqual(elements.screen_saver_enabled.checked, false);
        assert.strictEqual(elements.screen_saver_fade_out_ms.value, '1500');
        assert.strictEqual(elements.screen_saver_fade_in_ms.value, '750');
        elements.screen_saver_enabled.checked = true;
        elements.idle_screen_enabled.checked = false;
        context.window.screensaverTimelineUpdate();
        assert.strictEqual(elements['screensaver-timeline-sleep'].hidden, false);
        assert.match(elements['screensaver-timeline-summary'].textContent, keepPanelAwake ? /controller stays active/ : /display turns off/);
        elements.screen_saver_timeout_seconds.value = '0';
        context.window.screensaverTimelineUpdate();
        assert.strictEqual(elements['screensaver-timeline-sleep'].hidden, true);
    }
    let resolveInfo;
    context.getDeviceInfo = () => new Promise(resolve => { resolveInfo = resolve; });
    const pending = context.window.init_screensaver_fragment();
    const previousLoads = loads;
    elements['screensaver-save-btn'] = makeElement();
    resolveInfo({ available_screens: [{ id: 'pad_2', name: 'Stale pad' }] });
    await pending;
    assert.strictEqual(loads, previousLoads);
    assert(!elements.idle_screen_pad.options.some(option => option.value === 'pad_2'));
    assert(!messages.some(message => message.type === 'error'), JSON.stringify(messages));
    console.log('portal_screensaver: PASS');
})().catch(error => {
    console.error(error);
    process.exitCode = 1;
});