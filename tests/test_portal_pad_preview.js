const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

function element() {
    return {
        style: { setProperty(name, value) { this[name] = value; } },
        dataset: {}, children: [], attributes: {}, classes: new Set(),
        classList: { add(...names) { names.forEach(name => this.owner.classes.add(name)); } },
        appendChild(child) { this.children.push(child); },
        replaceChildren(...children) { this.children = children; },
        insertBefore(child, before) {
            const index = this.children.indexOf(before);
            this.children.splice(index < 0 ? this.children.length : index, 0, child);
        },
        querySelector(selector) {
            for (const child of this.children) {
                if ((child.className || '').split(' ').includes(selector.slice(1))) return child;
                const nested = child.querySelector(selector);
                if (nested) return nested;
            }
            return null;
        },
        addEventListener() {},
        setAttribute(name, value) { this.attributes[name] = value; }
    };
}

function createElement() {
    const node = element();
    node.classList.owner = node;
    return node;
}

const context = {
    TextEncoder,
    deviceInfoCache: { widget_catalog: [] },
    document: { createElement, createElementNS: createElement },
    padColorToHex: (color, fallback) => color || fallback,
    padGetEffectiveDefault: key => ({ bg_color: '#333333', fg_color: '#ffffff', border_color: '#000000', border_width: '0', corner_radius: '8', content_pad: '4' })[key]
};
vm.createContext(context);
vm.runInContext(fs.readFileSync('src/app/web/portal_widget_editor.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_widget_gauge.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_pad_icons.js', 'utf8'), context);

const fields = ['name', 'icon', 'second_icon', 'axis_field', 'horizontal_icon', 'vertical_icon', 'default_axis'];
const widgetFiles = fs.readdirSync('src/app/widgets').filter(name => name.endsWith('_widget.cpp'))
    .map(name => 'src/app/widgets/' + name);
widgetFiles.push('src/app/device_classes/shutter_tester/widgets/waveform_widget.cpp');
for (const file of widgetFiles) {
    const source = fs.readFileSync(file, 'utf8');
    const match = source.match(/static const WidgetPreview (\w+)_preview = \{([^}]+)\};/);
    if (!match) continue;
    const values = JSON.parse('[' + match[2].replace(/nullptr/g, 'null') + ']');
    const metadata = { type: match[1] };
    fields.forEach((field, index) => { if (values[index] != null) metadata[field] = values[index]; });
    context.deviceInfoCache.widget_catalog.push(metadata);
}

const fragment = fs.readFileSync('src/app/web/pad-editor.fragment.html', 'utf8');
const selector = fragment.match(/<select id="pad-edit-widget-type"[\s\S]*?<\/select>/)[0];
const types = [...selector.matchAll(/<option value="([^"]+)"/g)].map(match => match[1]);
types.push('waveform');
for (const widget_type of types) {
    const cell = createElement();
    context.padRenderCellContent(cell, { widget_type });
    assert(cell.classes.has('pad-cell-has-widget'), widget_type);
    assert.strictEqual(cell.children[0].className, 'pad-widget-marker', widget_type);
    assert.strictEqual(cell.children[1].className, 'pad-widget-fallback', widget_type);
    assert.notStrictEqual(cell.children[1].children[1].textContent, 'Unknown Widget', widget_type);
    assert(cell.children[0].attributes['aria-label']);
    const labeled = createElement();
    context.padRenderCellContent(labeled, { widget_type, label_center: 'My label' });
    assert(labeled.children.some(child => child.textContent === 'My label'), widget_type);
    assert(!labeled.children.some(child => child.className === 'pad-widget-fallback'), widget_type);
}

assert.strictEqual(context.padWidgetPreview({ widget_type: 'rocker' }).icon, 'swap_vert');
assert.strictEqual(context.padWidgetPreview({ widget_type: 'rocker', widget_rocker_axis: 'horizontal' }).icon, 'swap_horiz');
assert.strictEqual(context.padWidgetPreview({ widget_type: 'scrollpad' }).icon, 'keyboard_double_arrow_up');
assert.strictEqual(context.padWidgetPreview({ widget_type: 'scrollpad', widget_scrollpad_axis: 'horizontal' }).icon, 'keyboard_double_arrow_left');
assert.strictEqual(context.padWidgetPreview({ widget_type: 'numericrocker' }).vertical, false);
assert.strictEqual(context.padWidgetPreview({ widget_type: 'numericrocker', widget_numericrocker_axis: 'vertical' }).vertical, true);
assert.strictEqual(context.padWidgetPreview({ widget_type: 'future_widget' }).name, 'Unknown Widget');
assert.strictEqual(context.padWidgetPreview({}), null);
context.deviceInfoCache.widget_catalog.push({ type: 'future_widget', name: 'Future Widget', icon: 'list',
    axis_field: 'custom_axis', horizontal_icon: 'swap_horiz', vertical_icon: 'swap_vert', default_axis: 'vertical' });
assert.strictEqual(context.padWidgetPreview({ widget_type: 'future_widget', custom_axis: 'horizontal' }).icon, 'swap_horiz');
const widgetCatalog = context.deviceInfoCache;
context.deviceInfoCache = null;
assert.strictEqual(context.padWidgetPreview({ widget_type: 'gauge' }).name, 'Unknown Widget');

for (const content of [{ label_top: 'Top' }, { label_bottom: 'Bottom' }, { icon_id: 'emoji_X' }, { bg_image_path: '/image.png' }]) {
    const cell = createElement();
    context.padRenderCellContent(cell, { widget_type: 'gauge', ...content });
    assert(!cell.children.some(child => child.className === 'pad-widget-fallback'));
    assert(!cell.children.some(child => child.textContent === '\u2022'));
}
const normal = createElement();
context.padRenderCellContent(normal, {});
assert.strictEqual(normal.children[0].textContent, '\u2022');

const grid = fs.readFileSync('src/app/web/portal_pad_grid.js', 'utf8');
assert(grid.includes('padRenderCellContent(cell, tplBtn)'));
assert(!grid.includes('pad-cell-widget-'));
const gridElement = createElement();
context.deviceInfoCache = widgetCatalog;
context.document.getElementById = id => id === 'pad-grid' ? gridElement : null;
context.padState = { cols: 3, rows: 1, buttons: [{ col: 0, row: 0, widget_type: 'list' }],
    templateButtons: [{ col: 1, row: 0, col_span: 2, widget_type: 'gauge' }] };
context.padBuildOccupancySet = () => new Set();
vm.runInContext(grid, context);
context.padRenderResizeHandles = () => {};
context.padRenderGrid();
assert.strictEqual(gridElement.children.length, 2);
for (const cell of gridElement.children) {
    assert(cell.children.some(child => child.className === 'pad-widget-marker'));
    assert(cell.children.some(child => child.className === 'pad-widget-fallback'));
}
assert(gridElement.children[1].classes.has('pad-cell-ghost'));
assert.strictEqual(gridElement.children[1].style.gridColumn, '2 / span 2');
assert.strictEqual(gridElement.children[1].style.gridRow, '1 / span 1');
assert.strictEqual(gridElement.children[1].children[0].title, 'Gauge Widget');
const css = fs.readFileSync('src/app/web/portal-custom.css', 'utf8');
const font = Buffer.from(css.match(/data:font\/woff2;base64,([^']+)/)[1], 'base64');
assert.strictEqual(font.toString('ascii', 0, 4), 'wOF2');
assert.strictEqual(font.length, font.readUInt32BE(8));
assert(!css.includes('.pad-cell-widget-'));

vm.runInContext(fs.readFileSync('src/app/web/portal_binding_builder.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_button_templates.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_button_builder.js', 'utf8'), context);
context.PAD_FIRMWARE_DEFAULTS = { widget_track_color: '#1A1A1A', widget_series_color: '#4CAF50',
    widget_indicator_color: '#FFFFFF', widget_tick_color: '#AAAAAA' };
const barBuilder = vm.runInContext('padButtonBuilders.find(builder => builder.id === "bar")', context);
const builderElements = Object.fromEntries(['fields', 'states', 'note'].map(name => ['[data-bb-' + name + ']', createElement()]));
context.testBuilderSession = {
    detail: { state: '42', unit: '%' }, scenario: barBuilder, name: 'Battery',
    dialog: { querySelector: selector => builderElements[selector] }
};
context.padButtonBuilderSetScenario(context.testBuilderSession, barBuilder);
vm.runInContext('padButtonBuilderSession = testBuilderSession', context);
context.padColorPopoverClose = () => {};
const updatePreview = context.padButtonBuilderUpdatePreview;
let previewUpdates = 0;
context.padButtonBuilderUpdatePreview = () => previewUpdates++;
context.padButtonBuilderRenderFields();
const builderInputs = builderElements['[data-bb-fields]'].children.map(label => label.children[0]);
assert.strictEqual(builderInputs.find(input => input.name === 'min').step, 'any');
assert.strictEqual(builderInputs.find(input => input.name === 'decimals').step, '1');
const orientationInput = builderInputs.find(input => input.name === 'orientation');
orientationInput.value = 'horizontal';
orientationInput.onchange();
assert.strictEqual(context.testBuilderSession.options.orientation, 'horizontal', 'Custom dropdowns dispatch change, not input');
assert.strictEqual(previewUpdates, 2);
const minimumInput = builderInputs.find(input => input.name === 'min');
minimumInput.value = '';
minimumInput.oninput();
assert.strictEqual(context.testBuilderSession.options.min, '', 'Keep incomplete numeric input raw');
assert.throws(() => context.padButtonBuilderValidateFields(context.testBuilderSession), /minimum and maximum/);
minimumInput.value = '-0.5';
minimumInput.oninput();
assert.strictEqual(context.testBuilderSession.options.min, '-0.5');
assert.doesNotThrow(() => context.padButtonBuilderValidateFields(context.testBuilderSession));
context.padButtonBuilderUpdatePreview = updatePreview;
vm.runInContext('padButtonBuilderSession = null', context);
for (const orientation of ['vertical', 'horizontal']) {
    for (const [state, signed, start, size] of [
        ['0', false, '0%', '0%'], ['50', false, '0%', '50%'], ['100', false, '0%', '100%'],
        ['200', false, '0%', '100%'], ['-200', false, '0%', '0%'],
        ['-50', true, '25%', '25%'], ['0', true, '50%', '0%'], ['50', true, '50%', '25%'],
        ['unavailable', false, undefined, undefined]
    ]) {
        const cell = createElement();
        const detail = { state };
        const button = { label_top: 'Power', label_bottom: state, widget_type: 'bar_chart',
            widget_bar_min: signed ? -100 : 0, widget_bar_max: 100,
            widget_bar_zero_centered: signed, widget_orientation: orientation };
        context.padRenderCellContent(cell, button);
        barBuilder.renderPreview(cell, button, detail);
        const bar = cell.querySelector('.pad-button-builder-bar');
        const track = cell.querySelector('.pad-button-builder-bar-track');
        const fill = cell.querySelector('.pad-button-builder-bar-fill');
        assert.strictEqual(bar.dataset.orientation, orientation);
        assert.strictEqual(bar.attributes.role, state === 'unavailable' ? 'img' : 'meter');
        assert.strictEqual(track.style['--bar-start'], start);
        assert.strictEqual(track.style['--bar-size'], size);
        assert.strictEqual(!!fill.hidden, state === 'unavailable');
        assert.strictEqual(track.style.background, '#1A1A1A');
        assert.strictEqual(fill.style.background, '#4CAF50');
        assert.strictEqual(cell.children.indexOf(bar) + 1, cell.children.indexOf(cell.querySelector('.pad-cell-label-bottom')));
    }
}
const gaugeBuilder = vm.runInContext('padButtonBuilders.find(builder => builder.id === "gauge")', context);
for (const [state, min, max, percent] of [
    ['0', 0, 100, 0], ['50', 0, 100, 50], ['100', 0, 100, 100],
    ['200', 0, 100, 100], ['-200', 0, 100, 0], ['-50', -100, 100, 25],
    ['unavailable', 0, 100, null]
]) {
    const detail = { entity_id: 'sensor.power', state, unit: '%' };
    const session = { detail, name: 'Power' };
    context.padButtonBuilderSetScenario(session, gaugeBuilder);
    Object.assign(session.options, { min, max, color: '#2196F3' });
    const button = { label_top: session.name, ...gaugeBuilder.build(detail, session.options),
        ...gaugeBuilder.preview(detail, session.options) };
    const cell = createElement();
    context.padRenderCellContent(cell, button);
    gaugeBuilder.renderPreview(cell, button, detail);
    assert(cell.querySelector('.pad-widget-marker'));
    assert.strictEqual(cell.querySelector('.pad-cell-label-center'), null);
    const gauge = cell.children.find(child => child.attributes.class === 'pad-button-builder-gauge');
    assert.strictEqual(gauge.attributes.role, percent === null ? 'img' : 'meter');
    assert.strictEqual(gauge.children[0].attributes.stroke, '#1A1A1A');
    if (percent === null) {
        assert.match(gauge.attributes['aria-label'], /unavailable/);
        assert.strictEqual(gauge.children.length, 6, 'Only track and ticks remain when data is unavailable');
        assert.strictEqual(button.label_bottom, '---');
    } else {
        assert.strictEqual(gauge.attributes['aria-valuemin'], String(min));
        assert.strictEqual(gauge.attributes['aria-valuemax'], String(max));
        assert.strictEqual(gauge.attributes['aria-valuenow'], String(Math.max(min, Math.min(max, Number(state)))));
        assert.strictEqual(gauge.attributes['aria-valuetext'], Number(state).toFixed(1) + ' %');
        assert.strictEqual(gauge.children[1].attributes['stroke-dasharray'], percent + ' 100');
        assert.strictEqual(gauge.children[1].attributes.stroke, '#2196F3');
        assert.strictEqual(gauge.children[2].attributes.transform, 'rotate(' + percent * 1.8 + ' 140 130)');
        assert.strictEqual(gauge.children[2].attributes.stroke, '#FFFFFF');
        assert.strictEqual(gauge.children.length, 8, 'One track, one ring, one needle, and five ticks');
    }
    assert.strictEqual(cell.children.indexOf(gauge) + 1, cell.children.indexOf(cell.querySelector('.pad-cell-label-bottom')));
}
const historyBuilder = vm.runInContext('padButtonBuilders.find(builder => builder.id === "history")', context);
let samplePoints;
for (const color of ['#4CAF50', '#2196F3']) {
    const cell = createElement();
    const detail = { state: '21.37', unit: '\u00b0C' };
    const session = { detail, name: 'Living room' };
    context.padButtonBuilderSetScenario(session, historyBuilder);
    const options = session.options;
    const button = { label_top: 'Living room', widget_sparkline_line_color: color,
        widget_sparkline_line_width: 2, widget_sparkline_current_dot: 5,
        ...historyBuilder.preview(detail, options) };
    context.padRenderCellContent(cell, button);
    historyBuilder.renderPreview(cell, button);
    assert.strictEqual(cell.querySelector('.pad-cell-label-center').textContent, '21.4 \u00b0C');
    assert.strictEqual(cell.querySelector('.pad-widget-marker'), null);
    const chart = cell.children.at(-1);
    assert.strictEqual(chart.attributes.role, 'img');
    assert.match(chart.attributes['aria-label'], /Illustrative trend, not actual history/);
    const [line, dot] = chart.children;
    assert.strictEqual(line.attributes.stroke, color);
    assert.strictEqual(dot.attributes.fill, color);
    assert.strictEqual(Number(dot.attributes.r) * 2, 5);
    const points = line.attributes.points.split(' ').map(point => point.split(',').map(Number));
    assert.deepStrictEqual(points.at(-1), [Number(dot.attributes.cx), Number(dot.attributes.cy)]);
    if (samplePoints) assert.strictEqual(line.attributes.points, samplePoints, 'The illustrative curve stays deterministic');
    samplePoints = line.attributes.points;
}
console.log('portal_pad_preview: PASS');