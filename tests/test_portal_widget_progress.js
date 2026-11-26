const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

class Element {
    constructor(id) {
        this.id = id; this.value = ''; this.checked = false; this.listeners = {};
        this.dataset = {}; this.style = {}; this.textContent = id;
        this.tagName = /(?:layout|shape|inactive-style|partial|order|origin|direction|effect|preset)$/.test(id) ? 'SELECT' : 'INPUT';
        this.swatch = { title: '' };
    }
    addEventListener(type, callback) { (this.listeners[type] ||= []).push(callback); }
    dispatchEvent(event) { (this.listeners[event.type] || []).forEach(callback => callback(event)); }
    setAttribute() {}
    appendChild(child) { elements.set(child.id, child); }
    closest() { return this; }
    querySelector(selector) { return selector === '.bc-swatch' ? this.swatch : null; }
}
const elements = new Map();
const document = {
    getElementById(id) {
        if (!elements.has(id)) elements.set(id, new Element(id));
        return elements.get(id);
    },
    querySelector(selector) {
        const match = /label\[for="([^"]+)"\]/.exec(selector);
        return match ? this.getElementById(match[1] + '-label') : null;
    },
    createElement() { return new Element(''); }
};
let confirmed = false;
const context = vm.createContext({
    document, TextEncoder, confirm: () => confirmed,
    Event: class { constructor(type) { this.type = type; } preventDefault() {} },
    padInitBindableColor() {}, padRefreshBindableColor() {},
    padGetEffectiveDefault: () => '#ffffff',
    padColorNormalizeLiteral: value => /^#[0-9a-f]{6}$/i.test(value) ? value : null,
    padColorValue: value => value || '',
    padGetBindableColor: id => document.getElementById(id).value.trim(),
    padSetBindableColor(id, value, fallback) {
        const el = document.getElementById(id);
        el.value = value || fallback || '';
        el.dispatchEvent(new context.Event('input'));
    }
});
vm.runInContext(fs.readFileSync('src/app/web/portal_widget_editor.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_widget_progress.js', 'utf8'), context);
const editor = context.padWidgetEditor('progress');
editor.groups = [];
editor.init();
const get = name => document.getElementById('pad-edit-progress-' + name);
const saved = {
    widget_type: 'progress', widget_data_binding: '[mqtt:work/progress]',
    widget_progress_min: '2', widget_progress_max: '100',
    widget_progress_units: '[pad:count]', widget_progress_invert: true,
    widget_progress_gap_x: 3, widget_progress_gap_y: 7,
    widget_progress_active_color: 'auto',
    widget_progress_inactive_style: 'hidden', widget_progress_inactive_color: '[pad:color]',
    widget_progress_seed: 0
};
context.padWidgetEditorsLoad(saved);
assert.strictEqual(get('gaps-linked').checked, false);
let draft = {};
context.padWidgetEditorBuild(draft, editor);
assert.strictEqual(draft.widget_progress_match_spacing, false);
assert.strictEqual(draft.widget_progress_gap_x, 3);
assert.strictEqual(draft.widget_progress_gap_y, 7);
assert.strictEqual(draft.widget_progress_inactive_color, '[pad:color]');
assert.strictEqual(draft.widget_progress_active_color, undefined);
assert.strictEqual(get('active-color').value, '');
assert.strictEqual(draft.widget_progress_seed, 0);
context.padWidgetEditorsLoad({ widget_type: 'progress' });
assert.strictEqual(get('gaps-linked').checked, true);
get('gaps-linked').checked = false;
get('gaps-linked').dispatchEvent(new context.Event('change'));
draft = {};
context.padWidgetEditorBuild(draft, editor);
assert.strictEqual(draft.widget_progress_match_spacing, false);
assert.strictEqual(draft.widget_progress_gap_x, 'auto');
assert.strictEqual(draft.widget_progress_gap_y, 'auto');
context.padWidgetEditorsLoad({ ...draft, widget_type: 'progress' });
assert.strictEqual(get('gaps-linked').checked, false);
get('gaps-linked').checked = true;
get('gaps-linked').dispatchEvent(new context.Event('change'));
draft = {};
context.padWidgetEditorBuild(draft, editor);
assert.strictEqual(draft.widget_progress_match_spacing, true);
context.padWidgetEditorsLoad(saved);
get('preset').value = 'soft_waffle';
get('preset').dispatchEvent(new context.Event('change'));
assert.strictEqual(get('shape').value, 'circle');
assert.strictEqual(get('preset').value, '');
assert.strictEqual(get('inactive-color').value, '[pad:color]');
confirmed = true;
let edits = 0;
get('stagger').addEventListener('input', () => edits++);
get('preset').value = 'soft_waffle';
get('preset').dispatchEvent(new context.Event('change'));
assert.strictEqual(edits, 1);
assert.strictEqual(get('preset').value, '');
draft = {};
context.padWidgetEditorBuild(draft, editor);
assert.strictEqual(draft.widget_progress_shape, 'block');
assert.strictEqual(draft.widget_progress_units, '[pad:count]');
assert.strictEqual(draft.widget_progress_invert, true);
assert.strictEqual(draft.widget_data_binding, '[mqtt:work/progress]');
assert.strictEqual(draft.widget_progress_min, '2');
assert.strictEqual(draft.widget_progress_max, '100');
assert.strictEqual(draft.widget_progress_inactive_color, undefined);
context.padWidgetEditorsLoad({ ...saved, ...draft, widget_type: 'progress' });
assert.strictEqual(get('shape').value, 'block');
get('columns-reset').dispatchEvent(new context.Event('click'));
draft = {};
context.padWidgetEditorBuild(draft, editor);
assert.strictEqual(draft.widget_progress_columns, 'auto');
assert.strictEqual(draft.widget_progress_shape, 'block');
assert.strictEqual(draft.widget_progress_gap_x, 2);
get('gap-x').value = '6';
get('gap-x').dispatchEvent(new context.Event('input'));
assert.strictEqual(get('gap-y').value, '6');
get('gap-x-reset').dispatchEvent(new context.Event('click'));
assert.strictEqual(get('gap-x').value, 'auto');
assert.strictEqual(get('gap-y').value, 'auto');
get('gaps-linked').checked = false;
get('gap-y').value = '7';
get('gap-x').value = '5';
get('gap-x-reset').dispatchEvent(new context.Event('click'));
assert.strictEqual(get('gap-y').value, '7');
get('inactive-color').value = '[pad:color]';
get('inactive-color').dispatchEvent(new context.Event('input'));
assert.strictEqual(get('inactive-color-reset').style.display, 'inline');
get('inactive-color-reset').dispatchEvent(new context.Event('click'));
assert.strictEqual(get('inactive-color').value, '');
assert.strictEqual(get('inactive-color-reset').style.display, 'none');
context.padWidgetEditorBuild({}, editor);
const seed = get('seed').value;
get('reshuffle').dispatchEvent(new context.Event('click'));
assert.notStrictEqual(get('seed').value, seed);
get('units').value = '12junk';
assert.throws(() => context.padWidgetEditorBuild({}, editor), /finite number/);
context.padWidgetEditorsLoad({
    widget_type: 'progress', widget_progress_layout: 'horizontal',
    widget_progress_shape: 'circle', widget_progress_thickness: 19
});
assert.strictEqual(get('thickness-wrap').hidden, true);
get('shape').value = 'block';
get('shape').dispatchEvent(new context.Event('change'));
assert.strictEqual(get('thickness-wrap').hidden, false);
assert.strictEqual(get('thickness').value, '19');
get('shape').value = 'circle';
get('shape').dispatchEvent(new context.Event('change'));
draft = {};
context.padWidgetEditorBuild(draft, editor);
assert.strictEqual(draft.widget_progress_thickness, 19);
console.log('portal_widget_progress: PASS');
