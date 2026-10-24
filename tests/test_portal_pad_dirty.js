const assert = require('assert');
const fs = require('fs');
const vm = require('vm');
const { execFileSync } = require('child_process');

const context = {
	console,
	MAX_ACTIONS: 3,
	padIconTypeChanged() {},
	padIconEditorRefresh() {},
	padFillRefreshVisibility() {},
	padWorkspaceRefresh() {},
	actionEditorListSetLabels() {},
	document: {
		addEventListener() {},
		getElementById(id) { return elements.get(id) || null; }
	}
};
const elements = new Map([
	['pad-edit-camera-feed-section', { style: {} }],
	['pad-edit-image-section', { style: {} }],
	['pad-edit-background-image-section', { style: {} }],
	['pad-edit-bg-image-scale-group', { style: {} }],
	['pad-edit-widget-type', { value: '' }]
]);
vm.createContext(context);
vm.runInContext(fs.readFileSync('src/app/web/portal_widget_editor.js', 'utf8'), context);
const source = fs.readFileSync('src/app/web/portal_pad_editor.js', 'utf8');
vm.runInContext(source.replace('let padDirty = false;', 'var padDirty = false;'), context);

for (const [text, count, expected] of [
	[' 3:50, 5:80 ', 5, [0, 0, 50, 0, 80]],
	['', 3, [0, 0, 0]],
]) {
	const parsed = context.padTrackParse(text, count, 'rows');
	assert.strictEqual(parsed.status, 'valid');
	assert.deepStrictEqual(Array.from(parsed.sizes), expected);
}
for (const [text, count, status] of [
	['3:', 5, 'incomplete'], ['3:50, 5:', 5, 'incomplete'],
	['3:50, 3:60', 5, 'invalid'], ['6:50', 5, 'invalid'],
	['1:50, 2:60, 3:70', 3, 'invalid'], ['2:0', 3, 'invalid'],
	['2:1.5', 3, 'invalid'],
]) {
	assert.strictEqual(context.padTrackParse(text, count, 'rows').status, status, text);
}

const trackElements = new Map();
const trackContext = {
	document: { activeElement: null, getElementById(id) { return trackElements.get(id) || null; } },
	deviceInfoCache: null,
	padGetEffectiveDefault() { return 0; },
	padRenderGrid() {},
	padPvHide() {},
};
for (const [id, value] of [['pad-row-heights', '1:50, 2:'], ['pad-column-widths', '2:20']]) {
	trackElements.set(id, { id, value, classList: { toggle() {} }, setAttribute() {}, focus() {} });
	trackElements.set(id + '-error', { textContent: '', hidden: true });
}
vm.createContext(trackContext);
vm.runInContext(source.replace('let padDirty = false;', 'var padDirty = false;'), trackContext);
vm.runInContext('padState.rows = 2; padState.cols = 3; padState.rowHeightsPx = [50, 0]; padState.columnWidthsPx = [0, 20, 0]; padTrackGridCountChanged("rows", 2, 3);', trackContext);
assert.deepStrictEqual(Array.from(vm.runInContext('padState.rowHeightsPx', trackContext)), [50, 0, 0], 'A count change must preserve existing sizes and add Auto tracks while text is incomplete');
assert.strictEqual(trackElements.get('pad-row-heights').value, '1:50, 2:', 'A count change must retain partial text');

assert.strictEqual(context.padDirty, false);
context.padMarkDirty({ isTrusted: false });
assert.strictEqual(context.padDirty, false);

context.padMarkDirty();
assert.strictEqual(context.padDirty, true);

context.padDirty = false;
context.padMarkDirty({ isTrusted: true });
assert.strictEqual(context.padDirty, true);

assert(source.includes('} finally {\n        // Loading action lists, bindings, and template buttons can update'));
assert(source.includes('if (current()) {'));
assert(source.includes('            padClearDirty();\n        }\n    }\n}\n\nfunction padCloneJson'));

const loadingName = { value: 'Previous pad' };
const loadingNames = [];
const loadingContext = {
	console,
	deviceInfoCache: { available_screens: [{ id: 'pad_1', name: 'Destination pad' }] },
	padWorkspace: { loading: false },
	padWorkspaceReset() {},
	padPruneDraftIconSources() {},
	document: { getElementById(id) { return id === 'pad-name' ? loadingName : { value: '' }; } },
	fetch() { return new Promise(() => {}); }
};
vm.createContext(loadingContext);
vm.runInContext(source, loadingContext);
loadingContext.padClearDirty = () => loadingNames.push(loadingName.value);
loadingContext.padLoadPage(1);
assert.strictEqual(loadingName.value, 'Destination pad');
assert.deepStrictEqual(loadingNames, ['Destination pad'], 'Load-time refresh must never use the previous pad name');
loadingContext.padLoadPage(2);
assert.strictEqual(loadingName.value, '', 'A destination without a catalog name must not inherit the previous name');

const target = {
	value: 'pad_4', options: [{ value: '' }, { value: 'pad_4' }], attributes: {},
	getAttribute(name) { return this.attributes[name]; },
	setAttribute(name, value) { this.attributes[name] = value; },
	removeAttribute(name) { delete this.attributes[name]; },
	remove(index) { this.options.splice(index, 1); this.value = ''; },
	appendChild(option) { this.options.push(option); }
};
elements.set('pad-level-action-0-target', target);
context.deviceInfoCache = { available_screens: [{ id: 'pad_4', name: 'Pad 5' }] };
context.document.createElement = () => ({ value: '', textContent: '' });
context._actionEditorExtensions = [];
vm.runInContext(fs.readFileSync('src/app/web/portal_action_editor_screen.js', 'utf8'), context);
context.padPopulateScreenDropdown();
assert.strictEqual(target.value, 'pad_4', 'Selecting a button must preserve pad-level screen targets');

for (const imageFetch of [false, true]) {
	for (const imageLibrary of [false, true]) {
		context.deviceInfoCache = { has_image_fetch: imageFetch, has_image_library: imageLibrary };
		context.padSetImageCapabilityVisibility(context.deviceInfoCache);
		context.padWidgetTypeChanged();
		assert.strictEqual(elements.get('pad-edit-camera-feed-section').style.display, imageFetch ? '' : 'none');
		assert.strictEqual(elements.get('pad-edit-image-section').style.display, imageLibrary ? '' : 'none');
		assert.strictEqual(elements.get('pad-edit-background-image-section').style.display,
			imageFetch || imageLibrary ? '' : 'none');
		assert.strictEqual(elements.get('pad-edit-bg-image-scale-group').style.display,
			imageFetch || imageLibrary ? '' : 'none');
	}
}
context.deviceInfoCache = { has_image_fetch: true, has_image_library: true };
elements.set('pad-edit-external-widget-option', {});
elements.set('pad-edit-extension-section', { tagName: 'DETAILS', dataset: {}, style: {}, querySelectorAll: () => [] });
elements.set('pad-edit-extension-id', { value: '', replaceChildren() {}, appendChild() {} });
vm.runInContext(fs.readFileSync('src/app/web/portal_widget_extension.js', 'utf8'), context);
elements.get('pad-edit-widget-type').value = 'external';
context.padWidgetTypeChanged();
assert.strictEqual(elements.get('pad-edit-camera-feed-section').style.display, 'none');
assert.strictEqual(elements.get('pad-edit-image-section').style.display, 'none');
assert.strictEqual(elements.get('pad-edit-bg-image-scale-group').style.display, 'none');

const mousepadOption = { hidden: true, disabled: true };
elements.set('pad-edit-mousepad-widget-option', mousepadOption);
const scrollpadOption = { hidden: true, disabled: true };
elements.set('pad-edit-scrollpad-widget-option', scrollpadOption);
elements.set('pad-edit-gamepad_stick-widget-option', {});
for (const type of ['mousepad', 'scrollpad', 'gamepad-stick']) {
	elements.set('pad-edit-' + type + '-section', { tagName: 'DETAILS', style: {}, dataset: {}, open: false, querySelectorAll: () => [] });
}
vm.runInContext(fs.readFileSync('src/app/web/portal_widget_inputs.js', 'utf8'), context);
for (const touch of [false, true]) {
	for (const usb of [false, true]) {
		context.padWidgetEditorsInit({ has_touch: touch, has_usb_hid: usb });
		assert.strictEqual(mousepadOption.hidden, !(touch && usb));
		assert.strictEqual(mousepadOption.disabled, !(touch && usb));
		assert.strictEqual(scrollpadOption.hidden, !(touch && usb));
		assert.strictEqual(scrollpadOption.disabled, !(touch && usb));
	}
}
context.padWidgetEditorsInit(null);
assert.strictEqual(mousepadOption.disabled, true);
assert.strictEqual(scrollpadOption.disabled, true);
context.padWidgetEditorsInit({ has_touch: true, has_usb_hid: true });
for (const id of ['pad-edit-tap-heading', 'pad-edit-tap-actions',
	'pad-edit-lp-heading', 'pad-edit-lp-actions']) elements.set(id, { tagName: 'DETAILS', style: {} });
for (const type of ['mousepad', 'scrollpad']) {
	elements.get('pad-edit-widget-type').value = type;
	context.padWidgetTypeChanged();
	assert.strictEqual(elements.get('pad-edit-' + type + '-section').style.display, '');
	assert.strictEqual(elements.get('pad-edit-' + type + '-section').open, false);
	for (const id of ['pad-edit-tap-heading', 'pad-edit-tap-actions', 'pad-edit-lp-heading', 'pad-edit-lp-actions']) {
		assert.strictEqual(elements.get(id).style.display, 'none');
	}
}
const tableSection = { tagName: 'DIV', style: {}, dataset: {} };
elements.set('pad-edit-table-section', tableSection);
vm.runInContext(fs.readFileSync('src/app/web/portal_widget_table.js', 'utf8'), context);
elements.get('pad-edit-widget-type').value = 'table';
context.padWidgetTypeChanged();
assert.strictEqual(tableSection.style.display, '');
assert.strictEqual(Object.prototype.hasOwnProperty.call(tableSection, 'open'), false);
elements.get('pad-edit-widget-type').value = '';
context.padWidgetTypeChanged();
assert.strictEqual(elements.get('pad-edit-mousepad-section').style.display, 'none');
assert.strictEqual(elements.get('pad-edit-scrollpad-section').style.display, 'none');
assert.strictEqual(elements.get('pad-edit-tap-actions').style.display, '');
assert.strictEqual(elements.get('pad-edit-lp-actions').style.display, '');

context.PAD_FIRMWARE_DEFAULTS = {
	widget_series_color: '#4CAF50', widget_series_color_2: '#2196F3',
	widget_series_color_3: '#9C27B0', widget_series_color_4: '#FF9800'
};
for (const name of ['sources', 'scale', 'layout', 'gridlines', 'target']) {
	elements.set('pad-edit-bar-' + name, { open: false });
}
elements.set('pad-edit-bar-chart-section', { style: {} });
elements.set('pad-edit-bar-gridline-style', { hidden: false });
elements.set('pad-edit-bar-target-style', { hidden: false });
elements.set('pad-edit-widget-bar-ticks', { value: '0' });
elements.set('pad-edit-widget-bar-marker-value', { value: '' });
for (let bar = 1; bar <= 4; bar++) {
	const suffix = bar === 1 ? '' : '-' + bar;
	elements.set('pad-edit-bar-source-' + bar, { open: true });
	elements.set(bar === 1 ? 'pad-edit-widget-data-binding' : 'pad-edit-widget-bar-data-binding' + suffix, { value: '' });
	elements.set('pad-edit-widget-bar-label' + suffix, { value: '' });
	elements.set('pad-edit-widget-bar-color' + suffix, {
		id: 'pad-edit-widget-bar-color' + suffix,
		value: context.PAD_FIRMWARE_DEFAULTS['widget_series_color' + (bar === 1 ? '' : '_' + bar)].toLowerCase()
	});
	elements.set('pad-edit-widget-bar-color' + suffix + '-pattern', { value: 'none' });
}
for (const pair of [1, 2]) elements.set('pad-edit-widget-bar-dual-binding-pair-' + pair, { checked: false });
elements.get('pad-edit-widget-type').value = 'bar_chart';
context.padRefreshWidgetEditor(true);
assert.deepStrictEqual([1, 2, 3, 4].map(bar => elements.get('pad-edit-bar-source-' + bar).open), [true, true, true, true]);
assert.strictEqual(elements.get('pad-edit-bar-gridline-style').hidden, false);
assert.strictEqual(elements.get('pad-edit-bar-target-style').hidden, false);
elements.get('pad-edit-widget-bar-label-2').value = 'Grid';
elements.get('pad-edit-widget-bar-color-3').value = '#123456';
elements.get('pad-edit-widget-bar-color-4-pattern').value = 'dots';
elements.get('pad-edit-widget-bar-ticks').value = '4';
elements.get('pad-edit-widget-bar-marker-value').value = '0';
context.padRefreshWidgetEditor(true);
assert.deepStrictEqual([1, 2, 3, 4].map(bar => elements.get('pad-edit-bar-source-' + bar).open), [true, true, true, true]);
assert.strictEqual(elements.get('pad-edit-bar-gridlines').open, false);
assert.strictEqual(elements.get('pad-edit-bar-target').open, false);
assert.strictEqual(elements.get('pad-edit-bar-target-style').hidden, false, 'Zero is a valid target');
elements.get('pad-edit-widget-bar-dual-binding-pair-1').checked = true;
elements.get('pad-edit-bar-source-2').open = false;
elements.get('pad-edit-widget-bar-ticks').value = '0';
elements.get('pad-edit-widget-bar-marker-value').value = '';
context.padRefreshWidgetEditor();
assert.strictEqual(elements.get('pad-edit-bar-source-2').open, false, 'Routine refresh preserves manual disclosure');
assert.strictEqual(elements.get('pad-edit-widget-bar-label-2').value, 'Grid');
assert.strictEqual(elements.get('pad-edit-widget-bar-color-3').value, '#123456');
assert.strictEqual(elements.get('pad-edit-widget-bar-color-4-pattern').value, 'dots');
assert.strictEqual(elements.get('pad-edit-bar-gridline-style').hidden, false);
assert.strictEqual(elements.get('pad-edit-bar-target-style').hidden, false);
context.padRefreshWidgetEditor(true);
assert.strictEqual(elements.get('pad-edit-bar-source-2').open, false, 'Pairing preserves manual disclosure');
elements.get('pad-edit-widget-type').value = '';
elements.get('pad-edit-bar-source-2').open = false;

for (const name of ['sources', 'scale', 'layout', 'needle', 'tick-marks', 'target']) {
	elements.set('pad-edit-gauge-' + name, { open: false });
}
elements.set('pad-edit-gauge-section', { style: {} });
for (const name of ['needle', 'tick', 'target']) elements.set('pad-edit-gauge-' + name + '-style', { hidden: false });
elements.set('pad-edit-gauge-show-needle', { checked: true });
elements.set('pad-edit-gauge-ticks', { value: '5' });
elements.set('pad-edit-gauge-marker-value', { value: '' });
for (let ring = 1; ring <= 4; ring++) {
	const suffix = ring === 1 ? '' : '-' + ring;
	elements.set('pad-edit-gauge-source-' + ring, { open: true });
	elements.set('pad-edit-gauge-data-binding' + suffix, { value: '' });
	elements.set('pad-edit-gauge-start-label' + suffix, { value: '' });
	elements.set('pad-edit-gauge-arc-color' + suffix, {
		value: context.PAD_FIRMWARE_DEFAULTS['widget_series_color' + (ring === 1 ? '' : '_' + ring)].toLowerCase()
	});
}
for (const pair of [1, 2]) elements.set('pad-edit-gauge-dual-binding-pair-' + pair, { checked: false });
elements.get('pad-edit-widget-type').value = 'gauge';
context.padRefreshWidgetEditor(true);
assert.deepStrictEqual([1, 2, 3, 4].map(ring => elements.get('pad-edit-gauge-source-' + ring).open), [true, true, true, true]);
assert.strictEqual(elements.get('pad-edit-gauge-needle').open, false);
assert.strictEqual(elements.get('pad-edit-gauge-tick-marks').open, false);
assert.strictEqual(elements.get('pad-edit-gauge-target').open, false);
assert.strictEqual(elements.get('pad-edit-gauge-target-style').hidden, false);
elements.get('pad-edit-gauge-data-binding-2').value = '[pad:power]';
elements.get('pad-edit-gauge-start-label-3').value = 'Grid';
elements.get('pad-edit-gauge-arc-color-4').value = '#123456';
elements.get('pad-edit-gauge-marker-value').value = '0';
context.padRefreshWidgetEditor(true);
assert.deepStrictEqual([1, 2, 3, 4].map(ring => elements.get('pad-edit-gauge-source-' + ring).open), [true, true, true, true]);
assert.strictEqual(elements.get('pad-edit-gauge-target').open, false, 'Configuring a target does not change manual disclosure');
elements.get('pad-edit-gauge-source-3').open = false;
elements.get('pad-edit-gauge-show-needle').checked = false;
elements.get('pad-edit-gauge-ticks').value = '0';
elements.get('pad-edit-gauge-marker-value').value = '';
elements.set('pad-edit-gauge-needle-width', { value: '7' });
elements.set('pad-edit-gauge-tick-width', { value: '3' });
elements.set('pad-edit-gauge-marker-zone-deg', { value: '19' });
context.padRefreshWidgetEditor();
assert.strictEqual(elements.get('pad-edit-gauge-source-3').open, false, 'Routine refresh preserves manual disclosure');
for (const name of ['needle', 'tick', 'target']) assert.strictEqual(elements.get('pad-edit-gauge-' + name + '-style').hidden, false);
assert.strictEqual(elements.get('pad-edit-gauge-needle-width').value, '7');
assert.strictEqual(elements.get('pad-edit-gauge-tick-width').value, '3');
assert.strictEqual(elements.get('pad-edit-gauge-marker-zone-deg').value, '19');
for (let ring = 2; ring <= 4; ring++) {
	elements.get('pad-edit-gauge-data-binding-' + ring).value = '';
	elements.get('pad-edit-gauge-start-label-' + ring).value = '';
	elements.get('pad-edit-gauge-arc-color-' + ring).value = context.PAD_FIRMWARE_DEFAULTS['widget_series_color_' + ring];
}
for (const pair of [1, 2]) elements.get('pad-edit-gauge-dual-binding-pair-' + pair).checked = true;
context.padRefreshWidgetEditor(true);
assert.deepStrictEqual([1, 2, 3, 4].map(ring => elements.get('pad-edit-gauge-source-' + ring).open), [true, true, false, true], 'Pairing does not reopen manually collapsed sources');
assert.strictEqual(elements.get('pad-edit-bar-source-2').open, false, 'Refreshing Gauge leaves the inactive Bar Chart editor unchanged');
const gaugeSection = elements.get('pad-edit-gauge-section');
elements.delete('pad-edit-gauge-section');
assert.doesNotThrow(() => context.padRefreshWidgetEditor(true), 'Missing widget markup is a no-op');
elements.set('pad-edit-gauge-section', gaugeSection);
const gaugeTicks = elements.get('pad-edit-gauge-ticks');
elements.delete('pad-edit-gauge-ticks');
assert.doesNotThrow(() => context.padRefreshWidgetEditor(true), 'Missing optional controls are skipped');
elements.set('pad-edit-gauge-ticks', gaugeTicks);
elements.set('pad-edit-sparkline-section', { style: {} });
for (const name of ['minimum', 'maximum']) {
	elements.set('pad-edit-sparkline-' + name + '-marker', { open: true });
	elements.set('pad-edit-sparkline-' + name + '-marker-style', { hidden: false });
}
elements.set('pad-edit-sparkline-marker-size-min', { value: '0' });
elements.set('pad-edit-sparkline-marker-size-max', { value: '5' });
elements.set('pad-edit-sparkline-min-fmt', { value: 'lo %.1f' });
elements.set('pad-edit-sparkline-min-label-color', { value: '[pad:accent]' });
for (let line = 1; line <= 3; line++) {
	const suffix = line === 1 ? '' : '-' + line;
	elements.set('pad-edit-sparkline-source-' + line, { open: true });
	elements.set('pad-edit-sparkline-data-binding' + suffix, { value: '' });
	elements.set('pad-edit-sparkline-current-label' + suffix, { value: '' });
	elements.set('pad-edit-sparkline-line-color' + suffix, {
		value: context.PAD_FIRMWARE_DEFAULTS['widget_series_color' + (line === 1 ? '' : '_' + line)]
	});
}
elements.set('pad-edit-sparkline-source-4', { open: true });
elements.get('pad-edit-widget-type').value = 'sparkline';
context.padRefreshWidgetEditor(true);
assert.deepStrictEqual([1, 2, 3].map(line => elements.get('pad-edit-sparkline-source-' + line).open), [true, true, true]);
assert.strictEqual(elements.get('pad-edit-sparkline-minimum-marker').open, true);
assert.strictEqual(elements.get('pad-edit-sparkline-minimum-marker-style').hidden, false);
assert.strictEqual(elements.get('pad-edit-sparkline-maximum-marker').open, true);
elements.get('pad-edit-sparkline-data-binding-2').value = '[pad:power]';
elements.get('pad-edit-sparkline-current-label-3').value = 'Outdoor';
context.padRefreshWidgetEditor(true);
assert.deepStrictEqual([1, 2, 3].map(line => elements.get('pad-edit-sparkline-source-' + line).open), [true, true, true]);
elements.get('pad-edit-sparkline-current-label-3').value = '';
elements.get('pad-edit-sparkline-line-color-3').value = '#123456';
context.padRefreshWidgetEditor(true);
assert.strictEqual(elements.get('pad-edit-sparkline-source-3').open, true, 'Custom colors preserve manual disclosure');
assert.strictEqual(elements.get('pad-edit-sparkline-source-4').open, true, 'Sparkline refresh must stop at three sources');
elements.get('pad-edit-sparkline-source-2').open = false;
elements.get('pad-edit-sparkline-marker-size-max').value = '0';
context.padRefreshWidgetEditor();
assert.strictEqual(elements.get('pad-edit-sparkline-source-2').open, false, 'Routine refresh preserves Sparkline disclosure');
assert.strictEqual(elements.get('pad-edit-sparkline-maximum-marker-style').hidden, false);
assert.strictEqual(elements.get('pad-edit-sparkline-min-fmt').value, 'lo %.1f');
assert.strictEqual(elements.get('pad-edit-sparkline-min-label-color').value, '[pad:accent]');
assert.strictEqual(elements.get('pad-edit-bar-source-2').open, false, 'Sparkline refresh leaves the inactive Bar Chart editor unchanged');
assert.strictEqual(elements.get('pad-edit-gauge-source-4').open, true, 'Sparkline refresh leaves the inactive Gauge editor unchanged');
elements.get('pad-edit-widget-type').value = 'camera_preview';
assert.doesNotThrow(() => context.padRefreshWidgetEditor(true), 'Widgets without refresh rules are a no-op');
elements.get('pad-edit-widget-type').value = '';
assert.doesNotThrow(() => context.padRefreshWidgetEditor(true), 'A normal button needs no widget refresh');

const devInfo = JSON.parse(execFileSync('python3', ['-c',
	"import json, runpy, sys; from types import SimpleNamespace; sys.path.insert(0, 'tools'); module = runpy.run_path('tools/portal-dev-server.py'); handler = object.__new__(module['PortalHandler']); handler.headers = {}; handler.server = SimpleNamespace(profile='esp32-p4-lcd4b'); module['reset_pad_fixtures'](handler.server); print(json.dumps(handler._device_info()))"
], { encoding: 'utf8' }));
for (const type of ['mousepad', 'scrollpad', 'gamepad_stick']) {
	assert.strictEqual(context.padWidgetEditor(type).available(devInfo), true);
}
for (const type of ['mousepad', 'scrollpad']) {
	const option = elements.get('pad-edit-' + type + '-widget-option');
	assert.strictEqual(option.hidden, false);
	assert.strictEqual(option.disabled, false);
}

const draftInput = { id: 'pad-edit-label-center', value: 'Original', checked: false };
const draftGroup = { open: false };
const draftRoot = {
	isConnected: true,
	querySelectorAll(selector) { return selector.includes('input') ? [draftInput] : selector === '#pad-edit-overlay details' ? [draftGroup] : []; },
	querySelector() { return { scrollTop: 42 }; },
	classList: { add() {}, remove() {}, toggle() {} }
};
const workspaceContext = {
	console, Map, Set, deviceInfoCache: { max_pad_buttons: 64 },
	padCloneJson(value) { return JSON.parse(JSON.stringify(value)); },
	padMaxButtons() { return 64; },
	padState: {
		page: 0, cols: 2, rows: 2, editCol: 0, editRow: 0, selectedLayer: 'main',
		layerSelections: { main: null, overlay: null },
		mainButtons: [{ col: 0, row: 0, label_center: 'Original' }], overlayButtons: [],
		get buttons() { return this.selectedLayer === 'overlay' ? this.overlayButtons : this.mainButtons; },
		set buttons(value) { if (this.selectedLayer === 'overlay') this.overlayButtons = value; else this.mainButtons = value; }
	},
	padDirty: false,
	document: {
		getElementById() { return { value: '0', replaceChildren() {}, appendChild() {}, querySelectorAll() { return []; } }; },
		createElement() { return { setAttribute() {}, addEventListener() {} }; }
	},
	padMarkDirty() { workspaceContext.padDirty = true; workspaceContext.revisions++; },
	revisions: 0,
	padRenderGrid() {},
	padUpdateButtonPreview() {},
	padPvHide() {},
	padDialogBuildButton() { return { col: 0, row: 0, label_center: draftInput.value }; },
	padDialogValidateButton() { if (draftInput.value === '[unfinished') throw new Error('Incomplete binding'); },
	padDialogShowValidationError(message) { workspaceContext.error = message.message || message; },
	padDialogClearValidationError() { workspaceContext.error = ''; }
};
vm.createContext(workspaceContext);
vm.runInContext(fs.readFileSync('src/app/web/portal_pad_workspace.js', 'utf8'), workspaceContext);
workspaceContext.root = draftRoot;
vm.runInContext("padWorkspace = { root, selected: true, forms: new Map(), errors: new Map(), loading: false };", workspaceContext);
workspaceContext.padWorkspaceRefresh = () => {};
workspaceContext.padWorkspaceSummaries = () => {};
const setWorkspaceScope = workspaceContext.padWorkspaceSetScope;
workspaceContext.padWorkspaceSetScope = () => {};
workspaceContext.padWorkspaceCapture();
draftInput.value = '[unfinished';
workspaceContext.padWorkspaceEdit();
assert.strictEqual(workspaceContext.padState.buttons[0].label_center, 'Original');
assert.strictEqual(workspaceContext.padDirty, true);
assert.strictEqual(workspaceContext.error, 'Incomplete binding');
assert.strictEqual(vm.runInContext("padWorkspace.forms.get('main:0,0').values[0].value", workspaceContext), '[unfinished');
workspaceContext.padDialogOpen = () => {};
workspaceContext.padDialogFocusValidationError = () => {};
assert.throws(() => workspaceContext.padWorkspaceValidate(), /Incomplete binding/);
const revisions = workspaceContext.revisions;
workspaceContext.padWorkspaceEdit();
assert.strictEqual(workspaceContext.revisions, revisions, 'Trailing change must not rebuild an unchanged form');
draftInput.value = 'Corrected';
workspaceContext.padWorkspaceEdit();
assert.strictEqual(workspaceContext.padState.buttons[0].label_center, 'Corrected');
workspaceContext.padWorkspaceValidate();
assert.strictEqual(vm.runInContext('padWorkspace.errors.size', workspaceContext), 0);
let catalogReady = false;
const validateDraft = workspaceContext.padDialogValidateButton;
workspaceContext.padDialogValidateButton = () => {
	if (!catalogReady) throw new Error('Extension catalog is still loading');
};
workspaceContext.padWorkspaceEdit(true);
assert.match(workspaceContext.error, /still loading/);
const catalogRevision = workspaceContext.revisions;
const draftLookup = workspaceContext.document.getElementById;
workspaceContext.document.getElementById = id => id === draftInput.id ? draftInput : { value: '0' };
workspaceContext.padActionPrefixes = () => [];
workspaceContext.padWidgetActionPrefixes = () => [];
workspaceContext.actionEditorTypeChanged = () => {};
workspaceContext.padWidgetTypeChanged = () => {};
workspaceContext.padIconTypeChanged = () => {};
workspaceContext.padIconEditorRefresh = () => {};
workspaceContext.padConfirmChanged = () => {};
workspaceContext.padDialogOpen = () => {
	draftGroup.open = true;
	workspaceContext.padWorkspaceAfterSelect();
};
catalogReady = true;
assert.doesNotThrow(() => workspaceContext.padWorkspaceValidate(), 'Reopened drafts revalidate after async dependencies settle');
assert.strictEqual(draftGroup.open, false, 'Manual draft collapse overrides initial configured disclosure on reopening');
assert.strictEqual(vm.runInContext('padWorkspace.errors.size', workspaceContext), 0);
assert.strictEqual(draftInput.value, 'Corrected');
assert.strictEqual(workspaceContext.revisions, catalogRevision, 'Catalog settlement does not mark a new edit');
catalogReady = false;
workspaceContext.padWorkspaceEdit(true);
catalogReady = true;
workspaceContext.padWorkspaceWidgetSettled('unrelated');
assert.strictEqual(vm.runInContext('padWorkspace.errors.size', workspaceContext), 1, 'Unrelated widgets do not revalidate this draft');
workspaceContext.padWorkspaceWidgetSettled('0');
assert.strictEqual(vm.runInContext('padWorkspace.errors.size', workspaceContext), 0, 'The active widget settlement revalidates the draft');
assert.strictEqual(workspaceContext.revisions, catalogRevision, 'Host revalidation must not create a dirty revision');
workspaceContext.padDialogValidateButton = validateDraft;
workspaceContext.padDialogOpen = () => {};
workspaceContext.document.getElementById = draftLookup;
workspaceContext.padWorkspaceReset();
assert.strictEqual(vm.runInContext('padWorkspace.forms.size', workspaceContext), 0);
assert.strictEqual(vm.runInContext('padWorkspace.selected', workspaceContext), false);
vm.runInContext("padWorkspace.selected = true; padWorkspace.errors.set('main:0,0', 'Incomplete binding');", workspaceContext);
draftInput.value = '[unfinished';
workspaceContext.padWorkspaceRenderTabs = () => {};
setWorkspaceScope('pad');
assert.strictEqual(vm.runInContext('padWorkspace.selected', workspaceContext), false);
assert.strictEqual(vm.runInContext("padWorkspace.forms.get('main:0,0').values[0].value", workspaceContext), '[unfinished');
assert.strictEqual(vm.runInContext("padWorkspace.errors.get('main:0,0')", workspaceContext), 'Incomplete binding');
workspaceContext.padWorkspaceSetScope = setWorkspaceScope;
vm.runInContext('padWorkspace.selected = true;', workspaceContext);
workspaceContext.padWorkspaceSelect(0, 0);
assert.strictEqual(vm.runInContext('padWorkspace.selected', workspaceContext), false, 'Clicking the selected button deselects it');
assert.strictEqual(vm.runInContext("padWorkspace.forms.get('main:0,0').values[0].value", workspaceContext), '[unfinished');
assert.strictEqual(vm.runInContext("padWorkspace.errors.get('main:0,0')", workspaceContext), 'Incomplete binding');
let openedPosition = null;
workspaceContext.padDialogOpen = (col, row) => { openedPosition = [col, row]; };
workspaceContext.padWorkspaceSelect(0, 0);
assert.deepStrictEqual(openedPosition, [0, 0], 'Clicking an unselected button opens its editor');

workspaceContext.padWorkspaceSetScope = () => {};
workspaceContext.padDialogOpen = () => {};
workspaceContext.padState.layerSelections = { main: { col: 0, row: 0 }, overlay: { col: 0, row: 0 } };
vm.runInContext('padWorkspace.selected = true;', workspaceContext);
draftInput.value = 'Main same-coordinate draft';
workspaceContext.padWorkspaceCapture();
workspaceContext.padWorkspaceSetLayer('overlay');
vm.runInContext('padWorkspace.selected = true;', workspaceContext);
draftInput.value = 'Overlay same-coordinate draft';
workspaceContext.padWorkspaceCapture();
vm.runInContext("padWorkspace.errors.set('main:0,0', 'Main error'); padWorkspace.errors.set('overlay:0,0', 'Overlay error');", workspaceContext);
workspaceContext.padWorkspaceSetLayer('main');
assert.strictEqual(vm.runInContext("padWorkspace.forms.get('main:0,0').values[0].value", workspaceContext), 'Main same-coordinate draft');
assert.strictEqual(vm.runInContext("padWorkspace.forms.get('overlay:0,0').values[0].value", workspaceContext), 'Overlay same-coordinate draft');
assert.strictEqual(vm.runInContext("padWorkspace.errors.get('main:0,0')", workspaceContext), 'Main error');
assert.strictEqual(vm.runInContext("padWorkspace.errors.get('overlay:0,0')", workspaceContext), 'Overlay error');

const sharedControls = ['checkbox', 'text', 'button'].map((type, index) => ({
	type, disabled: index === 2, readOnly: false, dataset: {},
	hasAttribute(name) { return name === 'data-main-disabled' && this.dataset.mainDisabled !== undefined; }
}));
workspaceContext.document.getElementById = id => id === 'pad-workspace-pad-fields'
	? { querySelectorAll() { return sharedControls; } } : null;
workspaceContext.padState.selectedLayer = 'overlay';
workspaceContext.padWorkspaceRenderLayer();
workspaceContext.padWorkspaceRenderLayer();
assert(sharedControls.every(control => control.disabled), 'Overlay must lock every shared control, including checkboxes');
workspaceContext.padState.selectedLayer = 'main';
workspaceContext.padWorkspaceRenderLayer();
assert.deepStrictEqual(sharedControls.map(control => control.disabled), [false, false, true], 'Main must restore the original disabled states');
workspaceContext.document.getElementById = draftLookup;

vm.runInContext(fs.readFileSync('src/app/web/portal_pad_io.js', 'utf8'), workspaceContext);
workspaceContext.confirm = () => true;
workspaceContext.showMessage = () => {};
workspaceContext.padState.btnClipboard = { label_center: 'Copied' };
workspaceContext.padState.selectedLayer = 'overlay';
workspaceContext.padState.layerSelections = { main: { col: 0, row: 0 }, overlay: { col: 0, row: 0 } };
workspaceContext.padFillWithClipboard();
assert.strictEqual(vm.runInContext("padWorkspace.forms.get('main:0,0').values[0].value", workspaceContext), 'Main same-coordinate draft', 'Filling Overlay must preserve Main drafts');
assert.strictEqual(vm.runInContext("padWorkspace.errors.get('main:0,0')", workspaceContext), 'Main error', 'Filling Overlay must preserve Main validation errors');
assert.strictEqual(vm.runInContext("padWorkspace.forms.has('overlay:0,0') || padWorkspace.errors.has('overlay:0,0')", workspaceContext), false);
assert.deepStrictEqual(workspaceContext.padState.layerSelections.main, { col: 0, row: 0 });
assert.strictEqual(workspaceContext.padState.overlayButtons.length, 4);
workspaceContext.padState.selectedLayer = 'main';

const refreshElements = new Map();
const refreshContext = {
	console, padState: { page: 0, cols: 4, rows: 3, selectedLayer: 'main', mainButtons: [], overlayButtons: [] }, padDirty: false, padSaveInProgress: false,
	padMaxButtons() { return 64; },
	deviceInfoCache: { max_pads: 2, available_screens: [{ id: 'pad_0', name: 'Pad 1: Solar' }, { id: 'pad_1', name: 'Pad 2: Lights' }] },
	document: {
		getElementById(id) {
			assert.notStrictEqual(id, 'pad-page-select', 'Refresh must not depend on the removed pad dropdown');
			if (!refreshElements.has(id)) refreshElements.set(id, { value: 'Solar', textContent: '', children: [], replaceChildren() { this.children = []; }, appendChild(child) { this.children.push(child); }, querySelectorAll() { return []; } });
			return refreshElements.get(id);
		},
		createElement() { return { dataset: {}, setAttribute() {} }; },
		querySelectorAll() { return []; }
	}
};
vm.createContext(refreshContext);
vm.runInContext(fs.readFileSync('src/app/web/portal_pad_workspace.js', 'utf8'), refreshContext);
vm.runInContext('padWorkspace = { root: { isConnected: true, classList: { toggle() {} }, querySelectorAll() { return []; } }, loading: false, railSignature: "[0,[]]" };', refreshContext);
const fitWorkspace = refreshContext.padWorkspaceFit;
refreshContext.padWorkspaceFit = () => {};
for (const [dirty, saving, status] of [[false, false, ''], [true, false, 'Unsaved edits'], [true, true, 'Saving...'], [false, false, '']]) {
	refreshContext.padDirty = dirty;
	refreshContext.padSaveInProgress = saving;
	refreshContext.padWorkspaceRefresh();
	assert.strictEqual(refreshElements.get('pad-workspace-status').textContent, status);
	assert.strictEqual(refreshElements.get('pad-save-btn').disabled, saving);
}
assert.strictEqual(refreshElements.has('pad-workspace-position'), false, 'Refresh must not access the removed footer');
assert.strictEqual(refreshElements.has('pad-workspace-geometry'), false, 'Refresh must not access the removed dimensions row');
const railLabels = () => refreshElements.get('pad-workspace-pads').children.map(button => button.textContent);
assert.deepStrictEqual(railLabels(), ['Pad 1: Solar', 'Pad 2: Lights']);
refreshContext.padState.page = 1;
refreshElements.get('pad-name').value = 'Lights';
refreshContext.padWorkspaceRefresh();
assert.deepStrictEqual(railLabels(), ['Pad 1: Solar', 'Pad 2: Lights'], 'Selecting a pad must not change its label');
refreshContext.deviceInfoCache.available_screens[0].name = 'Solar';
refreshContext.padWorkspaceRefresh();
assert.deepStrictEqual(railLabels(), ['Pad 1: Solar', 'Pad 2: Lights'], 'Bare custom catalog names must receive a numbered prefix');
refreshElements.get('pad-name').value = 'Kitchen';
refreshContext.padWorkspaceRefresh();
assert.deepStrictEqual(railLabels(), ['Pad 1: Solar', 'Pad 2: Kitchen'], 'Unsaved pad names must retain their numbered prefix');
refreshElements.get('pad-name').value = '';
refreshContext.padWorkspaceRefresh();
assert.deepStrictEqual(railLabels(), ['Pad 1: Solar', 'Pad 2'], 'Unnamed pads must retain their numbered label');
const fragment = fs.readFileSync('src/app/web/pad-editor.fragment.html', 'utf8');
for (const id of ['pad-workspace-position', 'pad-workspace-geometry', 'pad-workspace-settings', 'pad-quick-save-btn', 'pad-empty-state', 'pad-page-select']) {
	assert(!fragment.includes('id="' + id + '"'), 'Removed canvas control must not remain in the fragment: ' + id);
}
assert(!fragment.includes('id="pad-settings-menu-btn"'), 'More must not duplicate the Pad scope control');
assert(fragment.includes('id="pad-workspace-pad-scope"'), 'Pad settings must remain accessible through the scope control');
assert(!fragment.includes('<summary>Actions</summary>'), 'The Actions tab must not contain a duplicate Actions accordion');
assert(!fragment.includes('<summary>Appearance</summary>'), 'The Appearance tab must expose its controls directly');
assert(!fragment.includes('<h5 class="mb-0">Pad Bindings</h5>'), 'The Bindings tab must not repeat its title');
assert(!fragment.includes('<label for="pad-edit-bg-image-path">'), 'Local Image must not repeat its section title');
assert(fragment.includes('aria-label="Refresh image library"'), 'The icon-only refresh button needs an accessible name');

const scrollCalls = [];
const outerScroll = { scrollTop: 0, clientHeight: 400, scrollHeight: 800, parentElement: null, scrollBy(options) { scrollCalls.push(['outer', options.top]); } };
const innerScroll = { scrollTop: 20, clientHeight: 200, scrollHeight: 400, parentElement: outerScroll, scrollBy(options) { scrollCalls.push(['inner', options.top]); } };
refreshContext.getComputedStyle = () => ({ overflowY: 'auto', overflowX: 'hidden' });
const numericInput = { type: 'number', value: '42', parentElement: innerScroll };
let wheelPrevented = false;
const wheelEvent = { target: numericInput, deltaY: 50, deltaX: 0, deltaMode: 0, preventDefault() { wheelPrevented = true; } };
refreshContext.padWorkspaceWheel(wheelEvent);
assert(wheelPrevented, 'Numeric wheel events must prevent native value stepping');
assert.strictEqual(numericInput.value, '42');
assert.deepStrictEqual(scrollCalls.pop(), ['inner', 50]);
innerScroll.scrollTop = 200;
refreshContext.padWorkspaceWheel(wheelEvent);
assert.deepStrictEqual(scrollCalls.pop(), ['outer', 50], 'Scrolling must continue in the parent at the inner boundary');
refreshContext.padWorkspaceWheel({ ...wheelEvent, deltaY: -2, deltaMode: 1 });
assert.deepStrictEqual(scrollCalls.pop(), ['inner', -32], 'Line-mode wheel input must scroll in the requested direction');
refreshContext.padWorkspaceWheel({ ...wheelEvent, deltaY: -1, deltaMode: 2 });
assert.deepStrictEqual(scrollCalls.pop(), ['inner', -200], 'Page-mode wheel input must use the scroll container height');
wheelPrevented = false;
refreshContext.padWorkspaceWheel({ ...wheelEvent, target: { type: 'text' } });
assert.strictEqual(wheelPrevented, false, 'Text inputs must retain native scrolling');
refreshContext.padWorkspaceWheel({ ...wheelEvent, ctrlKey: true });
assert.strictEqual(wheelPrevented, false, 'Browser zoom gestures must remain native');

const frameStyles = new Map();
const contentPane = { clientHeight: 700, scrollTop: 10, getBoundingClientRect() { return { top: 30 }; } };
const workspaceFrame = { getBoundingClientRect() { return { top: 70, left: 0 }; } };
refreshElements.set('content-pane', contentPane);
refreshElements.set('pad-workspace-inspector', { getBoundingClientRect() { return { left: 600 }; } });
refreshElements.set('pad-workspace-canvas', { clientWidth: 600, clientHeight: 600 });
refreshElements.set('pad-grid', { style: {} });
refreshContext.window = { innerWidth: 900 };
refreshContext.document.querySelector = () => null;
refreshContext.getComputedStyle = () => ({ paddingBottom: '24px' });
refreshContext.padGetEffectiveDefault = field => ({ pixel_shift_distance_px: 4, button_spacing_px: 6 })[field] ?? 0;
vm.runInContext('padWorkspace.root.querySelector = () => workspaceFrame;', Object.assign(refreshContext, { workspaceFrame }));
vm.runInContext('padWorkspace.root.style = frameStyle;', Object.assign(refreshContext, { frameStyle: { setProperty(name, value) { frameStyles.set(name, value); } } }));
fitWorkspace();
assert.strictEqual(frameStyles.get('--pad-inspector-left'), '600px', 'Error overlay must follow the inspector position');
assert.strictEqual(frameStyles.get('--pad-workspace-height'), '626px', 'Workspace must use available content height minus its offset and bottom padding');
contentPane.scrollTop = 30;
workspaceFrame.getBoundingClientRect = () => ({ top: 50, left: 0 });
fitWorkspace();
assert.strictEqual(frameStyles.get('--pad-workspace-height'), '626px', 'Scrolling must not alter workspace height');
contentPane.clientHeight = 800;
fitWorkspace();
assert.strictEqual(frameStyles.get('--pad-workspace-height'), '726px', 'Content height changes must update the workspace height');

async function checkWorkspaceSwitch() {
	workspaceContext.padWorkspaceConfirm = async () => false;
	workspaceContext.padLoadPage = async page => { workspaceContext.padState.page = page; };
	await workspaceContext.padWorkspaceSwitch(1);
	assert.strictEqual(workspaceContext.padState.page, 0);
	assert.strictEqual(workspaceContext.padState.buttons[0].label_center, 'Corrected');
	workspaceContext.padWorkspaceConfirm = async () => true;
	await workspaceContext.padWorkspaceSwitch(1);
	assert.strictEqual(workspaceContext.padState.page, 1);
}

async function checkSaveRaces() {
	const root = {};
	const groups = [{ open: true }, { open: false }];
	const body = { scrollTop: 420, querySelectorAll() { return groups; } };
	const workspace = { revision: 1, selected: true, scope: 'button', tab: 'Content', root: { querySelector() { return body; } } };
	const saveContext = { console, MAX_ACTIONS: 3, padWorkspace: workspace, document: { getElementById() { return root; } } };
	vm.createContext(saveContext);
	vm.runInContext(source.replace('let padDirty = false;', 'var padDirty = false;'), saveContext);
	vm.runInContext("padState.selectedLayer = 'overlay'; padState.editCol = 2; padState.editRow = 0;", saveContext);
	saveContext.padBuildSaveContext = () => ({ page: 0, name: 'Saved' });
	saveContext.padClearDirty = () => { saveContext.padDirty = false; };
	saveContext.showMessage = () => {};
	saveContext.getDeviceInfo = async () => {};
	let reloads = 0;
	let reloadedLayer = null;
	saveContext.padLoadPage = async (page, defaultsReady, layer) => {
		reloadedLayer = layer;
		reloads++;
		vm.runInContext('padState.editCol = 0; padState.editRow = 0;', saveContext);
		workspace.tab = 'Layout';
		body.scrollTop = 0;
		groups[0].open = false;
		groups[1].open = true;
	};
	saveContext.padDialogOpen = (col, row) => {
		saveContext.position = [col, row];
		vm.runInContext('padState.editCol = ' + col + '; padState.editRow = ' + row + ';', saveContext);
	};
	saveContext.padWorkspaceSetScope = scope => { workspace.scope = scope; };
	saveContext.padWorkspaceRenderTabs = () => {};
	saveContext.padQueuePersistence = async () => {
		vm.runInContext('padState.editCol = 1; padState.editRow = 0;', saveContext);
		workspace.tab = 'Actions';
		body.scrollTop = 640;
	};
	await saveContext.padSavePage();
	assert.deepStrictEqual(saveContext.position, [1, 0], 'Save reload must retain the latest selection');
	assert.strictEqual(reloadedLayer, 'overlay', 'Save reload must retain the selected layer');
	assert.strictEqual(workspace.tab, 'Actions');
	assert.strictEqual(body.scrollTop, 640, 'Save reload must restore the latest inspector scroll position');
	assert.deepStrictEqual(groups.map(group => group.open), [true, false], 'Save reload must restore disclosure states');
	assert.strictEqual(reloads, 1);
	saveContext.padQueuePersistence = async () => { workspace.revision++; saveContext.padDirty = true; };
	await saveContext.padSavePage();
	assert.strictEqual(saveContext.padDirty, true, 'Newer edits must remain dirty after an older save');
	assert.strictEqual(reloads, 1, 'An older save must not reload over newer edits');
	const notifications = [];
	const validationMessages = [];
	saveContext.showMessage = (message, type) => notifications.push({ message, type });
	saveContext.padDialogShowValidationError = error => validationMessages.push(error.message);
	saveContext.padBuildSaveContext = () => { throw new Error('Invalid button span'); };
	saveContext.padQueuePersistence = async () => { throw new Error('Validation must block persistence'); };
	assert.strictEqual(await saveContext.padSavePage(), null);
	assert.deepStrictEqual(validationMessages, ['Invalid button span']);
	assert.deepStrictEqual(notifications, [], 'Validation must use the overlay without a duplicate toast');
	await assert.rejects(saveContext.padSavePage({ bulk: true }), /Invalid button span/);
	saveContext.padBuildSaveContext = () => ({ page: 0 });
	saveContext.padQueuePersistence = async () => { throw new Error('Device unavailable'); };
	assert.strictEqual(await saveContext.padSavePage(), null);
	assert.deepStrictEqual(notifications, [{ message: 'Save failed: Device unavailable', type: 'error' }], 'Persistence failures must retain their toast');
}

checkWorkspaceSwitch().then(checkSaveRaces).then(() => console.log('portal_pad_dirty: PASS')).catch(error => { console.error(error); process.exitCode = 1; });
