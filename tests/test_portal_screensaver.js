const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

const fragment = fs.readFileSync('src/app/web/screensaver.fragment.html', 'utf8');
let elements;
let stored;
let submitted;
let releaseLoad;
let releaseInfo;
const messages = [];
function mount() {
    elements = {};
    for (const match of fragment.matchAll(/<([a-z]+)\b([^>]*\bid="([^"]+)"[^>]*)>/g)) {
        const attributes = match[2];
        const element = {
            value: '', checked: false, disabled: false, hidden: /\bhidden\b/.test(attributes),
            dataset: {}, textContent: '', listeners: {}, options: [],
            type: (attributes.match(/\btype="([^"]+)"/) || [])[1],
            addEventListener(event, listener) { this.listeners[event] = listener; },
            appendChild(option) { this.options.push(option); },
            get selectedIndex() { return this.options.findIndex(option => option.value === this.value); }
        };
        elements[match[3]] = element;
    }
}
const context = {
    console, window: {}, API_CONFIG: '/api/config',
    deviceInfoCache: null,
    async getDeviceInfo() {
        if (releaseInfo !== undefined) await new Promise(resolve => { releaseInfo = resolve; });
        context.deviceInfoCache = { available_screens: [{ id: 'pad_0', name: 'Clock' }, { id: 'info', name: 'Info' }] };
        return context.deviceInfoCache;
    },
    document: {
        getElementById(id) { return elements[id] || null; },
        createElement() { return {}; },
        querySelectorAll() { return []; },
        querySelector(selector) {
            const match = selector.match(/^\[name="([^"]+)"\]$/);
            return match ? elements[match[1]] || null : null;
        }
    },
    showMessage(message, type) { messages.push({ message, type }); },
    async fetch(url, options) {
        if (!options) {
            assert.strictEqual(url, '/api/config');
            if (releaseLoad !== undefined) await new Promise(resolve => { releaseLoad = resolve; });
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
context.window.screensaverTimelineUpdate = undefined;
function defaults(lightOnly) {
    return {
        caps: { touch: true }, screen_saver_backlight_only: lightOnly,
        screen_saver_enabled: true, screen_saver_timeout_seconds: 300,
        screen_saver_fade_out_ms: 800, screen_saver_fade_in_ms: 400,
        screen_saver_wake_on_touch: true, screen_saver_wake_binding: '[mqtt:presence]',
        idle_screen_enabled: true, idle_screen_timeout_seconds: 60, idle_screen_pad: 'pad_0'
    };
}
async function finishLoad() {
    await new Promise(resolve => setImmediate(resolve));
}

(async function () {
    for (const lightOnly of [false, true]) {
        mount();
        stored = defaults(lightOnly);
        releaseLoad = null;
        await context.window.init_screensaver_fragment();
        assert.strictEqual(elements.idle_screen_pad.options.length, 1);
        assert.strictEqual(elements['screensaver-timeline-sleep'].hidden, true);
        releaseLoad();
        releaseLoad = undefined;
        await finishLoad();
        assert.strictEqual(elements['screensaver-settings'].dataset.backlightOnly, String(lightOnly));
        assert.strictEqual(elements['screensaver-enable-setting'].hidden, lightOnly);
        assert.strictEqual(elements['screensaver-transition-settings'].hidden, lightOnly);
        assert.strictEqual(elements['screensaver-timeline-sleep-title'].textContent, lightOnly ? 'Backlight off' : 'Display off');
        assert.strictEqual(elements['screensaver-timeline-idle-title'].textContent, 'Clock');
        assert.strictEqual(elements['screensaver-timeline-rail'].dataset.stageCount, 2);
        assert.strictEqual(elements['screensaver-timeline-sleep-time'].textContent, '5 min');
        for (const id of ['screen_saver_enabled', 'screen_saver_fade_out_ms', 'screen_saver_fade_in_ms']) {
            assert.strictEqual(elements[id].disabled, lightOnly);
        }
        for (const id of ['idle_screen_enabled', 'idle_screen_pad', 'idle_screen_timeout_seconds',
            'screen_saver_timeout_seconds', 'screen_saver_wake_on_touch', 'screen_saver_wake_binding']) {
            assert.strictEqual(elements[id].disabled, false);
        }
        await context.saveFragmentConfig(false);
        assert.strictEqual(submitted.idle_screen_pad, 'pad_0');
        assert.strictEqual(submitted.screen_saver_wake_on_touch, true);
        assert.strictEqual(submitted.screen_saver_wake_binding, '[mqtt:presence]');
        for (const id of ['screen_saver_enabled', 'screen_saver_fade_out_ms', 'screen_saver_fade_in_ms']) {
            assert.strictEqual(Object.hasOwn(submitted, id), !lightOnly);
        }
        elements.screen_saver_enabled.checked = false;
        context.window.screensaverTimelineUpdate();
        assert.strictEqual(elements['screensaver-timeline-sleep'].hidden, !lightOnly);
        elements.screen_saver_timeout_seconds.value = '0';
        elements.screen_saver_timeout_seconds.listeners.input();
        assert.strictEqual(elements['screensaver-timeline-sleep'].hidden, true);
        assert.strictEqual(elements['screensaver-timeline-idle'].hidden, false);
        assert.strictEqual(elements['screensaver-timeline-summary'].textContent, 'The idle screen remains visible until activity.');
        await context.saveFragmentConfig(false);
        await context.loadConfig();
        assert.strictEqual(Number(elements.screen_saver_timeout_seconds.value), 0);
        assert.strictEqual(Number(elements.screen_saver_fade_out_ms.value), 800);
        elements.idle_screen_enabled.checked = false;
        elements.idle_screen_enabled.listeners.change();
        assert.strictEqual(elements['screensaver-timeline-rail'].dataset.stageCount, 0);
        assert(!elements['screensaver-timeline-summary'].textContent.includes('turns off'));
        elements.screen_saver_timeout_seconds.value = '120';
        elements.screen_saver_enabled.checked = true;
        context.window.screensaverTimelineUpdate();
        assert.strictEqual(elements['screensaver-timeline-rail'].dataset.stageCount, 1);
        assert.strictEqual(elements['screensaver-timeline-sleep-time'].textContent, '2 min');
    }

    stored = defaults(true);
    stored.caps.touch = false;
    await context.loadConfig();
    assert.strictEqual(elements['screensaver-touch-setting'].hidden, true);
    assert.strictEqual(elements.screen_saver_wake_on_touch.disabled, true);
    await context.saveFragmentConfig(false);
    assert(!Object.hasOwn(submitted, 'screen_saver_wake_on_touch'));
    stored = defaults(false);
    await context.loadConfig();
    assert.strictEqual(elements.screen_saver_wake_on_touch.disabled, false);
    assert.strictEqual(elements['screensaver-transition-settings'].hidden, false);
    assert.strictEqual(elements.screen_saver_fade_out_ms.disabled, false);
    assert.strictEqual(elements['screensaver-timeline-sleep-title'].textContent, 'Display off');

    elements = {};
    await context.loadConfig();
    context.window.screensaverTimelineUpdate();
    mount();
    context.deviceInfoCache = null;
    releaseInfo = null;
    const abandonedInit = context.window.init_screensaver_fragment();
    assert.strictEqual(elements.idle_screen_pad.options.length, 0);
    mount();
    releaseInfo();
    releaseInfo = undefined;
    await abandonedInit;
    assert.strictEqual(elements.idle_screen_pad.options.length, 0);
    await context.window.init_screensaver_fragment();
    await finishLoad();
    assert.strictEqual(elements.idle_screen_pad.options.length, 1);
    assert.strictEqual(elements['screensaver-settings'].dataset.backlightOnly, 'false');
    assert(messages.every(message => message.type === 'success'));

    const brightness = fs.readFileSync('src/app/web/brightness.fragment.html', 'utf8');
    assert(!brightness.includes('screen_saver_'));
    const components = fs.readFileSync('src/app/portal_components.cpp', 'utf8');
    assert.match(components, /#if HAS_DISPLAY\s*#include "components\/display_component.cpp"\s*#include "components\/screensaver_component.cpp"/);
    console.log('portal_screensaver: PASS');
})().catch(error => { console.error(error); process.exitCode = 1; });