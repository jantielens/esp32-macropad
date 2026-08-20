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
    for (let page = 0; page < 8; page++) {
        const option = new Element();
        elements.set('pad-page-option-' + page, option);
    }
    return {
        addEventListener() {},
        getElementById(id) {
            if (!elements.has(id)) {
                const element = new Element();
                if (id === 'pad-page-select') {
                    element.options = Array.from({ length: 8 }, (_, page) =>
                        elements.get('pad-page-option-' + page));
                }
                elements.set(id, element);
            }
            return elements.get(id);
        },
        createElement(tag) {
            if (tag !== 'canvas') return new Element();
            const context = {
                clearRect() {}, fillText() {}, putImageData() {},
                getImageData() { return { data: new Uint8ClampedArray(64 * 64 * 4) }; },
            };
            return {
                getContext() { return context; },
                toBlob(callback) { callback(new Blob(['icon'], { type: 'image/png' })); },
            };
        },
    };
}

function loadPortal(context) {
    vm.createContext(context);
    for (const file of [
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
    if (url === '/api/config?no_reboot=1') return 'config-save';
    if (url === '/api/config') return options && options.method === 'POST' ? 'reboot' : 'config-read';
    if (url === '/api/reboot') return 'reboot';
    if (url === '/api/component/button-defaults/config') return 'button-defaults';
    if (url === '/api/component/display/brightness') return 'brightness-' + JSON.parse(options.body).brightness;
    if (url.startsWith('/api/icons/page')) return 'icon-delete';
    if (url.startsWith('/api/icons/install')) return 'icon-install';
    if (url.startsWith('/api/pad/button_sizes')) return 'button-sizes';
    if (url.startsWith('/api/pad')) return options.method === 'DELETE' ? 'pad-delete' : 'pad-save';
    return url;
}

async function runImport(options = {}) {
    const requests = [];
    const messages = [];
    const reloads = [];
    const fetchFailure = options.fetchFailure;
    let corruptState = true;
    const document = makeDocument();
    const context = {
        Blob,
        Uint8ClampedArray,
        console: { error() {}, log() {}, warn() {} },
        document,
        deviceInfoCache: { display_blank_on_save: true, max_pads: 2 },
        MAX_ACTIONS: 3,
        confirm() { return true; },
        showMessage(message, type) { messages.push({ message, type }); },
        padBindingsFromJson() { return []; },
        padBindingsToDict() { return null; },
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
        padGetBindableColor() { return '#000000'; },
        padSetBindableColor() {},
        padInitBindableColor() {},
        padLoadLevelActions() {},
        padPopulateTemplateDropdown() {},
        bindingValidatePadBindings() { return { valid: true, count: 0 }; },
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
            if (kind === 'config-read') return response(true, { backlight_brightness: 42 });
            if (kind === 'button-sizes') return response(true, {
                button_w: 100, button_h: 100, padding: 2, gap: 2, font_small_h: 10,
            });
            return response(true);
        },
    };
    loadPortal(context);
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
        { cols: 2, rows: 3, template_pad: 1, buttons: [{ col: 0, row: 0, icon_id: 'emoji_one' }] },
        {
            cols: 4,
            rows: 1,
            template_pad: 0,
            pad_actions: [{ type: 'sound_alert', sound_alert_kind: 'tone', sound_alert_pattern: '1' }],
            buttons: [{ col: 1, row: 0, icon_id: 'emoji_two' }],
        },
    ];
    await context.deviceImportConfig({
        target: {
            files: [{ text: async () => JSON.stringify({
                _format: 'esp32-macropad-config',
                _version: 1,
                config: { mqtt_enabled: true },
                button_defaults: { bg_color: '101010' },
                pads,
            }) }],
            value: 'import.json',
        },
    });
    await Promise.resolve();
    return { context, requests, messages, reloads };
}

function pageRequests(result, page) {
    return result.requests.filter(request => request.page === page);
}

(async function() {
    const success = await runImport();
    const padSaves = success.requests.filter(request => request.kind === 'pad-save');
    assert.deepStrictEqual(padSaves.map(request => request.page), [0, 1], JSON.stringify(success.messages));
    assert.deepStrictEqual(success.requests.map(request => request.kind), [
        'config-save', 'button-defaults',
        'config-read', 'brightness-0', 'icon-delete', 'button-sizes', 'icon-install', 'pad-save', 'brightness-42',
        'config-read', 'brightness-0', 'icon-delete', 'button-sizes', 'icon-install', 'pad-save', 'brightness-42',
        'device-info', 'reboot',
    ]);
    assert.ok(success.requests[6].url.includes('id=pad_0_0_0'));
    assert.ok(success.requests[13].url.includes('id=pad_1_1_0'));
    assert.strictEqual(JSON.parse(padSaves[0].options.body).cols, 2);
    assert.strictEqual(JSON.parse(padSaves[0].options.body).rows, 3);
    assert.strictEqual(JSON.parse(padSaves[0].options.body).buttons[0].icon_id, 'emoji_one');
    assert.strictEqual(JSON.parse(padSaves[1].options.body).cols, 4);
    assert.strictEqual(JSON.parse(padSaves[1].options.body).rows, 1);
    assert.strictEqual(JSON.parse(padSaves[1].options.body).buttons[0].icon_id, 'emoji_two');
    assert.strictEqual(JSON.parse(padSaves[0].options.body).template_pad, 1);
    assert.strictEqual(JSON.parse(padSaves[1].options.body).template_pad, 0);
    assert.ok(!Object.hasOwn(JSON.parse(padSaves[0].options.body), 'pad_actions'));
    assert.deepStrictEqual(JSON.parse(padSaves[1].options.body).pad_actions, [
        { type: 'sound_alert', sound_alert_kind: 'tone', sound_alert_pattern: '1' },
    ]);
    assert.deepStrictEqual(success.reloads, [1]);
    assert.strictEqual(success.requests.filter(request => request.kind === 'device-info').length, 1);
    assert.strictEqual(success.requests.filter(request => request.kind === 'reboot').length, 1);

    for (const failure of ['config-save', 'button-defaults', 'brightness-0', 'icon-delete', 'icon-install', 'pad-save', 'brightness-42']) {
        const result = await runImport({ fetchFailure: failure });
        assert.strictEqual(result.requests.filter(request => request.kind === 'reboot').length, 0, failure);
        assert.strictEqual(result.requests.filter(request => request.kind === 'pad-save' && request.page === 1).length, 0, failure);
        assert.deepStrictEqual(result.reloads, [], failure);
        assert.ok(result.messages.some(message => message.type === 'error'), failure);
        if (['icon-delete', 'icon-install', 'pad-save', 'brightness-42'].includes(failure)) {
            assert.ok(result.requests.some(request => request.kind === 'brightness-42'), failure);
        }
    }

    for (const failure of ['icon-delete', 'pad-delete']) {
        const result = await runImport({
            fetchFailure: failure,
            pads: [null, { cols: 2, rows: 2, buttons: [{ col: 0, row: 0, icon_id: 'emoji_later' }] }],
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
            'config-read', 'brightness-0', 'icon-delete', 'icon-install', 'pad-save', 'brightness-42',
            'device-info',
            'config-read', 'brightness-0', 'icon-delete', 'icon-install', 'pad-save', 'brightness-42',
            'device-info',
        ],
        'concurrent saves must not overlap display blanking'
    );

    console.log('portal_pad_import: PASS');
})().catch(error => {
    console.error(error);
    process.exit(1);
});
