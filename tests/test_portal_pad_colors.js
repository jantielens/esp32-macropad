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
vm.runInContext(fs.readFileSync('src/app/web/portal_widget_editor.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_widget_gauge.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_widget_bar_chart.js', 'utf8'), context);
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
for (const selector of ['#cp-base', '#cp-base-summary', '#cp-base-body', '#cp-color-editor', '#cp-fill', '#cp-fill-geometry', '#cp-gen', '#cp-fx', '#cp-input-label', '#cp-input', '#cp-recent-grid', '#cp-recent-heading', '#cp-gen-source']) {
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
for (let reference = 1; reference <= 3; reference++) {
    assert.match(sparkline, new RegExp('<input type="number" id="pad-edit-sparkline-ref-' + reference + '-y" step="any"'));
}
for (const identifier of ['min-label', 'max-label', 'ref-1', 'ref-2', 'ref-3']) {
    assert.match(sparkline, new RegExp('id="pad-edit-sparkline-' + identifier + '-color"[^>]*maxlength="63"'));
    assert.match(sparkline, new RegExp('<label for="pad-edit-sparkline-' + identifier + '-color">Color '));
}
const barChart = fs.readFileSync('src/app/web/_widget_bar_chart.html', 'utf8');
assert.match(barChart, /id="pad-edit-widget-bar-bg-color"[^>]*maxlength="63"/);

assert.strictEqual(context.padFillPattern('unknown'), 'none');
assert.strictEqual(context.padFillPattern('none'), 'none');
assert.strictEqual(context.padFillPattern('checkerboard'), 'checkerboard');
assert.strictEqual(context.padFillGeometry(-1, 8, 2, 64), 2);
assert.strictEqual(context.padFillGeometry(1000, 8, 2, 64), 64);
assert.strictEqual(context.padFillGeometry('bad', 8, 2, 64), 8);
const fillInputs = new Map();
for (const [id, value] of Object.entries({
    'pad-edit-widget-type': 'bar_chart', 'pad-edit-pattern-spacing': '8', 'pad-edit-pattern-stroke': '1',
    'pad-edit-track-outline-width': '0', 'pad-edit-track-outline-color': '#000000', 'pad-edit-track-radius': '4'
})) fillInputs.set(id, { value, style: {}, dispatchEvent() {} });
for (const id of ['pad-edit-pattern-geometry', 'pad-edit-pattern-stroke-wrap', 'pad-edit-track-outline'])
    fillInputs.set(id, { style: {} });
const patternsSection = { dataset: {}, open: false };
fillInputs.set('pad-edit-patterns-section', patternsSection);
const fillControls = ['bg', 'widget_bar', 'widget_bar_2'].map((prefix, index) => {
    const selection = { value: 'none' };
    const color = { id: 'pattern-color-' + index, value: '#000000', dispatchEvent() {} };
    const colorWrap = { style: {} };
    fillInputs.set(color.id, color);
    return {
        dataset: { patternField: index === 2 ? 'widget_bar_pattern_2' : prefix + '_pattern',
            colorField: index === 2 ? 'widget_bar_pattern_color_2' : prefix + '_pattern_color' },
        querySelector(selector) { return selector === 'select' ? selection : selector === '.bc-input' ? color : colorWrap; }
    };
});
context.document = { querySelectorAll() { return fillControls; }, getElementById(id) { return fillInputs.get(id); } };
context.Event = class {};
context.padFillLoad({});
const oldFill = {};
context.padFillBuild(oldFill, 'bar_chart');
assert.deepStrictEqual(oldFill, {});
assert.strictEqual(fillInputs.get('pad-edit-pattern-geometry').style.display, '');
assert.strictEqual(fillInputs.get('pad-edit-pattern-stroke').disabled, true);
assert.strictEqual(patternsSection.open, false);
const patternButton = { bg_color: '#FFFFFF', bg_pattern: 'dots', bg_pattern_color: '#FFFFFF',
    widget_bar_pattern: 'diagonal', widget_bar_pattern_color: '[pad:color]',
    widget_bar_pattern_2: 'checkerboard', widget_bar_pattern_color_2: '#123456',
    pattern_spacing: 4, pattern_stroke_width: 8, widget_track_outline_width: 3, widget_track_outline_color: '[pad:outline]' };
context.padFillLoad(patternButton);
assert.strictEqual(patternsSection.open, true);
patternsSection.open = false;
context.padFillRefreshVisibility();
assert.strictEqual(patternsSection.open, false);
context.padFillLoad(patternButton);
assert.strictEqual(patternsSection.open, true);
const savedFill = { bg_color: patternButton.bg_color };
context.padFillBuild(savedFill, 'bar_chart');
assert.strictEqual(savedFill.bg_pattern_color, '#FFFFFF');
assert.strictEqual(savedFill.bg_color, '#FFFFFF');
assert.strictEqual(savedFill.widget_bar_pattern_color, '[pad:color]');
assert.strictEqual(savedFill.widget_bar_pattern_2, 'checkerboard');
assert.strictEqual(savedFill.pattern_stroke_width, 4);
assert.strictEqual(savedFill.widget_track_outline_color, '[pad:outline]');
fillControls[0].querySelector('select').value = 'none';
const plainFill = {};
context.padFillBuild(plainFill, 'gauge');
assert.strictEqual(plainFill.bg_pattern, undefined);
assert.strictEqual(plainFill.bg_pattern_color, '#FFFFFF');
assert.strictEqual(plainFill.widget_bar_pattern, undefined);
const gaugeFill = {};
context.padFillBuild(gaugeFill, 'gauge');
assert.strictEqual(gaugeFill.widget_bar_track_radius, undefined);

const pickerContext = { Event: class {}, clearTimeout() {}, document: { body: { contains() { return false; } } } };
vm.createContext(pickerContext);
vm.runInContext(fs.readFileSync('src/app/web/portal_pad_colors.js', 'utf8'), pickerContext);
pickerContext.padFillRefreshVisibility = () => {};
const selection = { value: 'none' };
const patternColor = { value: '#000000' };
const controls = { dataset: {}, querySelector(selector) { return selector === 'select' ? selection : patternColor; } };
let applyEvents = 0;
const target = { value: '#FFFFFF', maxLength: 191, dispatchEvent() {
    applyEvents++;
    assert.strictEqual(selection.value, 'dots');
    assert.strictEqual(patternColor.value, '#123456');
    assert.strictEqual(this.value, '#AABBCC');
} };
function draft() {
    return { target, fill: { controls, base: '#abc', color: '#123456', pattern: 'dots', originalColor: '#000000' },
        pop: { style: {} }, bd: { style: {} } };
}
pickerContext._cpPopover = draft();
pickerContext.padColorPopoverClose();
assert.strictEqual(target.value, '#FFFFFF');
assert.strictEqual(selection.value, 'none');
assert.strictEqual(patternColor.value, '#000000');
assert.strictEqual(applyEvents, 0);
pickerContext._cpPopover = draft();
pickerContext.padColorPopoverApply('#123456');
assert.strictEqual(applyEvents, 1);
assert.strictEqual(controls.dataset.colorSet, '1');
assert.strictEqual(pickerContext._cpPopover.fill, null);

const spacingInput = { value: '8' };
const strokeInput = { value: '1' };
controls.querySelector = selector => ({
    select: selection, '.bc-input': patternColor,
    '[data-fill-spacing]': spacingInput, '[data-fill-stroke]': strokeInput
})[selector];
target.dataset = { allowBindings: 'false', fillControls: 'dialog-pattern' };
pickerContext._cpPopover = draft();
pickerContext._cpPopover.fill.geometry = { spacing: 12, stroke: 2 };
pickerContext.padColorPopoverClose();
assert.strictEqual(spacingInput.value, '8');
assert.strictEqual(strokeInput.value, '1');
pickerContext._cpPopover = draft();
pickerContext._cpPopover.fill.geometry = { spacing: 12, stroke: 2 };
pickerContext.padColorPopoverApply('#123456');
assert.strictEqual(spacingInput.value, 12);
assert.strictEqual(strokeInput.value, 2);
assert.strictEqual(applyEvents, 2);
pickerContext._cpPopover = draft();
pickerContext._cpPopover.fill.color = '[expr:1]';
pickerContext.padColorPopoverApply('#123456');
assert.strictEqual(applyEvents, 2);
pickerContext.padColorPopoverClose();
pickerContext.document.getElementById = id => id === 'dialog-pattern' ? controls : null;
assert.strictEqual(pickerContext.padColorFillControls(target), controls);
const background = pickerContext.padFillBackground('diagonal', '#123456', 12, 2);
assert.match(background.image, /repeating-linear-gradient\(135deg,#123456 0 2px,transparent 2px 12px\)/);
assert.strictEqual(background.size, 'auto');
assert.deepStrictEqual({ ...pickerContext.padFillBackground('none', '#123456', 12, 2) },
    { image: 'none', size: 'auto' });

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
