const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

const context = {
    validateBinding(v) {
        return v.indexOf('[bad') !== -1
            ? { valid: false, errors: [{ message: 'Unknown binding scheme' }] }
            : { valid: true, errors: [] };
    }
};
vm.createContext(context);
vm.runInContext(fs.readFileSync('src/app/web/portal_pad_colors.js', 'utf8'), context);

const norm = context.padColorNormalizeLiteral;
assert.strictEqual(norm('#4caf50'), '#4CAF50');
assert.strictEqual(norm('4CAF50'), '#4CAF50');
assert.strictEqual(norm('0xff0000'), '#FF0000');
assert.strictEqual(norm('#fff'), '#FFFFFF');
assert.strictEqual(norm(' #1a2 '), '#11AA22');
for (const bad of ['', 'red', '#12', '#1234', '#GGGGGG', '#FF000080', '[mqtt:a]']) {
    assert.strictEqual(norm(bad), null, bad);
}

const check = context.padColorCheck;
assert.deepStrictEqual({ ...check('#fff', 0) }, { value: '#FFFFFF', error: '' });
assert.deepStrictEqual({ ...check('', 0) }, { value: '', error: '' });
assert.match(check('red', 0).error, /Not a color/);
assert.strictEqual(check('[expr:1]', 0).error, '');
assert.strictEqual(check('[bad:x]', 0).error, 'Unknown binding scheme');
assert.match(check('[expr:threshold([mqtt:t],"#000000",1,"#FFFFFF")]', 20).error, /Too long/);

const expr = '[expr:threshold([mqtt:sensor;$.a,b], "#4CAF50", 25, "#ff9800", [pad:hi], "#F44336")]';
const parsed = context.padThresholdGenParse(expr);
assert.strictEqual(parsed.source, '[mqtt:sensor;$.a,b]');
assert.deepStrictEqual(JSON.parse(JSON.stringify(parsed.stops)), [
    { color: '#4CAF50', threshold: '' },
    { color: '#FF9800', threshold: '25' },
    { color: '#F44336', threshold: '[pad:hi]' }
]);
for (const bad of [
    '#FF0000',
    '[expr:1+2]',
    '[expr:threshold([mqtt:t], "#000000")]',
    '[expr:threshold([mqtt:t], "#000000", 5)]',
    '[expr:threshold([mqtt:t], [pad:c], 5, "#FFFFFF")]',
    '[expr:threshold([mqtt:t], "#000000", , "#FFFFFF")]'
]) {
    assert.strictEqual(context.padThresholdGenParse(bad), null, bad);
}

const source = { value: '' };
const sourceError = { style: { display: 'none' } };
context._cpPopover = { pop: { querySelector(selector) {
    return selector === '#cp-gen-source' ? source : sourceError;
} } };
assert.strictEqual(context.padThresholdSourceValidate(), false);
assert.strictEqual(sourceError.style.display, '');
source.value = '[mqtt:sensor;$.temp]';
assert.strictEqual(context.padThresholdSourceValidate(), true);
assert.strictEqual(sourceError.style.display, 'none');
source.value = '   ';
assert.strictEqual(context.padThresholdSourceValidate(), false);
assert.strictEqual(sourceError.style.display, '');

console.log('portal_pad_colors: PASS');
