const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

function element() {
    return {
        style: {}, dataset: {}, children: [], attributes: {}, classes: new Set(),
        classList: { add(...names) { names.forEach(name => this.owner.classes.add(name)); } },
        appendChild(child) { this.children.push(child); },
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
    deviceInfoCache: { widget_catalog: [] },
    document: { createElement },
    padColorToHex: (color, fallback) => color || fallback,
    padGetEffectiveDefault: key => ({ bg_color: '#333333', fg_color: '#ffffff', border_color: '#000000', border_width: '0', corner_radius: '8', content_pad: '4' })[key]
};
vm.createContext(context);
vm.runInContext(fs.readFileSync('src/app/web/portal_pad_icons.js', 'utf8'), context);

const fields = ['name', 'icon', 'second_icon', 'axis_field', 'horizontal_icon', 'vertical_icon', 'default_axis'];
for (const file of fs.readdirSync('src/app/widgets').filter(name => name.endsWith('_widget.cpp'))) {
    const source = fs.readFileSync('src/app/widgets/' + file, 'utf8');
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
console.log('portal_pad_preview: PASS');