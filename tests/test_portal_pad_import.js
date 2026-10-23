const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

class Element {
    constructor() {
        this.value = '';
        this.options = [];
        this.style = {};
    }

    appendChild(child) {
        this.options.push(child);
    }
}

function response(ok = true, json = {}) {
    return {
        ok,
        status: ok ? 200 : 500,
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
    if (url.startsWith('/api/icons/installed?pages=')) return 'icon-metadata';
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

async function runImport(options = {}) {
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
    let uploadedSourceNumber = 100;
    let corruptState = true;
    const document = makeDocument();
    const renderedSizes = document.renderedSizes;
    const context = {
        Blob,
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
        actionEditorListLoad(prefixes, actions) {
            (actions || []).forEach(function(a, i) { context.actionEditorLoad(prefixes[i], a); });
        },
        actionEditorListBuild(prefixes) {
            return prefixes.map(function(p) { return context.actionEditorBuild(p); })
                .filter(function(a) { return a && a.type; });
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

            if (corruptState && kind === 'config-read') {
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
                return response(true, sourceRecords.get(id) || {});
            }
            if (url === '/api/pad?page=1' && !fetchOptions.method) {
                return response(true, {
                    buttons: [{ icon: { source_id: sourceId(3), catalog_id: 'material-symbols:lightbulb', kind: 'mono' } }],
                    overlay_buttons: [],
                });
            }
            if (kind === 'icon-source-upload') {
                const id = sourceId(uploadedSourceNumber++);
                sourceRecords.set(id, JSON.parse(fetchOptions.body));
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
    context.padNormalizeIconSource = function(record, expected) {
        if (record.catalog_id !== expected.catalog_id || record.kind !== expected.kind) throw new Error('Source mismatch');
        return record;
    };
    context.padRenderIconPNG = async function(record, size) {
        renderedSizes.push(size);
        return new Blob(['icon'], { type: 'image/png' });
    };
    context.padLoadPage = async function(page) { reloads.push(page); };
    context.padBuildLevelActions = function() {
        return vm.runInContext('padState.padActions.map(function(action) { return Object.assign({}, action); })', context);
    };
    setPadState(context, options.originalPage === undefined ? 1 : options.originalPage, {
        cols: 3,
        rows: 2,
        buttons: [],
    });

    const pads = options.pads || [
        { cols: 2, rows: 3, template_pad: 1, buttons: [{ col: 0, row: 0, icon: { source_id: sourceId(1), catalog_id: 'material-symbols:home', kind: 'mono' } }],
            overlay_buttons: [{ col: 0, row: 0, icon: { source_id: sourceId(2), catalog_id: 'noto:house', kind: 'color' } }] },
        {
            cols: 4,
            rows: 1,
            template_pad: 0,
            pad_actions: [{ type: 'sound_alert', sound_alert_kind: 'tone', sound_alert_pattern: '1' }],
            buttons: [{ col: 1, row: 0, icon: { source_id: sourceId(1), catalog_id: 'material-symbols:home', kind: 'mono' } }],
        },
    ];
    await context.deviceImportConfig({
        target: {
            files: [{ text: async () => JSON.stringify({
                _format: 'esp32-macropad-config',
                    _version: 2,
                    _icon_sources: Object.fromEntries(sourceRecords),
                config: { mqtt_enabled: true },
                button_defaults: { bg_color: '101010' },
                pads,
            }) }],
            value: 'import.json',
        },
    });
    await Promise.resolve();
    return { context, requests, messages, reloads, canvasSizes: document.canvasSizes, renderedSizes };
}

function pageRequests(result, page) {
    return result.requests.filter(request => request.page === page);
}

(async function() {
    const success = await runImport();
    const padSaves = success.requests.filter(request => request.kind === 'pad-save');
    assert.deepStrictEqual(padSaves.map(request => request.page), [0, 1], JSON.stringify(success.messages));
    assert.deepStrictEqual(success.requests.map(request => request.kind), [
        'config-save', 'button-defaults', 'icon-context', 'icon-metadata',
        'button-sizes', 'icon-source-upload', 'icon-source-upload', 'config-read', 'brightness-0',
        'icon-install', 'icon-install', 'icon-install', 'pad-save', 'icon-inventory', 'icon-file-delete', 'brightness-42',
        'icon-context', 'icon-source-read', 'icon-metadata', 'button-sizes', 'config-read', 'brightness-0', 'icon-install', 'icon-install',
        'pad-save', 'icon-inventory', 'icon-file-delete', 'brightness-42',
        'device-info', 'reboot',
    ]);
    const installs = success.requests.filter(request => request.kind === 'icon-install');
    assert.ok(installs[0].url.includes('id=pad_0_main_0_0'));
    assert.ok(installs[1].url.includes('id=pad_0_overlay_0_0'));
    assert.ok(installs[2].url.includes('id=pad_0_main_1_0'));
    assert.ok(installs[3].url.includes('id=pad_1_main_1_0'));
    assert.ok(installs[4].url.includes('id=pad_1_main_0_0'));
    assert.strictEqual(success.requests.filter(request => request.kind === 'pad-read').length, 0,
        'Save must not fetch full pads to discover dependents');
    assert.strictEqual(success.requests.filter(request => request.kind === 'icon-context').length, 2,
        'Each icon-bearing pad save must fetch dependency context once');
    assert.strictEqual(JSON.parse(padSaves[0].options.body).cols, 2);
    assert.strictEqual(JSON.parse(padSaves[0].options.body).rows, 3);
    assert.strictEqual(JSON.parse(padSaves[0].options.body).buttons[0].icon.source_id, sourceId(100));
    assert.strictEqual(JSON.parse(padSaves[0].options.body).overlay_buttons[0].icon.source_id, sourceId(101));
    assert.strictEqual(JSON.parse(padSaves[1].options.body).cols, 4);
    assert.strictEqual(JSON.parse(padSaves[1].options.body).rows, 1);
    assert.strictEqual(JSON.parse(padSaves[1].options.body).buttons[0].icon.source_id, sourceId(100));
    assert.strictEqual(JSON.parse(padSaves[0].options.body).template_pad, 1);
    assert.strictEqual(JSON.parse(padSaves[1].options.body).template_pad, 0);
    assert.ok(!Object.hasOwn(JSON.parse(padSaves[0].options.body), 'pad_actions'));
    assert.deepStrictEqual(JSON.parse(padSaves[1].options.body).pad_actions, [
        { type: 'sound_alert', sound_alert_kind: 'tone', sound_alert_pattern: '1' },
    ]);
    assert.deepStrictEqual(success.reloads, [1]);
    assert.strictEqual(success.requests.filter(request => request.kind === 'device-info').length, 1);
    assert.strictEqual(success.requests.filter(request => request.kind === 'reboot').length, 1);

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
    assert(!templatePrepared.desiredKeys.has('pad_1_main_0_0'),
        'Template save must not clean up a dependent pad artifact');
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

    for (const failure of ['config-save', 'button-defaults', 'icon-context', 'icon-metadata', 'icon-source-upload', 'button-sizes', 'brightness-0', 'icon-install', 'pad-save', 'brightness-42']) {
        const result = await runImport({ fetchFailure: failure });
        assert.strictEqual(result.requests.filter(request => request.kind === 'reboot').length, 0, failure);
        assert.strictEqual(result.requests.filter(request => request.kind === 'pad-save' && request.page === 1).length, 0, failure);
        assert.deepStrictEqual(result.reloads, [], failure);
        assert.ok(result.messages.some(message => message.type === 'error'), failure);
        if (['icon-install', 'pad-save', 'brightness-42'].includes(failure)) {
            assert.ok(result.requests.some(request => request.kind === 'brightness-42'), failure);
        }
    }

    for (const failure of ['icon-inventory', 'icon-file-delete']) {
        const result = await runImport({ fetchFailure: failure });
        assert.strictEqual(result.requests.filter(request => request.kind === 'reboot').length, 1, failure);
        assert.strictEqual(result.requests.filter(request => request.kind === 'pad-save').length, 2, failure);
    }

    for (const failure of ['icon-delete', 'pad-delete']) {
        const result = await runImport({
            fetchFailure: failure,
            pads: [null, { cols: 2, rows: 2, buttons: [] }],
        });
        assert.strictEqual(result.requests.filter(request => request.kind === 'reboot').length, 0, failure);
        assert.strictEqual(result.requests.filter(request => request.page === 1).length, 0, failure);
        assert.deepStrictEqual(result.reloads, [], failure);
    }

    const emptyActions = await runImport({
        pads: [{ cols: 2, rows: 2, pad_actions: [], buttons: [] }],
    });
    const emptyActionsBody = JSON.parse(emptyActions.requests.find(request => request.kind === 'pad-save').options.body);
    assert.ok(!Object.hasOwn(emptyActionsBody, 'pad_actions'));

    const concurrent = await runImport();
    const firstConcurrentRequest = concurrent.requests.length;
    await Promise.all([
        concurrent.context.padSavePage(),
        concurrent.context.padSavePage(),
    ]);
    assert.deepStrictEqual(
        concurrent.requests.slice(firstConcurrentRequest).map(request => request.kind),
        [
            'icon-context', 'icon-metadata', 'config-read', 'brightness-0', 'icon-install', 'icon-install', 'pad-save', 'icon-inventory', 'brightness-42',
            'device-info',
            'icon-context', 'icon-metadata', 'config-read', 'brightness-0', 'icon-install', 'icon-install', 'pad-save', 'icon-inventory', 'brightness-42',
            'device-info',
        ],
        'concurrent saves must not overlap display blanking'
    );

    const reuse = await runImport();
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
        if (url.startsWith('/api/icons/installed?pages=')) {
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
    assert(reused.desiredKeys.has('pad_0_main_0_0'), 'Reused artifacts must remain in the cleanup keep-list');
    await reuse.context.padPersistPage(reused);
    const reuseRequests = reuse.requests.slice(requestsBeforeReuse);
    assert.strictEqual(reuseRequests.filter(request => request.kind === 'icon-metadata').length, 1);
    assert(reuseRequests.some(request => request.kind === 'pad-save'), 'Reusing icons must not skip the pad save');
    assert(!reuseRequests.some(request => request.kind === 'icon-install' || request.kind === 'icon-file-delete'),
        'An unchanged save must neither upload nor delete the matching artifact');
    assert.strictEqual(reuseRequests.find(request => request.kind === 'icon-metadata').url,
        '/api/icons/installed?pages=0');

    for (const mismatch of [null, { source_id: sourceId(2) }, { kind: 0 },
        { render_key: '00000001' }, { width: 89 }, { height: 89 }]) {
        reportedArtifact = mismatch && { ...artifact, ...mismatch };
        const prepared = await reuse.context.padPreparePageSave({ page: 0, payload: reusePayload, iconLayout: {} });
        assert.strictEqual(prepared.variants.length, 1, 'Missing or mismatched artifacts must regenerate');
    }

    console.log('portal_pad_import: PASS');
})().catch(error => {
    console.error(error);
    process.exit(1);
});
