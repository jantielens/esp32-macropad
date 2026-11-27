const assert = require('assert');
const fs = require('fs');
const vm = require('vm');
const path = require('path');
const {execFileSync} = require('child_process');

const binary = process.env.ACTION_CATALOG_BINARY || path.resolve('build/host-tests/tests/test_action_parse');
const catalog = JSON.parse(execFileSync(binary, ['--catalog'], {encoding:'utf8'})).portal;
const elements = new Map();
const document = {
    getElementById(id) {
        if (!elements.has(id)) elements.set(id, {
            id, value:'', checked:false, style:{}, options:[], children:[], attributes:{},
            classList:{contains() { return false; }},
            getAttribute(name) { return this.attributes[name]; },
            setAttribute(name, value) { this.attributes[name] = value; },
            removeAttribute(name) { delete this.attributes[name]; },
            hasAttribute(name) { return Object.hasOwn(this.attributes, name); },
            setCustomValidity(message) { this.error = message; }
        });
        return elements.get(id);
    },
    createElement() { return {}; }
};
const context = {console, document, TextEncoder, deviceInfoCache:{catalog, max_pads:6},
    escAttr(value) { return value || ''; },
    validateBinding() { return {valid:true, errors:[]}; }
};
vm.createContext(context);
vm.runInContext(fs.readFileSync('src/app/web/portal_action_editor.js', 'utf8'), context);

const prefix = 'cycle';
const action = {op:'pad.cycle', args:{direction:'previous', wrap:false, excluded_pads:'5, 1'}};
context.actionEditorLoad(prefix, action);
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild(prefix))), action);
const exclusions = document.getElementById(prefix + '-op-pad.cycle-excluded_pads-fixed');
for (const value of ['0', '7', '1,bad', '1,']) {
    exclusions.value = value;
    assert.throws(() => context.actionEditorBuild(prefix), /comma-separated pad numbers/);
}
exclusions.value = '1, 6';
assert.strictEqual(context.actionEditorBuild(prefix).args.excluded_pads, '1, 6');
document.getElementById(prefix + '-op-pad.cycle-excluded_pads-dynamic').checked = true;
document.getElementById(prefix + '-op-pad.cycle-excluded_pads-template').value = '[var:excluded]';
assert.deepStrictEqual(JSON.parse(JSON.stringify(context.actionEditorBuild(prefix).args.excluded_pads)),
    {$template:'[var:excluded]'});

const option = {name:'type_after_transcription', label:'Type transcript automatically',
    type:'boolean', control:'checkbox', default:false, dynamic:false, optional:true};
const recording = {id:'voice_note.record.save', arguments:[option]};
catalog.push({type:'test_recording', operations:[recording]});
context.actionEditorLoad(prefix, {op:recording.id, args:{type_after_transcription:true}});
assert.strictEqual(context.actionEditorBuild(prefix).args.type_after_transcription, true);
document.getElementById(prefix + '-op-' + recording.id + '-type_after_transcription-fixed').checked = false;
assert.strictEqual(context.actionEditorBuild(prefix).args.type_after_transcription, false);
option.control = 'hidden';
context.actionEditorLoad(prefix, {op:recording.id, args:{type_after_transcription:true}});
assert.strictEqual(context.actionEditorBuild(prefix).args.type_after_transcription, true);
console.log('portal_action_editor_cycle: PASS');
