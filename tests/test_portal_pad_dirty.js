const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

const context = {
	console,
	document: { addEventListener() {} }
};
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

console.log('portal_pad_dirty: PASS');
