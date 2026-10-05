const assert = require('assert');
const fs = require('fs');
const vm = require('vm');
const { execFileSync } = require('child_process');

const context = {
	console,
	MAX_ACTIONS: 3,
	padIconTypeChanged() {},
	actionEditorListSetLabels() {},
	document: {
		addEventListener() {},
		getElementById(id) { return elements.get(id) || null; }
	}
};
const elements = new Map([
	['pad-edit-camera-feed-section', { style: {} }],
	['pad-edit-image-section', { style: {} }],
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
assert(source.includes('        padClearDirty();\n    }\n}\n\nfunction padCloneJson'));

for (const imageFetch of [false, true]) {
	for (const imageLibrary of [false, true]) {
		context.deviceInfoCache = { has_image_fetch: imageFetch, has_image_library: imageLibrary };
		context.padSetImageCapabilityVisibility(context.deviceInfoCache);
		context.padWidgetTypeChanged();
		assert.strictEqual(elements.get('pad-edit-camera-feed-section').style.display, imageFetch ? '' : 'none');
		assert.strictEqual(elements.get('pad-edit-image-section').style.display, imageLibrary ? '' : 'none');
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
	'pad-edit-lp-heading', 'pad-edit-lp-actions']) elements.set(id, { style: {} });
for (const type of ['mousepad', 'scrollpad']) {
	elements.get('pad-edit-widget-type').value = type;
	context.padWidgetTypeChanged();
	assert.strictEqual(elements.get('pad-edit-' + type + '-section').style.display, '');
	assert.strictEqual(elements.get('pad-edit-' + type + '-section').open, true);
	for (const id of ['pad-edit-tap-heading', 'pad-edit-tap-actions', 'pad-edit-lp-heading', 'pad-edit-lp-actions']) {
		assert.strictEqual(elements.get(id).style.display, 'none');
	}
}
elements.get('pad-edit-widget-type').value = '';
context.padWidgetTypeChanged();
assert.strictEqual(elements.get('pad-edit-mousepad-section').style.display, 'none');
assert.strictEqual(elements.get('pad-edit-scrollpad-section').style.display, 'none');
assert.strictEqual(elements.get('pad-edit-tap-actions').style.display, '');
assert.strictEqual(elements.get('pad-edit-lp-actions').style.display, '');

const devInfo = JSON.parse(execFileSync('python3', ['-c',
	"import json, runpy, sys; from types import SimpleNamespace; sys.path.insert(0, 'tools'); module = runpy.run_path('tools/portal-dev-server.py'); handler = object.__new__(module['PortalHandler']); handler.headers = {}; handler.server = SimpleNamespace(profile='esp32-p4-lcd4b'); module['reset_pad_fixtures'](handler.server); print(json.dumps(handler._device_info()))"
], { encoding: 'utf8' }));
context.padSetHidWidgetCapabilityVisibility(devInfo);
for (const type of ['mousepad', 'scrollpad']) {
	const option = elements.get('pad-edit-' + type + '-widget-option');
	assert.strictEqual(option.hidden, false);
	assert.strictEqual(option.disabled, false);
}

console.log('portal_pad_dirty: PASS');
