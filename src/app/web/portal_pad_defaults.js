// portal_pad_defaults.js - Button defaults, appearance reset, help overlays, bindings, and color helpers
// Part of the ESP32 Macropad configuration portal.
// Bundled into portal_pad_editor.js during minification.

function showBindingHelp(section) {
    const overlay = document.getElementById('binding-help-overlay');
    if (!overlay) return;

    overlay.style.display = 'flex';

    const body = overlay.querySelector('.binding-docs-body');
    const target = section ? overlay.querySelector(`[data-binding-section="${section}"]`) : null;
    overlay.querySelectorAll('.binding-docs-section').forEach(sectionEl => {
        sectionEl.classList.toggle('is-active', sectionEl === target);
    });

    requestAnimationFrame(() => {
        if (target) {
            target.scrollIntoView({ behavior: 'smooth', block: 'start' });
        } else if (body) {
            body.scrollTop = 0;
        }
    });
}

function closeBindingHelp() {
    const overlay = document.getElementById('binding-help-overlay');
    if (!overlay) return;

    overlay.style.display = 'none';
    overlay.querySelectorAll('.binding-docs-section').forEach(sectionEl => {
        sectionEl.classList.remove('is-active');
    });
}

function showStyleHelp() {
    document.getElementById('style-help-overlay').style.display = 'flex';
}

/** Toggle label style input visibility for a label slot (top/center/bottom). */
function toggleLabelStyle(slot) {
    var wrap = document.getElementById('pad-edit-label-' + slot + '-style-wrap');
    var btn = wrap.previousElementSibling.querySelector('.style-toggle-btn');
    var visible = wrap.style.display !== 'none';
    wrap.style.display = visible ? 'none' : 'flex';
    btn.classList.toggle('active', !visible);
    if (!visible) wrap.querySelector('input').focus();
}

/** Show or hide a label style wrap based on whether it has a value. */
function syncLabelStyleVisibility(slot) {
    var input = document.getElementById('pad-edit-label-' + slot + '-style');
    var wrap = document.getElementById('pad-edit-label-' + slot + '-style-wrap');
    var btn = wrap.previousElementSibling.querySelector('.style-toggle-btn');
    var hasValue = input.value.trim().length > 0;
    wrap.style.display = hasValue ? 'flex' : 'none';
    btn.classList.toggle('active', hasValue);
}

// --- Button Defaults helpers ---

// Firmware hardcoded defaults (must match init_button_defaults in pad_config.cpp)
const PAD_FIRMWARE_DEFAULTS = {
    bg_color: '#333333', fg_color: '#ffffff', border_color: '#000000',
    border_width: '0', corner_radius: '8',
};

// Load device-level button defaults from the REST API
async function padLoadButtonDefaultsFromDevice() {
    try {
        const resp = await fetch('/api/component/button-defaults/config');
        if (resp.ok) {
            const defs = await resp.json();
            padState.buttonDefaults = defs || {};
        } else {
            padState.buttonDefaults = {};
        }
    } catch (e) {
        console.error('Failed to load button defaults:', e);
        padState.buttonDefaults = {};
    }
    padLoadButtonDefaults(padState.buttonDefaults);
}

function padLoadButtonDefaults(defs) {
    padState.buttonDefaults = defs || {};
    // NOTE: This function does double duty. It always caches button defaults in
    // padState (used by the pad-editor grid renderer), and also populates the
    // form fields below — but those fields only exist in the button-defaults
    // fragment. When called from the pad-editor fragment, every getElementById
    // returns null and the `if (el)` guards turn the form-population code into
    // safe no-ops. This is intentional and harmless; splitting into two
    // functions was considered but rejected as cosmetic-only churn.
    // Init bindable color controls
    ['pad-def-bg-color-wrap', 'pad-def-fg-color-wrap', 'pad-def-border-color-wrap'].forEach(id => {
        var el = document.getElementById(id);
        if (el) padInitBindableColor(el);
    });
    padSetBindableColor('pad-def-bg-color', defs.bg_color || '', PAD_FIRMWARE_DEFAULTS.bg_color);
    padSetBindableColor('pad-def-fg-color', defs.fg_color || '', PAD_FIRMWARE_DEFAULTS.fg_color);
    padSetBindableColor('pad-def-border-color', defs.border_color || '', PAD_FIRMWARE_DEFAULTS.border_color);
    var el;
    el = document.getElementById('pad-def-border-width'); if (el) el.value = defs.border_width || '';
    el = document.getElementById('pad-def-corner-radius'); if (el) el.value = defs.corner_radius || '';
    el = document.getElementById('pad-def-label-top-style'); if (el) el.value = defs.label_top_style || '';
    el = document.getElementById('pad-def-label-center-style'); if (el) el.value = defs.label_center_style || '';
    el = document.getElementById('pad-def-label-bottom-style'); if (el) el.value = defs.label_bottom_style || '';
    var ipSel = document.getElementById('pad-def-icon-position');
    if (ipSel) ipSel.value = defs.icon_position || 'above';

}

function padCollectButtonDefaults() {
    var d = {};
    var bgc = padGetBindableColor('pad-def-bg-color');
    var fgc = padGetBindableColor('pad-def-fg-color');
    var bdc = padGetBindableColor('pad-def-border-color');
    if (bgc) d.bg_color = bgc;
    if (fgc) d.fg_color = fgc;
    if (bdc) d.border_color = bdc;
    var el;
    el = document.getElementById('pad-def-border-width'); if (el && el.value.trim()) d.border_width = el.value.trim();
    el = document.getElementById('pad-def-corner-radius'); if (el && el.value.trim()) d.corner_radius = el.value.trim();
    el = document.getElementById('pad-def-label-top-style'); if (el && el.value.trim()) d.label_top_style = el.value.trim();
    el = document.getElementById('pad-def-label-center-style'); if (el && el.value.trim()) d.label_center_style = el.value.trim();
    el = document.getElementById('pad-def-label-bottom-style'); if (el && el.value.trim()) d.label_bottom_style = el.value.trim();
    var ip = document.getElementById('pad-def-icon-position');
    if (ip && ip.value && ip.value !== 'above') d.icon_position = ip.value;

    return d;
}

// Save device-level button defaults to /api/button-defaults
async function padSaveButtonDefaults() {
    var d = padCollectButtonDefaults();
    try {
        var resp = await fetch('/api/component/button-defaults/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(d),
        });
        if (!resp.ok) {
            var err = await resp.json().catch(function() { return {}; });
            throw new Error(err.error || 'HTTP ' + resp.status);
        }
        padState.buttonDefaults = d;
        showMessage('Button defaults saved', 'success');
        // Re-render current pad grid to reflect updated defaults
        padRenderGrid();
    } catch (err) {
        console.error('padSaveButtonDefaults error:', err);
        showMessage('Save failed: ' + err.message, 'error');
    }
}

// Get the effective default for a button appearance field (pad default → firmware default)
function padGetEffectiveDefault(field) {
    var d = padState.buttonDefaults || {};
    if (d[field]) return d[field];
    return PAD_FIRMWARE_DEFAULTS[field] || '';
}

// Reset a button appearance field to the effective default and update the reset hint
function padResetAppearance(inputId, defaultKey) {
    var eff = padGetEffectiveDefault(defaultKey);
    var el = document.getElementById(inputId);
    if (!el) return;
    // Color fields use the bindable-color component
    if (el.classList.contains('bc-input')) {
        padSetBindableColor(inputId, eff);
    } else {
        el.value = eff;
    }
    padUpdateResetHints();
}

// Mapping of input IDs → default key names for appearance fields
var PAD_APPEARANCE_FIELDS = [
    { input: 'pad-edit-bg-color', key: 'bg_color', isColor: true },
    { input: 'pad-edit-fg-color', key: 'fg_color', isColor: true },
    { input: 'pad-edit-border-color', key: 'border_color', isColor: true },
    { input: 'pad-edit-border-width', key: 'border_width', isColor: false },
    { input: 'pad-edit-corner-radius', key: 'corner_radius', isColor: false }
];

// Show/hide reset hints based on whether each field differs from its effective default
function padUpdateResetHints() {
    PAD_APPEARANCE_FIELDS.forEach(function(f) {
        var el = document.getElementById(f.input);
        var resetEl = document.getElementById(f.input + '-reset');
        if (!el || !resetEl) return;
        var val = el.value.trim().toLowerCase();
        var eff = (padGetEffectiveDefault(f.key) || '').toLowerCase();
        resetEl.style.display = (val && val !== eff) ? 'inline' : 'none';
    });
}

// --- Pad Bindings helpers ---

function padRenderBindings() {
    const list = document.getElementById('pad-bindings-list');
    if (!list) return;
    list.innerHTML = '';
    if (!padState.bindings || padState.bindings.length === 0) return;
    padState.bindings.forEach(function(b, idx) {
        const row = document.createElement('div');
        row.className = 'pad-binding-row';
        row.innerHTML =
            '<input type="text" class="pad-binding-name" value="' + escAttr(b.name) + '" placeholder="name" maxlength="31" spellcheck="false">' +
            '<span style="color:#86868b; flex-shrink:0;">→</span>' +
            '<input type="text" class="pad-binding-value" value="' + escAttr(b.value) + '" placeholder="[mqtt:topic;path]" maxlength="191" spellcheck="false">' +
            '<button type="button" class="btn btn-small pad-binding-del" style="padding:2px 8px; font-size:12px; color:#ff3b30;">✕</button>';
        row.querySelector('.pad-binding-name').addEventListener('input', function() {
            var v = this.value.trim();
            padState.bindings[idx].name = v;
            var ok = v === '' || padIsValidBindingName(v);
            this.style.borderColor = ok ? '' : '#ff3b30';
            this.title = ok ? '' : 'Must start with a letter; only letters, digits, and underscores allowed';
            padMarkDirty();
        });
        var valueInput = row.querySelector('.pad-binding-value');
        valueInput.addEventListener('input', function() {
            padState.bindings[idx].value = this.value;
            padMarkDirty();
            if (this.classList.contains('binding-error')) bindingClearError(this);
        });
        if (typeof bindingAttachValidation === 'function') bindingAttachValidation(valueInput);
        row.querySelector('.pad-binding-del').addEventListener('click', function() {
            padState.bindings.splice(idx, 1);
            padRenderBindings();
            padMarkDirty();
        });
        list.appendChild(row);
    });
}

function padIsValidBindingName(name) {
    return /^[a-zA-Z][a-zA-Z0-9_]*$/.test(name) && name.length < 32;
}

function padAddBinding() {
    if (!padState.bindings) padState.bindings = [];
    padState.bindings.push({ name: '', value: '' });
    padRenderBindings();
    // Focus the new name input
    const names = document.querySelectorAll('.pad-binding-name');
    if (names.length) names[names.length - 1].focus();
    padMarkDirty();
}

function padBindingsFromJson(obj) {
    var arr = [];
    if (obj && typeof obj === 'object') {
        Object.keys(obj).forEach(function(k) { arr.push({ name: k, value: obj[k] }); });
    }
    return arr;
}

function padBindingsToDict(bindings) {
    var bd = {};
    if (bindings) bindings.forEach(function(b) { if (b.name) bd[b.name] = b.value || ''; });
    return Object.keys(bd).length > 0 ? bd : null;
}

function padCacheColors(page, buttons, pageBgColor) {
    const seen = new Set();
    const colors = [];
    // Include page background color in cache
    if (pageBgColor && pageBgColor !== '#000000') {
        seen.add(pageBgColor); colors.push(pageBgColor);
    }
    for (const b of buttons) {
        [b.bg_color, b.fg_color, b.border_color].forEach(val => {
            const hex = padColorToHex(val, null);
            if (hex && !seen.has(hex)) { seen.add(hex); colors.push(hex); }
        });
    }
    padState.colorCache[page] = colors;
}



function padCollectUsedColors(editCol, editRow) {
    const seen = new Set();
    const colors = [];
    function add(val) {
        const hex = padColorToHex(val, null);
        if (!hex || seen.has(hex)) return;
        seen.add(hex);
        colors.push(hex);
    }
    // 1) Current button first (highest priority)
    const cur = padFindButton(editCol, editRow);
    if (cur) { add(cur.bg_color); add(cur.fg_color); add(cur.border_color); }
    // 2) Other buttons on same pad
    for (const b of padState.buttons) {
        if (b.col === editCol && b.row === editRow) continue;
        add(b.bg_color); add(b.fg_color); add(b.border_color);
    }
    // 3) Colors from other visited pads
    for (const [pg, hexArr] of Object.entries(padState.colorCache)) {
        if (parseInt(pg) === padState.page) continue;
        hexArr.forEach(hex => add(hex));
    }
    return colors.slice(0, 9);
}

// --- Binding length warning (pad editor) ---
// When a bindable field is filled to its maxlength while it contains a binding
// token, the value may be a truncated [scheme:...] expression. Show an inline
// hint pointing the user at named pad bindings, which keep long topics short.

function padBindingMaxlenHint(input) {
    if (!input || input.tagName !== 'INPUT') return;
    const max = input.maxLength;
    if (!max || max < 0) return;
    const val = input.value || '';
    const show = val.length >= max && val.indexOf('[') !== -1;
    let hint = input._maxlenHint;
    if (show) {
        if (!hint) {
            hint = document.createElement('div');
            hint.className = 'binding-maxlen-hint';
            hint.innerHTML = 'Reached the maximum length. For a long binding, define a ' +
                '<a href="#" onclick="showBindingHelp(\'pad\');return false;">named pad binding</a> ' +
                'and use the short <code>[pad:name]</code> alias here.';
            // Anchor to the enclosing field block so the hint always lands on its
            // own full-width line below the input, never as a flex sibling that
            // would squeeze the textbox narrow.
            const anchor = input.closest('.form-group') || input.closest('.bindable-color') || input;
            anchor.insertAdjacentElement('afterend', hint);
            input._maxlenHint = hint;
        }
        hint.style.display = '';
    } else if (hint) {
        hint.style.display = 'none';
    }
}

// Re-evaluate every bindable field in the pad-edit dialog (clears stale hints
// from a previous button and shows fresh ones for the current values).
function padScanMaxlenHints() {
    const overlay = document.getElementById('pad-edit-overlay');
    if (!overlay) return;
    overlay.querySelectorAll('input[maxlength]').forEach(padBindingMaxlenHint);
}

// Delegated listener — fires for any bindable input typed in the pad editor.
if (!window.__padMaxlenHintWired) {
    window.__padMaxlenHintWired = true;
    document.addEventListener('input', function(e) {
        const t = e.target;
        if (t && t.tagName === 'INPUT' && t.maxLength > 0 &&
            t.closest && t.closest('#pad-edit-overlay')) {
            padBindingMaxlenHint(t);
        }
    });
}

