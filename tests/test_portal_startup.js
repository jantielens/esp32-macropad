const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

async function main() {
    for (const outcome of ['success', 'empty', 'error', 'font-error', 'timeout']) {
        let stylesheet;
        let timeoutCallback;
        const classes = new Set();
        const fontContext = {
            window: { fetch: async () => ({}) },
            document: {
                createElement: () => ({}),
                getElementById: () => null,
                head: { appendChild: element => { stylesheet = element; } },
                documentElement: { classList: { add: name => classes.add(name) } },
                fonts: { load: async () => {
                    if (outcome === 'font-error') throw new Error('Font unavailable');
                    return outcome === 'success' ? [{}] : [];
                } }
            },
            setTimeout: callback => { timeoutCallback = callback; return 1; },
            clearTimeout() {},
            console
        };
        vm.createContext(fontContext);
        vm.runInContext(fs.readFileSync('src/app/web/portal_core.js', 'utf8'), fontContext);
        const loaded = fontContext.portalEnsureMaterialSymbols();
        assert.strictEqual(fontContext.portalEnsureMaterialSymbols(), loaded);
        if (outcome === 'timeout') timeoutCallback();
        else if (outcome === 'error') stylesheet.onerror();
        else stylesheet.onload();
        assert.strictEqual(await loaded, outcome === 'success');
        assert.strictEqual(classes.has('portal-icons-ready'), outcome === 'success');
        if (outcome === 'timeout') {
            fontContext.document.fonts.load = async () => [{}];
            stylesheet.onload();
            await Promise.resolve();
            assert(!classes.has('portal-icons-ready'), 'late responses must preserve text-only fallback');
        }
    }
    const requests = [];
    const timers = [];
    const nodes = new Map();
    const errors = [];
    const context = {
        TextEncoder,
        console: { ...console, error: (...args) => errors.push(args) },
        fetch(url) {
            return new Promise(resolve => requests.push({ url, resolve }));
        },
        document: {
            getElementById(id) {
                if (!nodes.has(id)) nodes.set(id, { style: {}, addEventListener() {} });
                return nodes.get(id);
            }
        },
        setInterval(callback) { timers.push(callback); return timers.length; },
        clearInterval() {},
        setTimeout() {},
        formatBytes: String,
        healthFormatBytes: String
    };
    context.window = context;
    vm.createContext(context);
    for (const module of ['portal_core', 'portal_config', 'portal_health']) {
        vm.runInContext(fs.readFileSync('src/app/web/' + module + '.js', 'utf8'), context);
    }
    for (const name of ['healthConfigureFromDeviceInfo', 'healthConfigureHistoryFromDeviceInfo',
        'healthInitSparklineTooltips', 'renderHealth', 'renderSensorsSection', 'renderHidSection',
        'updateOnlineUpdateSection']) {
        context[name] = () => {};
    }

    const firstVersion = context.loadVersion();
    const secondVersion = context.loadVersion();
    assert.strictEqual(requests.length, 1);
    assert.strictEqual(requests[0].url, '/api/info?catalog=1');
    requests[0].resolve({ ok: true, json: async () => ({ version: 'test', psram_size: 0 }) });
    await Promise.all([firstVersion, secondVersion]);
    await context.loadVersion();
    assert.strictEqual(requests.length, 1, 'header must reuse cached device info');

    const firstHealth = context.updateHealth();
    const sharedHealth = context.fetchHealthOnce();
    const secondHealth = context.updateHealth();
    assert.strictEqual(requests.length, 2, 'concurrent health consumers must share a request');
    const snapshot = { cpu_usage: 10, psram_free: 0 };
    requests[1].resolve({ ok: true, json: async () => snapshot });
    await Promise.all([firstHealth, sharedHealth, secondHealth]);
    assert.strictEqual(await context.fetchHealthOnce(), snapshot);
    assert.strictEqual(requests.length, 2);

    const nextHealth = context.updateHealth();
    assert.strictEqual(requests.length, 3, 'later polls must fetch fresh health');
    requests[2].resolve({ ok: true, json: async () => ({ cpu_usage: 20, psram_free: 0 }) });
    await nextHealth;
    assert.strictEqual(context.getLatestHealth().cpu_usage, 20);

    context.initHealthWidget();
    context.initHealthWidget();
    assert.strictEqual(timers.length, 1, 'widget initialization must not restart polling');
    assert.strictEqual(requests.length, 4);
    requests[3].resolve({ ok: true, json: async () => snapshot });
    await context.fetchHealthOnce();
    await Promise.resolve();
    assert.deepStrictEqual(errors, [], 'startup must not log errors');

    const editor = fs.readFileSync('src/app/web/portal_pad_editor.js', 'utf8');
    assert(!editor.includes("document.addEventListener('DOMContentLoaded'"));
    vm.runInContext(editor, context);
    let rendered = false;
    context.padRenderGrid = () => { rendered = true; };
    for (const name of ['padClearDirty', 'padInitBindableColor', 'padSetBindableColor',
        'padPageBackgroundModeChanged', 'padRenderBindings', 'padLoadLevelActions',
        'padPopulateTemplateDropdown', 'padCacheColors']) context[name] = () => {};
    context.padGetEffectiveDefault = () => '#123456';
    let resolveAppearance;
    const appearance = new Promise(resolve => { resolveAppearance = resolve; });
    const pageLoad = context.padLoadPage(0, appearance);
    requests[4].resolve({ status: 404 });
    await new Promise(resolve => setImmediate(resolve));
    assert.strictEqual(rendered, false, 'grid must wait for appearance defaults');
    resolveAppearance();
    await pageLoad;
    assert.strictEqual(rendered, true);

    vm.runInContext('deviceInfoCache = { has_sound_player: true, has_native_extensions: true };', context);
    const order = [];
    let resolveDefaults;
    let resolveExtensions;
    context.padLoadButtonDefaultsFromDevice = () => {
        order.push('defaults');
        return new Promise(resolve => { resolveDefaults = resolve; });
    };
    context.padLoadPage = async (page, defaultsReady) => {
        order.push('pad');
        assert.strictEqual(page, 0);
        await defaultsReady;
        order.push('grid');
    };
    context.padFetchSoundList = () => order.push('sounds');
    context.padLoadBlockCatalog = () => order.push('blocks');
    context.extensionFetchSlots = () => {
        order.push('extensions');
        return new Promise(resolve => { resolveExtensions = resolve; });
    };
    context.padPopulateExtensionDropdown = () => order.push('extension controls');
    const initialPad = context.padLoadInitialPage(0);
    assert.deepStrictEqual(order, ['defaults', 'pad']);
    resolveDefaults();
    await new Promise(resolve => setImmediate(resolve));
    assert.deepStrictEqual(order, ['defaults', 'pad', 'grid', 'sounds', 'blocks', 'extensions']);
    resolveExtensions();
    await initialPad;
    assert.strictEqual(order.at(-1), 'extension controls');

    function selectNode() {
        return {
            _value: '', disabled: false, options: [{ value: '', textContent: '(none)' }],
            get value() { return this._value; },
            set value(value) { this._value = !this.strict || this.options.some(option => option.value === value) ? value : ''; },
            replaceChildren() { this.options = []; },
            appendChild(option) { this.options.push(option); },
            remove(index) { this.options.splice(index, 1); }
        };
    }
    const extensionSelect = selectNode();
    const soundSelect = selectNode();
    const screenSelect = selectNode();
    screenSelect.strict = true;
    screenSelect.id = 'test-target';
    const screenBinding = { id: 'test-target-binding', value: '', focus() {} };
    const screenBindingGroup = { style: {} };
    screenSelect.attributes = {};
    screenSelect.getAttribute = name => screenSelect.attributes[name];
    screenSelect.hasAttribute = name => Object.hasOwn(screenSelect.attributes, name);
    screenSelect.setAttribute = (name, value) => { screenSelect.attributes[name] = value; };
    screenSelect.removeAttribute = name => { delete screenSelect.attributes[name]; };
    screenSelect.remove = index => {
        if (screenSelect.options[index].value === screenSelect.value) screenSelect.value = '';
        screenSelect.options.splice(index, 1);
    };
    context.document.createElement = () => ({ value: '', textContent: '' });
    context.document.getElementById = id => id === 'pad-edit-extension-id' ? extensionSelect :
        id === 'test-sound-alert-file' ? soundSelect : id === 'test-target' ? screenSelect :
        id === 'test-target-binding' ? screenBinding : id === 'test-target-binding-group' ? screenBindingGroup : null;
    for (const module of ['portal_pad_dialog', 'portal_action_editor', 'portal_action_editor_screen', 'portal_action_editor_sound_alert']) {
        vm.runInContext(fs.readFileSync('src/app/web/' + module + '.js', 'utf8'), context);
    }
    vm.runInContext('padExtensionCatalogLoading = true;', context);
    context.extensionCatalog = [];
    context.padPopulateExtensionDropdown('clock');
    assert.strictEqual(extensionSelect.value, 'clock');
    assert(extensionSelect.disabled);
    context.extensionCatalog = [{ id: 'clock', installed: true, enabled: true, version: '1' }];
    vm.runInContext('padExtensionCatalogLoading = false;', context);
    context.padPopulateExtensionDropdown();
    assert.strictEqual(extensionSelect.value, 'clock');
    assert(!extensionSelect.disabled);
    assert.strictEqual(extensionSelect.options.filter(option => option.value === 'clock').length, 1);

    soundSelect.value = 'saved.mp3';
    context.actionEditorPopulateSounds(['test'], ['other.mp3']);
    assert.strictEqual(soundSelect.value, 'saved.mp3');
    assert(soundSelect.options.some(option => option.value === 'saved.mp3'));
    context.actionEditorPopulateSounds(['test'], ['saved.mp3']);
    assert.strictEqual(soundSelect.value, 'saved.mp3');
    assert.strictEqual(soundSelect.options.filter(option => option.value === 'saved.mp3').length, 1);
    const screens = [
        {id:'info', name:'Device Info'}, {id:'pad_2', name:'Pad 3'},
        {id:'pad_0', name:'Living Room'}, {id:'pad_1', name:'Pad 2'}
    ];
    context.actionEditorPopulateScreens(['test'], screens);
    assert.deepStrictEqual(Array.from(screenSelect.options, option => [option.value, option.textContent]), [
        ['', '(none)'], ['pad_0', 'Pad 1: Living Room'], ['pad_1', 'Pad 2'],
        ['pad_2', 'Pad 3'], ['__binding__', 'Binding value'], ['info', 'Device Info']
    ]);
    screenSelect.value = 'pad_2';
    context.actionEditorPopulateScreens(['test'], screens);
    assert.strictEqual(screenSelect.value, 'pad_2', 'refresh must retain a selected screen');
    screenSelect.setAttribute('data-pending-value', 'pad_1');
    context.actionEditorPopulateScreens(['test'], screens);
    assert.strictEqual(screenSelect.value, 'pad_1', 'refresh must restore a deferred target');
    assert(!screenSelect.hasAttribute('data-pending-value'));
    const screenEditor = context._actionEditorExtensions.find(extension => extension.type === 'screen');
    vm.runInContext(fs.readFileSync('src/app/web/portal_binding_validator.js', 'utf8'), context);
    context.bindingClearError = () => {};
    context.bindingShowError = () => {};
    const actionMessages = [];
    let actionFocused = false;
    const actionGroup = { tagName: 'DETAILS', open: false, parentElement: null };
    screenBinding.parentElement = actionGroup;
    screenBinding.focus = () => { actionFocused = true; };
    context.showMessage = (message, type) => actionMessages.push({ message, type });
    screenEditor.load('test', { target: '[list:pads.selected]' });
    assert.strictEqual(screenSelect.value, '__binding__');
    assert.strictEqual(screenBindingGroup.style.display, '');
    assert.strictEqual(screenEditor.build('test', 'screen').target, '[list:pads.selected]');
    context.actionEditorPopulateScreens(['test'], screens);
    assert.strictEqual(screenEditor.build('test', 'screen').target, '[list:pads.selected]');
    screenBinding.value = '[list:pads.selected';
    assert.throws(() => screenEditor.build('test', 'screen'), error => error.fieldIds[0] === screenBinding.id);
    screenBinding.value = 'pad_1';
    assert.throws(() => screenEditor.build('test', 'screen'), /binding/);
    screenBinding.value = '[bad]';
    assert.throws(() => screenEditor.build('test', 'screen'), /binding/);
    screenBinding.value = '[pad:' + 'a'.repeat(25) + ']';
    assert.strictEqual(screenEditor.build('test', 'screen').target.length, 31);
    screenBinding.value = '[pad:' + 'a'.repeat(26) + ']';
    assert.throws(() => screenEditor.build('test', 'screen'), /31 UTF-8 bytes/);
    screenBinding.value = '[pad:' + 'é'.repeat(13) + ']';
    assert.throws(() => screenEditor.build('test', 'screen'), /31 UTF-8 bytes/);
    context.padWorkspace = {};
    assert.throws(() => screenEditor.build('test', 'screen'), /31 UTF-8 bytes/);
    delete context.padWorkspace;
    assert.strictEqual(actionMessages.length, 0, 'building reports errors without presenting host messages');
    assert.strictEqual(actionFocused, false, 'building must not steal focus, with or without a pad workspace');
    screenEditor.load('test', { target: 'missing_screen' });
    assert.strictEqual(screenEditor.build('test', 'screen').target, '');
    screenSelect.removeAttribute('data-screens-loaded');
    screenEditor.load('test', { target: 'pad_2' });
    screenSelect.setAttribute('data-pending-value', 'pad_2');
    screenSelect.value = '__binding__';
    screenBinding.value = '[list:pads.selected]';
    context.actionEditorScreenTargetChanged('test');
    context.actionEditorPopulateScreens(['test'], screens);
    assert.strictEqual(screenEditor.build('test', 'screen').target, '[list:pads.selected]', 'catalog loading must not overwrite a newer mode');
    const screenLookup = context.document.getElementById;
    context.document.getElementById = id => id === 'pad-edit-action-0-type' ? { value: 'screen' } :
        screenLookup(id.replace(/^pad-edit-action-0/, 'test'));
    screenBinding.value = '[list:pads.selected';
    assert.strictEqual(context.bindingValidateDialog().valid, false);
    screenSelect.value = 'pad_1';
    context.actionEditorScreenTargetChanged('test');
    assert.strictEqual(context.bindingValidateDialog().valid, true, 'inactive binding drafts must not block literal targets');
    screenSelect.value = '__binding__';
    context.actionEditorScreenTargetChanged('test');
    assert.strictEqual(context.bindingValidateDialog().valid, false, 'switching back must validate the retained binding draft');
    vm.runInContext(fs.readFileSync('src/app/web/portal_config_actions.js', 'utf8'), context);
    context.document.getElementById = id => {
        if (['hw-button-editors', 'mqtt-trigger-editors'].includes(id)) return { querySelectorAll: () => [{}] };
        if (id === 'mqtttrig-1-topic') return { value: 'test/topic' };
        if (id.endsWith('-type')) return { value: 'screen', getAttribute: () => null };
        if (id.endsWith('-target-binding')) return screenBinding;
        if (id.endsWith('-target-binding-group')) return screenBindingGroup;
        if (id.endsWith('-target')) return screenSelect;
        return screenLookup(id);
    };
    const actionWrites = [];
    context.fetch = async (url, options) => {
        actionWrites.push({ url, payload: JSON.parse(options.body) });
        return { ok: true };
    };
    screenBinding.value = '[pad:' + 'a'.repeat(26) + ']';
    for (const save of ['saveBootActions', 'saveSwipeActions', 'saveHwButtons', 'saveMqttTriggers', 'saveTimerConfig']) {
        actionFocused = false;
        actionGroup.open = false;
        actionMessages.length = 0;
        await context[save]();
        assert(actionFocused && actionGroup.open, save + ' must reveal and focus the invalid action');
        assert.match(actionMessages[0].message, /31 UTF-8 bytes/);
        assert.strictEqual(actionMessages[0].type, 'error');
        assert.strictEqual(actionWrites.length, 0, save + ' must not write invalid actions');
    }
    screenBinding.value = '[list:pads.selected]';
    await context.saveBootActions();
    assert.strictEqual(actionWrites.length, 1);
    assert.strictEqual(actionWrites[0].payload.actions[0].target, screenBinding.value);
    context.document.getElementById = screenLookup;
    let soundRequests = 0;
    context.getDeviceInfo = async () => ({ has_sound_player:false });
    context.fetch = async url => {
        assert.strictEqual(url, '/api/sounds/list');
        soundRequests++;
        return {ok:true, json:async () => ['new.mp3']};
    };
    await context.actionEditorWireFragment(['test']);
    assert.strictEqual(soundRequests, 0, 'unsupported audio must not fetch sounds');
    context.getDeviceInfo = async () => ({ has_sound_player:true });
    await context.actionEditorWireFragment(['test']);
    assert.strictEqual(soundRequests, 1, 'fragment wiring fetches sounds once for every editor prefix');
    assert(soundSelect.options.some(option => option.value === 'new.mp3'));
    assert.strictEqual(soundSelect.value, 'saved.mp3');
    const navSource = fs.readFileSync('src/app/web/portal_nav.js', 'utf8');
    const assets = [];
    const assetContext = { document: {
        createElement(tag) { return { tag, remove() {} }; },
        head: { appendChild(asset) { assets.push(asset); } }
    } };
    vm.createContext(assetContext);
    vm.runInContext(navSource.slice(navSource.indexOf('  var navigationAssetLoads'),
        navSource.indexOf('  function buildNav')), assetContext);
    await assetContext.loadItemAssets({ id:'welcome' });
    assert.strictEqual(assets.length, 0, 'home must not fetch alarm assets');
    const alarmItem = { portal_script:'/portal_alarms.js' };
    let initialized = false;
    const alarmAssets = assetContext.loadItemAssets(alarmItem).then(() => { initialized = true; });
    const sharedAssets = assetContext.loadItemAssets(alarmItem);
    assert.strictEqual(assets.length, 1, 'concurrent visits share the asset request');
    assert.strictEqual(assets[0].src, '/portal_alarms.js');
    assert.strictEqual(initialized, false, 'initialization waits for the script');
    assets[0].onload();
    await Promise.all([alarmAssets, sharedAssets]);
    await assetContext.loadItemAssets(alarmItem);
    assert.strictEqual(assets.length, 1, 'revisits use the loaded script');
    const retryItem = { portal_style:'/alarm.css' };
    const failure = assetContext.loadItemAssets(retryItem);
    assets[1].onerror();
    await assert.rejects(failure, /Portal asset unavailable/);
    const retry = assetContext.loadItemAssets(retryItem);
    assert.strictEqual(assets.length, 3, 'a failed asset can be retried');
    assets[2].onload();
    await retry;
    console.log('portal_startup: PASS');
}

main().catch(error => { console.error(error); process.exitCode = 1; });