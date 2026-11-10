const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

function element() {
    return {
        style: { setProperty(name, value) { this[name] = value; } },
        dataset: {}, children: [], attributes: {}, classes: new Set(),
        classList: {
            add(...names) { names.forEach(name => this.owner.classes.add(name)); },
            remove(...names) { names.forEach(name => this.owner.classes.delete(name)); },
            contains(name) { return this.owner.classes.has(name); }
        },
        appendChild(child) { this.children.push(child); },
        append(...children) { this.children.push(...children); },
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
        querySelectorAll() { return []; },
        addEventListener() {},
        setAttribute(name, value) { this.attributes[name] = value; },
        removeAttribute(name) { delete this.attributes[name]; }
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
        getElementById: () => null,
        createTextNode: textContent => ({ textContent, querySelector: () => null })
    },
    padGetEffectiveDefault: key => ({ bg_color: '#333333', fg_color: '#ffffff', border_color: '#000000', border_width: '0', corner_radius: '8', content_pad: '4', default_pad_bg_color: '#000000' })[key]
};
vm.createContext(context);
vm.runInContext(fs.readFileSync('src/app/web/portal_pad_colors.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_widget_editor.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_widget_gauge.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_widget_toggle.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_widget_rocker.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_widget_numericrocker.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_binding_validator.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_pad_icons.js', 'utf8'), context);
context.padAppendIconPreview = function(container, icon, foreground) {
    const image = createElement();
    image.className = 'pad-cell-icon';
    image.dataset.kind = icon.kind;
    image.dataset.foreground = foreground;
    container.appendChild(image);
    return image;
};

{
    const cell = createElement();
    context.padRenderCellContent(cell, { label_center: '21.5', reserve_label_slots: true });
    const labels = cell.querySelector('.pad-cell-group-labels');
    assert.strictEqual(labels.children.length, 1);
    assert.strictEqual(context.padCellPreviewSignature({ label_center: '21.5' }),
        context.padCellPreviewSignature({ label_center: '21.5', reserve_label_slots: true }));
}

{
    const cell = createElement();
    context.padRenderCellContent(cell, { label_top: 'Test', label_center: 'Test' });
    const labels = cell.querySelector('.pad-cell-group-labels');
    assert.strictEqual(labels.children.length, 2);
    assert.strictEqual(labels.dataset.centered, undefined);
}

{
    const icon = { catalog_id: 'material-symbols:home', kind: 'mono' };
    const render = button => {
        const cell = createElement();
        context.padRenderCellContent(cell, { icon, ...button });
        return cell.querySelector('.pad-cell-icon').dataset.foreground;
    };
    assert.strictEqual(render({ fg_color: '#abcdef' }), '#abcdef');
    assert.strictEqual(render({ fg_color: '#abcdef', icon_color: '#ff0000' }), '#ff0000');
    assert.strictEqual(render({ fg_color: '#abcdef', icon_color: '[health:color]' }), '#abcdef');
    assert.notStrictEqual(context.padCellPreviewSignature({ icon, icon_color: '#ff0000' }),
        context.padCellPreviewSignature({ icon, icon_color: '#0000ff' }));
}

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
    assert.strictEqual(cell.children[0].className, 'pad-content-widget-region', widget_type);
    assert.strictEqual(cell.children.length, 1, widget_type);
    assert.notStrictEqual(cell.children[0].title, 'Unknown Widget Widget', widget_type);
    assert(cell.children[0].attributes['aria-label']);
    const labeled = createElement();
    context.padRenderCellContent(labeled, { widget_type, label_center: 'My label' });
    assert(descendants(labeled).some(child => child.textContent === 'My label'), widget_type);
    assert(!labeled.children.some(child => child.className === 'pad-widget-fallback'), widget_type);
}

assert.strictEqual(context.padWidgetPreview({ widget_type: 'rocker' }).icon, 'swap_vert');
assert.strictEqual(context.padWidgetPreview({ widget_type: 'rocker', widget_rocker_axis: 'horizontal' }).icon, 'swap_horiz');
assert.strictEqual(context.padWidgetPreview({ widget_type: 'scrollpad' }).icon, 'keyboard_double_arrow_up');
assert.strictEqual(context.padWidgetPreview({ widget_type: 'scrollpad', widget_scrollpad_axis: 'horizontal' }).icon, 'keyboard_double_arrow_left');
assert.strictEqual(context.padWidgetPreview({ widget_type: 'numericrocker' }).icon, 'exposure');
assert.strictEqual(context.padCellPreviewSignature({ widget_type: 'numericrocker', widget_numericrocker_axis: 'horizontal' }),
    context.padCellPreviewSignature({ widget_type: 'numericrocker', widget_numericrocker_axis: 'vertical' }));
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
for (const layer of ['main', 'overlay']) {
    const cell = createElement();
    cell.dataset.layer = layer;
    context.padRenderCellContent(cell, { bg_color: 'transparent', label_center: 'Visible content' });
    assert.strictEqual(cell.style.background, 'transparent');
    assert(descendants(cell).some(child => child.textContent === 'Visible content'));
    context.padRenderCellContent(cell, { bg_color: '[mqtt:background]' });
    assert.strictEqual(cell.style.background, '#333333', 'Unresolved preview must not retain stale transparency');
    context.padRenderCellContent(cell, { bg_color: '#333333' });
    assert.strictEqual(cell.style.background, '#333333', 'Opaque editing must clear transparency');
    context.padRenderCellContent(cell, { bg_color: '#fff' });
    assert.strictEqual(cell.style.background, '#FFFFFF', 'Short hex backgrounds remain supported');
    context.padRenderCellContent(cell, {});
    assert.strictEqual(cell.style.background, '#333333', 'Unset backgrounds remain opaque');
}
context.deviceInfoCache.widget_catalog.push({ type: 'future_widget', name: 'Future Widget', icon: 'list',
    axis_field: 'custom_axis', horizontal_icon: 'swap_horiz', vertical_icon: 'swap_vert', default_axis: 'vertical' });
assert.strictEqual(context.padWidgetPreview({ widget_type: 'future_widget', custom_axis: 'horizontal' }).icon, 'swap_horiz');
for (const [background, button, expected] of [
    ['#000000', { bg_color: '#000000', border_color: '#000000', border_width: '2' }, true],
    ['#000000', { bg_color: '#101010', border_width: '0' }, true],
    ['#000000', { bg_color: '#ffffff', border_width: '0' }, false],
    ['#000000', { bg_color: '#ffffff', border_color: '#000000', border_width: '2' }, false],
    ['#000000', { bg_color: '#000000', border_color: '#ffffff', border_width: '2' }, true],
    ['#ffffff', { bg_color: '#ffffff', border_width: '0' }, true],
    ['#ffffff', { bg_color: '#000000', border_width: '0' }, false]
]) {
    context.document.getElementById = () => ({ value: background });
    const cell = createElement();
    const before = JSON.stringify(button);
    context.padRenderCellContent(cell, button);
    assert.strictEqual(cell.classes.has('pad-cell-boundary-guide'), expected, JSON.stringify([background, button]));
    assert.strictEqual(JSON.stringify(button), before);
    context.padRenderCellContent(cell, { bg_color: background === '#000000' ? '#ffffff' : '#000000', border_width: '0' });
    assert(!cell.classes.has('pad-cell-boundary-guide'), 'Appearance edits must remove stale guides');
}
context.document.getElementById = () => null;
const longBinding = '[mqtt:home/building/room/sensor/temperature;state;%.1f C|offline]';
const nestedExpression = '[expr:[mqtt:home/setpoint;state;%.1f]*2]';
const mixedBindingText = 'Room ' + longBinding + ' is [health:cpu]\nTarget ' + nestedExpression;
const bindingButton = { label_center: mixedBindingText };
const bindingButtonBefore = JSON.stringify(bindingButton);
const bindingCell = createElement();
context.padRenderCellContent(bindingCell, bindingButton);
const bindingLabel = bindingCell.querySelector('.pad-cell-label-center');
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
    const label = labeledCell.querySelector('.' + className);
    assert.strictEqual(visibleText(label), expected);
    assert.strictEqual(descendants(label).find(child => child.className === 'pad-cell-binding').title, value);
}

const incompleteCell = createElement();
const incompleteBinding = '[mqtt:home/building/temperature;state';
context.padRenderCellContent(incompleteCell, { label_center: incompleteBinding });
const incompleteLabel = incompleteCell.querySelector('.pad-cell-label-center');
assert(visibleText(incompleteLabel).startsWith('[mqtt:...') && visibleText(incompleteLabel).endsWith('/temperature'));
assert.strictEqual(incompleteCell.attributes['aria-description'], incompleteBinding);

const iconBindingCell = createElement();
context.padRenderCellContent(iconBindingCell, {
    icon: { catalog_id: 'noto:thermometer', kind: 'color' }, icon_position: 'left', label_center: '[mqtt:home/very/long/temperature]'
});
const iconRowLabel = iconBindingCell.querySelector('.pad-cell-label-center');
assert(visibleText(iconRowLabel).startsWith('[mqtt:...') && visibleText(iconRowLabel).endsWith('/temperature]'));
{
    const button = { widget_type: 'sparkline', icon: { kind: 'color' }, label_center: 'Room' };
    const appearance = { widget_position: 'overlay-bottom-right', group_size_pct: 90,
        icon_position: 'below', content_pad: 50, border_width: 8, corner_radius: 30,
        reserve_label_slots: true };
    assert.strictEqual(context.padCellPreviewSignature(button), context.padCellPreviewSignature({ ...button, ...appearance }));
    const cell = createElement();
    context.padRenderCellContent(cell, { ...button, ...appearance });
    assert.strictEqual(cell.querySelector('.pad-content-widget-region').attributes['aria-label'],
        'Sparkline widget placeholder, not a live rendering');
    assert.strictEqual(visibleText(cell.querySelector('.pad-cell-label-center')), 'Room');
    assert(cell.querySelector('.pad-cell-icon'));
}
for (const image of [{ bg_image_url: 'https://example.test/image.png' }, { bg_image_path: '/image.png' }]) {
    const imageOnly = createElement();
    context.padRenderCellContent(imageOnly, image);
    assert.strictEqual(imageOnly.querySelector('.pad-content-widget-region').attributes['aria-label'],
        'Background image configured');
    assert.strictEqual(imageOnly.title, 'Background image configured');
    assert(descendants(imageOnly).some(child => child.textContent === 'image'));
    const cell = createElement();
    context.padRenderCellContent(cell, { ...image, widget_type: 'gauge', label_center: 'Camera' });
    const marker = cell.querySelector('.pad-content-widget-region');
    assert.match(marker.attributes['aria-label'], /Gauge widget.*Background image configured/);
    assert(descendants(marker).some(child => child.textContent === 'image'));
    assert.strictEqual(visibleText(cell.querySelector('.pad-cell-label-center')), 'Camera');
    assert.notStrictEqual(context.padCellPreviewSignature(image), context.padCellPreviewSignature({}));
}
for (const icon_position of ['left', 'above']) {
    const cell = createElement();
    const button = { icon: { catalog_id: 'noto:thermometer', kind: 'color' }, icon_position, label_center: 'Before' };
    context.padRenderCellContent(cell, button);
    const icon = cell.querySelector('.pad-cell-icon');
    icon.src = 'blob:retained-icon';
    for (const label of ['After', '', '[health:cpu]']) {
        cell.replaceChildren();
        context.padRenderCellContent(cell, { ...button, label_center: label }, icon);
        assert.strictEqual(cell.querySelector('.pad-cell-icon'), icon, 'Label edits must retain the rendered icon');
        assert.strictEqual(icon.src, 'blob:retained-icon');
        const center = cell.querySelector('.pad-cell-label-center');
        assert.strictEqual(center ? visibleText(center) : '', label);
    }
}
const widgetCatalog = context.deviceInfoCache;
{
    const icon = { catalog_id: 'noto:thermometer', kind: 'color' };
    const button = { icon, icon_badge_padding: 8, icon_badge_color: '#123456', icon_position: 'left',
        label_top: 'Indoor', label_center: '21.4 C', widget_type: 'sparkline' };
    const cell = createElement();
    context.padRenderCellContent(cell, button);
    assert.strictEqual(cell.querySelector('.pad-cell-icon-badge'), null);
    const retained = cell.querySelector('.pad-cell-icon');
    cell.replaceChildren();
    context.padRenderCellContent(cell, { ...button, icon_position: 'right', icon_badge_color: '#123456' }, retained);
    assert.strictEqual(cell.querySelector('.pad-cell-icon-badge'), null);
    assert.strictEqual(cell.querySelector('.pad-cell-icon'), retained);
    const signature = context.padCellPreviewSignature(button);
    assert.strictEqual(context.padCellPreviewSignature({ ...button, icon_badge_padding: 0 }), signature);
    assert.strictEqual(context.padCellPreviewSignature({ ...button, icon_badge_color: '#654321' }), signature);
    for (const changed of [{ ...button, icon_badge_padding: 0 }, { ...button, icon: undefined }]) {
        const plain = createElement();
        context.padRenderCellContent(plain, changed);
        assert.strictEqual(plain.querySelector('.pad-cell-icon-badge'), null);
    }
}
context.deviceInfoCache = null;
assert.strictEqual(context.padWidgetPreview({ widget_type: 'gauge' }).name, 'Unknown Widget');

for (const content of [{ label_top: 'Top' }, { label_bottom: 'Bottom' },
    { icon: { catalog_id: 'noto:thermometer', kind: 'color' } }, { bg_image_path: '/image.png' }]) {
    const cell = createElement();
    context.padRenderCellContent(cell, { widget_type: 'gauge', ...content });
    assert(!cell.children.some(child => child.className === 'pad-widget-fallback'));
    assert(!cell.children.some(child => child.textContent === '\u2022'));
}
const normal = createElement();
context.padRenderCellContent(normal, {});
assert.strictEqual(normal.children.length, 0);

const grid = fs.readFileSync('src/app/web/portal_pad_grid.js', 'utf8');
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.padComputeAxisTrackSizes(3, 101, 2, [0, 20, 0]))), {
    sizes: [39, 20, 38], valid: true
});
for (const gap of [0, 6, 10]) {
    for (const [count, size] of [[8, 400], [6, 300]]) {
        const result = context.padComputeAxisTrackSizes(count, size, gap, []);
        assert.strictEqual(result.valid, true);
        assert.strictEqual(result.sizes.reduce((sum, track) => sum + track, 0) + (count - 1) * gap, size);
        assert(Math.max(...result.sizes) - Math.min(...result.sizes) <= 1);
    }
}
assert.strictEqual(context.padComputeAxisTrackSizes(3, 100, 2, [30, 30, 30]).valid, false);
assert.deepStrictEqual(Array.from(context.padComputeAxisTrackSizes(3, 100, 2, [30, 30, 30]).sizes), [32, 32, 32]);
assert(grid.includes('padRenderCellContent(cell, button)'));
assert(!grid.includes('pad-cell-widget-'));
const gridElement = createElement();
context.deviceInfoCache = widgetCatalog;
context.document.getElementById = id => id === 'pad-grid' ? gridElement : null;
context.padState = { cols: 3, rows: 1, selectedLayer: 'main', mainButtons: [{ col: 0, row: 0, widget_type: 'list' }], overlayButtons: [],
    get buttons() { return this.selectedLayer === 'overlay' ? this.overlayButtons : this.mainButtons; },
    set buttons(value) { if (this.selectedLayer === 'overlay') this.overlayButtons = value; else this.mainButtons = value; },
    templateButtons: [{ col: 1, row: 0, col_span: 2, widget_type: 'gauge',
        label_top: '[mqtt:home/building/temperature;state;%.1f C]' }] };
context.padBuildOccupancySet = () => new Set();
vm.runInContext(grid, context);
{
    const cell = createElement();
    const background = { value: '#000000' };
    context.document.getElementById = id => id === 'pad-grid' ? gridElement : background;
    context.padRenderCellContent(cell, { bg_color: '#000000' });
    gridElement.querySelectorAll = () => [cell];
    assert(cell.classes.has('pad-cell-boundary-guide'));
    background.value = '#ffffff';
    context.padUpdateGridBackground();
    assert(!cell.classes.has('pad-cell-boundary-guide'), 'Pad background changes must refresh existing guides');
    gridElement.querySelectorAll = () => [];
    context.document.getElementById = id => id === 'pad-grid' ? gridElement : null;
}
const resizeGeometry = { columnSizes: [40, 20, 40], rowSizes: [30, 70], gapX: 2, gapY: 2 };
assert.strictEqual(context.padResizeTrackDelta(6, 'right', resizeGeometry, 0, 0, 1, 1), 1);
assert.strictEqual(context.padResizeTrackDelta(-5, 'right', resizeGeometry, 0, 0, 2, 1), -1);
context.padRenderResizeHandles = () => {};
context.padRenderGrid();
assert.strictEqual(gridElement.children.length, 2);
for (const cell of gridElement.children) assert(cell.children.some(child => child.className === 'pad-content-widget-region'));
assert(!gridElement.children[0].children.some(child => child.className === 'pad-widget-fallback'));
assert(!gridElement.children[1].children.some(child => child.className === 'pad-widget-fallback'));
assert(gridElement.children[1].classes.has('pad-cell-ghost'));
assert.strictEqual(gridElement.children[1].style.gridColumn, '2 / span 2');
assert.strictEqual(gridElement.children[1].style.gridRow, '1 / span 1');
assert.strictEqual(gridElement.children[1].querySelector('.pad-content-widget-region').title, 'Gauge widget placeholder, not a live rendering');
const templateBindingLabel = gridElement.children[1].querySelector('.pad-cell-label-top');
assert(visibleText(templateBindingLabel).startsWith('[mqtt:...') && visibleText(templateBindingLabel).endsWith('/temperature]'));
const css = fs.readFileSync('src/app/web/portal-custom.css', 'utf8');
{
    const originalRenderGrid = context.padRenderGrid;
    const cell = createElement();
    const button = { col: 0, row: 0, icon: { catalog_id: 'noto:thermometer', kind: 'color' }, label_center: 'Before' };
    context.padRenderCellContent(cell, button);
    const icon = cell.querySelector('.pad-cell-icon');
    const children = cell.children;
    let rebuilds = 0;
    context.document.querySelector = () => cell;
    context.padRenderGrid = () => { rebuilds++; };
    for (const changed of [{ bg_image_interval_ms: 1000 }, { actions: [{ type: 'screen', target: 'info' }] }]) {
        context.padUpdateButtonPreview(button, { ...button, ...changed });
        assert.strictEqual(cell.children, children, 'Nonvisual model changes must not touch preview content');
    }
    context.padUpdateButtonPreview({ ...button, col_span: 1, row_span: 1 }, button);
    assert.strictEqual(rebuilds, 0, 'Explicit unit spans and omitted spans have identical geometry');
    const labeled = { ...button, label_center: 'After' };
    context.padUpdateButtonPreview(button, labeled);
    assert.strictEqual(visibleText(cell.querySelector('.pad-cell-label-center')), 'After');
    assert.strictEqual(cell.querySelector('.pad-cell-icon'), icon);
    const withImage = { ...labeled, bg_image_path: '/image.png' };
    context.padUpdateButtonPreview(labeled, withImage);
    assert.match(cell.attributes['aria-description'], /Background image configured/);
    assert(descendants(cell.querySelector('.pad-content-widget-region')).some(child => child.textContent === 'image'));
    context.padUpdateButtonPreview(withImage, labeled);
    assert.strictEqual(cell.querySelector('.pad-content-widget-region'), null);
    assert.doesNotMatch(cell.attributes['aria-description'], /Background image configured/);
    assert.strictEqual(cell.title, '');
    const colored = { ...labeled, fg_color: '#123456' };
    context.padUpdateButtonPreview(labeled, colored);
    assert.strictEqual(cell.style.color, '#123456');
    assert.notStrictEqual(cell.querySelector('.pad-cell-icon'), icon, 'Foreground changes must rerender the artwork');
    const widget = { col: 0, row: 0, widget_type: 'rocker', widget_rocker_axis: 'horizontal' };
    context.padUpdateButtonPreview(colored, widget);
    assert.strictEqual(cell.querySelector('.pad-widget-glyph').textContent, 'swap_horiz');
    context.padUpdateButtonPreview(widget, { ...widget, widget_rocker_axis: 'vertical' });
    assert.strictEqual(cell.querySelector('.pad-widget-glyph').textContent, 'swap_vert');
    assert.strictEqual(rebuilds, 0);
    context.padUpdateButtonPreview(widget, { ...widget, col_span: 2 });
    assert.strictEqual(rebuilds, 1, 'Geometry changes must rebuild the grid');
    const empty = createElement();
    let replacement;
    empty.replaceWith = node => { replacement = node; };
    context.document.querySelector = selector => selector.includes('.pad-cell-empty') ? empty : replacement;
    context.padUpdateButtonPreview(undefined, widget);
    assert.strictEqual(rebuilds, 1, 'Creating a unit-size button must replace only its empty position');
    assert(replacement.classes.has('pad-cell-btn'));
    assert(!replacement.classes.has('pad-cell-empty'));
    assert.strictEqual(replacement.style.gridColumn, '1 / span 1');
    assert.strictEqual(replacement.style.gridRow, '1 / span 1');
    assert.strictEqual(replacement.draggable, true);
    context.padUpdateButtonPreview(widget, { ...widget, label_center: 'Second edit' });
    assert.strictEqual(visibleText(replacement.querySelector('.pad-cell-label-center')), 'Second edit');
    assert.strictEqual(rebuilds, 1);
    context.padState.selectedLayer = 'overlay';
    context.padUpdateButtonPreview(undefined, widget);
    assert.strictEqual(replacement.dataset.layer, 'overlay');
    assert(replacement.classes.has('pad-cell-overlay'));
    assert.strictEqual(rebuilds, 1, 'Overlay creation must also leave other previews untouched');
    context.padState.selectedLayer = 'main';
    context.padUpdateButtonPreview(undefined, { ...widget, col_span: 2 });
    assert.strictEqual(rebuilds, 2, 'New spanning buttons still require a layout rebuild');
    context.document.querySelector = () => null;
    context.padUpdateButtonPreview(undefined, widget);
    assert.strictEqual(rebuilds, 3, 'Missing empty previews must fall back to a complete layout render');
    context.padRenderGrid = originalRenderGrid;
    delete context.document.querySelector;
}
{
    const savedState = context.padState;
    const savedGetElementById = context.document.getElementById;
    const savedOccupancy = context.padBuildOccupancySet;
    const savedGeometry = context.padGetGridGeometry;
    const listeners = {};
    const testGrid = createElement();
    Object.defineProperty(testGrid, 'innerHTML', { set() { this.children = []; } });
    testGrid.appendChild = function(child) {
        this.children.push(child);
        child.remove = () => { this.children.splice(this.children.indexOf(child), 1); };
    };
    context.document.getElementById = id => id === 'pad-grid' ? testGrid : null;
    context.document.addEventListener = (name, handler) => { listeners[name] = handler; };
    context.document.removeEventListener = name => { delete listeners[name]; };
    let timer;
    context.setTimeout = callback => { timer = callback; };
    context.padGetGridGeometry = () => ({ columnSizes: [100, 100, 100, 100], rowSizes: [100, 100], gapX: 0, gapY: 0 });
    context.padBuildOccupancySet = () => new Set(context.padState.buttons.flatMap(button =>
        Array.from({ length: button.col_span || 1 }, (_, dc) =>
            Array.from({ length: button.row_span || 1 }, (_, dr) => (button.col + dc) + ',' + (button.row + dr))).flat()));
    context.padMarkDirty = () => {};
    const icon = { catalog_id: 'noto:thermometer', kind: 'color' };
    const moved = { col: 0, row: 0, icon };
    const other = { col: 3, row: 0, icon };
    context.padState = { cols: 4, rows: 2, selectedLayer: 'main', mainButtons: [moved, other], overlayButtons: [],
        get buttons() { return this.selectedLayer === 'overlay' ? this.overlayButtons : this.mainButtons; },
        templateButtons: [{ col: 1, row: 0, icon }] };
    context.padRenderGrid();
    const movedCell = testGrid.children.find(cell => cell.dataset.col === 0 && cell.dataset.row === 0);
    const otherCell = testGrid.children.find(cell => cell.dataset.col === 3 && cell.dataset.row === 0);
    const movedIcon = movedCell.querySelector('.pad-cell-icon');
    const otherIcon = otherCell.querySelector('.pad-cell-icon');

    context.padResizeDragStart(0, 0, 'right', 0, 0);
    listeners.mousemove({ clientX: 30, clientY: 0 });
    assert.strictEqual(movedCell.style.gridColumn, '1 / span 2');
    assert(!testGrid.children.some(cell => cell.classes.has('pad-cell-ghost')), 'A covered template must disappear');
    assert.strictEqual(otherCell.querySelector('.pad-cell-icon'), otherIcon);
    listeners.mousemove({ clientX: 0, clientY: 0 });
    listeners.mouseup({ type: 'mouseup' });
    let prevented = false;
    let stopped = false;
    listeners.click({ preventDefault() { prevented = true; }, stopPropagation() { stopped = true; } });
    assert(prevented && stopped, 'The click following a resize must not deselect the button');
    timer();
    assert(!listeners.click, 'Click suppression must not affect later clicks');
    assert(testGrid.children.some(cell => cell.classes.has('pad-cell-ghost')), 'An uncovered template must return');
    assert.strictEqual(movedCell.querySelector('.pad-cell-icon'), movedIcon);
    assert(testGrid.children.includes(otherCell), 'Resize must retain unrelated buttons');

    let animationFrame;
    context.requestAnimationFrame = callback => { animationFrame = callback; };
    movedCell.closest = () => movedCell;
    context.padDragStart({ target: movedCell, dataTransfer: { setData() {} } }, 0, 0);
    context.padDrop({ preventDefault() {} }, 2, 1);
    context.padDragEnd({ target: movedCell });
    animationFrame();
    assert(testGrid.children.includes(movedCell), 'Move must retain the moved button');
    assert.strictEqual(movedCell.style.gridColumn, '3 / span 1');
    assert.strictEqual(movedCell.style.gridRow, '2 / span 1');
    assert.strictEqual(movedCell.querySelector('.pad-cell-icon'), movedIcon);
    assert.strictEqual(movedCell.style.opacity, '', 'A completed drag must not be dimmed by a late animation frame');
    assert(testGrid.children.includes(otherCell), 'Move must retain unrelated buttons');
    assert.strictEqual(otherCell.querySelector('.pad-cell-icon'), otherIcon);
    assert(testGrid.children.some(cell => cell.classes.has('pad-cell-empty') && cell.dataset.col === 0 && cell.dataset.row === 0));

    context.padState.selectedLayer = 'overlay';
    const overlay = { col: 0, row: 0, icon };
    context.padState.overlayButtons = [overlay];
    context.padRenderGrid();
    const mainPreview = testGrid.children.find(cell => cell.dataset.layer === 'main' && cell.dataset.col === 3);
    const mainIcon = mainPreview.querySelector('.pad-cell-icon');
    const overlayCell = testGrid.children.find(cell => cell.dataset.layer === 'overlay' && cell.dataset.col === 0);
    context.padResizeDragStart(0, 0, 'right', 0, 0);
    listeners.mousemove({ clientX: 30, clientY: 0 });
    listeners.mouseup({ type: 'mouseup' });
    timer();
    assert.strictEqual(overlayCell.style.gridColumn, '1 / span 2');
    assert(testGrid.children.includes(mainPreview), 'Overlay resizing must retain main-layer previews');
    assert.strictEqual(mainPreview.querySelector('.pad-cell-icon'), mainIcon);

    vm.runInContext(fs.readFileSync('src/app/web/portal_pad_dialog.js', 'utf8'), context);
    context.padWorkspaceClearButton = () => {};
    context.padWorkspaceReturn = () => {};
    context.padState = { cols: 4, rows: 2, selectedLayer: 'main', editCol: 0, editRow: 0,
        mainButtons: [{ col: 0, row: 0, col_span: 2, row_span: 2, widget_type: 'gauge' }, other],
        overlayButtons: [],
        get buttons() { return this.selectedLayer === 'overlay' ? this.overlayButtons : this.mainButtons; },
        set buttons(value) { if (this.selectedLayer === 'overlay') this.overlayButtons = value; else this.mainButtons = value; },
        templateButtons: [{ col: 1, row: 0, icon }] };
    context.padRenderGrid();
    const retainedButton = testGrid.children.find(cell => cell.dataset.layer === 'main' && cell.dataset.col === 3);
    const retainedIcon = retainedButton.querySelector('.pad-cell-icon');
    const removedButton = testGrid.children.find(cell => cell.dataset.layer === 'main' && cell.dataset.col === 0);
    context.padDialogClear();
    assert(!testGrid.children.includes(removedButton), 'Clear must remove the selected button');
    assert(testGrid.children.includes(retainedButton), 'Clear must retain unrelated buttons');
    assert.strictEqual(retainedButton.querySelector('.pad-cell-icon'), retainedIcon);
    assert(testGrid.children.some(cell => cell.classes.has('pad-cell-ghost') && cell.dataset.col === 1),
        'Clear must reveal an unblocked template button');
    for (const [col, row] of [[0, 0], [0, 1], [1, 1]]) {
        assert(testGrid.children.some(cell => cell.classes.has('pad-cell-empty') &&
            Number(cell.dataset.col) === col && Number(cell.dataset.row) === row), 'Clear must free the button span');
    }

    context.padState.selectedLayer = 'overlay';
    context.padState.overlayButtons = [{ col: 0, row: 0, col_span: 2 }, { col: 3, row: 1, icon }];
    context.padRenderGrid();
    const retainedMain = testGrid.children.find(cell => cell.dataset.layer === 'main' && Number(cell.dataset.col) === 3);
    const retainedOverlay = testGrid.children.find(cell => cell.dataset.layer === 'overlay' && Number(cell.dataset.col) === 3);
    context.padDialogClear();
    assert(testGrid.children.includes(retainedMain), 'Overlay clear must retain main-layer previews');
    assert(testGrid.children.includes(retainedOverlay), 'Overlay clear must retain unrelated overlay buttons');
    for (const col of [0, 1]) {
        assert(testGrid.children.some(cell => cell.classes.has('pad-cell-empty') &&
            cell.dataset.layer === 'overlay' && Number(cell.dataset.col) === col && Number(cell.dataset.row) === 0),
        'Overlay clear must free its full span');
    }
    delete context.padWorkspaceClearButton;
    delete context.padWorkspaceReturn;
    context.padState = savedState;
    context.document.getElementById = savedGetElementById;
    delete context.document.addEventListener;
    delete context.document.removeEventListener;
    delete context.setTimeout;
    context.padBuildOccupancySet = savedOccupancy;
    context.padGetGridGeometry = savedGeometry;
    delete context.padMarkDirty;
    delete context.requestAnimationFrame;
}
assert(!css.includes('data:font/woff2;base64,'));
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
    const button = { widget_type, label_top: 'Top', label_center: 'Center', label_bottom: 'Bottom',
        icon: { catalog_id: 'noto:thermometer', kind: 'color' } };
    const original = JSON.stringify(button);
    const cell = createElement();
    context.padRenderCellContent(cell, button);
    const marker = cell.querySelector('.pad-content-widget-region');
    assert(marker.attributes['aria-label']);
    assert.strictEqual(cell.querySelector('.pad-cell-group-labels').children.map(visibleText).join('|'), 'Top|Center|Bottom');
    assert(cell.querySelector('.pad-cell-icon'));
    assert.strictEqual(JSON.stringify(button), original, 'Preview rendering leaves widget configuration unchanged');
}
const ordinaryToggle = { col: 0, row: 0, widget_type: 'toggle', label_center: 'Ordinary',
    widget_toggle_on_color: '#4caf50', widget_toggle_off_color: '#59636e', widget_toggle_knob_color: '#ffffff', widget_toggle_width_pct: 75 };
const templateToggle = { col: 1, row: 0, widget_type: 'toggle', label_center: 'Template', col_span: 2,
    widget_toggle_on_color: '#2196f3', widget_toggle_off_color: '#59636e', widget_toggle_knob_color: '#ffffff', widget_toggle_width_pct: 70 };
const ordinaryToggleBefore = JSON.stringify(ordinaryToggle);
const templateToggleBefore = JSON.stringify(templateToggle);
context.padState = { cols: 3, rows: 1, selectedLayer: 'main', mainButtons: [ordinaryToggle], overlayButtons: [],
    get buttons() { return this.selectedLayer === 'overlay' ? this.overlayButtons : this.mainButtons; },
    set buttons(value) { if (this.selectedLayer === 'overlay') this.overlayButtons = value; else this.mainButtons = value; },
    templateButtons: [templateToggle] };
gridElement.children = [];
context.padRenderGrid();
assert.strictEqual(gridElement.children.length, 2);
assert(gridElement.children[0].querySelector('.pad-content-widget-region'));
assert(gridElement.children[1].querySelector('.pad-content-widget-region'));
assert(gridElement.children[1].classes.has('pad-cell-ghost'));
assert.strictEqual(gridElement.children[1].style.gridColumn, '2 / span 2');
assert.strictEqual(JSON.stringify(ordinaryToggle), ordinaryToggleBefore);
assert.strictEqual(JSON.stringify(templateToggle), templateToggleBefore);
const overlayToggle = { col: 0, row: 0, widget_type: 'toggle', label_center: 'Overlay' };
context.padState.overlayButtons = [overlayToggle];
context.padState.selectedLayer = 'overlay';
gridElement.children = [];
context.padRenderGrid();
assert.strictEqual(gridElement.children[0].dataset.layer, 'main');
assert(gridElement.children[0].classes.has('pad-cell-main-preview'));
const overlayCell = gridElement.children.find(cell => cell.classes.has('pad-cell-overlay'));
assert.strictEqual(overlayCell.dataset.layer, 'overlay');
assert(gridElement.children.some(cell => cell.classes.has('pad-cell-empty') && cell.dataset.layer === 'overlay'));
context.padState.selectedLayer = 'main';
gridElement.children = [];
context.padRenderGrid();
assert(!gridElement.children.some(cell => cell.dataset.layer === 'overlay'));
const barBuilder = vm.runInContext('padButtonBuilders.find(builder => builder.id === "bar")', context);
const builderElements = Object.fromEntries(['fields', 'states', 'note', 'sources', 'selection', 'name', 'id', 'state'].map(name => ['[data-bb-' + name + ']', createElement()]));
const makeDraft = (detail, name) => {
    const candidate = { providerId: 'ha', sourceId: detail.entity_id || 'sensor.preview',
        displayName: name, reference: { entity_id: detail.entity_id || 'sensor.preview' },
        sample: detail.state, sampleTruncated: false, unit: detail.unit,
        formatModes: ['raw', 'number'], capabilities: { display: true, numericWidget: true, liveSampling: true } };
    return { sources: [{ candidate, name, options: {} }], options: {} };
};
context.testBuilderSession = {
    draft: makeDraft({ state: '42', unit: '%' }, 'Battery'), scenario: barBuilder,
    dialog: { querySelector: selector => builderElements[selector] }
};
context.padButtonBuilderSetScenario(context.testBuilderSession, barBuilder);
vm.runInContext('padButtonBuilderSession = testBuilderSession', context);
context.padColorPopoverClose = () => {};
const updateValidation = context.padButtonBuilderUpdateValidation;
let validationUpdates = 0;
context.padButtonBuilderUpdateValidation = () => validationUpdates++;
context.padButtonBuilderRenderFields();
const builderInputs = builderElements['[data-bb-fields]'].children.map(label => label.children[0]);
assert.strictEqual(builderInputs.find(input => input.name === 'min').step, 'any');
assert.strictEqual(builderInputs.find(input => input.name === 'decimals').step, '1');
const orientationInput = builderInputs.find(input => input.name === 'orientation');
orientationInput.value = 'horizontal';
orientationInput.onchange();
assert.strictEqual(context.testBuilderSession.draft.options.orientation, 'horizontal', 'Custom dropdowns dispatch change, not input');
assert.strictEqual(validationUpdates, 2);
const minimumInput = builderInputs.find(input => input.name === 'min');
minimumInput.value = '';
minimumInput.oninput();
assert.strictEqual(context.testBuilderSession.draft.options.min, '', 'Keep incomplete numeric input raw');
assert.throws(() => context.padButtonBuilderValidateFields(context.testBuilderSession), /minimum and maximum/);
minimumInput.value = '-0.5';
minimumInput.oninput();
assert.strictEqual(context.testBuilderSession.draft.options.min, '-0.5');
assert.doesNotThrow(() => context.padButtonBuilderValidateFields(context.testBuilderSession));
context.padButtonBuilderUpdateValidation = updateValidation;
vm.runInContext('padButtonBuilderSession = null', context);
for (const source of ['', 'on', 'off', '[expr:[ha:climate.bureau_boven_thermostaat;attributes.temperature]>20?"on":"off"]']) {
    const cell = createElement();
    context.padRenderCellContent(cell, { widget_type: 'toggle', widget_data_binding: source,
        label_top: 'Bureau', label_bottom: '[ha:climate.bureau_boven_thermostaat;attributes.temperature;%.1f \u00b0C]',
        widget_toggle_on_color: '#123456' });
    assert.strictEqual(cell.querySelector('.pad-content-widget-region').attributes['aria-label'], 'Toggle switch widget placeholder, not a live rendering');
    assert(!cell.children.some(child => child.attributes.role === 'switch'));
    assert(descendants(cell).some(child => child.textContent === 'Bureau'));
}
const renderPad = { cols: 3, rows: 2, column_widths_px: [0, 40, 0] };
const renderButton = { col: 1, row: 0, col_span: 2, row_span: 1, icon_scale_pct: 75,
    label_top: 'Name', border_width: '2', content_pad: '4', widget_type: 'gauge' };
const originalKey = context.padIconRenderKey(renderPad, renderButton);
// The same vector is checked by the firmware validator in test_pad_confirmation.cpp.
assert.strictEqual(originalKey, 'ac65574e');
assert.notStrictEqual(originalKey, '4c31e060', 'Icons rendered before centering must regenerate');
assert.strictEqual(context.padIconRenderKey(renderPad, { ...renderButton, label_top: 'Other text', fg_color: 'ff0000' }), originalKey);
for (const change of [{ icon_scale_pct: 80 }, { col_span: 1 }, { label_top: '' }])
    assert.notStrictEqual(context.padIconRenderKey(renderPad, { ...renderButton, ...change }), originalKey);
assert.notStrictEqual(context.padIconRenderKey({ ...renderPad, column_widths_px: [0, 50, 0] }, renderButton), originalKey);
assert.notStrictEqual(context.padIconRenderKey({ ...renderPad, cols: 4 }, renderButton), originalKey);

const largePad = { cols: 10, rows: 10, column_widths_px: [0, 0, 0, 0, 0, 0, 0, 0, 21, 0],
    row_heights_px: [0, 0, 0, 0, 0, 0, 0, 0, 0, 31] };
const largeKey = context.padIconRenderKey(largePad, { col: 9, row: 9 });
assert.strictEqual(largeKey, '027ad4aa');
assert.notStrictEqual(context.padIconRenderKey({ ...largePad,
    column_widths_px: [0, 0, 0, 0, 0, 0, 0, 0, 22, 0] }, { col: 9, row: 9 }), largeKey);
assert.deepStrictEqual(Array.from(context.padSelectTemplateButtons(
    { cols: 10, rows: 10, buttons: [{ col: 8, row: 8 }] },
    [{ col: 8, row: 8 }, { col: 9, row: 9 }])), [{ col: 9, row: 9 }]);

{
    const size = 8;
    const pixels = new Uint8ClampedArray(size * size * 4);
    const offset = () => JSON.parse(JSON.stringify(context.padIconArtworkOffset(pixels, size)));
    assert.deepStrictEqual(offset(), { x: 0, y: 0 }, 'Fully transparent images remain unchanged');
    for (let y = 0; y < 3; y++) {
        for (let x = 3; x < 6; x++) pixels[(y * size + x) * 4 + 3] = 255;
    }
    pixels[(1 * size + 6) * 4 + 3] = 1;
    assert.deepStrictEqual(offset(), { x: -1, y: 3 }, 'Visible bounds, including translucent edges, are centered');
}

async function checkPickerResults() {
    vm.runInContext(fs.readFileSync('src/app/web/portal_icon_picker.js', 'utf8'), context);
    const nodes = new Map();
    const pickerNode = selector => {
        if (!nodes.has(selector)) nodes.set(selector, Object.assign(createElement(), { value: 'all' }));
        return nodes.get(selector);
    };
    const resultGrid = pickerNode('[data-icon-grid]');
    resultGrid.querySelectorAll = () => descendants(resultGrid).filter(node => node.dataset && node.dataset.url);
    pickerNode('[data-icon-search]').value = '';
    const liveUrls = new Set();
    let nextUrl = 0;
    let renderCount = 0;
    context.URL = {
        createObjectURL() { const url = 'blob:' + (++nextUrl); liveUrls.add(url); return url; },
        revokeObjectURL(url) { assert(liveUrls.delete(url)); },
    };
    context.clearTimeout = clearTimeout;
    context.AbortController = AbortController;
    context.padIconLoadArtwork = async icons => icons;
    context.padRenderIconPNG = async (record, size, foreground) => {
        assert.strictEqual(foreground, '#fedcba');
        renderCount++;
        return {};
    };
    const session = {
        dialog: { querySelector: pickerNode }, urls: new Set(), searchGeneration: 1,
        colors: { background: '#123456', foreground: '#fedcba' },
        activeSource: 'all', collections: [],
        searchController: new AbortController(), matching: [{ id: 'noto:house', name: 'House' }], visibleCount: 1,
    };
    context.testPickerSession = session;
    vm.runInContext('padIconPickerSession = testPickerSession;', context);
    await context.padIconPickerDrawResults(session, 1);
    await context.padIconPickerDrawResults(session, 1);
    assert.strictEqual(renderCount, 1);
    assert.strictEqual(liveUrls.size, 1);
    session.matching.push({ id: 'noto:sun', name: 'Sun' });
    session.visibleCount = 2;
    await context.padIconPickerDrawResults(session, 1);
    assert.strictEqual(renderCount, 2);
    assert.strictEqual(resultGrid.children.length, 2);
    assert.strictEqual(resultGrid.children[0].children[0].style.backgroundColor, '#123456');
    assert.strictEqual(liveUrls.size, 2);
    await context.padIconPickerRenderResults(session);
    assert.strictEqual(liveUrls.size, 0);
    assert.strictEqual(session.urls.size, 0);
    assert.strictEqual(resultGrid.children.length, 0);
}

checkPickerResults().then(() => console.log('portal_pad_preview: PASS')).catch(error => {
    console.error(error);
    process.exitCode = 1;
});