const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

const context = {
    validateBinding(v) {
        return v.indexOf('[bad') !== -1
            ? { valid: false, errors: [{ message: 'Unknown binding scheme' }] }
            : { valid: true, errors: [] };
    }
};
vm.createContext(context);
vm.runInContext(fs.readFileSync('src/app/web/portal_pad_colors.js', 'utf8'), context);

const norm = context.padColorNormalizeLiteral;
assert.strictEqual(norm('#4caf50'), '#4CAF50');
assert.strictEqual(norm('4CAF50'), '#4CAF50');
assert.strictEqual(norm('0xff0000'), '#FF0000');
assert.strictEqual(norm('#fff'), '#FFFFFF');
assert.strictEqual(norm(' #1a2 '), '#11AA22');
for (const bad of ['', 'red', '#12', '#1234', '#GGGGGG', '#FF000080', '[mqtt:a]']) {
    assert.strictEqual(norm(bad), null, bad);
}

const check = context.padColorCheck;
assert.deepStrictEqual({ ...check('#fff', 0) }, { value: '#FFFFFF', error: '' });
assert.deepStrictEqual({ ...check('', 0) }, { value: '', error: '' });
assert.match(check('red', 0).error, /Not a color/);
assert.strictEqual(check('[expr:1]', 0).error, '');
assert.match(check('[expr:1]', 0, false).error, /requires a color/);
assert.strictEqual(check('#abc', 0, false).value, '#AABBCC');
assert.strictEqual(check('#abc', 0, false).error, '');
assert.doesNotMatch(check('red', 0, false).error, /binding/);
assert.strictEqual(check('[expr:1]', 0, true).error, '');
assert.strictEqual(check('[bad:x]', 0).error, 'Unknown binding scheme');
assert.match(check('[expr:threshold([mqtt:t],"#000000",1,"#FFFFFF")]', 20).error, /Too long/);

const expr = '[expr:threshold([mqtt:sensor;$.a,b], "#4CAF50", 25, "#ff9800", [pad:hi], "#F44336")]';
const parsed = context.padThresholdGenParse(expr);
assert.strictEqual(parsed.source, '[mqtt:sensor;$.a,b]');
assert.deepStrictEqual(JSON.parse(JSON.stringify(parsed.stops)), [
    { color: '#4CAF50', threshold: '' },
    { color: '#FF9800', threshold: '25' },
    { color: '#F44336', threshold: '[pad:hi]' }
]);
for (const bad of [
    '#FF0000',
    '[expr:1+2]',
    '[expr:threshold([mqtt:t], "#000000")]',
    '[expr:threshold([mqtt:t], "#000000", 5)]',
    '[expr:threshold([mqtt:t], [pad:c], 5, "#FFFFFF")]',
    '[expr:threshold([mqtt:t], "#000000", , "#FFFFFF")]'
]) {
    assert.strictEqual(context.padThresholdGenParse(bad), null, bad);
}

const source = { value: '' };
const sourceError = { style: { display: 'none' } };
context._cpPopover = { pop: { querySelector(selector) {
    return selector === '#cp-gen-source' ? source : sourceError;
} } };
assert.strictEqual(context.padThresholdSourceValidate(), false);
assert.strictEqual(sourceError.style.display, '');
source.value = '[mqtt:sensor;$.temp]';
assert.strictEqual(context.padThresholdSourceValidate(), true);
assert.strictEqual(sourceError.style.display, 'none');
source.value = '   ';
assert.strictEqual(context.padThresholdSourceValidate(), false);
assert.strictEqual(sourceError.style.display, '');

const elements = {};
for (const selector of ['#cp-gen', '#cp-fx', '#cp-input-label', '#cp-input', '#cp-recent-grid', '#cp-recent-heading', '#cp-gen-source']) {
    elements[selector] = { style: {}, children: [], value: '', focus() {}, appendChild(child) { this.children.push(child); } };
}
Object.defineProperty(elements['#cp-recent-grid'], 'innerHTML', { set() { this.children = []; } });
const dialog = { pop: {
    style: {}, querySelector(selector) { return elements[selector]; }, querySelectorAll() { return []; }
}, bd: { style: {} } };
context.document = { activeElement: null };
context.padState = { editCol: 1, editRow: 1 };
context.padColorPopoverCreate = () => dialog;
context.padThresholdGenLoad = () => {};
context.padColorPopoverValidate = () => {};
context.padColorPopoverReposition = () => {};
context.padColorSwatchButton = value => ({ value });
context.padCollectUsedColors = () => ['#FFFFFF', '[expr:1]'];
context.padColorPopoverOpen({}, { value: '#FFFFFF', dataset: { allowBindings: 'false' } });
assert.strictEqual(elements['#cp-gen'].style.display, 'none');
assert.strictEqual(elements['#cp-fx'].style.display, 'none');
assert.strictEqual(elements['#cp-input-label'].textContent, 'Color');
assert.strictEqual(elements['#cp-input'].placeholder, '#RRGGBB');
assert.strictEqual(elements['#cp-recent-grid'].children.length, 1);
context.padColorPopoverOpen({}, { value: '[expr:1]', dataset: {} });
assert.strictEqual(elements['#cp-gen'].style.display, '');
assert.strictEqual(elements['#cp-fx'].style.display, '');
assert.strictEqual(elements['#cp-input-label'].textContent, 'Color or expression');
assert.strictEqual(elements['#cp-recent-grid'].children.length, 2);
const waveform = fs.readFileSync('src/app/web/_widget_waveform.html', 'utf8');
assert.strictEqual((waveform.match(/data-allow-bindings="false"/g) || []).length, 0);
assert.strictEqual((waveform.match(/maxlength="11"/g) || []).length, 6);
for (const widget of ['rocker', 'numericrocker']) {
    const markup = fs.readFileSync('src/app/web/_widget_' + widget + '.html', 'utf8');
    assert.match(markup, /class="bc-input" data-allow-bindings="false"/);
}
for (const widget of ['gauge', 'bar_chart', 'sparkline', 'waveform']) {
    const markup = fs.readFileSync('src/app/web/_widget_' + widget + '.html', 'utf8');
    for (const input of markup.matchAll(/<input\b[^>]*class="bc-input"[^>]*>/g)) {
        const identifier = /id="([^"]+)"/.exec(input[0])[1];
        const label = markup.match(new RegExp('<label for="' + identifier + '">([\\s\\S]*?)</label>'));
        assert.ok(label && label[1].includes('class="fx-hint"'), identifier + ' needs a binding hint');
    }
}
const sparkline = fs.readFileSync('src/app/web/_widget_sparkline.html', 'utf8');
assert.strictEqual((sparkline.match(/class="sparkline-color-row"/g) || []).length, 5);
for (const identifier of ['min-label', 'max-label', 'ref-1', 'ref-2', 'ref-3']) {
    assert.match(sparkline, new RegExp('id="pad-edit-sparkline-' + identifier + '-color"[^>]*maxlength="63"'));
    assert.match(sparkline, new RegExp('<label for="pad-edit-sparkline-' + identifier + '-color">Color '));
}
const barChart = fs.readFileSync('src/app/web/_widget_bar_chart.html', 'utf8');
assert.match(barChart, /id="pad-edit-widget-bar-bg-color"[^>]*maxlength="63"/);

const defaultsFixture = JSON.parse(fs.readFileSync('tools/mock-data/pad-editor.json', 'utf8'));
const defaultInputs = new Map();
context.padState = { buttonDefaults: {} };
context.document = { getElementById: id => defaultInputs.get(id) || null, addEventListener() {} };
context.window = {};
context.Event = class {};
vm.runInContext(fs.readFileSync('src/app/web/portal_pad_defaults.js', 'utf8'), context);
context.padInitLayoutDiagram = () => {};
context.padUpdateLayoutDiagram = () => {};
for (const id of ['pad-def-pad-bg-color', 'pad-def-bg-color', 'pad-def-fg-color', 'pad-def-border-color',
    'pad-def-pixel-shift-distance', 'pad-def-button-shadow-enabled']) {
    defaultInputs.set(id, { value: '', checked: false, dispatchEvent() {} });
}
const monoDefaults = { ...defaultsFixture.firmware_defaults, ...defaultsFixture.epaper_firmware_defaults };
context.padLoadButtonDefaults({ firmware_defaults: monoDefaults });
assert.strictEqual(defaultInputs.get('pad-def-bg-color').value, '#FFFFFF');
assert.strictEqual(defaultInputs.get('pad-def-fg-color').value, '#000000');
assert.strictEqual(defaultInputs.get('pad-def-pixel-shift-distance').value, 0);
assert.strictEqual(context.padGetEffectiveDefault('widget_series_color'), '#000000');
assert.strictEqual(context.padGetEffectiveDefault('pixel_shift_distance_px'), 0);
defaultInputs.get('pad-def-pixel-shift-distance').value = '0';
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.padCollectButtonDefaults())), {});
context.padLoadButtonDefaults({ firmware_defaults: defaultsFixture.firmware_defaults,
    pixel_shift_distance_px: 0, button_shadow_enabled: false, bg_color: '#ABCDEF' });
assert.strictEqual(context.padGetEffectiveDefault('pixel_shift_distance_px'), 0);
assert.strictEqual(context.padGetEffectiveDefault('button_shadow_enabled'), false);
assert.strictEqual(context.padGetEffectiveDefault('bg_color'), '#ABCDEF');
assert.strictEqual(context.padState.buttonDefaults.firmware_defaults, undefined);
const saved = JSON.parse(JSON.stringify(context.padCollectButtonDefaults()));
assert.strictEqual(saved.bg_color, '#ABCDEF');
assert.strictEqual(saved.pixel_shift_distance_px, 0);
assert.strictEqual(saved.button_shadow_enabled, false);
assert.strictEqual(saved.firmware_defaults, undefined);

for (const id of ['pad-def-border-width', 'pad-def-corner-radius', 'pad-def-content-pad']) {
    defaultInputs.set(id, { value: '', checked: false, dispatchEvent() {} });
}
context.padLoadButtonDefaults({ firmware_defaults: monoDefaults, border_width: 0, corner_radius: 0, content_pad: 0 });
for (const id of ['pad-def-border-width', 'pad-def-corner-radius', 'pad-def-content-pad']) {
    assert.strictEqual(defaultInputs.get(id).value, 0);
    defaultInputs.get(id).value = '0';
}
const zeroAppearance = JSON.parse(JSON.stringify(context.padCollectButtonDefaults()));
assert.strictEqual(zeroAppearance.border_width, '0');
assert.strictEqual(zeroAppearance.corner_radius, '0');
assert.strictEqual(zeroAppearance.content_pad, '0');

(async function testDefaultsSaveRebootWarning() {
    let pendingCalls = 0;
    const messages = [];
    context.padCollectButtonDefaults = () => ({ base_text_size: 30 });
    context.padRenderGrid = () => {};
    context.setPendingReboot = () => { ++pendingCalls; };
    context.showMessage = (message, type) => messages.push({ message, type });
    context.fetch = async () => ({ ok: true });
    context.padLoadButtonDefaultsFromDevice = async () => context.padLoadButtonDefaults({
        typography_status: { reboot_required: false }
    });
    await context.padSaveButtonDefaults();
    assert.strictEqual(pendingCalls, 0);
    assert.strictEqual(messages.pop().type, 'success');

    context.padLoadButtonDefaultsFromDevice = async () => context.padLoadButtonDefaults({
        typography_status: { reboot_required: true }
    });
    await context.padSaveButtonDefaults();
    assert.strictEqual(pendingCalls, 1);
    assert.strictEqual(messages.length, 0);

    context.fetch = async () => ({ ok: false, status: 500, json: async () => ({ error: 'Save failed' }) });
    context.console = { error() {} };
    await context.padSaveButtonDefaults();
    assert.strictEqual(pendingCalls, 1);
    assert.strictEqual(messages.pop().type, 'error');
    console.log('portal_pad_colors: PASS');
})().catch(error => { console.error(error); process.exitCode = 1; });
