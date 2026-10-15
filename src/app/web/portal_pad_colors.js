// portal_pad_colors.js - Color management for the pad editor
// Part of the ESP32 Macropad configuration portal.

function padColorToHex(val, fallback) {
    if (val === undefined || val === null || val === '') return fallback;
    if (typeof val === 'string') {
        if (val.startsWith('#')) return val;
        if (val.startsWith('[')) return val; // binding template — return as-is
        // Try parsing as bare hex string (legacy "4caf50" format)
        if (/^[0-9a-fA-F]{6}$/.test(val)) return '#' + val;
        return fallback;
    }
    if (typeof val === 'number') {
        return '#' + (val & 0xFFFFFF).toString(16).padStart(6, '0');
    }
    return fallback;
}

// Returns the raw color string (hex or binding) for storing in config
function padColorValue(val) {
    if (val === undefined || val === null || val === '') return '';
    if (typeof val === 'number') return '#' + (val & 0xFFFFFF).toString(16).padStart(6, '0');
    if (typeof val === 'string') {
        if (val.startsWith('#') || val.startsWith('[')) return val;
        if (/^[0-9a-fA-F]{6}$/.test(val)) return '#' + val;
    }
    return String(val);
}

function padColorsToHex(btn) {
    // Normalize colors for JSON save: legacy ints → hex strings, strip # from static hex
    ['bg_color', 'fg_color', 'border_color'].forEach(k => {
        const v = btn[k];
        if (typeof v === 'number') {
            btn[k] = (v & 0xFFFFFF).toString(16).padStart(6, '0');
        } else if (typeof v === 'string' && v.startsWith('#')) {
            btn[k] = v.slice(1);
        }
        // Binding strings (starting with '[') pass through unchanged
    });
}

/** Basic palette colors for the color picker popover. */
var PAD_COLOR_PALETTE = [
    '#000000','#333333','#666666','#999999','#CCCCCC','#FFFFFF',
    '#F44336','#FF9800','#FFEB3B','#4CAF50','#2196F3','#9C27B0',
    '#E91E63','#FF5722','#FFC107','#8BC34A','#03A9F4','#673AB7',
    '#1A1A1A','#263238','#37474F','#455A64','#607D8B','#795548'
];

var PAD_COLOR_LITERAL_RE = /^(?:#|0x)?([0-9a-f]{3}|[0-9a-f]{6})$/i;

/** Normalize a literal color (#RGB, #RRGGBB, RRGGBB, 0xRRGGBB) to #RRGGBB, or null. */
function padColorNormalizeLiteral(val) {
    var m = PAD_COLOR_LITERAL_RE.exec(String(val == null ? '' : val).trim());
    if (!m) return null;
    var h = m[1];
    if (h.length === 3) h = h[0] + h[0] + h[1] + h[1] + h[2] + h[2];
    return '#' + h.toUpperCase();
}

/**
 * Check a color-or-expression value. Returns { value, error }: value is the
 * normalized value to store, error is '' when valid. Empty input has no error
 * but an empty value.
 */
function padColorCheck(raw, maxLen, allowBindings) {
    var v = String(raw == null ? '' : raw).trim();
    if (!v) return { value: '', error: '' };
    var value = v;
    if (v.indexOf('[') !== -1) {
        if (allowBindings === false) return { value: v, error: 'This field requires a color, not an expression' };
        if (typeof validateBinding === 'function') {
            var r = validateBinding(v);
            if (!r.valid) return { value: v, error: r.errors[0].message };
        }
    } else {
        value = padColorNormalizeLiteral(v);
        if (!value) return { value: v, error: 'Not a color \u2014 use #RRGGBB or #RGB' + (allowBindings === false ? '' : ', or a [binding]') };
    }
    if (maxLen > 0 && value.length > maxLen)
        return { value: value, error: 'Too long for this field (max ' + maxLen + ' characters)' };
    return { value: value, error: '' };
}

/** Singleton popover state. */
var _cpPopover = null;
var _cpErrorTimer = null;

/** Create a swatch button that submits a color or binding value. */
function padColorSwatchButton(val) {
    var sw = document.createElement('button');
    sw.type = 'button';
    sw.className = 'color-popover-swatch';
    sw.title = val;
    sw.setAttribute('aria-label', val);
    sw.dataset.value = val;
    if (val.charAt(0) === '#') sw.style.background = val;
    else sw.classList.add('cp-swatch-binding');
    sw.addEventListener('click', function() {
        if (_cpPopover && _cpPopover.fill) {
            _cpPopover.pop.querySelector('#cp-input').value = val;
            padColorPopoverValidate(true);
        } else padColorPopoverSubmit(val);
    });
    return sw;
}

/** Open a hidden native color input, seeded with a hex value. */
function padColorOpenNativePicker(picker, hex) {
    picker.value = (padColorNormalizeLiteral(hex) || '#888888').toLowerCase();
    picker.click();
}

/** Create (once) the singleton color popover DOM. */
function padColorPopoverCreate() {
    if (_cpPopover) return _cpPopover;
    // Backdrop
    var bd = document.createElement('div');
    bd.className = 'color-popover-backdrop';
    bd.style.display = 'none';
    bd.addEventListener('click', padColorPopoverClose);
    document.body.appendChild(bd);
    // Popover
    var pop = document.createElement('div');
    pop.className = 'color-popover';
    pop.style.display = 'none';
    pop.setAttribute('role', 'dialog');
    pop.setAttribute('aria-label', 'Choose color');
    pop.innerHTML =
        '<button type="button" class="color-popover-close" id="cp-close" aria-label="Close">&times;</button>' +
        '<details class="cp-color-section" id="cp-base" open>' +
            '<summary id="cp-base-summary">Base Color <span id="cp-base-value" class="cp-summary-value"></span></summary>' +
            '<div id="cp-base-body"></div>' +
        '</details>' +
        '<details class="cp-color-section" id="cp-fill" style="display:none;">' +
            '<summary>Pattern: <span id="cp-pattern-summary">None</span>' +
                '<span class="bindable-color cp-summary-preview"><span class="bc-swatch" id="cp-fill-badge"></span></span>' +
            '</summary>' +
            '<div class="cp-pattern-settings">' +
            '<label class="cp-gen-label" for="cp-fill-pattern">Pattern</label>' +
            '<select id="cp-fill-pattern">' + PAD_FILL_PATTERNS.map(function(entry) {
                return '<option value="' + entry[0] + '">' + entry[1] + '</option>';
            }).join('') + '</select>' +
            '</div><div id="cp-pattern-body"></div>' +
        '</details>' +
        '<div id="cp-color-editor">' +
        '<div class="color-popover-section" id="cp-recent-heading" style="display:none;">Used in your pads</div>' +
        '<div class="color-popover-grid" id="cp-recent-grid"></div>' +
        '<div class="color-popover-section">Palette</div>' +
        '<div class="color-popover-grid" id="cp-palette-grid"></div>' +
        '<details class="cp-gen" id="cp-gen">' +
            '<summary class="color-popover-section" style="cursor:pointer;list-style:none;margin-bottom:0;">' +
                '<span class="cp-gen-arrow">&#9654;</span> Generate Color by Threshold' +
            '</summary>' +
            '<div class="cp-gen-body">' +
                '<label class="cp-gen-label" for="cp-gen-source">Data Source</label>' +
                '<input type="text" id="cp-gen-source" class="cp-gen-input" maxlength="191" placeholder="[mqtt:topic;$.path]" spellcheck="false" aria-describedby="cp-gen-source-error">' +
                '<div class="binding-error-msg" id="cp-gen-source-error" role="alert" style="display:none;">Enter a data source</div>' +
                '<div class="cp-gen-label">Color Stops</div>' +
                '<div id="cp-gen-stops"></div>' +
                '<button type="button" class="btn btn-sm btn-outline-secondary cp-gen-add" id="cp-gen-add">+ Add stop</button>' +
                '<div class="binding-error-msg" id="cp-gen-error" role="alert" style="display:none;"></div>' +
            '</div>' +
        '</details>' +
        '<div class="color-popover-input-row">' +
            '<label class="cp-gen-label" for="cp-input"><span id="cp-input-label">Color or expression</span> <span class="fx-hint" id="cp-fx" title="Binding help">fx</span></label>' +
            '<div class="cp-input-wrap">' +
                '<span class="cp-preview" id="cp-preview" aria-hidden="true"></span>' +
                '<input type="text" id="cp-input" maxlength="191" placeholder="#RRGGBB or [expr:\u2026]" spellcheck="false" aria-describedby="cp-error">' +
            '</div>' +
            '<div class="binding-error-msg" id="cp-error" role="alert" style="display:none;"></div>' +
        '</div></div>' +
        '<div class="cp-actions">' +
            '<button type="button" class="btn btn-small btn-primary" id="cp-apply" disabled>Apply</button>' +
        '</div>';
    document.body.appendChild(pop);
    pop.querySelector('#cp-base-body').appendChild(pop.querySelector('#cp-color-editor'));
    // Build palette grid (static) + custom picker
    var pg = pop.querySelector('#cp-palette-grid');
    PAD_COLOR_PALETTE.forEach(function(hex) { pg.appendChild(padColorSwatchButton(hex)); });
    var custom = document.createElement('button');
    custom.type = 'button';
    custom.className = 'color-popover-swatch cp-custom';
    custom.title = 'Custom color\u2026';
    custom.setAttribute('aria-label', 'Custom color');
    custom.textContent = '+';
    var picker = document.createElement('input');
    picker.type = 'color';
    picker.className = 'cp-gen-cpick';
    picker.tabIndex = -1;
    picker.setAttribute('aria-hidden', 'true');
    custom.addEventListener('click', function() {
        padColorOpenNativePicker(picker, pop.querySelector('#cp-input').value);
    });
    picker.addEventListener('input', function() {
        pop.querySelector('#cp-input').value = picker.value.toUpperCase();
        padColorPopoverValidate(true);
    });
    pg.appendChild(custom);
    pg.appendChild(picker);

    var cpInput = pop.querySelector('#cp-input');
    pop.querySelector('#cp-apply').addEventListener('click', function() {
        padColorPopoverSubmit(cpInput.value);
    });
    cpInput.addEventListener('keydown', function(e) {
        if (e.key === 'Enter') { padColorPopoverSubmit(cpInput.value); e.preventDefault(); }
    });
    cpInput.addEventListener('input', function() {
        padColorPopoverValidate(false);
        if (_cpErrorTimer) clearTimeout(_cpErrorTimer);
        _cpErrorTimer = setTimeout(function() { _cpErrorTimer = null; padColorPopoverValidate(true); }, 400);
    });
    cpInput.addEventListener('blur', function() { padColorPopoverValidate(true); });
    ['base', 'color'].forEach(function(layer) {
        var section = pop.querySelector(layer === 'base' ? '#cp-base' : '#cp-fill');
        var other = pop.querySelector(layer === 'base' ? '#cp-fill' : '#cp-base');
        section.addEventListener('toggle', function() {
            if (!_cpPopover || !_cpPopover.fill || !_cpPopover.target) return;
            if (section.open) {
                other.open = false;
                if (_cpPopover.layer !== layer) padColorPopoverLayer(layer);
            } else if (!other.open) other.open = true;
        });
    });
    pop.querySelector('#cp-fill-pattern').addEventListener('change', function() {
        _cpPopover.fill.pattern = padFillPattern(this.value);
        padColorPopoverValidate(true);
    });
    pop.querySelector('#cp-fx').addEventListener('click', function() {
        if (typeof showBindingHelp === 'function') showBindingHelp();
    });
    pop.querySelector('#cp-close').addEventListener('click', padColorPopoverClose);
    // Escape closes from anywhere, unless the binding help overlay is on top
    document.addEventListener('keydown', function(e) {
        if (e.key !== 'Escape' || pop.style.display === 'none') return;
        var help = document.getElementById('binding-help-overlay');
        if (help && help.style.display !== 'none' && help.style.display !== '') return;
        e.stopPropagation();
        padColorPopoverClose();
    }, true);
    // Generator: toggle arrow on open/close + reposition popover
    var genDetails = pop.querySelector('#cp-gen');
    genDetails.addEventListener('toggle', function() {
        genDetails.querySelector('.cp-gen-arrow').textContent = genDetails.open ? '\u25BC' : '\u25B6';
        if (genDetails.open) padThresholdSourceValidate();
        padColorPopoverReposition();
    });
    pop.querySelector('#cp-gen-source').addEventListener('input', padThresholdGenerate);
    pop.querySelector('#cp-gen-add').addEventListener('click', function() {
        var stops = pop.querySelector('#cp-gen-stops');
        var n = stops.children.length;
        // Cycle through the bright palette rows (indices 6-17) for new stops
        padThresholdGenAddStop(stops, PAD_COLOR_PALETTE[6 + (n % 12)], '');
        padThresholdGenSyncRemove();
        padThresholdGenerate();
        padColorPopoverReposition();
    });
    if (typeof bindingAttachValidation === 'function') {
        bindingAttachValidation(pop.querySelector('#cp-gen-source'));
    }
    _cpPopover = { pop: pop, bd: bd, target: null, anchor: null, returnFocus: null };
    return _cpPopover;
}

/** Validate #cp-input, update preview and Apply state; optionally show the error. */
function padColorPopoverValidate(showError) {
    if (!_cpPopover) return null;
    var pop = _cpPopover.pop;
    var target = _cpPopover.target;
    var res = padColorCheck(pop.querySelector('#cp-input').value, target && target.maxLength > 0 ? target.maxLength : 0, !target || target.dataset.allowBindings !== 'false');
    if (_cpPopover.fill) {
        _cpPopover.fill[_cpPopover.layer] = pop.querySelector('#cp-input').value;
        pop.querySelector('#cp-base-value').textContent = padColorNormalizeLiteral(_cpPopover.fill.base) || _cpPopover.fill.base;
        pop.querySelector('#cp-pattern-summary').textContent = PAD_FILL_PATTERNS.find(function(entry) {
            return entry[0] === _cpPopover.fill.pattern;
        })[1];
        padFillPaintSwatch(pop.querySelector('#cp-fill-badge'), _cpPopover.fill.pattern,
            _cpPopover.fill.base, _cpPopover.fill.color);
        for (var layer of ['base', 'color']) {
            var layerResult = padColorCheck(_cpPopover.fill[layer], target.maxLength, true);
            if (!layerResult.value || layerResult.error) {
                res.error = (layer === 'base' ? 'Base Color: ' : 'Pattern Color: ') + (layerResult.error || 'Enter a color');
                break;
            }
        }
    }
    var preview = pop.querySelector('#cp-preview');
    var ok = !!res.value && !res.error;
    pop.querySelectorAll('.color-popover-swatch').forEach(function(swatch) {
        swatch.classList.toggle('active', swatch.dataset.value === res.value);
    });
    preview.classList.toggle('cp-preview-binding', ok && res.value.charAt(0) !== '#');
    preview.classList.toggle('cp-preview-none', !ok);
    preview.style.background = (ok && res.value.charAt(0) === '#') ? res.value : '';
    pop.querySelector('#cp-apply').disabled = !ok;
    var input = pop.querySelector('#cp-input');
    var err = pop.querySelector('#cp-error');
    if (res.error && showError) {
        err.textContent = res.error;
        err.style.display = '';
        input.classList.add('binding-error');
    } else if (!res.error) {
        err.style.display = 'none';
        input.classList.remove('binding-error');
    }
    return res;
}

/** Apply a value if valid; otherwise load it into the textbox and show why. */
function padColorPopoverSubmit(val) {
    if (!_cpPopover) return;
    var input = _cpPopover.pop.querySelector('#cp-input');
    input.value = String(val).trim();
    var res = padColorPopoverValidate(true);
    if (res && res.value && !res.error) padColorPopoverApply(res.value);
    else input.focus();
}

/** Open the color popover anchored to a swatch element, targeting a specific input. */
function padColorPopoverOpen(swatch, input) {
    var cp = padColorPopoverCreate();
    cp.target = input;
    cp.anchor = swatch;
    cp.returnFocus = document.activeElement;
    cp.layer = 'base';
    var fillControls = input.dataset.fillPattern ? padFillControls().find(function(controls) {
        return controls.dataset.patternField === input.dataset.fillPattern;
    }) : null;
    cp.fill = fillControls ? {
        controls: fillControls, base: input.value,
        pattern: fillControls.querySelector('select').value,
        color: fillControls.querySelector('.bc-input').value,
        originalColor: fillControls.querySelector('.bc-input').value
    } : null;
    cp.pop.querySelector('#cp-fill').style.display = cp.fill ? '' : 'none';
    cp.pop.querySelector('#cp-base-summary').style.display = cp.fill ? '' : 'none';
    cp.pop.querySelector('#cp-base').open = true;
    cp.pop.querySelector('#cp-fill').open = false;
    cp.pop.querySelector('#cp-base-body').appendChild(cp.pop.querySelector('#cp-color-editor'));
    if (cp.fill) {
        cp.pop.querySelector('#cp-fill-pattern').value = cp.fill.pattern;
    }
    var allowBindings = input.dataset.allowBindings !== 'false';
    cp.pop.querySelector('#cp-gen').style.display = allowBindings ? '' : 'none';
    cp.pop.querySelector('#cp-fx').style.display = allowBindings ? '' : 'none';
    cp.pop.querySelector('#cp-input-label').textContent = cp.fill ? 'Base Color or expression' : allowBindings ? 'Color or expression' : 'Color';
    cp.pop.querySelector('#cp-input').placeholder = allowBindings ? '#RRGGBB or [expr:\u2026]' : '#RRGGBB';
    padThresholdGenLoad(allowBindings ? input.value : '');
    // Show off-screen first so we can measure
    cp.pop.style.left = '-9999px';
    cp.pop.style.top = '-9999px';
    cp.pop.style.display = 'block';
    cp.bd.style.display = 'block';
    // Populate recently used (before measuring)
    var recentGrid = cp.pop.querySelector('#cp-recent-grid');
    var recentHeading = cp.pop.querySelector('#cp-recent-heading');
    recentGrid.innerHTML = '';
    padCollectUsedColors(padState.editCol || -1, padState.editRow || -1).forEach(function(val) {
        var hex = padColorNormalizeLiteral(val);
        if (hex || (allowBindings && val.charAt(0) === '[')) recentGrid.appendChild(padColorSwatchButton(hex || val));
    });
    recentHeading.style.display = recentGrid.children.length ? '' : 'none';
    // Highlight active color in palette & used list
    var cur = (padColorNormalizeLiteral(input.value) || input.value.trim()).toUpperCase();
    cp.pop.querySelectorAll('.color-popover-swatch[data-value]').forEach(function(sw) {
        sw.classList.toggle('active', sw.dataset.value.toUpperCase() === cur);
    });
    var cpInput = cp.pop.querySelector('#cp-input');
    cpInput.maxLength = input.maxLength > 0 ? input.maxLength : 191;
    cpInput.value = input.value.trim();
    if (cp.fill && cp.fill.pattern !== 'none') {
        cp.pop.querySelector('#cp-base').open = false;
        cp.pop.querySelector('#cp-fill').open = true;
        padColorPopoverLayer('color');
    }
    if (typeof bindingClearError === 'function') bindingClearError(cp.pop.querySelector('#cp-gen-source'));
    padColorPopoverValidate(true);
    cp.bd.style.display = 'block';
    cp.pop.style.display = 'block';
    padColorPopoverReposition();
    cpInput.focus();
}

function padColorPopoverLayer(layer) {
    var cp = _cpPopover;
    if (!cp || !cp.fill) return;
    cp.fill[cp.layer] = cp.pop.querySelector('#cp-input').value;
    cp.layer = layer;
    cp.pop.querySelector(layer === 'base' ? '#cp-base-body' : '#cp-pattern-body').appendChild(cp.pop.querySelector('#cp-color-editor'));
    cp.pop.querySelector('#cp-input').value = cp.fill[layer];
    cp.pop.querySelector('#cp-input-label').textContent = layer === 'base' ? 'Base Color or expression' : 'Pattern Color or expression';
    padThresholdGenLoad(cp.fill[layer]);
    padColorPopoverValidate(true);
    padColorPopoverReposition();
}

/** Reposition the popover to stay within the viewport. */
function padColorPopoverReposition() {
    if (!_cpPopover || !_cpPopover.anchor) return;
    var cp = _cpPopover;
    var rect = cp.anchor.getBoundingClientRect();
    var popRect = cp.pop.getBoundingClientRect();
    var popW = popRect.width, popH = popRect.height, margin = 8;
    var left = rect.left;
    var top = rect.bottom + margin;
    if (left + popW > window.innerWidth - margin) left = window.innerWidth - popW - margin;
    if (left < margin) left = margin;
    if (top + popH > window.innerHeight - margin) top = rect.top - popH - margin;
    if (top < margin) top = margin;
    cp.pop.style.left = left + 'px';
    cp.pop.style.top = top + 'px';
}

var CP_GEN_DEFAULT_STOPS = [
    { color: '#4CAF50', threshold: '' },
    { color: '#8BC34A', threshold: '25' },
    { color: '#FF9800', threshold: '50' },
    { color: '#F44336', threshold: '75' }
];
var CP_GEN_NUMBER_RE = /^-?(?:\d+\.?\d*|\.\d+)$/;

/** Add one color-stop row to the generator. */
function padThresholdGenAddStop(container, color, threshold) {
    var row = document.createElement('div');
    row.className = 'cp-gen-stop';
    var isFirst = container.children.length === 0;
    row.innerHTML =
        '<button type="button" class="cp-gen-swatch" aria-label="Stop color"></button>' +
        '<input type="color" class="cp-gen-cpick" tabindex="-1" aria-hidden="true">' +
        (isFirst
            ? '<span class="cp-gen-base-label">Base (below first threshold)</span>'
            : '<span class="cp-gen-ge">&ge;</span><input type="text" class="cp-gen-tval" aria-label="Threshold" spellcheck="false">' +
              '<button type="button" class="cp-gen-del" aria-label="Remove stop" title="Remove stop">&times;</button>');
    var swatch = row.querySelector('.cp-gen-swatch');
    var cpick = row.querySelector('.cp-gen-cpick');
    cpick.value = (color || '#888888').toLowerCase();
    swatch.style.background = cpick.value;
    cpick.addEventListener('input', function() { swatch.style.background = cpick.value; padThresholdGenerate(); });
    swatch.addEventListener('click', function() { cpick.click(); });
    var tval = row.querySelector('.cp-gen-tval');
    if (tval) {
        tval.value = threshold || '';
        tval.addEventListener('input', padThresholdGenerate);
    }
    var del = row.querySelector('.cp-gen-del');
    if (del) del.addEventListener('click', function() {
        row.remove();
        padThresholdGenSyncRemove();
        padThresholdGenerate();
        padColorPopoverReposition();
    });
    container.appendChild(row);
}

/** Hide remove buttons when only base + one stop remain. */
function padThresholdGenSyncRemove() {
    var stops = _cpPopover.pop.querySelector('#cp-gen-stops');
    var canRemove = stops.children.length > 2;
    stops.querySelectorAll('.cp-gen-del').forEach(function(b) { b.style.visibility = canRemove ? '' : 'hidden'; });
}

/** Split a function argument list on top-level commas (ignores brackets, parens, strings). */
function padSplitTopLevelArgs(s) {
    var args = [], depth = 0, quote = '', start = 0;
    for (var i = 0; i < s.length; i++) {
        var c = s.charAt(i);
        if (quote) { if (c === quote) quote = ''; continue; }
        if (c === '"' || c === "'") quote = c;
        else if (c === '(' || c === '[') depth++;
        else if (c === ')' || c === ']') depth--;
        else if (c === ',' && depth === 0) { args.push(s.slice(start, i).trim()); start = i + 1; }
    }
    args.push(s.slice(start).trim());
    return args;
}

/** Parse `[expr:threshold(src, "#c0", t1, "#c1", ...)]` into { source, stops }, or null. */
function padThresholdGenParse(val) {
    var m = /^\[expr:\s*threshold\(([\s\S]*)\)\s*\]$/.exec(String(val == null ? '' : val).trim());
    if (!m) return null;
    var args = padSplitTopLevelArgs(m[1]);
    if (args.length < 4 || args.length % 2 !== 0 || !args[0]) return null;
    var stops = [];
    for (var i = 1; i < args.length; i += 2) {
        var q = /^"([^"]*)"$/.exec(args[i]);
        var color = q ? padColorNormalizeLiteral(q[1]) : null;
        if (!color || (i > 1 && !args[i - 1])) return null;
        stops.push({ color: color, threshold: i > 1 ? args[i - 1] : '' });
    }
    return { source: args[0], stops: stops };
}

/** Load the generator from an existing threshold expression, or reset to defaults. */
function padThresholdGenLoad(val) {
    if (!_cpPopover) return;
    var pop = _cpPopover.pop;
    var parsed = padThresholdGenParse(val);
    var gen = pop.querySelector('#cp-gen');
    if (parsed) gen.setAttribute('open', '');
    else gen.removeAttribute('open');
    gen.querySelector('.cp-gen-arrow').textContent = parsed ? '\u25BC' : '\u25B6';
    pop.querySelector('#cp-gen-source').value = parsed ? parsed.source : '';
    var stops = pop.querySelector('#cp-gen-stops');
    stops.innerHTML = '';
    (parsed ? parsed.stops : CP_GEN_DEFAULT_STOPS).forEach(function(s) {
        padThresholdGenAddStop(stops, s.color, s.threshold);
    });
    padThresholdGenSyncRemove();
    var err = pop.querySelector('#cp-gen-error');
    err.style.display = 'none';
    err.textContent = '';
}

/** Build threshold expression from generator inputs and write to #cp-input. */
function padThresholdSourceValidate() {
    var pop = _cpPopover.pop;
    var hasSource = !!pop.querySelector('#cp-gen-source').value.trim();
    pop.querySelector('#cp-gen-source-error').style.display = hasSource ? 'none' : '';
    return hasSource;
}

function padThresholdGenerate() {
    if (!_cpPopover) return;
    var pop = _cpPopover.pop;
    var src = pop.querySelector('#cp-gen-source').value.trim();
    var rows = pop.querySelectorAll('#cp-gen-stops .cp-gen-stop');
    var colors = [];
    var thresholds = [];
    for (var i = 0; i < rows.length; i++) {
        colors.push(rows[i].querySelector('.cp-gen-cpick').value.toUpperCase());
        if (i > 0) thresholds.push(rows[i].querySelector('.cp-gen-tval').value.trim());
    }
    var error = '';
    var hasSource = padThresholdSourceValidate();
    if (hasSource && thresholds.some(function(t) { return t === ''; })) error = 'Enter a threshold for each stop';
    else {
        // Literal thresholds must ascend; bound thresholds are only known at runtime.
        var prev = null;
        for (var j = 0; j < thresholds.length && !error; j++) {
            if (!CP_GEN_NUMBER_RE.test(thresholds[j])) continue;
            var n = parseFloat(thresholds[j]);
            if (prev !== null && n <= prev) error = 'Thresholds must be in ascending order';
            prev = n;
        }
    }
    var err = pop.querySelector('#cp-gen-error');
    err.textContent = error;
    err.style.display = error ? '' : 'none';
    if (!hasSource || error) return;
    var parts = '[expr:threshold(' + src + ', "' + colors[0] + '"';
    for (var k = 0; k < thresholds.length; k++) {
        parts += ', ' + thresholds[k] + ', "' + colors[k + 1] + '"';
    }
    parts += ')]';
    pop.querySelector('#cp-input').value = parts;
    padColorPopoverValidate(true);
}

/** Apply a value from the popover to the target input. */
function padColorPopoverApply(val) {
    if (!_cpPopover || !_cpPopover.target) return;
    if (_cpPopover.fill) {
        var fill = _cpPopover.fill;
        var base = padColorCheck(fill.base, _cpPopover.target.maxLength, true);
        var color = padColorCheck(fill.color, _cpPopover.target.maxLength, true);
        if (!base.value || base.error || !color.value || color.error) return;
        fill.controls.querySelector('select').value = fill.pattern;
        fill.controls.querySelector('.bc-input').value = color.value;
        if (fill.pattern !== 'none' || color.value !== fill.originalColor) fill.controls.dataset.colorSet = '1';
        val = base.value;
        padFillRefreshVisibility();
    }
    _cpPopover.target.value = val;
    _cpPopover.target.dispatchEvent(new Event('input', { bubbles: true }));
    padColorPopoverClose();
}

/** Close the popover. */
function padColorPopoverClose() {
    if (!_cpPopover) return;
    if (_cpErrorTimer) { clearTimeout(_cpErrorTimer); _cpErrorTimer = null; }
    _cpPopover.pop.style.display = 'none';
    _cpPopover.bd.style.display = 'none';
    _cpPopover.target = null;
    _cpPopover.fill = null;
    var rf = _cpPopover.returnFocus;
    _cpPopover.returnFocus = null;
    if (rf && typeof rf.focus === 'function' && document.body.contains(rf)) rf.focus();
}

/** Initialize a .bindable-color container: wire swatch click to open popover. Idempotent. */
function padInitBindableColor(container) {
    if (!container || container.dataset.bcInit) return;
    container.dataset.bcInit = '1';
    const input = container.querySelector('.bc-input');
    const swatch = container.querySelector('.bc-swatch');
    if (!input || !swatch) return;

    function syncSwatch() {
        const v = input.value.trim();
        if (/^#[0-9a-fA-F]{6}$/.test(v)) {
            swatch.style.background = v;
            swatch.classList.remove('bc-binding');
        } else if (v.length > 0) {
            swatch.style.background = '';
            swatch.classList.add('bc-binding');
        } else {
            swatch.style.background = '#e5e5ea';
            swatch.classList.remove('bc-binding');
        }
        padFillSyncSwatch(input, swatch);
    }

    input.addEventListener('input', syncSwatch);
    swatch.addEventListener('click', function() {
        padColorPopoverOpen(swatch, input);
    });
    syncSwatch();
    if (input.dataset.fillPattern) padInitFillColor(container, input);
}

/** Set a bindable color input value and sync its swatch. */
function padSetBindableColor(id, val, fallback) {
    const el = document.getElementById(id);
    if (!el) return;
    el.value = padColorValue(val) || fallback || '';
    el.dispatchEvent(new Event('input', { bubbles: true }));
}

/** Get a bindable color input value as a string. */
function padGetBindableColor(id) {
    const el = document.getElementById(id);
    return el ? el.value.trim() : '';
}

var PAD_FILL_PATTERNS = [
    ['none', 'None'], ['diagonal', 'Diagonal stripes'], ['reverse_diagonal', 'Reverse diagonal stripes'],
    ['crosshatch', 'Crosshatch'], ['dots', 'Dots'], ['checkerboard', 'Checkerboard']
];

function padFillPattern(value) {
    return PAD_FILL_PATTERNS.some(function(entry) { return entry[0] === value; }) ? value : 'none';
}

function padFillGeometry(value, fallback, minimum, maximum) {
    var number = Number(value);
    if (value === undefined || value === null || value === '' || !Number.isFinite(number)) number = fallback;
    return Math.max(minimum, Math.min(maximum, Math.trunc(number)));
}

function padInitFillColor(container, input) {
    var controls = document.createElement('div');
    controls.className = 'pad-fill-controls';
    controls.dataset.patternField = input.dataset.fillPattern;
    controls.dataset.colorField = input.dataset.fillColor;
    controls.dataset.baseInput = input.id;
    controls.hidden = true;
    controls.innerHTML = '<label for="' + input.id + '-pattern">Pattern</label>' +
        '<select id="' + input.id + '-pattern">' + PAD_FILL_PATTERNS.map(function(entry) {
            return '<option value="' + entry[0] + '">' + entry[1] + '</option>';
        }).join('') + '</select>' +
        '<div class="pad-fill-pattern-color"><label for="' + input.id + '-pattern-color">Pattern Color</label>' +
        '<div class="bindable-color"><div class="bc-swatch"></div><input type="text" class="bc-input" id="' +
        input.id + '-pattern-color" maxlength="' + input.maxLength + '" value="#000000" spellcheck="false"></div></div>';
    container.insertAdjacentElement('afterend', controls);
    controls.querySelector('select').addEventListener('change', function() {
        if (this.value !== 'none') controls.dataset.colorSet = '1';
        padFillRefreshVisibility();
    });
    controls.querySelector('.bc-input').addEventListener('input', function() { controls.dataset.colorSet = '1'; });
    if (input.dataset.fillPattern === 'bg_pattern') {
        ['pad-edit-pattern-spacing', 'pad-edit-pattern-stroke'].forEach(function(id) {
            document.getElementById(id).addEventListener('input', padFillRefreshVisibility);
        });
    }
}

function padFillSyncSwatch(input, swatch) {
    if (!input.dataset.fillPattern) return;
    var controls = padFillControls().find(function(control) { return control.dataset.patternField === input.dataset.fillPattern; });
    if (!controls) return;
    var pattern = padFillPattern(controls.querySelector('select').value);
    padFillPaintSwatch(swatch, pattern, input.value, controls.querySelector('.bc-input').value);
}

function padFillPaintSwatch(swatch, pattern, baseValue, patternValue) {
    swatch.classList.toggle('bc-pattern', pattern !== 'none');
    swatch.title = PAD_FILL_PATTERNS.find(function(entry) { return entry[0] === pattern; })[1];
    swatch.style.background = padColorNormalizeLiteral(baseValue) || (baseValue ? '' : '#e5e5ea');
    swatch.classList.toggle('bc-binding', !!baseValue && !padColorNormalizeLiteral(baseValue));
    if (pattern === 'none') return;
    var base = padColorNormalizeLiteral(baseValue) || '#EEEEEE';
    var color = padColorNormalizeLiteral(patternValue) || '#111111';
    var spacing = padFillGeometry(document.getElementById('pad-edit-pattern-spacing').value, 8, 2, 64);
    var stroke = padFillGeometry(document.getElementById('pad-edit-pattern-stroke').value, 1, 1, Math.min(8, spacing));
    function stripes(angle) { return 'repeating-linear-gradient(' + angle + 'deg,' + color + ' 0 ' + stroke + 'px,transparent ' + stroke + 'px ' + spacing + 'px)'; }
    var image = pattern === 'diagonal' ? stripes(135) : pattern === 'reverse_diagonal' ? stripes(45) :
        pattern === 'crosshatch' ? stripes(135) + ',' + stripes(45) :
        pattern === 'dots' ? 'radial-gradient(circle at ' + stroke / 2 + 'px ' + stroke / 2 + 'px,' + color + ' 0 ' + stroke / 2 + 'px,transparent ' + stroke / 2 + 'px)' :
        'conic-gradient(' + color + ' 0 25%,transparent 0 50%,' + color + ' 0 75%,transparent 0)';
    var size = pattern === 'dots' ? spacing + 'px ' + spacing + 'px' :
        pattern === 'checkerboard' ? spacing * 2 + 'px ' + spacing * 2 + 'px' : 'auto';
    swatch.style.backgroundColor = base;
    swatch.style.backgroundImage = image;
    swatch.style.backgroundSize = size;
    swatch.style.setProperty('--fill-base', base);
    swatch.style.setProperty('--fill-preview', image);
    swatch.style.setProperty('--fill-preview-size', size);
    var bound = !padColorNormalizeLiteral(baseValue) || !padColorNormalizeLiteral(patternValue);
    swatch.classList.toggle('bc-binding', bound);
    if (bound) swatch.title += ' (bound color; representative preview)';
}

function padFillControls() {
    return Array.from(document.querySelectorAll('.pad-edit-modal .pad-fill-controls'));
}

function padFillRefreshVisibility(resetSections) {
    var type = document.getElementById('pad-edit-widget-type').value;
    const editor = typeof padWidgetEditor === 'function' && padWidgetEditor(type);
    var stroked = false;
    var patterned = false;
    padFillControls().forEach(function(controls) {
        var supported = controls.dataset.patternField === 'bg_pattern' || editor && (editor.patterns || []).includes(controls.dataset.patternField);
        var pattern = padFillPattern(controls.querySelector('select').value);
        controls.querySelector('.pad-fill-pattern-color').style.display = pattern === 'none' ? 'none' : '';
        if (supported && pattern !== 'none') {
            patterned = true;
            if (pattern !== 'checkerboard') stroked = true;
        }
        var baseInput = document.getElementById(controls.dataset.baseInput);
        if (baseInput) padFillSyncSwatch(baseInput, baseInput.closest('.bindable-color').querySelector('.bc-swatch'));
    });
    document.getElementById('pad-edit-pattern-geometry').style.display = '';
    document.getElementById('pad-edit-pattern-stroke-wrap').style.display = '';
    document.getElementById('pad-edit-pattern-stroke').disabled = !stroked;
    var patterns = document.getElementById('pad-edit-patterns-section');
    if (patterns && (resetSections === true || patterns.dataset.patterned !== String(patterned))) {
        patterns.open = patterned;
        patterns.dataset.patterned = String(patterned);
    }
    var trackSettings = document.getElementById('pad-edit-track-settings');
    var trackHost = editor && editor.track && document.getElementById(editor.track.host);
    if (trackSettings && trackHost && trackSettings.parentElement !== trackHost) trackHost.appendChild(trackSettings);
    document.getElementById('pad-edit-track-outline').style.display = editor && editor.track && editor.track.outline ? '' : 'none';
    var radius = document.getElementById('pad-edit-track-radius-wrap');
    if (radius) radius.style.display = editor && editor.track && editor.track.radius ? '' : 'none';
}

function padFillLoad(button) {
    padFillControls().forEach(function(controls) {
        controls.querySelector('select').value = padFillPattern(button[controls.dataset.patternField]);
        var color = controls.querySelector('.bc-input');
        padSetBindableColor(color.id, button[controls.dataset.colorField], '#000000');
        controls.dataset.colorSet = Object.prototype.hasOwnProperty.call(button, controls.dataset.colorField) ? '1' : '';
    });
    var spacing = padFillGeometry(button.pattern_spacing, 8, 2, 64);
    document.getElementById('pad-edit-pattern-spacing').value = spacing;
    document.getElementById('pad-edit-pattern-stroke').value = padFillGeometry(button.pattern_stroke_width, 1, 1, Math.min(8, spacing));
    document.getElementById('pad-edit-track-outline-width').value = button.widget_track_outline_width === undefined ? 0 : button.widget_track_outline_width;
    padSetBindableColor('pad-edit-track-outline-color', button.widget_track_outline_color, '#000000');
    padFillRefreshVisibility(true);
}

function padFillBuild(button, type) {
    const editor = typeof padWidgetEditor === 'function' && padWidgetEditor(type);
    var configured = false;
    padFillControls().forEach(function(controls) {
        if (controls.dataset.patternField !== 'bg_pattern' && !(editor && (editor.patterns || []).includes(controls.dataset.patternField))) return;
        padWidgetValidateField(controls.querySelector('.bc-input'), { kind: 'color', binding: {} });
        var pattern = padFillPattern(controls.querySelector('select').value);
        if (pattern !== 'none') {
            button[controls.dataset.patternField] = pattern;
            configured = true;
        }
        if (pattern !== 'none' || controls.dataset.colorSet === '1') {
            button[controls.dataset.colorField] = controls.querySelector('.bc-input').value.trim() || '#000000';
            configured = true;
        }
    });
    var spacing = padFillGeometry(document.getElementById('pad-edit-pattern-spacing').value, 8, 2, 64);
    var stroke = padFillGeometry(document.getElementById('pad-edit-pattern-stroke').value, 1, 1, Math.min(8, spacing));
    if (configured || spacing !== 8 || stroke !== 1) {
        button.pattern_spacing = spacing;
        button.pattern_stroke_width = stroke;
    }
    if (editor && editor.track && editor.track.outline) {
        if (editor) padWidgetValidateField(document.getElementById('pad-edit-track-outline-width'), { kind: 'number' });
        padWidgetValidateField(document.getElementById('pad-edit-track-outline-color'), { kind: 'color', binding: {} });
        var width = padFillGeometry(document.getElementById('pad-edit-track-outline-width').value, 0, 0, 8);
        var color = padGetBindableColor('pad-edit-track-outline-color') || '#000000';
        if (width || color !== '#000000') {
            button.widget_track_outline_width = width;
            button.widget_track_outline_color = color;
        }
    }
}