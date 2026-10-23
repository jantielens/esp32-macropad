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

function descendants(node) {
    return (node.children || []).flatMap(child => [child, ...descendants(child)]);
}

function visibleText(node) {
    return node.textContent !== undefined ? node.textContent : (node.children || []).map(visibleText).join('');
}

const context = {
    TextEncoder,
    window: {},
    deviceInfoCache: { widget_catalog: [] },
    document: {
        createElement, createElementNS: createElement,
        createTextNode: textContent => ({ textContent, querySelector: () => null })
    },
    padColorToHex: (color, fallback) => color || fallback,
    padColorNormalizeLiteral: color => /^#[0-9a-f]{6}$/i.test(color || '') ? color.toLowerCase() : '',
    padGetEffectiveDefault: key => ({ bg_color: '#333333', fg_color: '#ffffff', border_color: '#000000', border_width: '0', corner_radius: '8', content_pad: '4' })[key]
};
vm.createContext(context);
vm.runInContext(fs.readFileSync('src/app/web/portal_widget_editor.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_widget_gauge.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_widget_toggle.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_widget_rocker.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_widget_numericrocker.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_binding_validator.js', 'utf8'), context);
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
    assert.strictEqual(cell.children.length, 1, widget_type);
    assert.notStrictEqual(cell.children[0].title, 'Unknown Widget Widget', widget_type);
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
for (const [background, gripColor] of [
    ['#ffffff', '#17212b'], ['#f0f0f0', '#17212b'], ['#ffff00', '#17212b'],
    ['#bcbcbc', '#17212b'], ['#bbbbbb', '#ffffff'],
    ['#4e8cff', '#ffffff'], ['#0000ff', '#ffffff'], ['#17212b', '#ffffff']
]) {
    const cell = createElement();
    context.padRenderCellContent(cell, { bg_color: background });
    assert.strictEqual(cell.style['--pad-resize-grip-color'], gripColor, background);
}
context.deviceInfoCache.widget_catalog.push({ type: 'future_widget', name: 'Future Widget', icon: 'list',
    axis_field: 'custom_axis', horizontal_icon: 'swap_horiz', vertical_icon: 'swap_vert', default_axis: 'vertical' });
assert.strictEqual(context.padWidgetPreview({ widget_type: 'future_widget', custom_axis: 'horizontal' }).icon, 'swap_horiz');
const longBinding = '[mqtt:home/building/room/sensor/temperature;state;%.1f C|offline]';
const nestedExpression = '[expr:[mqtt:home/setpoint;state;%.1f]*2]';
const mixedBindingText = 'Room ' + longBinding + ' is [health:cpu]\nTarget ' + nestedExpression;
const bindingButton = { label_center: mixedBindingText };
const bindingButtonBefore = JSON.stringify(bindingButton);
const bindingCell = createElement();
context.padRenderCellContent(bindingCell, bindingButton);
const bindingLabel = bindingCell.children.find(child => child.className === 'pad-cell-label-center');
const bindingSpans = descendants(bindingLabel).filter(child => child.className === 'pad-cell-binding');
const longAbbreviation = bindingSpans[0].textContent;
assert(longAbbreviation.startsWith('[mqtt:...') && longAbbreviation.endsWith('/temperature]'));
assert.strictEqual(visibleText(bindingLabel), 'Room ' + longAbbreviation + ' is [health:cpu]\nTarget [expr:...]');
assert.deepStrictEqual(bindingSpans.slice(1).map(span => span.textContent), ['[health:cpu]', '[expr:...]']);
assert.deepStrictEqual(bindingSpans.map(span => span.title), [longBinding, '[health:cpu]', nestedExpression]);
assert(bindingSpans.every(span => span.attributes['aria-hidden'] === 'true'));
assert.strictEqual(bindingCell.attributes['aria-description'], mixedBindingText);
assert.strictEqual(JSON.stringify(bindingButton), bindingButtonBefore);
assert(bindingSpans.every(span => span.textContent.length <= 28));

for (const [field, value, expected] of [
    ['label_top', '[mqtt:home/setpoint;state;%.1f|offline]', '[mqtt:home/setpoint]'],
    ['label_bottom', '[health:cpu]', '[health:cpu]']
]) {
    const labeledCell = createElement();
    context.padRenderCellContent(labeledCell, { [field]: value });
    const className = field === 'label_top' ? 'pad-cell-label-top' : 'pad-cell-label-bottom';
    const label = labeledCell.children.find(child => child.className === className);
    assert.strictEqual(visibleText(label), expected);
    assert.strictEqual(descendants(label).find(child => child.className === 'pad-cell-binding').title, value);
}

const incompleteCell = createElement();
const incompleteBinding = '[mqtt:home/building/temperature;state';
context.padRenderCellContent(incompleteCell, { label_center: incompleteBinding });
const incompleteLabel = incompleteCell.children.find(child => child.className === 'pad-cell-label-center');
assert(visibleText(incompleteLabel).startsWith('[mqtt:...') && visibleText(incompleteLabel).endsWith('/temperature'));
assert.strictEqual(incompleteCell.attributes['aria-description'], incompleteBinding);

const iconBindingCell = createElement();
context.padRenderCellContent(iconBindingCell, {
    icon_id: 'emoji_thermometer', icon_position: 'left', label_center: '[mqtt:home/very/long/temperature]'
});
const iconRow = iconBindingCell.children.find(child => child.className === 'pad-cell-icon-row');
const iconRowLabel = iconRow.children.find(child => child.className === 'pad-cell-label-center');
assert(visibleText(iconRowLabel).startsWith('[mqtt:...') && visibleText(iconRowLabel).endsWith('/temperature]'));
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
for (const width of ['1', '2', '3']) {
    const bordered = createElement();
    context.padRenderCellContent(bordered, { border_width: width, border_color: '#123456' });
    assert.strictEqual(bordered.style.border, width + 'px solid #123456');
    assert.strictEqual(bordered.style['--pad-cell-border-color'], '#123456');
}

const grid = fs.readFileSync('src/app/web/portal_pad_grid.js', 'utf8');
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.padComputeAxisTrackSizes(3, 101, 2, [0, 20, 0]))), {
    sizes: [39, 20, 38], valid: true
});
assert.strictEqual(context.padComputeAxisTrackSizes(3, 100, 2, [30, 30, 30]).valid, false);
assert.deepStrictEqual(Array.from(context.padComputeAxisTrackSizes(3, 100, 2, [30, 30, 30]).sizes), [32, 32, 32]);
assert(grid.includes('padRenderCellContent(cell, tplBtn)'));
assert(!grid.includes('pad-cell-widget-'));
const gridElement = createElement();
context.deviceInfoCache = widgetCatalog;
context.document.getElementById = id => id === 'pad-grid' ? gridElement : null;
context.padState = { cols: 3, rows: 1, buttons: [{ col: 0, row: 0, widget_type: 'list' }],
    templateButtons: [{ col: 1, row: 0, col_span: 2, widget_type: 'gauge',
        label_top: '[mqtt:home/building/temperature;state;%.1f C]' }] };
context.padBuildOccupancySet = () => new Set();
vm.runInContext(grid, context);
const resizeGeometry = { columnSizes: [40, 20, 40], rowSizes: [30, 70], gapX: 2, gapY: 2 };
assert.strictEqual(context.padResizeTrackDelta(6, 'right', resizeGeometry, 0, 0, 1, 1), 1);
assert.strictEqual(context.padResizeTrackDelta(-5, 'right', resizeGeometry, 0, 0, 2, 1), -1);
context.padRenderResizeHandles = () => {};
context.padRenderGrid();
assert.strictEqual(gridElement.children.length, 2);
for (const cell of gridElement.children) assert(cell.children.some(child => child.className === 'pad-widget-marker'));
assert(!gridElement.children[0].children.some(child => child.className === 'pad-widget-fallback'));
assert(!gridElement.children[1].children.some(child => child.className === 'pad-widget-fallback'));
assert(gridElement.children[1].classes.has('pad-cell-ghost'));
assert.strictEqual(gridElement.children[1].style.gridColumn, '2 / span 2');
assert.strictEqual(gridElement.children[1].style.gridRow, '1 / span 1');
assert.strictEqual(gridElement.children[1].children[0].title, 'Gauge Widget');
const templateBindingLabel = gridElement.children[1].children.find(child => child.className === 'pad-cell-label-top');
assert(visibleText(templateBindingLabel).startsWith('[mqtt:...') && visibleText(templateBindingLabel).endsWith('/temperature]'));
const css = fs.readFileSync('src/app/web/portal-custom.css', 'utf8');
const font = Buffer.from(css.match(/data:font\/woff2;base64,([^']+)/)[1], 'base64');
assert.strictEqual(font.toString('ascii', 0, 4), 'wOF2');
assert.strictEqual(font.length, font.readUInt32BE(8));
assert(!css.includes('.pad-cell-widget-'));

vm.runInContext(fs.readFileSync('src/app/web/portal_binding_builder.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_button_templates.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_button_builder.js', 'utf8'), context);
vm.runInContext(`BUTTON_BUILDER_PROVIDERS.push({
    id: 'ha',
    buildBinding(candidate, { format = '' } = {}) {
        return '[ha:' + candidate.reference.entity_id + (format ? ';;' + format : '') + ']';
    },
    supportsHistory(candidate) {
        return candidate.capabilities.numericWidget;
    },
    historyBackfill() { return null; }
});`, context);
context.PAD_FIRMWARE_DEFAULTS = { widget_track_color: '#1A1A1A', widget_series_color: '#4CAF50',
    widget_indicator_color: '#FFFFFF', widget_tick_color: '#AAAAAA' };
for (const widget_type of ['toggle', 'rocker', 'numericrocker', 'future_widget']) {
    const button = { widget_type, label_top: 'Top', label_center: 'Center', label_bottom: 'Bottom', icon_id: 'emoji_X' };
    const original = JSON.stringify(button);
    const cell = createElement();
    context.padRenderCellContent(cell, button);
    const marker = cell.children.find(child => child.className === 'pad-widget-marker');
    assert(marker.attributes['aria-label']);
    assert.strictEqual(cell.children.slice(1).map(visibleText).join('|'), 'Top|Center|Bottom');
    assert.strictEqual(cell.querySelector('.pad-cell-icon'), null);
    assert.strictEqual(JSON.stringify(button), original, 'Preview rendering leaves widget configuration unchanged');
}
const ordinaryToggle = { col: 0, row: 0, widget_type: 'toggle', label_center: 'Ordinary',
    widget_toggle_on_color: '#4caf50', widget_toggle_off_color: '#59636e', widget_toggle_knob_color: '#ffffff', widget_toggle_width_pct: 75 };
const templateToggle = { col: 1, row: 0, widget_type: 'toggle', label_center: 'Template', col_span: 2,
    widget_toggle_on_color: '#2196f3', widget_toggle_off_color: '#59636e', widget_toggle_knob_color: '#ffffff', widget_toggle_width_pct: 70 };
const ordinaryToggleBefore = JSON.stringify(ordinaryToggle);
const templateToggleBefore = JSON.stringify(templateToggle);
context.padState = { cols: 3, rows: 1, buttons: [ordinaryToggle], templateButtons: [templateToggle] };
gridElement.children = [];
context.padRenderGrid();
assert.strictEqual(gridElement.children.length, 2);
assert(gridElement.children[0].querySelector('.pad-widget-marker'));
assert(gridElement.children[1].querySelector('.pad-widget-marker'));
assert(gridElement.children[1].classes.has('pad-cell-ghost'));
assert.strictEqual(gridElement.children[1].style.gridColumn, '2 / span 2');
assert.strictEqual(JSON.stringify(ordinaryToggle), ordinaryToggleBefore);
assert.strictEqual(JSON.stringify(templateToggle), templateToggleBefore);
const barBuilder = vm.runInContext('padButtonBuilders.find(builder => builder.id === "bar")', context);
const builderElements = Object.fromEntries(['fields', 'states', 'note', 'sources', 'selection', 'name', 'id', 'state'].map(name => ['[data-bb-' + name + ']', createElement()]));
const makeDraft = (detail, name) => {
    const candidate = { providerId: 'ha', sourceId: detail.entity_id || 'sensor.preview',
        displayName: name, reference: { entity_id: detail.entity_id || 'sensor.preview' },
        sample: detail.state, sampleTruncated: false, unit: detail.unit,
        formatModes: ['raw', 'number'], capabilities: { display: true, numericWidget: true, liveSampling: true } };
    return { sources: [{ candidate, name, options: {} }], options: {}, previewState: detail.state };
};
context.testBuilderSession = {
    draft: makeDraft({ state: '42', unit: '%' }, 'Battery'), scenario: barBuilder,
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
assert.strictEqual(context.testBuilderSession.draft.options.orientation, 'horizontal', 'Custom dropdowns dispatch change, not input');
assert.strictEqual(previewUpdates, 2);
const minimumInput = builderInputs.find(input => input.name === 'min');
minimumInput.value = '';
minimumInput.oninput();
assert.strictEqual(context.testBuilderSession.draft.options.min, '', 'Keep incomplete numeric input raw');
assert.throws(() => context.padButtonBuilderValidateFields(context.testBuilderSession), /minimum and maximum/);
minimumInput.value = '-0.5';
minimumInput.oninput();
assert.strictEqual(context.testBuilderSession.draft.options.min, '-0.5');
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
        barBuilder.renderPreview(makeDraft(detail, 'Power'), cell, button);
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
    const session = { draft: makeDraft(detail, 'Power') };
    context.padButtonBuilderSetScenario(session, gaugeBuilder);
    Object.assign(session.draft.options, { min, max, color: '#2196F3' });
    const button = { label_top: 'Power', ...gaugeBuilder.build(session.draft),
        ...gaugeBuilder.preview(session.draft) };
    const cell = createElement();
    context.padRenderCellContent(cell, button);
    gaugeBuilder.renderPreview(session.draft, cell, button);
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
    const detail = { entity_id: 'sensor.temperature', state: '21.37', unit: '\u00b0C' };
    const session = { draft: makeDraft(detail, 'Living room') };
    context.padButtonBuilderSetScenario(session, historyBuilder);
    session.draft.sources[0].options.color = color;
    const button = { ...historyBuilder.build(session.draft),
        ...historyBuilder.preview(session.draft) };
    context.padRenderCellContent(cell, button);
    historyBuilder.renderPreview(session.draft, cell, button);
    assert.strictEqual(cell.querySelector('.pad-cell-label-top').textContent, 'Living room');
    assert.strictEqual(cell.querySelector('.pad-cell-label-center').textContent, '21.4 \u00b0C');
    assert.strictEqual(cell.querySelector('.pad-widget-marker'), null);
    assert.ok(cell.classes.has('pad-button-builder-preview-single-history'));
    const chart = cell.children.at(-1);
    assert.strictEqual(chart.attributes.role, 'img');
    assert.strictEqual(chart.attributes.viewBox, '0 0 280 190');
    assert.match(chart.attributes['aria-label'], /Illustrative trend, not actual history/);
    assert.match(chart.attributes['aria-label'], /Living room: 21\.4/);
    const [line, dot] = chart.children;
    assert.strictEqual(line.attributes.stroke, color);
    assert.strictEqual(dot.attributes.fill, color);
    assert.strictEqual(line.attributes.points.split(' ').at(-1).split(',')[0], '276',
        'The single-value trend spans the preview width without an endpoint label');
    assert.strictEqual(Number(dot.attributes.r) * 2, 5);
    const points = line.attributes.points.split(' ').map(point => point.split(',').map(Number));
    assert.deepStrictEqual(points.at(-1), [Number(dot.attributes.cx), Number(dot.attributes.cy)]);
    if (samplePoints) assert.strictEqual(line.attributes.points, samplePoints, 'The illustrative curve stays deterministic');
    samplePoints = line.attributes.points;
}
{
    const detail = { entity_id: 'sensor.temperature', state: '21.37', unit: '\u00b0C' };
    const session = { draft: makeDraft(detail, 'Living room') };
    context.padButtonBuilderSetScenario(session, historyBuilder);
    session.draft.sources.push(context.padButtonBuilderSource(historyBuilder, {
        providerId: 'ha', sourceId: 'sensor.humidity', displayName: 'Humidity',
        reference: { entity_id: 'sensor.humidity' }, sample: '54.2', sampleTruncated: false, unit: '%',
        formatModes: ['raw', 'number'], capabilities: { display: true, numericWidget: true, liveSampling: true }
    }, 1));
    const render = () => {
        const cell = createElement();
        const button = { ...historyBuilder.build(session.draft), ...historyBuilder.preview(session.draft) };
        context.padRenderCellContent(cell, button);
        historyBuilder.renderPreview(session.draft, cell, button);
        return cell;
    };
    const cell = render();
    const chart = cell.children.at(-1);
    assert.strictEqual(chart.children.length, 6, 'Both values have a line, dot, and endpoint label');
    assert.strictEqual(chart.children[5].textContent, '54.2 %');
    assert.strictEqual(chart.children[5].attributes.fill, '#2196F3');
    assert.strictEqual(chart.children[5].attributes.y, chart.children[4].attributes.cy);
    session.draft.sources[1].candidate.sample = 'unavailable';
    const unavailable = render();
    assert.strictEqual(unavailable.children.at(-1).children[3].textContent, '---');
    assert.strictEqual(unavailable.children.at(-1).children.length, 4, 'Unavailable values have a label but no zero or fake trend');
}
for (const source of ['', 'on', 'off', '[expr:[ha:climate.bureau_boven_thermostaat;attributes.temperature]>20?"on":"off"]']) {
    const cell = createElement();
    context.padRenderCellContent(cell, { widget_type: 'toggle', widget_data_binding: source,
        label_top: 'Bureau', label_bottom: '[ha:climate.bureau_boven_thermostaat;attributes.temperature;%.1f \u00b0C]',
        widget_toggle_on_color: '#123456' });
    assert.strictEqual(cell.children[0].className, 'pad-widget-marker');
    assert.strictEqual(cell.children[0].attributes['aria-label'], 'Toggle switch Widget');
    assert(!cell.children.some(child => child.attributes.role === 'switch'));
    assert(cell.children.some(child => child.textContent === 'Bureau'));
}
assert.strictEqual(context.padWidgetEditor('toggle').icon.width, 0.4);
assert.strictEqual(context.padWidgetEditor('toggle').icon.height, 0.4);
console.log('portal_pad_preview: PASS');