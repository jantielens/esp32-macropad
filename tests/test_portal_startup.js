const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

async function main() {
    const requests = [];
    const timers = [];
    const nodes = new Map();
    const errors = [];
    const context = {
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
            value: '', disabled: false, options: [{ value: '', textContent: '(none)' }],
            replaceChildren() { this.options = []; },
            appendChild(option) { this.options.push(option); },
            remove(index) { this.options.splice(index, 1); }
        };
    }
    const extensionSelect = selectNode();
    const soundSelect = selectNode();
    context.document.createElement = () => ({ value: '', textContent: '' });
    context.document.getElementById = id => id === 'pad-edit-extension-id' ? extensionSelect :
        id === 'test-sound-alert-file' ? soundSelect : null;
    for (const module of ['portal_pad_dialog', 'portal_action_editor']) {
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