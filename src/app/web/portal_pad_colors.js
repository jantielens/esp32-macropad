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

function padIsBinding(val) {
    return typeof val === 'string' && val.startsWith('[');
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

/** Singleton popover state. */
var _cpPopover = null;

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
    pop.innerHTML =
        '<div class="color-popover-close" id="cp-close">&times;</div>' +
        '<div class="color-popover-section" id="cp-recent-heading" style="display:none;">Recently Used</div>' +
        '<div class="color-popover-grid" id="cp-recent-grid"></div>' +
        '<div class="color-popover-section">Palette</div>' +
        '<div class="color-popover-grid" id="cp-palette-grid"></div>' +
        '<details class="cp-gen" id="cp-gen">' +
            '<summary class="color-popover-section" style="cursor:pointer;list-style:none;margin-bottom:0;">' +
                '<span class="cp-gen-arrow">&#9654;</span> Generate Color by Threshold' +
            '</summary>' +
            '<div class="cp-gen-body">' +
                '<label class="cp-gen-label">Data Source</label>' +
                '<input type="text" id="cp-gen-source" class="cp-gen-input" maxlength="191" placeholder="[mqtt:topic;$.path]" spellcheck="false">' +
                '<label class="cp-gen-label">Color Stops</label>' +
                '<div id="cp-gen-stops"></div>' +
            '</div>' +
        '</details>' +
        '<div class="color-popover-input-row">' +
            '<input type="text" id="cp-input" maxlength="191" placeholder="#RRGGBB or [expr:…]" spellcheck="false">' +
            '<button type="button" class="btn btn-small btn-primary" id="cp-apply">Apply</button>' +
        '</div>';
    document.body.appendChild(pop);
    // Build palette grid (static)
    var pg = pop.querySelector('#cp-palette-grid');
    PAD_COLOR_PALETTE.forEach(function(hex) {
        var sw = document.createElement('div');
        sw.className = 'color-popover-swatch';
        sw.style.background = hex;
        sw.title = hex;
        sw.dataset.hex = hex;
        sw.addEventListener('click', function() { padColorPopoverApply(hex); });
        pg.appendChild(sw);
    });
    // Unified input apply
    pop.querySelector('#cp-apply').addEventListener('click', function() {
        var v = pop.querySelector('#cp-input').value.trim();
        if (v) padColorPopoverApply(v);
    });
    pop.querySelector('#cp-input').addEventListener('keydown', function(e) {
        if (e.key === 'Enter') { pop.querySelector('#cp-apply').click(); e.preventDefault(); }
    });
    // Close button
    pop.querySelector('#cp-close').addEventListener('click', padColorPopoverClose);
    // Escape to close
    pop.addEventListener('keydown', function(e) {
        if (e.key === 'Escape') padColorPopoverClose();
    });
    // Generator: toggle arrow on open/close + reposition popover
    var genDetails = pop.querySelector('#cp-gen');
    genDetails.addEventListener('toggle', function() {
        genDetails.querySelector('.cp-gen-arrow').textContent = genDetails.open ? '\u25BC' : '\u25B6';
        padColorPopoverReposition();
    });
    // Generator: auto-generate on source changes
    pop.querySelector('#cp-gen-source').addEventListener('input', padThresholdGenerate);
    _cpPopover = { pop: pop, bd: bd, target: null, anchor: null };
    return _cpPopover;
}

/** Open the color popover anchored to a swatch element, targeting a specific input. */
function padColorPopoverOpen(swatch, input) {
    var cp = padColorPopoverCreate();
    cp.target = input;
    cp.anchor = swatch;
    // Reset generator state
    padThresholdGenReset();
    // Show off-screen first so we can measure
    cp.pop.style.left = '-9999px';
    cp.pop.style.top = '-9999px';
    cp.pop.style.display = 'block';
    cp.bd.style.display = 'block';
    // Populate recently used (before measuring)
    var recentGrid = cp.pop.querySelector('#cp-recent-grid');
    var recentHeading = cp.pop.querySelector('#cp-recent-heading');
    recentGrid.innerHTML = '';
    var usedColors = padCollectUsedColors(padState.editCol || -1, padState.editRow || -1);
    if (usedColors.length > 0) {
        recentHeading.style.display = '';
        usedColors.forEach(function(hex) {
            if (!hex.startsWith('#')) return; // skip bindings in used list
            var sw = document.createElement('div');
            sw.className = 'color-popover-swatch';
            sw.style.background = hex;
            sw.title = hex;
            sw.addEventListener('click', function() { padColorPopoverApply(hex); });
            recentGrid.appendChild(sw);
        });
        if (recentGrid.children.length === 0) recentHeading.style.display = 'none';
    } else {
        recentHeading.style.display = 'none';
    }
    // Highlight active color in palette & recent
    var cur = input.value.trim().toUpperCase();
    cp.pop.querySelectorAll('.color-popover-swatch').forEach(function(sw) {
        sw.classList.toggle('active', (sw.dataset.hex || sw.title || '').toUpperCase() === cur);
    });
    // Set input to current value
    cp.pop.querySelector('#cp-input').value = input.value.trim();
    cp.bd.style.display = 'block';
    cp.pop.style.display = 'block';
    padColorPopoverReposition();
    cp.pop.querySelector('#cp-input').focus();
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

var CP_GEN_DEFAULT_COLORS = ['#4CAF50', '#8BC34A', '#FF9800', '#F44336'];

/** Add one color-stop row to the generator. */
function padThresholdGenAddStop(container, color, threshold) {
    var row = document.createElement('div');
    row.className = 'cp-gen-stop';
    var isFirst = container.children.length === 0;
    row.innerHTML =
        '<div class="cp-gen-swatch" style="background:' + (color || '#888') + '"></div>' +
        '<input type="color" class="cp-gen-cpick" value="' + (color || '#888888') + '">' +
        (isFirst
            ? '<span class="cp-gen-base-label">Base</span>'
            : '<span class="cp-gen-ge">&ge;</span><input type="text" class="cp-gen-tval" placeholder="" value="' + (threshold || '') + '">');
    var swatch = row.querySelector('.cp-gen-swatch');
    var cpick = row.querySelector('.cp-gen-cpick');
    cpick.addEventListener('input', function() { swatch.style.background = cpick.value; padThresholdGenerate(); });
    swatch.addEventListener('click', function() { cpick.click(); });
    var tval = row.querySelector('.cp-gen-tval');
    if (tval) tval.addEventListener('input', padThresholdGenerate);
    container.appendChild(row);
}

/** Reset the generator to default state. */
function padThresholdGenReset() {
    if (!_cpPopover) return;
    var pop = _cpPopover.pop;
    var gen = pop.querySelector('#cp-gen');
    gen.removeAttribute('open');
    gen.querySelector('.cp-gen-arrow').textContent = '\u25B6';
    pop.querySelector('#cp-gen-source').value = '';
    var stops = pop.querySelector('#cp-gen-stops');
    stops.innerHTML = '';
    CP_GEN_DEFAULT_COLORS.forEach(function(c) { padThresholdGenAddStop(stops, c, ''); });
}

/** Build threshold expression from generator inputs and write to #cp-input. */
function padThresholdGenerate() {
    if (!_cpPopover) return;
    var pop = _cpPopover.pop;
    var src = pop.querySelector('#cp-gen-source').value.trim();
    if (!src) return;
    var rows = pop.querySelectorAll('#cp-gen-stops .cp-gen-stop');
    var colors = [];
    var thresholds = [];
    for (var i = 0; i < rows.length; i++) {
        colors.push(rows[i].querySelector('.cp-gen-cpick').value.toUpperCase());
        if (i > 0) {
            var tInput = rows[i].querySelector('.cp-gen-tval');
            thresholds.push(tInput ? tInput.value.trim() : '');
        }
    }
    // Auto-fill empty thresholds with even spacing over 0-100
    var allEmpty = thresholds.every(function(v) { return v === ''; });
    if (allEmpty && thresholds.length > 0) {
        var step = 100 / colors.length;
        for (var j = 0; j < thresholds.length; j++) {
            thresholds[j] = String(Math.round(step * (j + 1)));
        }
    }
    // Build expression: threshold(src, "#c0", t1, "#c1", ...)
    var parts = '[expr:threshold(' + src + ', "' + colors[0] + '"';
    for (var k = 0; k < thresholds.length; k++) {
        parts += ', ' + thresholds[k] + ', "' + colors[k + 1] + '"';
    }
    parts += ')]';
    pop.querySelector('#cp-input').value = parts;
}

/** Apply a value from the popover to the target input. */
function padColorPopoverApply(val) {
    if (!_cpPopover || !_cpPopover.target) return;
    _cpPopover.target.value = val;
    _cpPopover.target.dispatchEvent(new Event('input', { bubbles: true }));
    padColorPopoverClose();
}

/** Close the popover. */
function padColorPopoverClose() {
    if (!_cpPopover) return;
    _cpPopover.pop.style.display = 'none';
    _cpPopover.bd.style.display = 'none';
    _cpPopover.target = null;
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
    }

    input.addEventListener('input', syncSwatch);
    swatch.addEventListener('click', function() {
        padColorPopoverOpen(swatch, input);
    });
    syncSwatch();
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

/**
 * Toggle monospace font on mixed-binding inputs based on whether they contain a binding token.
 */
function padUpdateMixedBindingFont(input) {
    if (input.value.indexOf('[') !== -1) {
        input.classList.add('has-binding');
    } else {
        input.classList.remove('has-binding');
    }
}