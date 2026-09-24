const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

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

console.log('portal_pad_dirty: PASS');
