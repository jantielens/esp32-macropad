const assert = require('assert');
const fs = require('fs');
const vm = require('vm');
const { execFileSync } = require('child_process');

const context = {
	console,
	MAX_ACTIONS: 3,
	padIconTypeChanged() {},
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
const source = fs.readFileSync('src/app/web/portal_pad_editor.js', 'utf8');
vm.runInContext(source.replace('let padDirty = false;', 'var padDirty = false;'), context);

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
elements.get('pad-edit-widget-type').value = 'external';
context.padWidgetTypeChanged();
assert.strictEqual(elements.get('pad-edit-camera-feed-section').style.display, 'none');
assert.strictEqual(elements.get('pad-edit-image-section').style.display, 'none');
assert.strictEqual(elements.get('pad-edit-bg-image-scale-group').style.display, 'none');

const mousepadOption = { hidden: true, disabled: true };
elements.set('pad-edit-mousepad-widget-option', mousepadOption);
const scrollpadOption = { hidden: true, disabled: true };
elements.set('pad-edit-scrollpad-widget-option', scrollpadOption);
for (const touch of [false, true]) {
	for (const usb of [false, true]) {
		context.padSetHidWidgetCapabilityVisibility({ has_touch: touch, has_usb_hid: usb });
		assert.strictEqual(mousepadOption.hidden, !(touch && usb));
		assert.strictEqual(mousepadOption.disabled, !(touch && usb));
		assert.strictEqual(scrollpadOption.hidden, !(touch && usb));
		assert.strictEqual(scrollpadOption.disabled, !(touch && usb));
	}
}
context.padSetHidWidgetCapabilityVisibility(null);
assert.strictEqual(mousepadOption.disabled, true);
assert.strictEqual(scrollpadOption.disabled, true);
for (const id of ['pad-edit-mousepad-section', 'pad-edit-scrollpad-section', 'pad-edit-tap-heading', 'pad-edit-tap-actions',
	'pad-edit-lp-heading', 'pad-edit-lp-actions']) elements.set(id, { tagName: 'DETAILS', style: {} });
for (const type of ['mousepad', 'scrollpad']) {
	elements.get('pad-edit-widget-type').value = type;
	context.padWidgetTypeChanged();
	assert.strictEqual(elements.get('pad-edit-' + type + '-section').style.display, '');
	assert.strictEqual(elements.get('pad-edit-' + type + '-section').open, true);
	for (const id of ['pad-edit-tap-heading', 'pad-edit-tap-actions', 'pad-edit-lp-heading', 'pad-edit-lp-actions']) {
		assert.strictEqual(elements.get(id).style.display, 'none');
	}
}
const tableSection = { tagName: 'DIV', style: {} };
elements.set('pad-edit-table-section', tableSection);
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
assert.deepStrictEqual([1, 2, 3, 4].map(bar => elements.get('pad-edit-bar-source-' + bar).open), [true, false, false, false]);
assert.strictEqual(elements.get('pad-edit-bar-gridline-style').hidden, true);
assert.strictEqual(elements.get('pad-edit-bar-target-style').hidden, true);
elements.get('pad-edit-widget-bar-label-2').value = 'Grid';
elements.get('pad-edit-widget-bar-color-3').value = '#123456';
elements.get('pad-edit-widget-bar-color-4-pattern').value = 'dots';
elements.get('pad-edit-widget-bar-ticks').value = '4';
elements.get('pad-edit-widget-bar-marker-value').value = '0';
context.padRefreshWidgetEditor(true);
assert.deepStrictEqual([1, 2, 3, 4].map(bar => elements.get('pad-edit-bar-source-' + bar).open), [true, true, true, true]);
assert.strictEqual(elements.get('pad-edit-bar-gridlines').open, true);
assert.strictEqual(elements.get('pad-edit-bar-target').open, true);
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
assert.strictEqual(elements.get('pad-edit-bar-gridline-style').hidden, true);
assert.strictEqual(elements.get('pad-edit-bar-target-style').hidden, true);
context.padRefreshWidgetEditor(true);
assert.strictEqual(elements.get('pad-edit-bar-source-2').open, true, 'Paired sources open on load');
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
assert.deepStrictEqual([1, 2, 3, 4].map(ring => elements.get('pad-edit-gauge-source-' + ring).open), [true, false, false, false]);
assert.strictEqual(elements.get('pad-edit-gauge-needle').open, true);
assert.strictEqual(elements.get('pad-edit-gauge-tick-marks').open, true);
assert.strictEqual(elements.get('pad-edit-gauge-target').open, false);
assert.strictEqual(elements.get('pad-edit-gauge-target-style').hidden, true);
elements.get('pad-edit-gauge-data-binding-2').value = '[pad:power]';
elements.get('pad-edit-gauge-start-label-3').value = 'Grid';
elements.get('pad-edit-gauge-arc-color-4').value = '#123456';
elements.get('pad-edit-gauge-marker-value').value = '0';
context.padRefreshWidgetEditor(true);
assert.deepStrictEqual([1, 2, 3, 4].map(ring => elements.get('pad-edit-gauge-source-' + ring).open), [true, true, true, true]);
assert.strictEqual(elements.get('pad-edit-gauge-target').open, true, 'Zero is a configured Gauge target');
elements.get('pad-edit-gauge-source-3').open = false;
elements.get('pad-edit-gauge-show-needle').checked = false;
elements.get('pad-edit-gauge-ticks').value = '0';
elements.get('pad-edit-gauge-marker-value').value = '';
elements.set('pad-edit-gauge-needle-width', { value: '7' });
elements.set('pad-edit-gauge-tick-width', { value: '3' });
elements.set('pad-edit-gauge-marker-zone-deg', { value: '19' });
context.padRefreshWidgetEditor();
assert.strictEqual(elements.get('pad-edit-gauge-source-3').open, false, 'Routine refresh preserves manual disclosure');
for (const name of ['needle', 'tick', 'target']) assert.strictEqual(elements.get('pad-edit-gauge-' + name + '-style').hidden, true);
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
assert.deepStrictEqual([1, 2, 3, 4].map(ring => elements.get('pad-edit-gauge-source-' + ring).open), [true, true, true, true], 'Both Gauge pairs open their empty sources');
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
assert.deepStrictEqual([1, 2, 3].map(line => elements.get('pad-edit-sparkline-source-' + line).open), [true, false, false]);
assert.strictEqual(elements.get('pad-edit-sparkline-minimum-marker').open, false);
assert.strictEqual(elements.get('pad-edit-sparkline-minimum-marker-style').hidden, true);
assert.strictEqual(elements.get('pad-edit-sparkline-maximum-marker').open, true);
elements.get('pad-edit-sparkline-data-binding-2').value = '[pad:power]';
elements.get('pad-edit-sparkline-current-label-3').value = 'Outdoor';
context.padRefreshWidgetEditor(true);
assert.deepStrictEqual([1, 2, 3].map(line => elements.get('pad-edit-sparkline-source-' + line).open), [true, true, true]);
elements.get('pad-edit-sparkline-current-label-3').value = '';
elements.get('pad-edit-sparkline-line-color-3').value = '#123456';
context.padRefreshWidgetEditor(true);
assert.strictEqual(elements.get('pad-edit-sparkline-source-3').open, true, 'Custom colors reveal optional lines');
assert.strictEqual(elements.get('pad-edit-sparkline-source-4').open, true, 'Sparkline refresh must stop at three sources');
elements.get('pad-edit-sparkline-source-2').open = false;
elements.get('pad-edit-sparkline-marker-size-max').value = '0';
context.padRefreshWidgetEditor();
assert.strictEqual(elements.get('pad-edit-sparkline-source-2').open, false, 'Routine refresh preserves Sparkline disclosure');
assert.strictEqual(elements.get('pad-edit-sparkline-maximum-marker-style').hidden, true);
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
context.padSetHidWidgetCapabilityVisibility(devInfo);
for (const type of ['mousepad', 'scrollpad']) {
	const option = elements.get('pad-edit-' + type + '-widget-option');
	assert.strictEqual(option.hidden, false);
	assert.strictEqual(option.disabled, false);
}

const draftInput = { id: 'pad-edit-label-center', value: 'Original', checked: false };
const draftRoot = {
	isConnected: true,
	querySelectorAll(selector) { return selector.includes('input') ? [draftInput] : []; },
	querySelector() { return { scrollTop: 42 }; },
	classList: { add() {}, remove() {} }
};
const workspaceContext = {
	console, Map, Set,
	padState: { page: 0, cols: 2, rows: 2, editCol: 0, editRow: 0, buttons: [{ col: 0, row: 0, label_center: 'Original' }] },
	padDirty: false,
	document: {
		getElementById() { return { value: '0', replaceChildren() {}, appendChild() {} }; },
		createElement() { return { setAttribute() {}, addEventListener() {} }; }
	},
	padMarkDirty() { workspaceContext.padDirty = true; workspaceContext.revisions++; },
	revisions: 0,
	padRenderGrid() {},
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
assert.strictEqual(vm.runInContext("padWorkspace.forms.get('0,0').values[0].value", workspaceContext), '[unfinished');
workspaceContext.padDialogOpen = () => {};
assert.throws(() => workspaceContext.padWorkspaceValidate(), /Incomplete binding/);
const revisions = workspaceContext.revisions;
workspaceContext.padWorkspaceEdit();
assert.strictEqual(workspaceContext.revisions, revisions, 'Trailing change must not rebuild an unchanged form');
draftInput.value = 'Corrected';
workspaceContext.padWorkspaceEdit();
assert.strictEqual(workspaceContext.padState.buttons[0].label_center, 'Corrected');
workspaceContext.padWorkspaceValidate();
assert.strictEqual(vm.runInContext('padWorkspace.errors.size', workspaceContext), 0);
workspaceContext.padWorkspaceReset();
assert.strictEqual(vm.runInContext('padWorkspace.forms.size', workspaceContext), 0);
assert.strictEqual(vm.runInContext('padWorkspace.selected', workspaceContext), false);
vm.runInContext("padWorkspace.selected = true; padWorkspace.errors.set('0,0', 'Incomplete binding');", workspaceContext);
draftInput.value = '[unfinished';
workspaceContext.padWorkspaceRenderTabs = () => {};
setWorkspaceScope('pad');
assert.strictEqual(vm.runInContext('padWorkspace.selected', workspaceContext), false);
assert.strictEqual(vm.runInContext("padWorkspace.forms.get('0,0').values[0].value", workspaceContext), '[unfinished');
assert.strictEqual(vm.runInContext("padWorkspace.errors.get('0,0')", workspaceContext), 'Incomplete binding');
workspaceContext.padWorkspaceSetScope = setWorkspaceScope;
vm.runInContext('padWorkspace.selected = true;', workspaceContext);
workspaceContext.padWorkspaceSelect(0, 0);
assert.strictEqual(vm.runInContext('padWorkspace.selected', workspaceContext), false, 'Clicking the selected button deselects it');
assert.strictEqual(vm.runInContext("padWorkspace.forms.get('0,0').values[0].value", workspaceContext), '[unfinished');
assert.strictEqual(vm.runInContext("padWorkspace.errors.get('0,0')", workspaceContext), 'Incomplete binding');
let openedPosition = null;
workspaceContext.padDialogOpen = (col, row) => { openedPosition = [col, row]; };
workspaceContext.padWorkspaceSelect(0, 0);
assert.deepStrictEqual(openedPosition, [0, 0], 'Clicking an unselected button opens its editor');

const refreshElements = new Map();
const refreshContext = {
	console, padState: { page: 0, cols: 4, rows: 3 }, padDirty: false, padSaveInProgress: false,
	deviceInfoCache: { max_pads: 2, available_screens: [{ id: 'pad_0', name: 'Pad 1: Solar' }, { id: 'pad_1', name: 'Pad 2: Lights' }] },
	document: {
		getElementById(id) {
			assert.notStrictEqual(id, 'pad-page-select', 'Refresh must not depend on the removed pad dropdown');
			if (!refreshElements.has(id)) refreshElements.set(id, { value: 'Solar', textContent: '', children: [], replaceChildren() { this.children = []; }, appendChild(child) { this.children.push(child); } });
			return refreshElements.get(id);
		},
		createElement() { return { dataset: {}, setAttribute() {} }; },
		querySelectorAll() { return []; }
	}
};
vm.createContext(refreshContext);
vm.runInContext(fs.readFileSync('src/app/web/portal_pad_workspace.js', 'utf8'), refreshContext);
vm.runInContext('padWorkspace = { root: { isConnected: true }, loading: false, railSignature: "[0,[]]" };', refreshContext);
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
	const workspace = { revision: 1, selected: true, scope: 'button', tab: 'Content' };
	const saveContext = { console, MAX_ACTIONS: 3, padWorkspace: workspace, document: { getElementById() { return root; } } };
	vm.createContext(saveContext);
	vm.runInContext(source.replace('let padDirty = false;', 'var padDirty = false;'), saveContext);
	vm.runInContext('padState.editCol = 2; padState.editRow = 0;', saveContext);
	saveContext.padBuildSaveContext = () => ({ page: 0, name: 'Saved' });
	saveContext.padClearDirty = () => { saveContext.padDirty = false; };
	saveContext.showMessage = () => {};
	saveContext.getDeviceInfo = async () => {};
	let reloads = 0;
	saveContext.padLoadPage = async () => {
		reloads++;
		vm.runInContext('padState.editCol = 0; padState.editRow = 0;', saveContext);
		workspace.tab = 'Layout';
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
	};
	await saveContext.padSavePage();
	assert.deepStrictEqual(saveContext.position, [1, 0], 'Save reload must retain the latest selection');
	assert.strictEqual(workspace.tab, 'Actions');
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
