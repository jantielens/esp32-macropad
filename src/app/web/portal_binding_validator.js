/**
 * Client-side binding syntax validator.
 *
 * Architecture: scheme-agnostic orchestrator + per-scheme registry.
 * Each scheme registers itself via bindingRegisterScheme() at the
 * bottom of this file. Adding a new scheme = appending one block.
 *
 * Validates [scheme:params] tokens in binding input fields.
 * Runs on blur and before save to give users immediate feedback
 * about syntax errors without needing to deploy to the device.
 */

// ─── Scheme Registry ─────────────────────────────────────────────
//
// Each scheme registers a definition object with these optional fields:
//   validate(params, opts)  — custom validator (skips generic pipeline)
//   maxParams               — max semicolon-delimited params
//   widgetMaxParams         — max params when isWidgetBinding (no format)
//   firstParamRequired      — if true, empty first param is an error
//   firstParamLabel         — human label for first param in errors
//   keys                    — array of valid first-param values
//   keysLabel               — human label for the keys list
//   formatParam             — 0-based index of the format param (validated by generic pipeline)

var _bindingSchemeRegistry = {};

function bindingRegisterScheme(name, def) {
    _bindingSchemeRegistry[name] = def;
}

function _bindingGetScheme(name) {
    return _bindingSchemeRegistry[name.toLowerCase()] || null;
}

function _bindingSchemeNames() {
    return Object.keys(_bindingSchemeRegistry);
}

// ─── Tokenizer & Helpers ─────────────────────────────────────────

function bindingEditDistance(a, b) {
    if (a.length > 15 || b.length > 15) return 99;
    var m = a.length, n = b.length;
    var dp = [];
    for (var i = 0; i <= m; i++) {
        dp[i] = [i];
        for (var j = 1; j <= n; j++) {
            dp[i][j] = i === 0 ? j :
                Math.min(dp[i-1][j] + 1, dp[i][j-1] + 1,
                    dp[i-1][j-1] + (a[i-1] === b[j-1] ? 0 : 1));
        }
    }
    return dp[m][n];
}

/**
 * Find the closest registered scheme name to a typo.
 * Returns the suggestion if edit distance <= 2, else null.
 */
function bindingFuzzyScheme(name) {
    var names = _bindingSchemeNames();
    var best = null, bestDist = 3;
    for (var i = 0; i < names.length; i++) {
        var d = bindingEditDistance(name, names[i]);
        if (d < bestDist) { bestDist = d; best = names[i]; }
    }
    return best;
}

/**
 * Extract [scheme:params] tokens from a binding string,
 * respecting nested brackets.
 *
 * Returns array of { scheme, params, start, end, raw, unclosed }
 * where start/end are character positions in the input.
 */
function bindingTokenize(value) {
    var tokens = [];
    var i = 0;
    while (i < value.length) {
        if (value[i] !== '[') { i++; continue; }

        var schemeStart = i + 1;
        var colonPos = -1;
        for (var j = schemeStart; j < value.length; j++) {
            var c = value[j];
            if (c === ':') { colonPos = j; break; }
            if (c === ']' || c === '[') break;
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c === '_')) break;
        }
        if (colonPos === -1 || colonPos === schemeStart) { i++; continue; }

        var scheme = value.substring(schemeStart, colonPos);

        var depth = 1;
        var end = -1;
        for (var k = colonPos + 1; k < value.length; k++) {
            if (value[k] === '[') depth++;
            else if (value[k] === ']') { depth--; if (depth === 0) { end = k; break; } }
        }
        if (end === -1) {
            tokens.push({
                scheme: scheme,
                params: value.substring(colonPos + 1),
                start: i, end: value.length,
                raw: value.substring(i),
                unclosed: true
            });
            break;
        }

        tokens.push({
            scheme: scheme,
            params: value.substring(colonPos + 1, end),
            start: i, end: end + 1,
            raw: value.substring(i, end + 1),
            unclosed: false
        });
        i = end + 1;
    }
    return tokens;
}

/**
 * Strip pipe-fallback from params at bracket depth 0,
 * matching the firmware's split_pipe_fallback() behavior.
 */
function bindingSplitFallback(params) {
    var depth = 0, inQuote = false, lastPipe = -1;
    for (var i = 0; i < params.length; i++) {
        var c = params[i];
        if (c === '"' && (i === 0 || params[i-1] !== '\\')) inQuote = !inQuote;
        if (inQuote) continue;
        if (c === '[') depth++;
        else if (c === ']') depth--;
        else if (c === '|' && depth === 0) lastPipe = i;
    }
    if (lastPipe >= 0) {
        return { params: params.substring(0, lastPipe), fallback: params.substring(lastPipe + 1) };
    }
    return { params: params, fallback: null };
}

/**
 * Split params on semicolons at bracket depth 0.
 */
function bindingSplitParams(params) {
    var parts = [], depth = 0, inQuote = false, start = 0;
    for (var i = 0; i < params.length; i++) {
        var c = params[i];
        if (c === '"' && (i === 0 || params[i-1] !== '\\')) inQuote = !inQuote;
        if (inQuote) continue;
        if (c === '[') depth++;
        else if (c === ']') depth--;
        else if (c === ';' && depth === 0) {
            parts.push(params.substring(start, i));
            start = i + 1;
        }
    }
    parts.push(params.substring(start));
    return parts;
}

// ─── Format String Validator ─────────────────────────────────────

/**
 * Validate a printf-style format string.
 * Accepts common specifiers: %d %i %u %f %e %g %x %X %o %s %c %%
 * with optional flags, width, and precision.
 * Returns null if valid, or an error message.
 */
function bindingValidateFormat(fmt) {
    if (!fmt || fmt.trim() === '') return null;
    // Walk the string, find % sequences
    var i = 0;
    while (i < fmt.length) {
        if (fmt[i] !== '%') { i++; continue; }
        i++;
        if (i >= fmt.length) return 'Incomplete format specifier — trailing %';
        // %% is literal percent
        if (fmt[i] === '%') { i++; continue; }
        // optional flags: - + 0 space #
        while (i < fmt.length && '-+ 0#'.indexOf(fmt[i]) >= 0) i++;
        // optional width: digits or *
        if (i < fmt.length && fmt[i] === '*') i++;
        else while (i < fmt.length && fmt[i] >= '0' && fmt[i] <= '9') i++;
        // optional precision: .digits or .*
        if (i < fmt.length && fmt[i] === '.') {
            i++;
            if (i < fmt.length && fmt[i] === '*') i++;
            else while (i < fmt.length && fmt[i] >= '0' && fmt[i] <= '9') i++;
        }
        // optional length modifiers: l ll h hh
        if (i < fmt.length && (fmt[i] === 'l' || fmt[i] === 'h')) {
            i++;
            if (i < fmt.length && (fmt[i] === 'l' || fmt[i] === 'h')) i++;
        }
        // conversion specifier
        if (i >= fmt.length) return 'Incomplete format specifier — missing conversion character after %';
        if ('diouxXeEfFgGaAcspn'.indexOf(fmt[i]) === -1) {
            return 'Invalid format specifier "%' + fmt[i] + '"';
        }
        i++;
    }
    return null;
}

// ─── Expression Skeleton Validator ───────────────────────────────

/**
 * Validate expression syntax by replacing nested [binding:...] tokens
 * with a numeric placeholder, then checking operator/operand sequencing.
 *
 * Catches: trailing operators (1 +), leading binary ops (* 3),
 * missing operators (1 2), incomplete ternary (x ?), double operators (1 ++ 2).
 */
function bindingValidateExprSkeleton(exprBody) {
    // Replace nested [...] tokens with placeholder "1"
    var skeleton = '';
    var depth = 0;
    for (var i = 0; i < exprBody.length; i++) {
        if (exprBody[i] === '[') {
            if (depth === 0) skeleton += '1'; // placeholder for binding value
            depth++;
        } else if (exprBody[i] === ']') {
            depth--;
        } else if (depth === 0) {
            skeleton += exprBody[i];
        }
    }

    // Tokenize the skeleton into a simple token stream
    var tokens = [];
    var s = skeleton;
    var pos = 0;
    while (pos < s.length) {
        // skip whitespace
        if (s[pos] === ' ' || s[pos] === '\t' || s[pos] === '\n' || s[pos] === '\r') { pos++; continue; }
        // string literal
        if (s[pos] === '"') {
            var end = pos + 1;
            while (end < s.length && s[end] !== '"') {
                if (s[end] === '\\') end++; // skip escaped char
                end++;
            }
            tokens.push({ type: 'value', val: s.substring(pos, end + 1) });
            pos = end + 1;
            continue;
        }
        // number
        if ((s[pos] >= '0' && s[pos] <= '9') || (s[pos] === '.' && pos + 1 < s.length && s[pos+1] >= '0' && s[pos+1] <= '9')) {
            var nEnd = pos;
            while (nEnd < s.length && ((s[nEnd] >= '0' && s[nEnd] <= '9') || s[nEnd] === '.')) nEnd++;
            tokens.push({ type: 'value', val: s.substring(pos, nEnd) });
            pos = nEnd;
            continue;
        }
        // multi-char operators
        var two = s.substring(pos, pos + 2);
        if (two === '>=' || two === '<=' || two === '==' || two === '!=' || two === '&&' || two === '||') {
            tokens.push({ type: 'binop', val: two });
            pos += 2;
            continue;
        }
        // single-char operators and punctuation
        var ch = s[pos];
        if (ch === '+' || ch === '-') {
            // Could be unary or binary — classify based on context
            tokens.push({ type: 'plusminus', val: ch });
            pos++;
            continue;
        }
        if (ch === '*' || ch === '/' || ch === '%' || ch === '>' || ch === '<') {
            tokens.push({ type: 'binop', val: ch });
            pos++;
            continue;
        }
        if (ch === '!') {
            tokens.push({ type: 'unary', val: ch });
            pos++;
            continue;
        }
        if (ch === '?') { tokens.push({ type: '?', val: '?' }); pos++; continue; }
        if (ch === ':') { tokens.push({ type: ':', val: ':' }); pos++; continue; }
        if (ch === '(') { tokens.push({ type: '(', val: '(' }); pos++; continue; }
        if (ch === ')') { tokens.push({ type: ')', val: ')' }); pos++; continue; }
        if (ch === ',') { tokens.push({ type: ',', val: ',' }); pos++; continue; }
        // identifier (function name like "threshold")
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch === '_') {
            var iEnd = pos;
            while (iEnd < s.length && ((s[iEnd] >= 'a' && s[iEnd] <= 'z') || (s[iEnd] >= 'A' && s[iEnd] <= 'Z') ||
                   (s[iEnd] >= '0' && s[iEnd] <= '9') || s[iEnd] === '_')) iEnd++;
            tokens.push({ type: 'ident', val: s.substring(pos, iEnd) });
            pos = iEnd;
            continue;
        }
        // unknown character — skip
        pos++;
    }

    if (tokens.length === 0) return null; // empty handled elsewhere

    // Walk tokens checking operator/operand sequencing
    // expectValue = true means we expect a value (or unary), false means we expect an operator
    var expectValue = true;
    var ternaryDepth = 0;
    for (var t = 0; t < tokens.length; t++) {
        var tok = tokens[t];

        if (tok.type === '(' || tok.type === 'unary') {
            if (!expectValue) return 'Missing operator before "' + tok.val + '"';
            // stay in expectValue
            continue;
        }

        if (tok.type === ')') {
            if (expectValue) return 'Empty or incomplete expression before ")"';
            // stay in !expectValue
            continue;
        }

        if (tok.type === ',') {
            if (expectValue) return 'Missing value before ","';
            expectValue = true;
            continue;
        }

        if (tok.type === 'plusminus') {
            if (expectValue) {
                // unary — stay in expectValue
                continue;
            }
            // binary — expect value next
            expectValue = true;
            continue;
        }

        if (tok.type === 'binop') {
            if (expectValue) return 'Unexpected operator "' + tok.val + '" — missing value before it';
            expectValue = true;
            continue;
        }

        if (tok.type === '?') {
            if (expectValue) return 'Missing condition before "?"';
            ternaryDepth++;
            expectValue = true;
            continue;
        }

        if (tok.type === ':') {
            if (expectValue) return 'Missing value before ":" in ternary';
            if (ternaryDepth <= 0) return 'Unexpected ":" without matching "?"';
            ternaryDepth--;
            expectValue = true;
            continue;
        }

        if (tok.type === 'ident') {
            // function name — check if followed by (
            if (t + 1 < tokens.length && tokens[t+1].type === '(') {
                if (!expectValue) return 'Missing operator before "' + tok.val + '()"';
                t++; // consume the (
                expectValue = true; // expect first argument
                continue;
            }
            // bare identifier — treat as a value (could be future extension)
            if (!expectValue) return 'Missing operator between values';
            expectValue = false;
            continue;
        }

        if (tok.type === 'value') {
            if (!expectValue) return 'Missing operator between values';
            expectValue = false;
            continue;
        }
    }

    if (expectValue) {
        // Find the last meaningful token for a better error message
        var lastTok = tokens[tokens.length - 1];
        return 'Incomplete expression — missing value after "' + lastTok.val + '"';
    }

    if (ternaryDepth > 0) return 'Incomplete ternary — missing ":" branch';

    return null;
}

// ─── Validation Orchestrator ─────────────────────────────────────

/**
 * Validate a single token against the scheme registry.
 * Returns null if valid, or an error message string.
 *
 * Schemes with a custom validate() bypass the generic pipeline.
 * Unregistered schemes: fuzzy match → error; no match → pass silently
 * (may be a future scheme the validator doesn't know about yet).
 */
function bindingValidateToken(token, opts) {
    if (token.unclosed) return 'Unclosed bracket — missing ]';

    var scheme = token.scheme.toLowerCase();
    var def = _bindingGetScheme(scheme);

    if (!def) {
        var suggestion = bindingFuzzyScheme(scheme);
        if (suggestion) return 'Unknown scheme "' + token.scheme + '" — did you mean "' + suggestion + '"?';
        if (opts && opts.requireKnownScheme) return 'Unknown scheme "' + token.scheme + '"';
        return null; // unrecognised scheme — not validated
    }

    var split = bindingSplitFallback(token.params);
    var params = split.params;

    // Custom validator overrides the generic pipeline
    if (def.validate) {
        return def.validate(params, opts || {});
    }

    // ── Generic validation pipeline ──
    var parts = bindingSplitParams(params);

    if (def.firstParamRequired && parts[0].trim() === '') {
        return (def.firstParamLabel || 'Parameter') + ' is empty';
    }

    if (def.keys && parts[0].trim() !== '' && def.keys.indexOf(parts[0].trim()) === -1) {
        return 'Unknown ' + (def.keysLabel || 'key') + ' "' + parts[0].trim() + '"';
    }

    if (typeof def.maxParams === 'number' && parts.length > def.maxParams) {
        return 'Too many parameters — expected at most ' + def.maxParams;
    }

    if (opts && opts.isWidgetBinding &&
        typeof def.widgetMaxParams === 'number' && parts.length > def.widgetMaxParams) {
        return 'Widget data bindings must not include a format parameter';
    }

    // Validate format string at the declared position
    if (typeof def.formatParam === 'number' && parts.length > def.formatParam) {
        var fmt = parts[def.formatParam].trim();
        if (fmt !== '') {
            var fmtErr = bindingValidateFormat(fmt);
            if (fmtErr) return fmtErr;
        }
    }

    return null;
}

/**
 * Validate a binding string. Returns { valid, errors }.
 * errors is an array of { message, start, end }.
 */
function validateBinding(value, opts) {
    if (!value || typeof value !== 'string') return { valid: true, errors: [] };

    var trimmed = value.trim();
    if (trimmed === '' || trimmed.indexOf('[') === -1) return { valid: true, errors: [] };

    var errors = [];

    // Global bracket balance
    var depth = 0;
    for (var i = 0; i < trimmed.length; i++) {
        if (trimmed[i] === '[') depth++;
        else if (trimmed[i] === ']') depth--;
        if (depth < 0) {
            errors.push({ message: 'Unexpected ] at position ' + i, start: i, end: i + 1 });
            depth = 0;
        }
    }
    if (depth > 0) {
        errors.push({ message: 'Unclosed [ — missing ' + depth + ' closing bracket' + (depth > 1 ? 's' : '') });
    }

    // Tokenize and validate each token via registry
    var tokens = bindingTokenize(trimmed);
    for (var t = 0; t < tokens.length; t++) {
        var err = bindingValidateToken(tokens[t], opts || {});
        if (err) {
            errors.push({ message: err, start: tokens[t].start, end: tokens[t].end });
        }
    }

    return { valid: errors.length === 0, errors: errors };
}

// ─── Inline error display ────────────────────────────────────────

/**
 * Find the block-level container to hold the error message for a given input.
 * Returns { container, anchor } where the error span is appended after anchor
 * inside container. When anchor is null the span is appended as last child.
 */
function _bvErrorTarget(input) {
    var anchor = input.closest('.bindable-color') ||
                 input.closest('.binding-input-wrap') ||
                 input.closest('.label-style-wrap') ||
                 input;
    // Prefer the nearest block-level .form-group so the error never becomes
    // a flex sibling in a horizontal row.
    var fg = anchor.closest('.form-group');
    if (fg) return { container: fg, anchor: null };
    // Fallback: insert after the anchor in its parent (works when parent is block)
    return { container: anchor.parentNode, anchor: anchor };
}

/**
 * Show a binding error message below an input element.
 */
function bindingShowError(input, message) {
    bindingClearError(input);
    input.classList.add('binding-error');
    var span = document.createElement('span');
    span.className = 'binding-error-msg';
    span.textContent = message;
    // Tag with input id so clearError can find the right span when
    // multiple binding inputs share the same .form-group container.
    var uid = input.id || input.name || '';
    if (uid) span.dataset.bvFor = uid;
    var t = _bvErrorTarget(input);
    if (t.anchor) {
        t.container.insertBefore(span, t.anchor.nextSibling);
    } else {
        t.container.appendChild(span);
    }
}

/**
 * Clear binding error from an input element.
 */
function bindingClearError(input) {
    input.classList.remove('binding-error');
    var t = _bvErrorTarget(input);
    var uid = input.id || input.name || '';
    var msg = uid
        ? t.container.querySelector(':scope > .binding-error-msg[data-bv-for="' + uid + '"]')
        : t.container.querySelector(':scope > .binding-error-msg');
    if (msg) msg.remove();
}

/**
 * Validate a single input element and show/clear inline error.
 * Returns true if valid, false if errors found.
 */
function bindingValidateInput(input, opts) {
    var value = input.value;
    var result = validateBinding(value, opts);
    if (!result.valid) {
        bindingShowError(input, result.errors[0].message);
        return false;
    }
    bindingClearError(input);
    return true;
}

/**
 * Attach live validation to an input element.
 * Validates on blur immediately, and while typing with a debounce.
 */
function bindingAttachValidation(input, opts) {
    var debounceTimer = null;
    input.addEventListener('blur', function() {
        if (debounceTimer) { clearTimeout(debounceTimer); debounceTimer = null; }
        bindingValidateInput(input, opts);
    });
    input.addEventListener('input', function() {
        if (debounceTimer) clearTimeout(debounceTimer);
        debounceTimer = setTimeout(function() {
            debounceTimer = null;
            bindingValidateInput(input, opts);
        }, 400);
    });
}

// ─── Shared field ID groups ──────────────────────────────────────

var _BV_STD = [
    'pad-edit-label-top', 'pad-edit-label-center', 'pad-edit-label-bottom',
    'pad-edit-bg-color', 'pad-edit-fg-color', 'pad-edit-border-color',
    'pad-edit-border-width', 'pad-edit-corner-radius', 'pad-edit-btn-state'
];
var _BV_DEF = [
    'pad-def-bg-color', 'pad-def-fg-color', 'pad-def-border-color',
    'pad-def-border-width', 'pad-def-corner-radius'
];
var _BV_GAUGE_DATA = [
    'pad-edit-gauge-data-binding', 'pad-edit-gauge-data-binding-2',
    'pad-edit-gauge-data-binding-3', 'pad-edit-gauge-data-binding-4'
];
var _BV_GAUGE_COLOR = [
    'pad-edit-gauge-arc-color', 'pad-edit-gauge-arc-color-2',
    'pad-edit-gauge-arc-color-3', 'pad-edit-gauge-arc-color-4',
    'pad-edit-gauge-track-color', 'pad-edit-gauge-needle-color', 'pad-edit-gauge-tick-color',
    'pad-edit-gauge-marker-tick-color', 'pad-edit-gauge-marker-zone-color'
];
var _BV_GAUGE_NUM = ['pad-edit-gauge-min', 'pad-edit-gauge-max', 'pad-edit-gauge-marker-value'];
var _BV_SPARK_DATA = [
    'pad-edit-sparkline-data-binding', 'pad-edit-sparkline-data-binding-2',
    'pad-edit-sparkline-data-binding-3'
];
var _BV_SPARK_COLOR = [
    'pad-edit-sparkline-line-color', 'pad-edit-sparkline-line-color-2',
    'pad-edit-sparkline-line-color-3',
    'pad-edit-sparkline-max-label-color', 'pad-edit-sparkline-min-label-color',
    'pad-edit-sparkline-ref-1-color', 'pad-edit-sparkline-ref-2-color', 'pad-edit-sparkline-ref-3-color'
];
var _BV_SPARK_NUM = ['pad-edit-sparkline-min', 'pad-edit-sparkline-max'];
var _BV_BAR_COLOR = ['pad-edit-widget-bar-color', 'pad-edit-widget-bar-bg-color'];
var _BV_BAR_NUM = ['pad-edit-widget-bar-min', 'pad-edit-widget-bar-max'];
var _BV_GAUGE_LABEL = [
    'pad-edit-gauge-start-label', 'pad-edit-gauge-start-label-2',
    'pad-edit-gauge-start-label-3', 'pad-edit-gauge-start-label-4'
];
var _BV_TABLE_DATA = ['pad-edit-table-data-binding'];

function _bvAttach(ids, opts) {
    for (var i = 0; i < ids.length; i++) {
        var el = document.getElementById(ids[i]);
        if (el) bindingAttachValidation(el, opts);
    }
}

function _bvCount(ids, opts, skipEmpty) {
    var c = 0;
    for (var i = 0; i < ids.length; i++) {
        var el = document.getElementById(ids[i]);
        if (!el) continue;
        if (skipEmpty && !el.value.trim()) continue;
        if (!bindingValidateInput(el, opts)) c++;
    }
    return c;
}

/**
 * Attach live validation to all static binding inputs
 * that exist in the DOM at page load. Call once from page init.
 */
function bindingInitStaticInputs() {
    _bvAttach(_BV_STD);
    _bvAttach(_BV_DEF);

    var pageBg = document.getElementById('pad-edit-page-bg-color');
    if (pageBg) bindingAttachValidation(pageBg);

    var wdb = document.getElementById('pad-edit-widget-data-binding');
    if (wdb) bindingAttachValidation(wdb, { isWidgetBinding: true });

    _bvAttach(_BV_GAUGE_DATA, { isWidgetBinding: true });
    _bvAttach(_BV_GAUGE_COLOR);
    _bvAttach(_BV_GAUGE_NUM, { isWidgetBinding: true });
    _bvAttach(_BV_SPARK_DATA, { isWidgetBinding: true });
    _bvAttach(_BV_SPARK_COLOR);
    _bvAttach(_BV_SPARK_NUM, { isWidgetBinding: true });
    _bvAttach(_BV_BAR_COLOR);
    _bvAttach(_BV_BAR_NUM, { isWidgetBinding: true });
    _bvAttach(_BV_GAUGE_LABEL);
    _bvAttach(_BV_TABLE_DATA, { isWidgetBinding: true });

    var wakeEl = document.getElementById('screen_saver_wake_binding');
    if (wakeEl) bindingAttachValidation(wakeEl);
}

/**
 * Validate all binding fields currently in the button editor dialog.
 * Returns { valid, count } where count is the number of errors found.
 */
function bindingValidateDialog() {
    var count = _bvCount(_BV_STD);

    var wtype = document.getElementById('pad-edit-widget-type');
    var activeType = wtype ? wtype.value : '';

    if (activeType === 'bar_chart') {
        var wdb = document.getElementById('pad-edit-widget-data-binding');
        if (wdb && wdb.value.trim() && !bindingValidateInput(wdb, { isWidgetBinding: true })) count++;
    }

    if (activeType === 'gauge') {
        count += _bvCount(_BV_GAUGE_DATA, { isWidgetBinding: true }, true);
        count += _bvCount(_BV_GAUGE_COLOR, null, true);
        count += _bvCount(_BV_GAUGE_NUM, { isWidgetBinding: true }, true);
        count += _bvCount(_BV_GAUGE_LABEL, null, true);
    }
    if (activeType === 'sparkline') {
        count += _bvCount(_BV_SPARK_DATA, { isWidgetBinding: true }, true);
        count += _bvCount(_BV_SPARK_COLOR, null, true);
        count += _bvCount(_BV_SPARK_NUM, { isWidgetBinding: true }, true);
    }
    if (activeType === 'bar_chart') {
        count += _bvCount(_BV_BAR_COLOR, null, true);
        count += _bvCount(_BV_BAR_NUM, { isWidgetBinding: true }, true);
    }
    if (activeType === 'table') {
        count += _bvCount(_BV_TABLE_DATA, { isWidgetBinding: true }, true);
    }

    // Validate binding-capable action fields (tap, long-press, numeric rocker adjustment)
    var actionPrefixes = [
        'pad-edit-action-0', 'pad-edit-action-1', 'pad-edit-action-2',
        'pad-edit-lp-action-0', 'pad-edit-lp-action-1', 'pad-edit-lp-action-2',
        'pad-edit-nr-adjust'
    ];
    var actionSuffixes = (typeof _ACTION_BIND_SUFFIXES !== 'undefined') ? _ACTION_BIND_SUFFIXES : [
        '-notify-text', '-notify-duration', '-topic', '-payload', '-sequence',
        '-beep-pattern', '-timer-set-sec', '-timer-adjust-sec'
    ];
    for (var ai = 0; ai < actionPrefixes.length; ai++) {
        for (var si = 0; si < actionSuffixes.length; si++) {
            var el = document.getElementById(actionPrefixes[ai] + actionSuffixes[si]);
            if (el && el.value.trim() && !bindingValidateInput(el)) count++;
        }
    }

    return { valid: count === 0, count: count };
}

/**
 * Validate pad-level named binding values.
 * Call before saving the pad.
 * Returns { valid, count }.
 */
function bindingValidatePadBindings() {
    var count = 0;
    var valueInputs = document.querySelectorAll('#pad-bindings-list .pad-binding-value');
    valueInputs.forEach(function(input) {
        if (input.value.trim() && !bindingValidateInput(input)) count++;
    });
    return { valid: count === 0, count: count };
}

/**
 * Validate pad-level defaults binding fields.
 * Returns { valid, count }.
 */
function bindingValidateDefaults() {
    var count = _bvCount(_BV_DEF, null, true);
    return { valid: count === 0, count: count };
}

/**
 * Validate the home page wake binding field.
 * Returns { valid, count }.
 */
function bindingValidateHomeConfig() {
    var count = 0;
    var el = document.getElementById('screen_saver_wake_binding');
    if (el && el.value.trim() && !bindingValidateInput(el)) count++;
    return { valid: count === 0, count: count };
}

// ═════════════════════════════════════════════════════════════════
// Scheme Registrations
//
// Each block below is self-contained. To add a new binding scheme,
// append a new bindingRegisterScheme() call — no other code changes
// needed. The orchestrator validates unknown schemes gracefully.
// ═════════════════════════════════════════════════════════════════

// ─── Scheme: mqtt ────────────────────────────────────────────────
// [mqtt:topic;path;format]
bindingRegisterScheme('mqtt', {
    maxParams: 3,
    widgetMaxParams: 2,
    firstParamRequired: true,
    firstParamLabel: 'MQTT topic',
    formatParam: 2
});

// ─── Scheme: health ──────────────────────────────────────────────
// [health:key;format]
bindingRegisterScheme('health', {
    maxParams: 2,
    widgetMaxParams: 1,
    firstParamRequired: true,
    firstParamLabel: 'Health key',
    keysLabel: 'health key',
    formatParam: 1,
    keys: [
        'cpu', 'rssi', 'uptime',
        'heap_free', 'heap_min', 'heap_largest', 'heap_internal',
        'psram_free', 'psram_min', 'psram_largest',
        'heap_total', 'heap_internal_total', 'heap_internal_used',
        'psram_total', 'psram_used',
        'table', 'extended_table',
        'chip', 'chip_rev', 'chip_cores', 'cpu_freq', 'flash_size',
        'firmware', 'board', 'mac', 'reset_reason',
        'wifi_connected', 'wifi_ssid', 'ip', 'hostname',
        'brightness', 'volume',
        'ble_status', 'ble_state', 'ble_name', 'ble_pairing',
        'ble_bonded', 'ble_encrypted', 'ble_peer_addr', 'ble_peer_id_addr'
    ]
});

// ─── Scheme: net ─────────────────────────────────────────────────
// [net:channel]  or  [net:channel;age]
bindingRegisterScheme('net', {
    firstParamRequired: true,
    firstParamLabel: 'Network channel',
    keysLabel: 'network channel',
    keys: [
        'portal', 'mcp', 'mqtt_rx', 'mqtt_tx', 'mqtt', 'http', 'ble', 'ota', 'any'
    ],
    validate: function(params, opts) {
        var parts = bindingSplitParams(params);
        var chan = (parts[0] || '').trim();
        if (chan === '') return 'Network channel is empty';
        var valid = ['portal', 'mcp', 'mqtt_rx', 'mqtt_tx', 'mqtt', 'http', 'ble', 'ota', 'any'];
        if (valid.indexOf(chan) === -1) {
            return 'Unknown network channel "' + chan + '" — expected one of ' + valid.join(', ');
        }
        if (parts.length > 1) {
            var sub = (parts[1] || '').trim();
            if (sub !== '' && sub !== 'age') {
                return 'Second parameter must be "age" or omitted';
            }
        }
        if (parts.length > 2) return 'Too many parameters for net binding';
        return null;
    }
});

// ─── Scheme: time ────────────────────────────────────────────────
// [time:format;timezone]
bindingRegisterScheme('time', {
    maxParams: 2,
    firstParamRequired: true,
    firstParamLabel: 'Time format'
});

// ─── Scheme: timer ───────────────────────────────────────────────
// [timer:N;format] or [timer:N_state|N_expired|N_mode|N_target]
bindingRegisterScheme('timer', {
    validate: function(params, opts) {
        var parts = bindingSplitParams(params);
        var param = parts[0].trim();
        if (param === '') return 'Timer parameter is empty';
        var m = param.match(/^(\d+)(_.+)?$/);
        if (!m) return 'Invalid timer parameter "' + param + '" — expected N or N_state/N_expired/N_mode/N_target';
        var num = parseInt(m[1]);
        if (num < 1 || num > 3) return 'Timer number must be 1–3, got ' + num;
        var suffix = m[2] || '';
        if (suffix && ['_state', '_expired', '_mode', '_target'].indexOf(suffix) === -1) {
            return 'Unknown timer suffix "' + suffix + '" — valid: _state, _expired, _mode, _target';
        }
        if (parts.length > 2) return 'Too many parameters for timer binding';
        if (opts && opts.isWidgetBinding && parts.length > 1) {
            var fmt = parts[1].trim();
            var numericFormats = ['ss'];
            if (numericFormats.indexOf(fmt) === -1) {
                return 'Widget data bindings only allow numeric formats (none or "ss"), got "' + fmt + '"';
            }
        }
        return null;
    }
});

// ─── Scheme: music ───────────────────────────────────────────────
// [music:key], including optional compile-time Music analysis keys.
bindingRegisterScheme('music', {
    validate: function(params) {
        var key = bindingSplitParams(params)[0].trim();
        var base = ['file', 'file_name', 'title', 'artist', 'album', 'track',
            'index', 'count', 'elapsed_s', 'total_s', 'status'];
        if (base.indexOf(key) !== -1) return null;
        if (/^analysis\.(rms|peak)$/.test(key)) return null;
        var match = key.match(/^analysis\.band\.(\d+)$/);
        if (match) {
            var band = parseInt(match[1]);
            return band >= 0 && band < 8 ? null : 'Music analysis band must be 0 through 7';
        }
        return 'Unknown music key';
    }
});

// ─── Scheme: pad ─────────────────────────────────────────────────
// [pad:name;format]
bindingRegisterScheme('pad', {
    maxParams: 2,
    widgetMaxParams: 1,
    firstParamRequired: true,
    firstParamLabel: 'Pad binding name',
    formatParam: 1
});

// ─── Scheme: expr ────────────────────────────────────────────────
// [expr:expression;format]
// Custom validator: structural checks + recursive nested token validation.
bindingRegisterScheme('expr', {
    validate: function(params, opts) {
        // Find the last ; at depth 0 (format separator)
        var depth = 0, inQuote = false, lastSemiAt0 = -1;
        for (var i = 0; i < params.length; i++) {
            var c = params[i];
            if (c === '"' && (i === 0 || params[i-1] !== '\\')) inQuote = !inQuote;
            if (inQuote) continue;
            if (c === '[') depth++;
            else if (c === ']') depth--;
            else if (c === ';' && depth === 0) lastSemiAt0 = i;
        }

        var exprBody = lastSemiAt0 >= 0 ? params.substring(0, lastSemiAt0) : params;

        // Check balanced parentheses (skip nested [...] tokens)
        var parenDepth = 0;
        inQuote = false; depth = 0;
        for (var j = 0; j < exprBody.length; j++) {
            var ch = exprBody[j];
            if (ch === '"' && (j === 0 || exprBody[j-1] !== '\\')) inQuote = !inQuote;
            if (inQuote) continue;
            if (ch === '[') depth++;
            else if (ch === ']') depth--;
            if (depth > 0) continue;
            if (ch === '(') parenDepth++;
            else if (ch === ')') { parenDepth--; if (parenDepth < 0) return 'Unmatched ) in expression'; }
        }
        if (parenDepth > 0) return 'Unmatched ( in expression — missing )';
        if (inQuote) return 'Unclosed string quote in expression';
        if (exprBody.trim() === '') return 'Expression body is empty';

        // Recursively validate nested binding tokens
        var nested = bindingTokenize(exprBody);
        for (var t = 0; t < nested.length; t++) {
            var err = bindingValidateToken(nested[t], {});
            if (err) return 'In nested ' + nested[t].raw + ': ' + err;
        }

        // Validate expression syntax (operator/operand sequencing)
        var skelErr = bindingValidateExprSkeleton(exprBody);
        if (skelErr) return skelErr;

        // Validate format if present
        if (lastSemiAt0 >= 0) {
            var exprFmt = params.substring(lastSemiAt0 + 1).trim();
            if (exprFmt !== '') {
                var fmtErr = bindingValidateFormat(exprFmt);
                if (fmtErr) return fmtErr;
            }
        }

        return null;
    }
});
