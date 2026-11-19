const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

async function testOtaModeReboot() {
    for (const outcome of ['success', 'rejected']) {
        const button = { disabled: false };
        let url, method, dialog, message;
        const context = {
            confirm: () => true,
            document: { getElementById: () => button },
            fetch: async (target, options) => {
                url = target;
                method = options.method;
                return { ok: outcome === 'success', json: async () => ({
                    success: outcome === 'success', message: 'Activity in progress'
                }) };
            },
            showRebootDialog: value => { dialog = value; },
            showMessage: value => { message = value; }
        };
        vm.createContext(context);
        vm.runInContext(fs.readFileSync('src/app/web/portal_firmware.js', 'utf8'), context);
        await context.bootIntoOtaMode();
        assert.strictEqual(url, '/api/reboot/ota');
        assert.strictEqual(method, 'POST');
        assert.strictEqual(button.disabled, outcome === 'success');
        if (outcome === 'success') assert.strictEqual(dialog.context, 'ota-mode');
        else assert.match(message, /Activity in progress/);
    }
}

async function testRebootRedirect() {
    for (const contextName of ['ota-mode', 'reboot']) {
        const timers = [];
        let reloads = 0;
        const location = {
            origin: 'http://device',
            hash: '#manual-upload',
            reload() { ++reloads; }
        };
        const fetch = async () => ({ ok: true });
        const context = {
            window: { location, fetch },
            document: { getElementById: () => null },
            fetch,
            setTimeout(callback) { timers.push(callback); },
            AbortSignal,
            console
        };
        vm.createContext(context);
        vm.runInContext(fs.readFileSync('src/app/web/portal_core.js', 'utf8'), context);
        const pending = context.startReconnection({
            context: contextName, statusElement: {}, messageElement: {}
        });
        timers.shift()();
        await pending;
        await new Promise(resolve => setImmediate(resolve));
        timers.shift()();
        if (contextName === 'ota-mode') {
            assert.strictEqual(location.hash, '#manual-upload');
            assert.strictEqual(reloads, 1, 'reload even when the fragment is already selected');
        } else {
            assert.strictEqual(location.href, 'http://device');
            assert.strictEqual(reloads, 0);
        }
    }
}

async function testFirmwareUploadResponses() {
    for (const outcome of ['success', 'rejected', 'disconnect']) {
        const nodes = new Map();
        const timers = [];
        let request;
        let reconnects = 0;
        let pollingPaused = false;
        class FakeRequest {
            constructor() {
                request = this;
                this.handlers = {};
                this.upload = { addEventListener: (name, handler) => { this.progress = handler; } };
            }
            addEventListener(name, handler) { this.handlers[name] = handler; }
            open() {}
            send() {}
        }
        const context = {
            selectedFile: { name: 'firmware.bin' },
            document: { getElementById(id) {
                if (!nodes.has(id)) nodes.set(id, { style: {}, disabled: false });
                return nodes.get(id);
            } },
            window: { location: { origin: 'http://device' } },
            XMLHttpRequest: FakeRequest,
            FormData: class { append() {} },
            API_UPDATE: '/api/update',
            showRebootDialog() {},
            stopHealthPolling() { pollingPaused = true; },
            resumeHealthPolling() { pollingPaused = false; },
            startReconnection() { ++reconnects; },
            setTimeout(callback) { timers.push(callback); },
            console
        };
        vm.createContext(context);
        vm.runInContext(fs.readFileSync('src/app/web/portal_firmware.js', 'utf8'), context);
        await context.uploadFirmware();
        assert(pollingPaused, 'upload must pause health polling');
        request.progress({ lengthComputable: true, loaded: 98, total: 100 });
        if (outcome === 'disconnect') {
            request.handlers.error();
        } else {
            request.status = outcome === 'success' ? 200 : 409;
            request.response = { message: 'Stopping voice recording; retry update' };
            request.handlers.load();
        }
        if (outcome === 'rejected') {
            assert(!pollingPaused, 'failed upload must resume health polling');
            assert.match(nodes.get('reboot-message').textContent, /retry update/);
            assert.strictEqual(nodes.get('upload-btn').disabled, false);
            assert.strictEqual(nodes.get('firmware-file').disabled, false);
        }
        timers.forEach(callback => callback());
        assert.strictEqual(reconnects, outcome === 'rejected' ? 0 : 1);
    }
}

async function testFetchConcurrency() {
    const requests = [];
    const context = { document: { getElementById: () => null }, window: {
        fetch(input, init) {
            if (input === '/network-error') return Promise.reject(new Error('Connection failed'));
            if (input === '/empty') return Promise.resolve(new Response(null, { status: 204 }));
            let controller;
            const body = new ReadableStream({ start(streamController) { controller = streamController; } });
            const response = new Response(body, { headers: { 'Content-Type': 'application/json' } });
            if (init?.signal) {
                init.signal.addEventListener('abort', () => controller.error(init.signal.reason), { once: true });
            }
            requests.push({ input, controller, response });
            return Promise.resolve(response);
        }
    } };
    vm.createContext(context);
    vm.runInContext(fs.readFileSync('src/app/web/portal_core.js', 'utf8'), context);
    const pending = ['/first', '/second', '/third', '/fourth'].map(path => context.window.fetch(path));
    let firstSettled = false;
    pending[0].then(() => { firstSettled = true; });
    await new Promise(resolve => setImmediate(resolve));
    assert.strictEqual(requests.length, 2, 'headers alone must not release either transfer slot');
    assert.strictEqual(firstSettled, false, 'fetch callers must wait for the complete body');
    const encoder = new TextEncoder();
    requests[0].controller.enqueue(encoder.encode('{"value":'));
    await new Promise(resolve => setImmediate(resolve));
    assert.strictEqual(requests.length, 2, 'a partial body must retain its slot');
    requests[0].controller.enqueue(encoder.encode('"first"}'));
    requests[0].controller.close();
    const first = await pending[0];
    assert.strictEqual(first, requests[0].response, 'preserve the native response and its metadata');
    assert.strictEqual(first.bodyUsed, false, 'the caller must still be able to consume the response');
    assert.strictEqual(first.headers.get('Content-Type'), 'application/json');
    assert.deepStrictEqual(await first.json(), { value: 'first' });
    assert.strictEqual(requests.length, 3, 'one completed body must admit exactly one waiting request');
    const bodyFailure = assert.rejects(pending[1], /Download interrupted/);
    requests[1].controller.error(new Error('Download interrupted'));
    await bodyFailure;
    assert.strictEqual(requests.length, 4, 'a failed body must release its slot');
    requests[2].controller.enqueue(encoder.encode('third'));
    requests[2].controller.close();
    requests[3].controller.close();
    assert.strictEqual(await (await pending[2]).text(), 'third');
    assert.strictEqual(await (await pending[3]).text(), '');

    const abortController = new AbortController();
    const aborted = context.window.fetch('/aborted', { signal: abortController.signal });
    const abortFailure = assert.rejects(aborted, { name: 'AbortError' });
    await new Promise(resolve => setImmediate(resolve));
    abortController.abort();
    await abortFailure;
    await assert.rejects(context.window.fetch('/network-error'), /Connection failed/);
    const emptyResponses = await Promise.all([0, 1, 2].map(() => context.window.fetch('/empty')));
    assert(emptyResponses.every(response => response.status === 204 && response.body === null));
}

async function main() {
    await testOtaModeReboot();
    await testRebootRedirect();
    await testFirmwareUploadResponses();
    await testFetchConcurrency();
    const listeners = new Map();
    const activityRequests = [];
    const savedConfigs = [];
    let activityClock = 0;
    let inputValid = true;
    let saveClick;
    const powerContext = {
        window: { registerConfigFields: fields => assert.strictEqual(fields[0], 'auto_shutdown_timeout_seconds') },
        document: {
            addEventListener: (name, listener) => listeners.set(name, listener),
            getElementById: id => id === 'power-savings-save-btn'
                ? { addEventListener: (_, listener) => { saveClick = listener; } }
                : { checkValidity: () => inputValid }
        },
        performance: { now: () => activityClock },
        fetch: async (url, options) => { activityRequests.push({url, method: options.method}); return {ok: true}; },
        loadConfig() {},
        saveFragmentConfig: (reboot, fields) => savedConfigs.push({reboot, field: fields[0]}),
        showMessage() {},
        console
    };
    vm.createContext(powerContext);
    vm.runInContext(fs.readFileSync('src/app/web/portal_power_savings.js', 'utf8'), powerContext);
    assert.strictEqual(activityRequests.length, 0, 'loading an idle portal must not keep the device awake');
    listeners.get('input')({isTrusted: false});
    assert.strictEqual(activityRequests.length, 0, 'programmatic field updates are not user activity');
    listeners.get('keydown')({isTrusted: true});
    listeners.get('pointerdown')({isTrusted: true});
    assert.strictEqual(activityRequests.length, 1, 'bursty input should share one activity request');
    activityClock = 250;
    listeners.get('wheel')({isTrusted: true});
    assert.strictEqual(activityRequests.length, 2, 'continued interaction must renew activity');
    assert.deepStrictEqual(activityRequests[0], {url: '/api/component/power-savings/activity', method: 'POST'});
    powerContext.window.init_power_savings_fragment();
    inputValid = false;
    saveClick();
    assert.strictEqual(savedConfigs.length, 0, 'invalid values must not be submitted');
    inputValid = true;
    saveClick();
    assert.deepStrictEqual(savedConfigs, [{reboot: false, field: 'auto_shutdown_timeout_seconds'}]);
    for (const outcome of ['success', 'empty', 'error', 'font-error', 'timeout', 'font-timeout', 'no-font-api']) {
        let stylesheet;
        let timeoutCallback;
        let completeFont;
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
                    if (outcome === 'font-timeout') return new Promise(resolve => { completeFont = resolve; });
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
        assert(!classes.has('portal-icons-ready'), 'pending fonts must not reveal decorative glyphs');
        if (outcome === 'no-font-api') delete fontContext.document.fonts;
        if (outcome === 'timeout') timeoutCallback();
        else if (outcome === 'error') stylesheet.onerror();
        else stylesheet.onload();
        if (outcome === 'font-timeout') timeoutCallback();
        assert.strictEqual(await loaded, outcome === 'success');
        assert.strictEqual(classes.has('portal-icons-ready'), outcome === 'success');
        if (outcome === 'font-timeout') {
            completeFont([{}]);
            await new Promise(resolve => setImmediate(resolve));
            assert(!classes.has('portal-icons-ready'), 'a late font download must not reveal offline glyphs');
        }
        if (outcome === 'timeout') {
            fontContext.document.fonts.load = async () => [{}];
            stylesheet.onload();
            await Promise.resolve();
            assert(!classes.has('portal-icons-ready'), 'late responses must preserve text-only fallback');
        }
    }
    const requests = [];
    const timers = [];
    const timeouts = [];
    const clearedTimers = [];
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
        clearInterval(timer) { clearedTimers.push(timer); },
        setTimeout(callback) { timeouts.push(callback); },
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
    context.stopHealthPolling();
    assert.deepStrictEqual(clearedTimers, [1]);
    timeouts.forEach(callback => callback());
    context.initHealthWidget();
    assert.strictEqual(timers.length, 1, 'accepted sleep must prevent delayed polling restarts');
    assert.strictEqual(requests.length, 4, 'stopped polling must not issue another health request');

    const editor = fs.readFileSync('src/app/web/portal_pad_editor.js', 'utf8');
    assert(!editor.includes("document.addEventListener('DOMContentLoaded'"));
    vm.runInContext(editor, context);
    let rendered = false;
    context.padRenderGrid = () => { rendered = true; };
    for (const name of ['padClearDirty', 'padInitBindableColor', 'padSetBindableColor',
        'padPageBackgroundModeChanged', 'padRenderBindings', 'padLoadLevelActions',
        'padPopulateTemplateDropdown', 'padCacheColors', 'padPruneDraftIconSources']) context[name] = () => {};
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
    const initialPad = context.padLoadInitialPage(0);
    assert.deepStrictEqual(order, ['defaults', 'pad']);
    resolveDefaults();
    await new Promise(resolve => setImmediate(resolve));
    assert.deepStrictEqual(order, ['defaults', 'pad', 'grid', 'sounds', 'blocks']);
    await initialPad;

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
    extensionSelect.strict = true;
    const extensionInputs = {
        'pad-edit-extension-config': { value: '', maxLength: 511 },
        'pad-edit-extension-tick-interval': { value: '' },
        'pad-edit-extension-upscale': { value: '1' }
    };
    const soundSelect = selectNode();
    const screenSelect = selectNode();
    const operationScreenSelect = selectNode();
    operationScreenSelect.strict = true;
    operationScreenSelect.attributes = {};
    operationScreenSelect.getAttribute = name => operationScreenSelect.attributes[name];
    operationScreenSelect.setAttribute = (name, value) => { operationScreenSelect.attributes[name] = value; };
    operationScreenSelect.removeAttribute = name => { delete operationScreenSelect.attributes[name]; };
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
        id === 'test-op-audio.alert.mp3-file-fixed' ? soundSelect : id === 'test-target' ? screenSelect :
        id === 'test-op-screen.open-target-fixed' ? operationScreenSelect :
        id === 'test-target-binding' ? screenBinding : id === 'test-target-binding-group' ? screenBindingGroup : extensionInputs[id] || null;
    for (const module of ['portal_pad_dialog', 'portal_action_editor', 'portal_action_editor_screen']) {
        vm.runInContext(fs.readFileSync('src/app/web/' + module + '.js', 'utf8'), context);
    }
    vm.runInContext(fs.readFileSync('src/app/web/portal_widget_editor.js', 'utf8'), context);
    vm.runInContext(fs.readFileSync('src/app/web/portal_widget_extension.js', 'utf8'), context);
    const extensionEditor = context.padWidgetEditor('external');
    extensionEditor.supported = true;
    let resolveCatalog;
    context.fetch = () => new Promise(resolve => { resolveCatalog = resolve; });
    let settlements = 0;
    const onSettled = () => { settlements++; };
    const catalogLoad = extensionEditor.init(onSettled);
    extensionEditor.load({ extension_id: 'clock' });
    assert.strictEqual(extensionSelect.value, 'clock');
    assert(extensionSelect.disabled);
    assert.throws(() => extensionEditor.build({}), /still loading/);
    extensionEditor.load({ extension_id: 'weather' });
    extensionInputs['pad-edit-extension-config'].value = ' opaque {not JSON} ';
    resolveCatalog({ ok: true, json: async () => ({ slots: [
        { id: 'clock', installed: true, enabled: true, version: '1' },
        { id: 'weather', installed: true, enabled: true, version: '2' },
        { id: 'disabled', installed: true, enabled: false, version: '1' }
    ] }) });
    await catalogLoad;
    assert.strictEqual(settlements, 1, 'Catalog success notifies its host without requiring workspace state');
    assert.strictEqual(extensionSelect.value, 'weather', 'Late catalog keeps the newer selected button');
    assert(!extensionSelect.disabled);
    assert.strictEqual(extensionSelect.options.filter(option => option.value === 'clock').length, 1);
    assert(!extensionSelect.options.some(option => option.value === 'disabled'));
    const extensionButton = {};
    context.padWidgetEditorBuild(extensionButton, extensionEditor);
    assert.strictEqual(extensionButton.extension_id, 'weather');
    assert.strictEqual(extensionButton.extension_config, 'opaque {not JSON}');
    assert.strictEqual(extensionButton.extension_tick_interval_ms, undefined);
    extensionInputs['pad-edit-extension-config'].value = 'x'.repeat(512);
    assert.throws(() => context.padWidgetEditorBuild({}, extensionEditor), /511/);
    assert.strictEqual(extensionInputs['pad-edit-extension-config'].value.length, 512);
    extensionInputs['pad-edit-extension-config'].value = 'retained';
    const oldLoad = extensionEditor.init(onSettled);
    const settleOld = resolveCatalog;
    extensionEditor.dispose();
    const newLoad = extensionEditor.init(onSettled);
    extensionEditor.load({ extension_id: 'clock' });
    settleOld({ ok: true, json: async () => ({ slots: [{ id: 'stale', installed: true, enabled: true }] }) });
    await oldLoad;
    assert.strictEqual(settlements, 1, 'Disposed catalog must not notify the current host');
    assert.strictEqual(extensionSelect.value, 'clock', 'Disposed catalog cannot mutate the current mount');
    resolveCatalog({ ok: false });
    await newLoad;
    assert.strictEqual(settlements, 2, 'Catalog failure also notifies its host');
    assert.match(extensionSelect.options[0].textContent, /unavailable/);
    assert.throws(() => extensionEditor.build({}), /unavailable/);
    assert.strictEqual(extensionInputs['pad-edit-extension-config'].value, 'retained');
    context.fetch = async () => ({ ok: true, json: async () => ({ slots: [] }) });
    await extensionEditor.init();
    extensionEditor.load({ extension_id: 'missing' });
    assert.match(extensionSelect.options[0].textContent, /No enabled extensions/);
    assert.strictEqual(extensionSelect.value, '');
    assert(!extensionSelect.options.some(option => option.value === 'missing'));
    assert.throws(() => extensionEditor.build({}), /installed enabled/);

    vm.runInContext(`deviceInfoCache.catalog = [{ operations: [
        { id:'audio.alert.mp3', arguments:[{name:'file', control:'sounds'}] }
    ] }];`, context);
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
    vm.runInContext(`deviceInfoCache.catalog = [{ operations: [
        { id: 'screen.open', arguments: [{ name: 'target', control: 'screens' }] },
        { id: 'test.navigate', arguments: [{ name: 'destination', control: 'screens' }] }
    ] }];`, context);
    const secondScreenSelect = selectNode();
    secondScreenSelect.strict = true;
    secondScreenSelect.attributes = {};
    secondScreenSelect.getAttribute = name => secondScreenSelect.attributes[name];
    secondScreenSelect.setAttribute = (name, value) => { secondScreenSelect.attributes[name] = value; };
    secondScreenSelect.removeAttribute = name => { delete secondScreenSelect.attributes[name]; };
    const getElement = context.document.getElementById;
    context.document.getElementById = id => id === 'test-op-test.navigate-destination-fixed'
        ? secondScreenSelect : getElement(id);
    context.actionEditorPopulateScreens(['test'], screens);
    assert.strictEqual(operationScreenSelect.value, 'pad_0', 'new operation selects first available screen');
    assert.strictEqual(secondScreenSelect.value, 'pad_0', 'screen pickers work for any operation descriptor');
    assert.deepStrictEqual(Array.from(operationScreenSelect.options, option => [option.value, option.textContent]), [
        ['pad_0', 'Pad 1: Living Room'], ['pad_1', 'Pad 2'],
        ['pad_2', 'Pad 3'], ['info', 'Device Info']
    ]);
    operationScreenSelect.value = 'pad_2';
    context.actionEditorPopulateScreens(['test'], screens);
    assert.strictEqual(operationScreenSelect.value, 'pad_2', 'refresh retains the selected operation screen');
    operationScreenSelect.setAttribute('data-pending-value', 'pad_1');
    context.actionEditorPopulateScreens(['test'], screens);
    assert.strictEqual(operationScreenSelect.value, 'pad_1', 'late screen list restores the saved operation target');
    operationScreenSelect.value = '';
    context.actionEditorPopulateScreens(['test'], []);
    assert.strictEqual(operationScreenSelect.value, '', 'empty screen list does not invent a target');
    context.actionEditorPopulateScreens(['test'], screens);
    assert.strictEqual(operationScreenSelect.value, 'pad_0', 'first screen is selected after a delayed list');
    vm.runInContext(fs.readFileSync('src/app/web/portal_binding_validator.js', 'utf8'), context);
    context.bindingClearError = () => {};
    context.bindingShowError = () => {};
    const actionMessages = [];
    let actionFocused = false;
    const actionGroup = { tagName: 'DETAILS', open: false, parentElement: null };
    screenBinding.parentElement = actionGroup;
    screenBinding.focus = () => { actionFocused = true; };
    context.showMessage = (message, type) => actionMessages.push({ message, type });
    const screenLookup = context.document.getElementById;
    vm.runInContext(fs.readFileSync('src/app/web/portal_config_actions.js', 'utf8'), context);
    context.document.getElementById = id => {
        if (['hw-button-editors', 'mqtt-trigger-editors'].includes(id)) return { children: [{}] };
        if (id === 'mqtttrig-1-topic') return { value: 'test/topic' };
        if (id === 'timer-config-save') return { disabled: false };
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
    context.bootActionsLoaded = true;
    context.swipeActionsLoaded = true;
    context.hwButtonsLoaded = true;
    context.mqttTriggersLoaded = true;
    const originalBuild = context.actionEditorBuild;
    let invalidAction = true;
    context.actionEditorBuild = () => {
        if (invalidAction) throw Object.assign(new Error('Invalid dynamic screen target'), {fieldIds: [screenBinding.id]});
        return {op: 'screen.open', args: {target: {$template: screenBinding.value}}};
    };
    screenBinding.value = '[pad:' + 'a'.repeat(26) + ']';
    for (const save of ['saveBootActions', 'saveSwipeActions', 'saveHwButtons', 'saveMqttTriggers', 'saveTimerConfig']) {
        actionFocused = false;
        actionGroup.open = false;
        actionMessages.length = 0;
        await context[save]();
        assert(actionFocused && actionGroup.open, save + ' must reveal and focus the invalid action');
        assert.match(actionMessages[0].message, /Invalid dynamic screen target/);
        assert.strictEqual(actionMessages[0].type, 'error');
        assert.strictEqual(actionWrites.length, 0, save + ' must not write invalid actions');
    }
    screenBinding.value = '[var:selected_pad]';
    invalidAction = false;
    await context.saveBootActions();
    assert.strictEqual(actionWrites.length, 1);
    assert.strictEqual(actionWrites[0].payload.actions[0].args.target.$template, screenBinding.value);
    assert.strictEqual(actionWrites[0].payload.action_format_version, 1);
    context.actionEditorBuild = originalBuild;
    context.document.getElementById = screenLookup;
    context.getDeviceInfo = async () => ({ available_screens:screens });
    await context.actionEditorWireFragment(['test']);
    assert.strictEqual(operationScreenSelect.value, 'pad_0');
    const navSource = fs.readFileSync('src/app/web/portal_nav.js', 'utf8');
    const navClassList = () => {
        const values = new Set();
        return {
            contains: name => values.has(name),
            toggle: (name, enabled) => enabled ? values.add(name) : values.delete(name)
        };
    };
    const navHeaders = ['a', 'b', 'c'].map(id => {
        const toggle = { attrs: {}, setAttribute(name, value) { this.attrs[name] = value; } };
        return {
            classList: navClassList(),
            getAttribute: () => id,
            querySelector: selector => selector === '.nav-category-toggle' ? toggle : {textContent: id},
            toggle
        };
    });
    const navItems = navHeaders.map(() => ({classList: navClassList()}));
    const navState = {
        navEl: {
            querySelector: () => null,
            querySelectorAll: selector => selector === '.nav-category-header' ? navHeaders :
                selector === '.nav-category-items' ? navItems : []
        },
        itemMap: {first: {cat: {id: 'a'}}, third: {cat: {id: 'c'}}},
        currentItem: 'first'
    };
    vm.createContext(navState);
    vm.runInContext(navSource.slice(navSource.indexOf('  function setActive('),
        navSource.indexOf('  // ---------- Fragment loading')), navState);
    const expanded = () => navHeaders.filter(header => header.classList.contains('expanded'))
        .map(header => header.getAttribute('data-category'));
    navState.setActive('first');
    navState.expandCategoryFor('first');
    navState.toggleExpandedCategory(navHeaders[1]);
    assert.deepStrictEqual(expanded(), ['a', 'b'], 'browsing keeps the active category open');
    navState.toggleExpandedCategory(navHeaders[2]);
    assert.deepStrictEqual(expanded(), ['a', 'c'], 'a third category replaces the browsed category');
    navState.toggleExpandedCategory(navHeaders[0]);
    assert.deepStrictEqual(expanded(), ['c'], 'the active category can be collapsed');
    assert(navHeaders[0].classList.contains('active'), 'the collapsed active category stays marked');
    assert.strictEqual(navHeaders[0].toggle.attrs['aria-expanded'], 'false');
    navState.toggleExpandedCategory(navHeaders[0]);
    assert.deepStrictEqual(expanded(), ['a', 'c'], 'reopening the active category preserves the browsed category');
    navState.currentItem = 'third';
    navState.setActive('third');
    navState.expandCategoryFor('third');
    assert.deepStrictEqual(expanded(), ['c'], 'selecting an item closes the previous category');
    assert(navHeaders[2].classList.contains('active'));
    navState.currentItem = 'welcome';
    navState.setActive('welcome');
    navState.expandCategoryFor('welcome');
    navState.toggleExpandedCategory(navHeaders[0]);
    navState.toggleExpandedCategory(navHeaders[1]);
    assert.deepStrictEqual(expanded(), ['b'], 'without an active category only the browsed category stays open');
    const navInit = navSource.slice(navSource.indexOf('  function init()'),
        navSource.indexOf("  if (document.readyState === 'loading')"));
    const headerLookup = context.document.getElementById;
    context.document.getElementById = id => {
        if (!nodes.has(id)) nodes.set(id, {style: {}, textContent: ''});
        return nodes.get(id);
    };
    for (const recovery of [false, true]) {
        let selected;
        const data = { ap_mode: true, recovery_ap: recovery,
            primary: {fragment: recovery ? 'setup' : 'special-home'},
            categories: [] };
        const routing = {
            window: { addEventListener() {} }, NAV_API: '/api/portal/nav',
            itemMap: recovery ? {setup: {}} : {mode: {}, 'special-home': {}},
            portalEnsureMaterialSymbols() {}, updateLayoutHeight() {}, buildNav() {},
            getItemFromHash() { return 'mode'; },
            navigateTo(id) { selected = id; },
            fetch: async () => ({ok: true, json: async () => data})
        };
        vm.createContext(routing);
        vm.runInContext(navInit + '\ninit();', routing);
        await new Promise(resolve => setImmediate(resolve));
        assert.strictEqual(selected, recovery ? 'setup' : 'mode',
            'Only recovery APs override the saved hash with the setup wizard');
        context.getDeviceInfo = async () => ({version: 'test', ap_active: true, recovery_ap: recovery});
        await context.loadVersion();
        assert.strictEqual(vm.runInContext('portalMode', context), recovery ? 'core' : 'full',
            'An active live AP must not become a core portal');
    }
    context.document.getElementById = headerLookup;
    const assets = [];
    const assetContext = { window: {}, itemMap: {}, document: {
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