const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

class Element {
    constructor() {
        this.value = '';
        this.options = [];
        this.style = {};
        this.classList = { toggle() {} };
    }

    setAttribute() {}

    appendChild(child) {
        this.options.push(child);
    }
}

function response(ok = true, json = {}) {
    return {
        ok,
        status: ok ? 200 : 500,
        headers: { get() { return 'application/json'; } },
        async json() { return json; },
    };
}

function makeDocument() {
    const elements = new Map();
    const canvasSizes = [];
    const renderedSizes = [];
    return {
        canvasSizes,
        renderedSizes,
        addEventListener() {},
        getElementById(id) {
            assert.notStrictEqual(id, 'pad-page-select', 'Import must not access the removed pad dropdown');
            if (!elements.has(id)) {
                const element = new Element();
                elements.set(id, element);
            }
            return elements.get(id);
        },
        createElement(tag) {
            if (tag !== 'canvas') return new Element();
            const context = {
                clearRect() {}, fillText() {}, putImageData() {},
                getImageData(x, y, width, height) {
                    if (width < 1 || height < 1) {
                        const error = new Error('The source width is 0');
                        error.name = 'IndexSizeError';
                        throw error;
                    }
                    return { data: new Uint8ClampedArray(width * height * 4) };
                },
            };
            return {
                getContext() { return context; },
                toBlob(callback) {
                    canvasSizes.push([this.width, this.height]);
                    callback(new Blob(['icon'], { type: 'image/png' }));
                },
            };
        },
    };
}

function loadPortal(context) {
    context.padWorkspaceRefresh = () => {};
    vm.createContext(context);
    const providedGlobals = { ...context };
    vm.runInContext(fs.readFileSync('src/app/web/portal_pad_colors.js', 'utf8'), context);
    Object.assign(context, providedGlobals);
    for (const file of [
        'src/app/web/portal_widget_editor.js',
        'src/app/web/portal_widget_gauge.js',
        'src/app/web/portal_pad_icons.js',
        'src/app/web/portal_pad_editor.js',
        'src/app/web/portal_pad_io.js',
    ]) {
        vm.runInContext(fs.readFileSync(file, 'utf8'), context, { filename: file });
    }
}

function setPadState(context, page, pad) {
    context.__pad = pad;
    vm.runInContext([
        'padState.page = ' + page + ';',
        'padState.rawJson = null;',
        'padState.cols = __pad.cols;',
        'padState.rows = __pad.rows;',
        'padState.buttons = __pad.buttons.map(function(button) { return Object.assign({}, button); });',
        'padState.bindings = [];',
    ].join('\n'), context);
}

function requestPage(url) {
    const match = url.match(/[?&]page=(\d+)/);
    return match ? Number(match[1]) : null;
}

function requestKind(url, options) {
    if (url === '/api/info') return 'saved-pad-list';
    if (url === '/api/config?no_reboot=1') return 'config-save';
    if (url === '/api/config') return options && options.method === 'POST' ? 'reboot' : 'config-read';
    if (url === '/api/reboot') return 'reboot';
    if (url === '/api/component/button-defaults/config') return 'button-defaults';
    if (url === '/api/component/display/brightness') return 'brightness-' + JSON.parse(options.body).brightness;
    if (url.startsWith('/api/icons/page')) return 'icon-delete';
    if (url.startsWith('/api/icons/source')) return options && options.method === 'POST' ? 'icon-source-upload' : 'icon-source-read';
    if (url.startsWith('/api/icons/installed?keys=')) return 'icon-metadata';
    if (url.startsWith('/api/icons/installed')) return 'icon-inventory';
    if (url.startsWith('/api/icons/install')) return 'icon-install';
    if (url.startsWith('/api/icons/file')) return options && options.method === 'DELETE' ? 'icon-file-delete' : 'icon-file-read';
    if (url.startsWith('/api/pad/button_sizes')) return 'button-sizes';
    if (url.startsWith('/api/pad/icon_context')) return 'icon-context';
    if (url.startsWith('/api/pad')) {
        if (options.method === 'DELETE') return 'pad-delete';
        if (options.method === 'POST') return 'pad-save';
        return 'pad-read';
    }
    return url;
}

function sourceId(number) {
    return 's_' + String(number).padStart(24, '0');
}

const pngSource = {
    kind: 'color',
    media_type: 'image/png',
    data: 'iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR4nGNgYAAAAAMAASsJTYQAAAAASUVORK5CYII=',
};

async function setupPadTest(options = {}) {
    const requests = [];
    const messages = [];
    const reloads = [];
    const fetchFailure = options.fetchFailure;
    const installedIcons = new Set(['pad_0_main_9_9', 'pad_1_main_9_9']);
    const sourceRecords = new Map([
        [sourceId(1), { catalog_id: 'material-symbols:home', kind: 'mono', media_type: 'image/svg+xml', data: '<svg viewBox="0 0 24 24"><path d="M0 0h24v24H0z"/></svg>' }],
        [sourceId(2), { catalog_id: 'noto:house', kind: 'color', media_type: 'image/svg+xml', data: '<svg viewBox="0 0 24 24"><path fill="#ff0000" d="M0 0h24v24H0z"/></svg>' }],
        [sourceId(3), { catalog_id: 'material-symbols:lightbulb', kind: 'mono', media_type: 'image/svg+xml', data: '<svg viewBox="0 0 24 24"><path d="M0 0h24v24H0z"/></svg>' }],
    ]);
    for (const [id, record] of options.sources || []) sourceRecords.set(id, record);
    let uploadedSourceNumber = 100;
    let corruptState = true;
    const document = makeDocument();
    const renderedSizes = document.renderedSizes;
    const context = {
        Blob,
        crypto: require('crypto').webcrypto,
        atob,
        btoa,
        TextEncoder,
        Uint8ClampedArray,
        window: {},
        console: { error() {}, log() {}, warn() {} },
        document,
        deviceInfoCache: { display_blank_on_save: true, max_pads: 16, max_pad_buttons: 64,
            available_screens: Array.from({ length: 16 }, (_, page) => ({ id: 'pad_' + page })) },
        MAX_ACTIONS: 3,
        confirm() { return true; },
        showMessage(message, type) { messages.push({ message, type }); },
        padBindingsFromJson() { return []; },
        padBindingsToDict() { return null; },
        padRenderBindings() { context.importedBindingsRendered = true; },
        actionEditorBuild() { return {}; },
        actionEditorLoad() {},
        actionEditorOperation(id) {
            if (id === 'screen.open') return { id, arguments: [{ name: 'target', type: 'screen', max_length: 31, dynamic: true }] };
            if (id === 'timer.adjust') return { id, arguments: [
                { name: 'timer', type: 'integer', min: 1, max: 4, dynamic: true },
                { name: 'seconds', type: 'integer', min: -86400, max: 86400, dynamic: true },
            ] };
            return null;
        },
        validateBinding(value) { return { valid: value !== '[invalid' }; },
        actionEditorListLoad(prefixes, actions) {
            (actions || []).forEach(function(a, i) { context.actionEditorLoad(prefixes[i], a); });
        },
        actionEditorListBuild(prefixes) {
            return prefixes.map(function(p) { return context.actionEditorBuild(p); })
                .filter(function(a) { return a && a.op; });
        },
        padColorsToHex() {},
        padGetEffectiveDefault() { return '#000000'; },
        padGetBindableColor() { return '#000000'; },
        padSetBindableColor() {},
        padInitBindableColor() {},
        padLoadLevelActions() {},
        padPopulateTemplateDropdown() {},
        bindingValidatePadBindings() { return { valid: context.importedBindingsRendered === true, count: context.importedBindingsRendered ? 0 : 1 }; },
        bindingValidateDefaults() { return { valid: true, count: 0 }; },
        getDeviceInfo: async function() { requests.push({ kind: 'device-info' }); },
        padLoadPage: async function(page) { reloads.push(page); },
        setTimeout(callback) {
            Promise.resolve().then(callback);
            return 1;
        },
        fetch: async function(url, fetchOptions = {}) {
            const kind = requestKind(url, fetchOptions);
            const entry = { url, options: fetchOptions, kind, page: requestPage(url) };
            requests.push(entry);
            await Promise.resolve();

            if (corruptState && kind === 'config-read' && !options.keepEditor) {
                corruptState = false;
                vm.runInContext('padState.page = 7; padState.cols = 8; padState.rows = 8; padState.buttons = [{ col: 7, row: 7, icon_id: "emoji_wrong" }];', context);
            }
            if (fetchFailure === kind) return response(false);
            if (kind === 'saved-pad-list') return response(true, { configured_pads: [0, 1] });
            if (kind === 'icon-context') return response(true, { dependents: [] });
            if (kind === 'config-read') return response(true, { backlight_brightness: 42 });
            if (kind === 'icon-metadata') return response(true, { icons: [...installedIcons], artifacts: {} });
            if (kind === 'icon-inventory') return response(true, { icons: [...installedIcons] });
            if (kind === 'icon-source-read') {
                const id = new URL(url, 'http://localhost').searchParams.get('id');
                const source = sourceRecords.get(id);
                if (source && source.media_type === 'image/png') return {
                    ok: true, status: 200,
                    headers: { get() { return 'image/png'; } },
                    async blob() { return new Blob([Buffer.from(source.data, 'base64')], { type: 'image/png' }); },
                    async json() { throw new Error('Binary PNG must not be parsed as JSON'); },
                };
                return response(true, source || {});
            }
            if (url === '/api/pad?page=1' && !fetchOptions.method) {
                return response(true, {
                    buttons: [{ icon: { source_id: sourceId(3), catalog_id: 'material-symbols:lightbulb', kind: 'mono' } }],
                    overlay_buttons: [],
                });
            }
            if (kind === 'icon-source-upload') {
                const id = sourceId(uploadedSourceNumber++);
                sourceRecords.set(id, fetchOptions.headers['Content-Type'] === 'image/png'
                    ? { kind: 'color', media_type: 'image/png', data: Buffer.from(fetchOptions.body).toString('base64') }
                    : JSON.parse(fetchOptions.body));
                return response(true, { id });
            }
            if (kind === 'icon-file-delete') {
                installedIcons.delete(new URL(url, 'http://localhost').searchParams.get('name').replace(/\.png$/, ''));
            }
            if (kind === 'button-sizes') return response(true, {
                button_w: 100, button_h: 100, padding: 4, gap: 2, font_small_h: 10,
            });
            return response(true);
        },
    };
    loadPortal(context);
    context.padDialogShowValidationError = () => {};
    const normalizeProductionIconSource = context.padNormalizeIconSource;
    context.padNormalizeIconSource = function(record, expected) {
        if (record.catalog_id !== expected.catalog_id || record.kind !== expected.kind) throw new Error('Source mismatch');
        return record;
    };
    context.padRenderIconPNG = async function(record, size) {
        renderedSizes.push(size);
        return new Blob(['icon'], { type: 'image/png' });
    };
    context.padLoadPage = async function(page) {
        reloads.push(page);
        vm.runInContext('padState.loadError = null;', context);
    };
    context.padBuildLevelActions = function() {
        return vm.runInContext('padState.padActions.map(function(action) { return Object.assign({}, action); })', context);
    };
    setPadState(context, options.originalPage === undefined ? 1 : options.originalPage, {
        cols: 3,
        rows: 2,
        buttons: [],
    });
    vm.runInContext('padState.loadError = null;', context);

    return { context, requests, messages, reloads, canvasSizes: document.canvasSizes, renderedSizes,
        normalizeProductionIconSource, validateImportedIconSources: context.padValidateImportedIconSources };
}

async function testDeviceExportDefaults() {
    let download;
    let fail = false;
    const messages = [];
    const context = {
        Blob,
        TextEncoder,
        console,
        deviceInfoCache: { max_pads: 1 },
        window: {},
        document: { createElement: () => ({ click() {} }) },
        URL: {
            createObjectURL(blob) { download = blob; return 'blob:export'; },
            revokeObjectURL() {},
        },
        showMessage(message, type) { messages.push({ message, type }); },
        fetch: async url => {
            if (url === '/api/config') return response(true, { mqtt_enabled: true });
            if (url.startsWith('/api/pad?')) return { ok: false, status: 404 };
            if (url === '/api/component/button-defaults/config')
                return fail ? response(false) : response(true, {
                    icon_badge_padding: 7, firmware_defaults: {}, typography_status: { reboot_required: true },
                });
            throw new Error('Unexpected request: ' + url);
        },
        padCollectExportIconSources: async () => ({}),
        DEVICE_CONFIG_FORMAT: 'esp32-macropad-config',
        DEVICE_CONFIG_VERSION: 2,
    };
    vm.createContext(context);
    vm.runInContext(fs.readFileSync('src/app/web/portal_pad_io.js', 'utf8'), context);
    await context.deviceExportConfig();
    assert.deepStrictEqual(JSON.parse(await download.text()).button_defaults, { icon_badge_padding: 7 });
    download = null;
    fail = true;
    await context.deviceExportConfig();
    assert.strictEqual(download, null, 'A failed defaults read must not produce an incomplete backup');
    assert(messages.some(message => message.type === 'error' && message.message.includes('Failed to fetch pad and button defaults')));
}

async function testClearPad() {
    const document = makeDocument();
    const messages = [];
    let confirmed = false;
    const context = {
        console, document, deviceInfoCache: {}, MAX_ACTIONS: 3,
        confirm() { return confirmed; },
        showMessage(message, type) { messages.push({ message, type }); },
        padGetEffectiveDefault() { return '#123456'; },
        padGetBindableColor() { return document.getElementById('pad-edit-page-bg-color').value; },
        padInitBindableColor() {},
        padSetBindableColor(id, value) { document.getElementById(id).value = value; },
        padRenderBindings() {},
        actionEditorListLoad(prefixes, actions) { context.actions = actions; },
        actionEditorListBuild() { return context.actions; },
        padDialogClearValidationError() { context.validationError = ''; },
        padRenderGrid() {},
        padExitPlacementMode() { vm.runInContext('padState.placingBlock = null;', context); },
    };
    loadPortal(context);
    context.padCaptureIconLayout = () => ({});
    document.getElementById('pad-edit-page-bg-color').parentElement = { style: {} };
    for (const id of ['pad-row-heights', 'pad-column-widths']) {
        const field = document.getElementById(id);
        field.classList = { toggle(name, enabled) { field.invalid = enabled; } };
        field.setAttribute = (name, value) => { field[name] = value; };
    }
    vm.runInContext(`
        padState.page = 4;
        padState.cols = 5; padState.rows = 4;
        padState.rawJson = { custom_setting: true, row_heights_px: [0, 0, 0, 100] };
        padState.selectedLayer = 'overlay';
        padState.layerSelections = { main: { col: 1, row: 1 }, overlay: { col: 2, row: 2 } };
        padState.mainButtons = [{ col: 0, row: 0 }];
        padState.overlayButtons = [{ col: 1, row: 1 }];
        padState.bindings = [{ name: 'color', value: '#ffffff' }];
        padState.templatePad = 1; padState.templateButtons = [{ col: 2, row: 0 }];
        padState.backgroundImagePath = '/images/old.png';
        padState.trackValidationError = true;
        padState.importedIconSources = { stale: {} };
        padState.importedIconSourceIds = { stale: 'source' };
        padState.placingBlock = {}; padState.dragSource = { col: 1, row: 1 };
        padState.btnClipboard = { icon: { draft_source_id: 'clipboard', kind: 'color' } };
        padState.padClipboard = { buttons: [], overlay_buttons: [] };
        padState.draftIconSources = { stale: {}, clipboard: {} };
        padState.buttonDefaults = { bg_color: '#abcdef' };
    `, context);
    context.actions = [{ type: 'screen' }];
    context.validationError = 'Invalid track sizes';
    document.getElementById('pad-row-heights').value = '4:100';
    document.getElementById('pad-column-widths').value = '2:';
    for (const id of ['pad-name', 'pad-wake-screen', 'pad-button-shadow', 'pad-page-bg-mode']) {
        document.getElementById(id).value = 'override';
    }
    const before = vm.runInContext('JSON.stringify(padState)', context);
    await context.padDeletePage();
    assert.strictEqual(vm.runInContext('JSON.stringify(padState)', context), before,
        'Cancelling Clear Pad must preserve the draft');
    assert.strictEqual(document.getElementById('pad-row-heights').value, '4:100');
    assert.strictEqual(messages.length, 0);

    confirmed = true;
    await context.padDeletePage();
    assert.deepStrictEqual(messages, [{ message: 'Pad cleared (unsaved)', type: 'success' }]);
    assert.strictEqual(vm.runInContext('padState.loadError', context), null,
        'Confirming Clear Pad permits an intentional replacement');
    const saved = context.padBuildSaveContext();
    assert.strictEqual(saved.page, 4);
    assert.deepStrictEqual(JSON.parse(saved.body), { action_format_version: 1, layout: 'grid', cols: 3, rows: 2, buttons: [] },
        'Clearing a customized pad must produce a saveable empty pad without old settings');
    for (const id of ['pad-row-heights', 'pad-column-widths']) {
        const field = document.getElementById(id);
        assert.strictEqual(field.value, '');
        assert.strictEqual(field.invalid, false);
        assert.strictEqual(field['aria-invalid'], 'false');
        assert.strictEqual(document.getElementById(id + '-error').hidden, true);
    }
    assert.strictEqual(document.getElementById('pad-custom-track-sizes-section').open, false);
    assert.strictEqual(document.getElementById('pad-edit-page-bg-color').readOnly, true);
    assert.strictEqual(context.validationError, '');
    assert.strictEqual(vm.runInContext('padDirty', context), true);
    assert.strictEqual(vm.runInContext('padState.selectedLayer', context), 'main');
    assert.strictEqual(vm.runInContext('padState.trackValidationError', context), false);
    assert.strictEqual(vm.runInContext('padState.placingBlock', context), null);
    assert.strictEqual(vm.runInContext('padState.dragSource', context), null);
    assert.strictEqual(vm.runInContext('JSON.stringify(padState.layerSelections)', context), '{"main":null,"overlay":null}');
    for (const key of ['importedIconSources', 'importedIconSourceIds']) {
        assert.strictEqual(vm.runInContext('JSON.stringify(padState.' + key + ')', context), '{}');
    }
    assert.strictEqual(vm.runInContext('JSON.stringify(Object.keys(padState.draftIconSources))', context), '["clipboard"]');
    assert.strictEqual(vm.runInContext('padState.buttonDefaults.bg_color', context), '#abcdef',
        'Clear Pad must preserve device-wide defaults');
    assert.strictEqual(vm.runInContext('padState.btnClipboard.icon.draft_source_id', context), 'clipboard');
    assert.notStrictEqual(vm.runInContext('padState.padClipboard', context), null);
}

(async function() {
    await testDeviceExportDefaults();
    await testClearPad();
    const success = await setupPadTest();
    for (const invalid of [
        { action_format_version: 2, buttons: [] },
        { buttons: [{ actions: [{ op: 'screen.open', args: { target: 'pad_0' } }] }] },
        { action_format_version: 1, buttons: [{ actions: [{ type: 'screen', target: 'pad_0' }] }] },
        { action_format_version: 1, buttons: [{ actions: [{ type: '', op: 'screen.open', args: {} }] }] },
        { action_format_version: 1, buttons: [{ actions: [{ op: 'unknown', args: {} }] }] },
        { action_format_version: 1, buttons: [{ actions: [{ op: 'screen.open', args: {} }] }] },
        { action_format_version: 1, buttons: [{ actions: [{ op: 'screen.open', args: { target: 'pad_0', extra: 1 } }] }] },
        { action_format_version: 1, buttons: [{ actions: [{ op: 'screen.open', args: { target: { $template: '[var:screen]', extra: 1 } } }] }] },
        { action_format_version: 1, buttons: [{ actions: [{ op: 'screen.open', args: { target: { $template: '[invalid' } } }] }] },
        { action_format_version: 1, buttons: [{ actions: [{ op: 'screen.open', args: { target: '' } }] }] },
        { action_format_version: 1, buttons: [{ actions: [{ op: 'timer.adjust', args: { timer: 1.5, seconds: 1 } }] }] },
    ]) {
        assert.throws(() => success.context.padCheckImportedActionFormat(
            Object.assign({ cols: 1, rows: 1 }, invalid)));
    }
    success.context.deviceInfoCache.max_grid_cols = 10;
    success.context.deviceInfoCache.max_grid_rows = 10;
    success.context.padPageBackgroundModeChanged = () => {};
    success.context.padLoadTemplateButtons = async () => {};
    success.context.padRenderGrid = () => {};
    success.context.padMarkDirty = () => {};
    const importPad = async pad => success.context.padImportPad({
        target: { files: [{ text: async () => JSON.stringify(pad) }], value: 'pad.json' },
    });
    await importPad({ cols: 10, rows: 10, bg_image_path: '/images/background.png', buttons: [{ col: 9, row: 9 }] });
    assert(success.messages.some(message => message.type === 'success' && message.message.includes('Pad imported')));
    assert.strictEqual(vm.runInContext('padState.cols', success.context), 10);
    assert.strictEqual(JSON.parse(success.context.padBuildSaveContext().body).bg_image_path,
        '/images/background.png', 'Imported pad background references must survive saving');
    const unpackagedWidget = {
        col: 0, row: 0, widget_type: 'external', extension_id: 'other-board-widget',
        extension_config: { source: 'sensor' }, extension_tick_interval_ms: 750,
    };
    await importPad({ action_format_version: 1, cols: 1, rows: 1, buttons: [unpackagedWidget] });
    assert.strictEqual(JSON.parse(success.context.padBuildSaveContext().body).bg_image_path, undefined,
        'Importing a pad without a background must clear the previous selection');
    assert.deepStrictEqual(JSON.parse(vm.runInContext('JSON.stringify(padState.buttons)', success.context)),
        [unpackagedWidget], 'Import must retain widgets whose editor is not packaged');
    assert.deepStrictEqual(JSON.parse(success.context.padBuildSaveContext().body).buttons,
        [unpackagedWidget], 'Saving an unedited imported widget must preserve its complete configuration');
    await importPad({
        action_format_version: 0, cols: 10, rows: 10, name: 'Legacy import',
        pad_actions: [{ type: 'screen' }],
        buttons: [{ col: 0, row: 0, label: 'Retained', actions: [{ type: 'mqtt' }] }],
        overlay_buttons: [{ col: 1, row: 1, label: 'Overlay', lp_action: 'screen' }],
    });
    assert.deepStrictEqual(JSON.parse(vm.runInContext('JSON.stringify(padState.mainButtons)', success.context)),
        [{ col: 0, row: 0, label: 'Retained' }]);
    assert.deepStrictEqual(JSON.parse(vm.runInContext('JSON.stringify(padState.overlayButtons)', success.context)),
        [{ col: 1, row: 1, label: 'Overlay' }]);
    assert.strictEqual(vm.runInContext('padState.padActions.length', success.context), 0);
    assert.strictEqual(success.context.document.getElementById('pad-name').value, 'Legacy import');
    assert(success.messages.some(message => /older action format.*draft/i.test(message.message)));
    const oldBackgroundPad = {
        action_format_version: 1, cols: 10, rows: 10,
        buttons: [{ col: 0, row: 0, label: 'Keep me', bg_color: 'transparent', bg_opacity: 25,
            fg_color: 'ffffff', actions: [{ op: 'screen.open', args: { target: 'pad_1' } }] }],
        overlay_buttons: [{ col: 1, row: 1, label: 'Overlay', bg_color: 'not-a-color', bg_opacity: 0 }],
    };
    const writesBeforeCleanup = success.requests.filter(request => request.kind === 'pad-save').length;
    await importPad(oldBackgroundPad);
    const cleanedBackgrounds = JSON.parse(success.context.padBuildSaveContext().body);
    assert.deepStrictEqual(cleanedBackgrounds.buttons, [{
        col: 0, row: 0, label: 'Keep me', fg_color: 'ffffff',
        actions: [{ op: 'screen.open', args: { target: 'pad_1' } }],
    }]);
    assert.deepStrictEqual(cleanedBackgrounds.overlay_buttons, [{ col: 1, row: 1, label: 'Overlay' }]);
    assert.strictEqual(success.requests.filter(request => request.kind === 'pad-save').length, writesBeforeCleanup,
        'Import cleanup must not write the pad');
    assert(success.messages.some(message => /unsupported button backgrounds.*draft/i.test(message.message)));
    assert.strictEqual(oldBackgroundPad.buttons[0].bg_color, 'transparent', 'Import must not alter the original input');
    await importPad({ action_format_version: 2, cols: 1, rows: 1, buttons: [] });
    assert.strictEqual(vm.runInContext('padState.cols', success.context), 10, 'A newer format must not replace the draft');
    await importPad({ action_format_version: 1, cols: 1, rows: 1,
        buttons: [{ actions: [{ type: 'mqtt' }] }] });
    assert.strictEqual(vm.runInContext('padState.cols', success.context), 10, 'Invalid current-format actions must not be discarded');
    success.context.deviceInfoCache.max_grid_cols = 5;
    await importPad({ cols: 10, rows: 10, buttons: [] });
    assert(success.messages.some(message => message.type === 'error' && message.message.includes('maximum 5 columns')));
    vm.runInContext("padState.loadError = 'Load failed';", success.context);
    let replacementPrompt = '';
    success.context.confirm = message => { replacementPrompt = message; return false; };
    await importPad({ cols: 2, rows: 2, buttons: [] });
    assert.match(replacementPrompt, /replace its settings/i);
    assert.strictEqual(vm.runInContext('padState.cols', success.context), 10,
        'Declining replacement preserves the existing editor state');
    success.context.confirm = () => true;
    await importPad({ cols: 2, rows: 2, buttons: [] });
    assert.strictEqual(vm.runInContext('padState.loadError', success.context), null,
        'A confirmed compatible import permits an intentional replacement');
    const uploadFetch = success.context.fetch;
    success.context.fetch = async () => response(false, { success: false, message: 'Invalid icon source or PNG' });
    await assert.rejects(success.context.padUploadIconSources({ sourceUploads: [['draft:test', pngSource]] }),
        /Invalid icon source or PNG/);
    success.context.fetch = uploadFetch;
    const normalizedPng = success.normalizeProductionIconSource(pngSource, { kind: 'color' });
    assert.strictEqual(JSON.stringify(normalizedPng), JSON.stringify(pngSource),
        'Local PNG source records retain the exact approved envelope');
    for (const extraBytes of [0, 1, 2]) {
        const data = Buffer.concat([Buffer.from(pngSource.data, 'base64'), Buffer.alloc(extraBytes)]).toString('base64');
        assert.strictEqual(success.normalizeProductionIconSource(
            Object.assign({}, pngSource, { data }), { kind: 'color' }).data, data,
        'Local PNG source validation accepts every canonical padding form');
    }
    assert.throws(() => success.normalizeProductionIconSource(
        Object.assign({}, pngSource, { catalog_id: 'noto:house' }), { kind: 'color' }), /metadata is invalid/);
    assert.throws(() => success.normalizeProductionIconSource(
        Object.assign({}, pngSource, { media_type: 'image/jpeg' }), { kind: 'color' }), /SVG artwork/);
    assert.throws(() => success.normalizeProductionIconSource(
        Object.assign({}, pngSource, { data: 'not-base64' }), { kind: 'color' }), /canonical base64/);
    const oversizedPng = Buffer.from(pngSource.data, 'base64');
    oversizedPng.writeUInt32BE(721, 16);
    assert.throws(() => success.normalizeProductionIconSource(
        Object.assign({}, pngSource, { data: oversizedPng.toString('base64') }), { kind: 'color' }), /dimensions/);
    const largeLocalPng = Object.assign({}, pngSource, {
        data: Buffer.concat([Buffer.from(pngSource.data, 'base64'), Buffer.alloc(50 * 1024)]).toString('base64'),
    });
    const localSourceId = sourceId(9);
    assert.ok(Buffer.byteLength(JSON.stringify(largeLocalPng)) > 64 * 1024);
    const validatedSources = success.validateImportedIconSources(
        [{ buttons: [{ icon: { source_id: localSourceId, kind: 'color' } }] }],
        { [localSourceId]: largeLocalPng });
    assert.strictEqual(validatedSources[localSourceId].data, largeLocalPng.data,
        'Import accepts normalized local PNG sources above the catalog SVG limit');
    const localImport = await setupPadTest({
        sources: [[localSourceId, largeLocalPng]],
    });
    const retained = await localImport.context.padLoadRetainedIconSource(localSourceId, { kind: 'color' });
    assert.strictEqual(retained.data, largeLocalPng.data);
    const selectorImport = localImport;
    assert.strictEqual(selectorImport.context.padOptionIconRenderKey(
        { cols: 3, rows: 2, column_widths_px: [0, 40, 0] },
        { col: 1, row: 0, col_span: 2, widget_type: 'option_selector',
            widget_option_label: 'Cool', widget_option_icon_5: { kind: 'mono' },
            widget_option_icon_size_pct: 75, widget_option_icon_badge_padding: 4 }, 5), 'e3f58eb6');
    assert.strictEqual(selectorImport.context.padOptionIconRenderKey(
        { cols: 3, rows: 2, column_widths_px: [0, 40, 0] },
        { col: 1, row: 0, col_span: 2, widget_type: 'option_selector',
            widget_option_label: 'Cool', widget_option_icon_5: { kind: 'mono' },
            widget_option_icon_size_pct: 75, widget_option_icon_badge_padding: 4,
            reserve_label_slots: true }, 5), 'f3611d57');
    assert.strictEqual(selectorImport.context.padOptionIconRenderKey(
        { cols: 3, rows: 2, column_widths_px: [0, 40, 0] },
        { col: 1, row: 0, col_span: 2, widget_type: 'option_selector',
            widget_option_label: 'Cool', widget_option_icon_5: { kind: 'mono' },
            widget_option_icon_size_pct: 75, widget_option_icon_badge_padding: 4,
            widget_option_inset: 0 }, 5), '3cf96935');
    assert.strictEqual(selectorImport.context.padOptionIconRenderKey(
        { cols: 3, rows: 2, column_widths_px: [0, 40, 0] },
        { col: 1, row: 0, col_span: 2, widget_type: 'option_selector',
            widget_option_label: 'Cool', widget_option_icon_5: { kind: 'mono' },
            widget_option_icon_size_pct: 75, widget_option_icon_badge_padding: 4,
            widget_option_inset: 0, widget_option_height_pct: 50 }, 5), 'f09f7bd1');
    await assert.rejects(localImport.context.padReadIconSourceResponse({
        headers: { get() { return 'image/png'; } },
        async blob() { return new Blob([new Uint8Array(512 * 1024 + 1)]); },
    }), /too large/);

    const exportedSources = await success.context.padCollectExportIconSources([{
        page: 0,
        payload: {
            template_pad: 1,
            buttons: [{ icon: { source_id: sourceId(1), catalog_id: 'material-symbols:home', kind: 'mono' } }],
            overlay_buttons: [{ icon: { source_id: sourceId(2), catalog_id: 'noto:house', kind: 'color' } }],
        },
    }], [0], 2);
    assert.deepStrictEqual(Object.keys(exportedSources).sort(), [sourceId(1), sourceId(2), sourceId(3)].sort());
    await assert.rejects(success.context.padCollectExportIconSources([{
        page: 0,
        payload: { buttons: [{ icon: { source_id: sourceId(9), catalog_id: 'material-symbols:home', kind: 'mono' } }] },
    }], [0], 2), /Source mismatch/);

    const smallPrepared = await success.context.padPreparePageSave({
        page: 0, iconLayout: {},
        body: JSON.stringify({ cols: 2, rows: 2, column_widths_px: [10, 0], row_heights_px: [10, 0],
            buttons: [{ col: 0, row: 0, icon: { source_id: sourceId(1), catalog_id: 'material-symbols:home', kind: 'mono' } }] }),
    });
    assert.strictEqual(smallPrepared.variants[0].blob.type, 'image/png');
    assert.strictEqual(success.renderedSizes[success.renderedSizes.length - 1], 1,
        'A 10px track with 4px padding and 1px borders must produce a 1px icon variant');

    const legacyButtons = [{ col: 0, row: 0, icon_id: 'mi_home' }];
    const legacyOverlay = [{ col: 1, row: 1, icon: { icon_id: 'emoji_house' } }];
    await assert.rejects(success.context.padPreparePageSave({
        page: 0,
        iconLayout: {},
        payload: { cols: 2, rows: 2, buttons: legacyButtons, overlay_buttons: legacyOverlay },
    }), /Unsupported icon_id/);
    assert.strictEqual(legacyButtons[0].icon_id, 'mi_home', 'Save preparation must not mutate editor state');
    assert.strictEqual(legacyOverlay[0].icon.icon_id, 'emoji_house');

    const legacyTemplateButtons = [{ col: 0, row: 0, icon_id: 'mi_home' }];
    await assert.rejects(success.context.padPreparePageSave({
        page: 0,
        templatePage: 1,
        templateButtons: legacyTemplateButtons,
        iconLayout: {},
        payload: { cols: 2, rows: 2, template_pad: 1, buttons: [], overlay_buttons: [] },
    }), /Unsupported icon_id/);
    assert.strictEqual(legacyTemplateButtons[0].icon_id, 'mi_home', 'Template preparation must not mutate cached config');

    const originalFetch = success.context.fetch;
    success.context.fetch = async function(url, fetchOptions = {}) {
        if (url === '/api/pad/icon_context?page=0') {
            return response(true, { dependents: [{ page: 1, cols: 1, rows: 1, buttons: [], button_count: 0 }] });
        }
        return originalFetch(url, fetchOptions);
    };
    const templatePrepared = await success.context.padPreparePageSave({
        page: 0,
        iconLayout: {},
        payload: {
            cols: 2,
            rows: 2,
            buttons: [{ col: 0, row: 0, icon: {
                source_id: sourceId(1), catalog_id: 'material-symbols:home', kind: 'mono',
            } }],
            overlay_buttons: [],
        },
    });
    assert(templatePrepared.variants.some(variant => variant.key === 'pad_1_main_0_0'));
    success.context.fetch = async function(url, fetchOptions = {}) {
        if (url === '/api/pad/icon_context?page=0') {
            return response(true, { dependents: [{ page: 1, cols: 1, rows: 1, buttons: [], button_count: 64 }] });
        }
        return originalFetch(url, fetchOptions);
    };
    await assert.rejects(success.context.padPreparePageSave({
        page: 0,
        iconLayout: {},
        payload: templatePrepared.payload,
    }), /Combined Main and Overlay button limit exceeded for dependent Pad 2/);

    vm.runInContext(fs.readFileSync('src/app/web/portal_pad_defaults.js', 'utf8'), success.context);
    vm.runInContext([
        'PAD_FIRMWARE_DEFAULTS = { content_pad: "4", border_width: "1", button_spacing_px: "2",',
        '  pad_inset_top_px: 0, pad_inset_right_px: 0, pad_inset_bottom_px: 0, pad_inset_left_px: 0,',
        '  pixel_shift_distance_px: 0 };',
        'PAD_TYPOGRAPHY_STATUS = { reboot_required: false };',
        'padState.buttonDefaults = { content_pad: "4" };',
    ].join('\n'), success.context);
    success.context.padCollectButtonDefaults = () => ({ content_pad: '6' });
    success.context.padLoadButtonDefaultsFromDevice = async () => {};
    success.context.padRenderGrid = () => {};
    success.context.showMessage = () => {};
    const importFetch = success.context.fetch;
    const parentIconPad = {
        cols: 2,
        rows: 2,
        buttons: [{ col: 0, row: 0, icon: {
            source_id: sourceId(1), catalog_id: 'material-symbols:home', kind: 'mono',
        } }],
        overlay_buttons: [],
    };
    const dependentIconPad = { template_pad: 0, cols: 1, rows: 1, buttons: [], overlay_buttons: [] };
    success.context.fetch = async function(url, fetchOptions = {}) {
        const originalResponse = await importFetch(url, fetchOptions);
        if (url === '/api/pad?page=0' && !fetchOptions.method) return response(true, parentIconPad);
        if (url === '/api/pad?page=1' && !fetchOptions.method) return response(true, dependentIconPad);
        if (url.startsWith('/api/pad/button_sizes')) return response(true, {
            button_w: 100, button_h: 100, padding: 6, gap: 2, font_small_h: 10,
        });
        if (url.startsWith('/api/icons/installed')) return response(true, { icons: [], artifacts: {} });
        return originalResponse;
    };
    const regenerationStart = success.requests.length;
    await success.context.padSaveButtonDefaults();
    const regenerationRequests = success.requests.slice(regenerationStart);
    assert.strictEqual(regenerationRequests.filter(request => request.kind === 'pad-save').length, 2);
    assert.strictEqual(regenerationRequests.filter(request => request.kind === 'icon-install').length, 2);
    assert.deepStrictEqual(success.renderedSizes.slice(-2), [86, 86],
        'Updated default padding must size parent and inherited child variants');

    const concurrent = await setupPadTest({ keepEditor: true });
    vm.runInContext('padState.loadError = null;', concurrent.context);
    concurrent.context.padBuildSaveContext = () => ({
        page: 1, blankOnSave: true,
        payload: { cols: 4, rows: 1, template_pad: 0,
            buttons: [{ col: 1, row: 0, icon: {
                source_id: sourceId(1), catalog_id: 'material-symbols:home', kind: 'mono',
            } }] },
        templatePage: 0, templateButtons: [{ col: 0, row: 0, icon: {
            source_id: sourceId(1), catalog_id: 'material-symbols:home', kind: 'mono',
        } }],
    });
    const ordinaryContext = concurrent.context.padBuildSaveContext;
    concurrent.context.padBuildSaveContext = () => {
        const context = ordinaryContext();
        context.payload.buttons[0].icon.source_id = sourceId(1);
        return context;
    };
    const firstConcurrentRequest = concurrent.requests.length;
    await Promise.all([
        concurrent.context.padSavePage(),
        concurrent.context.padSavePage(),
    ]);
    assert.deepStrictEqual(
        concurrent.requests.slice(firstConcurrentRequest).map(request => request.kind)
            .filter(kind => kind !== 'icon-metadata' && kind !== 'icon-source-read' && kind !== 'button-sizes'),
        [
            'icon-context', 'config-read', 'brightness-0', 'icon-install', 'icon-install', 'pad-save', 'brightness-42',
            'device-info',
            'icon-context', 'config-read', 'brightness-0', 'icon-install', 'icon-install', 'pad-save', 'brightness-42',
            'device-info',
        ],
        'concurrent saves must not overlap display blanking'
    );

    const reuse = await setupPadTest();
    const reusePayload = {
        cols: 2, rows: 2,
        buttons: [{ col: 0, row: 0, icon: {
            source_id: sourceId(1), catalog_id: 'material-symbols:home', kind: 'mono',
        } }],
    };
    const artifact = {
        source_id: sourceId(1), kind: 1, width: 90, height: 90,
        render_key: reuse.context.padIconRenderKey(reusePayload, reusePayload.buttons[0]),
    };
    let reportedArtifact = artifact;
    const reuseFetch = reuse.context.fetch;
    reuse.context.fetch = async function(url, fetchOptions = {}) {
        const result = await reuseFetch(url, fetchOptions);
        if (url.startsWith('/api/icons/installed?keys=')) {
            return response(true, { artifacts: reportedArtifact ? { pad_0_main_0_0: reportedArtifact } : {} });
        }
        if (url === '/api/icons/installed') return response(true, { icons: ['pad_0_main_0_0'] });
        return result;
    };
    const rendersBeforeReuse = reuse.renderedSizes.length;
    const requestsBeforeReuse = reuse.requests.length;
    const reused = await reuse.context.padPreparePageSave({ page: 0, payload: reusePayload, iconLayout: {} });
    assert.strictEqual(reused.variants.length, 0);
    assert.strictEqual(reuse.renderedSizes.length, rendersBeforeReuse, 'Matching artifacts must skip PNG rendering');
    await reuse.context.padPersistPage(reused);
    const reuseRequests = reuse.requests.slice(requestsBeforeReuse);
    assert.strictEqual(reuseRequests.filter(request => request.kind === 'icon-metadata').length, 1);
    assert(reuseRequests.some(request => request.kind === 'pad-save'), 'Reusing icons must not skip the pad save');
    assert(!reuseRequests.some(request => request.kind === 'icon-install' || request.kind === 'icon-file-delete'),
        'An unchanged save must neither upload nor delete the matching artifact');
    assert.strictEqual(reuseRequests.find(request => request.kind === 'icon-metadata').url,
        '/api/icons/installed?keys=pad_0_main_0_0');

    const optionIcons = {
        cols: 2, rows: 2,
        buttons: [{ col: 0, row: 0, widget_type: 'option_selector',
            ...Object.fromEntries(Array.from({ length: 5 }, (_, i) =>
                ['widget_option_icon' + (i ? '_' + (i + 1) : ''), reusePayload.buttons[0].icon])) }],
    };
    const requestsBeforeBatch = reuse.requests.length;
    await reuse.context.padPreparePageSave({ page: 0, payload: optionIcons, iconLayout: {} });
    const batches = reuse.requests.slice(requestsBeforeBatch)
        .filter(request => request.kind === 'icon-metadata')
        .map(request => new URL(request.url, 'http://localhost').searchParams.get('keys').split(','));
    assert.deepStrictEqual(batches.map(batch => batch.length), [1, 1, 1, 1, 1]);
    assert(batches.flat().includes('pad_0_main_0_0_opt_5'));

    const optionSizing = async (overrides, expected) => {
        const button = {
            col: 0, row: 0, widget_type: 'option_selector', border_width: '1', content_pad: '4',
            widget_option_icon: reusePayload.buttons[0].icon, ...overrides,
        };
        const payload = { cols: 1, rows: 1, buttons: [button] };
        await reuse.context.padPreparePageSave({ page: 0, payload, iconLayout: {} });
        assert.strictEqual(reuse.renderedSizes.at(-1), expected);
    };
    await optionSizing({}, 82);
    await optionSizing({ widget_option_height_pct: 50 }, 37);
    await optionSizing({ widget_option_weight: 1, widget_option_weight_2: 9 }, 1);
    await optionSizing({ widget_option_weight: 2, widget_option_label_2: 'Other',
        widget_option_weight_2: 3 }, 28);
    await optionSizing({ widget_option_weight: 3, widget_option_label_2: 'Other',
        widget_option_weight_2: 2 }, 46);
    await optionSizing({ label_top: 'Title', widget_position: 'right', group_size_pct: 75 }, 14);
    await optionSizing({ reserve_label_slots: true, widget_position: 'right', group_size_pct: 75 }, 14);
    await optionSizing({ label_bottom: 'Title', widget_position: 'above', group_size_pct: 75 }, 14);
    await optionSizing({ icon: reusePayload.buttons[0].icon, widget_position: 'below', group_size_pct: 75 }, 14);
    await optionSizing({ label_center: 'Dynamic', widget_position: 'left' }, 78);
    await optionSizing({ label_center: 'Title', group_size_pct: 75, content_pad: '10', border_width: '2' }, 10);
    await optionSizing({ label_center: 'Title', group_size_pct: 50, widget_option_label_2: 'Other',
        widget_option_inset: 2, widget_option_icon_badge_padding: 3, widget_option_icon_size_pct: 75 }, 9);
    const defaultInsetKey = reuse.context.padOptionIconRenderKey(optionIcons, optionIcons.buttons[0], 1);
    assert.notStrictEqual(reuse.context.padOptionIconRenderKey(optionIcons,
        { ...optionIcons.buttons[0], widget_option_inset: 0 }, 1), defaultInsetKey);
    assert.notStrictEqual(reuse.context.padOptionIconRenderKey(optionIcons,
        { ...optionIcons.buttons[0], widget_option_height_pct: 50 }, 1), defaultInsetKey);
    assert.notStrictEqual(reuse.context.padOptionIconRenderKey(optionIcons,
        { ...optionIcons.buttons[0], widget_option_weight: 2 }, 1), defaultInsetKey);
    assert.strictEqual(reuse.context.padOptionIconRenderKey({
        cols: 3, rows: 2, column_widths_px: [0, 40, 0]
    }, {
        col: 1, row: 0, col_span: 2, widget_type: 'option_selector',
        widget_option_label: 'Cool', widget_option_icon_5: { kind: 'mono' },
        widget_option_icon_size_pct: 75, widget_option_icon_badge_padding: 4,
        widget_option_inset: 0, widget_option_height_pct: 50,
        widget_option_weight: 2, widget_option_weight_3: 7
    }, 5), '2a6ad685');

    const templatePayload = { ...reusePayload, template_pad: 1 };
    const requestsBeforeTemplate = reuse.requests.length;
    await reuse.context.padPreparePageSave({
        page: 0, payload: templatePayload, iconLayout: {}, templatePage: 1, templateButtons: [],
    });
    const templateKeys = reuse.requests.slice(requestsBeforeTemplate)
        .filter(request => request.kind === 'icon-metadata')
        .flatMap(request => new URL(request.url, 'http://localhost').searchParams.get('keys').split(','));
    assert(templateKeys.includes('pad_1_main_0_0'), 'Template fallback must be prepared');

    for (const mismatch of [null, { source_id: sourceId(2) }, { kind: 0 },
        { render_key: '00000001' }, { width: 89 }, { height: 89 }]) {
        reportedArtifact = mismatch && { ...artifact, ...mismatch };
        const prepared = await reuse.context.padPreparePageSave({ page: 0, payload: reusePayload, iconLayout: {} });
        assert.strictEqual(prepared.variants.length, 1, 'Missing or mismatched artifacts must regenerate');
    }

    const stages = [];
    const operation = {
        stage(...values) { stages.push(values); },
        async yield() {},
    };
    const preparedWithProgress = await reuse.context.padPreparePageSave({
        page: 0, payload: reusePayload, iconLayout: {}, operation,
        saveState: { committed: false },
    });
    await reuse.context.padPersistPage(preparedWithProgress);
    assert(stages.some(([stage, completed, total]) => stage === 'Preparing artwork' && completed === total && total > 0));
    assert(stages.some(([stage, completed, total]) => stage === 'Uploading button images' && completed === total && total > 0));
    assert(stages.some(([stage]) => stage === 'Saving configuration'));
    assert.strictEqual(preparedWithProgress.saveState.committed, true);

    const refreshFailure = await setupPadTest({ keepEditor: true });
    vm.runInContext('padState.loadError = null;', refreshFailure.context);
    refreshFailure.context.padBuildSaveContext = concurrent.context.padBuildSaveContext;
    refreshFailure.context.padLoadPage = async () => { throw new Error('Refresh unavailable'); };
    await assert.rejects(refreshFailure.context.padSavePage({ operation }),
        /Pad saved, but finishing the save failed: Refresh unavailable/);

    const validationFailure = await setupPadTest({ keepEditor: true });
    vm.runInContext('padState.loadError = null;', validationFailure.context);
    validationFailure.context.padBuildSaveContext = concurrent.context.padBuildSaveContext;
    const validationFetch = validationFailure.context.fetch;
    for (const [body, expected] of [
        [{ success: false, message: 'dialog message must be nonempty text' }, /dialog message must be nonempty text/],
        [{ error: 'Legacy validation error' }, /Legacy validation error/],
        [{}, /HTTP 500/],
    ]) {
        validationFailure.context.fetch = async (url, options = {}) => {
            if (requestKind(url, options) === 'pad-save') return response(false, body);
            return validationFetch(url, options);
        };
        await assert.rejects(validationFailure.context.padSavePage({ operation }), expected);
    }

    const restoreFailure = await setupPadTest({ keepEditor: true });
    vm.runInContext('padState.loadError = null;', restoreFailure.context);
    restoreFailure.context.padBuildSaveContext = concurrent.context.padBuildSaveContext;
    const restoreFetch = restoreFailure.context.fetch;
    restoreFailure.context.fetch = async (url, options = {}) => {
        if (requestKind(url, options) === 'brightness-42') return response(false);
        return restoreFetch(url, options);
    };
    await assert.rejects(restoreFailure.context.padSavePage({ operation }),
        /Pad saved, but finishing the save failed: Failed to set display brightness/);

    console.log('portal_pad_import: PASS');
})().catch(error => {
    console.error(error);
    process.exit(1);
});
