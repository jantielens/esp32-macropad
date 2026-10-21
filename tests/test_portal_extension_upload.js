const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

let request;
let pendingReboot = false;
const context = {
    window: {},
    document: { getElementById() { return null; } },
    showMessage(message, type) { throw new Error(type + ': ' + message); },
    setPendingReboot() { pendingReboot = true; },
    async fetch(url, options) {
        request = { url, options };
        return { ok: true, async json() { return { success: true }; } };
    },
};

vm.createContext(context);
vm.runInContext(fs.readFileSync('src/app/web/portal_extensions.js', 'utf8'), context);

(async function () {
    for (const name of [
        'word-clock@1.0.4-s3 (4).ext',
        'word-clock@1.0.4-s3.ext.ext',
        'arbitrary local name.bin',
    ]) {
        const file = { name };
        const input = { files: [file], value: 'selected' };
        await context.extensionUpload({ slot: 2 }, input);
        assert.strictEqual(request.url, '/api/extensions/upload?slot=2');
        assert.strictEqual(request.options.body, file);
        assert.strictEqual(input.value, '');
    }
    assert.strictEqual(pendingReboot, true);
    console.log('Portal extension uploads ignore local filenames');
})().catch(error => {
    console.error(error);
    process.exitCode = 1;
});
