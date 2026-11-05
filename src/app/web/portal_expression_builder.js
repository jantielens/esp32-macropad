function expressionBuilderEligible(field) {
    if (!field || field.disabled || field.readOnly) return false;
    return field.tagName === 'TEXTAREA' ||
        (field.tagName === 'INPUT' && ['text', 'search', ''].indexOf(field.type || '') >= 0);
}

const EXPRESSION_BUILDER_FUNCTIONS = [
    { label: 'abs(x)', text: 'abs(value)', operand: 'value' },
    { label: 'min(a, b)', text: 'min(value, other)', operand: 'value' },
    { label: 'max(a, b)', text: 'max(value, other)', operand: 'value' },
    { label: 'clamp(x, low, high)', text: 'clamp(\n  value,\n  minimum,\n  maximum\n)', operand: 'value' },
    { label: 'round(x)', text: 'round(value)', operand: 'value' },
    { label: 'floor(x)', text: 'floor(value)', operand: 'value' },
    { label: 'ceil(x)', text: 'ceil(value)', operand: 'value' },
    { label: 'threshold(value, below, boundary, above)', text: 'threshold(value, below, boundary, above)', operand: 'value' },
    { label: 'if / then / else', text: 'condition\n  ? then_value\n  : else_value', operand: 'condition' }
];

const EXPRESSION_BUILDER_PLACEHOLDERS = new Set([
    'value', 'other', 'minimum', 'maximum', 'below', 'boundary', 'above',
    'condition', 'then_value', 'else_value'
]);

function expressionBuilderUnsupported(reason) {
    return {
        supported: false,
        expression: '',
        formatting: { enabled: false, decimals: 1, unit: '' },
        error: reason || 'The existing field value must contain exactly one complete expression to edit it here.'
    };
}

function expressionBuilderEmpty() {
    return {
        supported: true,
        expression: '',
        formatting: { enabled: false, decimals: 1, unit: '' },
        error: ''
    };
}

function expressionBuilderDecodeFormat(format) {
    const match = /^%\.(\d)f(?: (.+))?$/.exec(format);
    if (!match) return null;
    let unit = '';
    const encoded = match[2] || '';
    for (let i = 0; i < encoded.length; i++) {
        if (encoded[i] !== '%') {
            unit += encoded[i];
            continue;
        }
        if (encoded[i + 1] !== '%') return null;
        unit += '%';
        i++;
    }
    const decimals = Number(match[1]);
    try {
        if (bindingBuilderFormat('number', decimals, bindingBuilderUnitSuffix(unit)) !== format) return null;
    } catch (_) {
        return null;
    }
    return { enabled: true, decimals, unit };
}

function expressionBuilderLoad(value, validationOptions = {}) {
    const text = String(value == null ? '' : value);
    const formatting = { enabled: false, decimals: 1, unit: '' };
    if (!text.trim()) return expressionBuilderEmpty();
    const tokens = bindingTokenize(text);
    if (!tokens.length) return expressionBuilderEmpty();
    if (tokens.length === 1 && tokens[0].raw === text.trim() &&
        !tokens[0].unclosed && tokens[0].scheme.toLowerCase() !== 'expr') {
        return expressionBuilderEmpty();
    }
    const expressions = tokens.filter(token => token.scheme.toLowerCase() === 'expr');
    if (expressions.length !== 1 || tokens.some(token => token.unclosed)) {
        return expressionBuilderUnsupported();
    }
    const token = expressions[0];
    const outer = bindingSplitFallback(token.params);
    if (outer.fallback !== null) return expressionBuilderUnsupported('Fallbacks are not supported by this editor.');
    const parts = bindingSplitParams(outer.params);
    if (parts.length > 2) return expressionBuilderUnsupported('This editor supports only one numeric format parameter.');
    if (parts.length === 2) {
        const restored = expressionBuilderDecodeFormat(parts[1]);
        if (!restored) return expressionBuilderUnsupported('This numeric format is not supported by this editor.');
        Object.assign(formatting, restored);
    }
    if (validationOptions.isWidgetBinding && formatting.enabled) {
        return expressionBuilderUnsupported('Widget data bindings cannot use numeric formatting.');
    }
    return {
        supported: true, expression: parts[0], formatting, error: '',
        prefix: text.slice(0, token.start), suffix: text.slice(token.end)
    };
}

function expressionBuilderFoldWhitespace(expression) {
    let result = '';
    let quote = false;
    let depth = 0;
    for (let i = 0; i < expression.length; i++) {
        const character = expression[i];
        if (character === '"' && !_bindingCharIsEscaped(expression, i)) quote = !quote;
        if (depth > 0) {
            result += character;
            if (!quote && character === '[') depth++;
            else if (!quote && character === ']') depth--;
        } else if (quote) {
            result += character;
        } else if (character === '[') {
            depth = 1;
            result += character;
        } else if (/\s/.test(character)) {
            if (result && result[result.length - 1] !== ' ') result += ' ';
        } else {
            result += character;
        }
    }
    return result.trim();
}

function expressionBuilderFindPlaceholder(expression) {
    let quote = false;
    let depth = 0;
    for (let i = 0; i < expression.length;) {
        const character = expression[i];
        if (character === '"' && !_bindingCharIsEscaped(expression, i)) quote = !quote;
        if (quote || depth > 0) {
            if (!quote && character === '[') depth++;
            else if (!quote && character === ']') depth--;
            i++;
            continue;
        }
        if (character === '[') {
            depth = 1;
            i++;
            continue;
        }
        if (/[A-Za-z_]/.test(character)) {
            let end = i + 1;
            while (end < expression.length && /[A-Za-z0-9_]/.test(expression[end])) end++;
            const identifier = expression.slice(i, end);
            if (EXPRESSION_BUILDER_PLACEHOLDERS.has(identifier)) return identifier;
            i = end;
            continue;
        }
        i++;
    }
    return '';
}

function expressionBuilderGenerate(expression, formatting = {}, byteLimit = 191, validationOptions = {}, surrounding = {}) {
    const body = expressionBuilderFoldWhitespace(String(expression == null ? '' : expression));
    if (!body) return { valid: false, binding: '', bytes: 0, body, error: '' };
    const placeholder = expressionBuilderFindPlaceholder(body);
    let format = '';
    try {
        if (formatting.enabled) {
            format = bindingBuilderFormat('number', Number(formatting.decimals),
                bindingBuilderUnitSuffix(String(formatting.unit || '')));
        }
    } catch (error) {
        return { valid: false, binding: '', bytes: 0, body, error: error.message };
    }
    const binding = '[expr:' + body + (format ? ';' + format : '') + ']';
    const fieldValue = (surrounding.prefix || '') + binding + (surrounding.suffix || '');
    const bytes = new TextEncoder().encode(fieldValue).length;
    const result = { binding, fieldValue, bytes, body };
    if (placeholder) return { ...result, valid: false, error: 'Replace the unfinished "' + placeholder + '" operand.' };
    if (bytes > byteLimit) {
        return { ...result, valid: false, error: 'The resulting field value exceeds ' + byteLimit + ' UTF-8 bytes.' };
    }
    if (validationOptions.isWidgetBinding && format) {
        return { ...result, valid: false, error: 'Widget data bindings must not include a format parameter.' };
    }
    const validation = validateBinding(fieldValue, validationOptions);
    if (!validation.valid) {
        return { ...result, valid: false, error: validation.errors[0].message };
    }
    return { ...result, valid: true, error: '' };
}

function expressionBuilderSnippet(name) {
    const snippet = EXPRESSION_BUILDER_FUNCTIONS.find(item => item.label === name);
    if (!snippet) return null;
    const start = snippet.text.indexOf(snippet.operand);
    return { text: snippet.text, selectionStart: start, selectionEnd: start + snippet.operand.length };
}

let expressionBuilderSession = null;

function expressionBuilderDialogHTML() {
    const functions = EXPRESSION_BUILDER_FUNCTIONS.map(item =>
        '<option value="' + _bhEsc(item.label) + '">' + _bhEsc(item.label) + '</option>').join('');
    return `<header><h2 id="expression-builder-title">Expression builder</h2>
        <button type="button" class="btn btn-sm" data-expression-close aria-label="Close Expression Builder">&#215;</button></header>
        <div class="pad-button-builder-body">
            <div data-expression-unsupported hidden>
                <p data-expression-load-error role="status"></p>
                <button type="button" class="btn btn-secondary" data-expression-start>Start new expression</button>
            </div>
            <div class="expression-builder-tools">
                <select class="form-select expression-builder-function" data-expression-function aria-label="Insert function">
                    <option value="" selected>Insert function</option>${functions}
                </select>
                <button type="button" class="btn btn-secondary" data-expression-binding data-expression-insert>Insert binding</button>
                <button type="button" class="btn btn-secondary" data-expression-color data-expression-insert>Insert color</button>
                <button type="button" class="btn btn-outline-secondary expression-builder-reference" data-expression-reference title="Binding Reference" aria-label="Open Binding Reference">?</button>
            </div>
            <label for="expression-builder-input">Expression</label>
            <textarea class="form-control expression-builder-input" id="expression-builder-input" rows="7" spellcheck="false" aria-describedby="expression-builder-error"></textarea>
            <div class="expression-builder-format-row">
                <label class="expression-builder-format-toggle" for="expression-builder-format">
                    <input type="checkbox" id="expression-builder-format" data-expression-format aria-controls="expression-builder-format-controls">
                    Numeric formatting
                </label>
                <div class="expression-builder-format-controls" id="expression-builder-format-controls" hidden>
                    <label class="expression-builder-format-control" for="expression-builder-decimals">Decimals
                        <select class="form-select" id="expression-builder-decimals" data-expression-decimals disabled>
                            <option>0</option><option selected>1</option><option>2</option><option>3</option>
                            <option>4</option><option>5</option><option>6</option>
                        </select>
                    </label>
                    <label class="expression-builder-format-control" for="expression-builder-unit">Unit suffix
                        <input class="form-control" type="text" id="expression-builder-unit" maxlength="60" data-expression-unit disabled>
                    </label>
                </div>
            </div>
            <section class="expression-builder-generated" data-expression-field-section aria-label="Resulting field value">
                <span class="binding-builder-code-label">Resulting field value</span>
                <pre><code data-expression-field-output></code></pre>
                <div class="expression-builder-validation" data-expression-error role="alert"></div>
                <span class="expression-builder-bytes" data-expression-bytes>0 / 191 bytes</span>
            </section>
            <section class="expression-builder-result" data-expression-result-section aria-label="Result">
                <span>Result</span>
                <div class="expression-builder-result-value">
                    <span class="expression-builder-result-swatch" data-expression-result-swatch aria-hidden="true" hidden></span>
                    <output data-expression-result aria-live="polite"></output>
                </div>
            </section>
            <input type="text" data-expression-color-staging maxlength="191" data-allow-bindings="false" hidden tabindex="-1" aria-hidden="true">
        </div>
        <footer><span>Changes this field only. Save your changes in the editor.</span><div>
            <button type="button" class="btn btn-secondary" data-expression-cancel>Cancel</button>
            <button type="button" class="btn btn-primary" data-expression-apply disabled>Replace field</button>
        </div></footer>`;
}

function expressionBuilderCaptureSelection(session) {
    const input = session.input;
    if (typeof input.selectionStart === 'number') {
        session.selection = { start: input.selectionStart, end: input.selectionEnd };
    }
}

function expressionBuilderInsertSnippet(session, snippet) {
    const range = session.selection || { start: session.input.selectionStart, end: session.input.selectionEnd };
    const value = session.input.value;
    session.input.value = value.slice(0, range.start) + snippet.text + value.slice(range.end);
    const start = range.start + snippet.selectionStart;
    session.input.focus();
    session.input.setSelectionRange(start, range.start + snippet.selectionEnd);
    expressionBuilderCaptureSelection(session);
    expressionBuilderRender(session);
}

function expressionBuilderRender(session) {
    const formatToggle = session.dialog.querySelector('[data-expression-format]');
    const validationOptions = bindingValidationOptions.get(session.target) || {};
    const rawOnly = validationOptions.isWidgetBinding === true;
    const byteLimit = Math.min(session.target.maxLength > 0 ? session.target.maxLength : 191, 191);
    session.input.disabled = !session.supported;
    session.dialog.querySelector('[data-expression-function]').disabled = !session.supported;
    session.dialog.querySelectorAll('[data-expression-insert]').forEach(button => button.disabled = !session.supported);
    session.dialog.querySelector('[data-expression-unsupported]').hidden = session.supported;
    session.dialog.querySelector('[data-expression-load-error]').textContent = session.loadError;
    const preserving = !!(session.prefix || session.suffix);
    session.dialog.querySelector('[data-expression-apply]').textContent =
        preserving ? 'Replace expression' : 'Replace field';
    const enabled = formatToggle.checked && !rawOnly;
    formatToggle.disabled = rawOnly || !session.supported;
    const decimals = session.dialog.querySelector('[data-expression-decimals]');
    const unit = session.dialog.querySelector('[data-expression-unit]');
    session.dialog.querySelector('#expression-builder-format-controls').hidden = !enabled || !session.supported;
    decimals.disabled = unit.disabled = !enabled || !session.supported;
    const result = session.supported
        ? expressionBuilderGenerate(session.input.value, {
            enabled, decimals: decimals.value, unit: unit.value
        }, byteLimit, validationOptions, session)
        : { valid: false, binding: '', bytes: 0, error: '' };
    if (session.guardError) {
        result.valid = false;
        result.error = session.guardError;
    }
    session.dialog.querySelector('[data-expression-field-section]').hidden = !session.supported;
    session.dialog.querySelector('[data-expression-result-section]').hidden = !session.supported;
    session.dialog.querySelector('[data-expression-field-output]').textContent =
        result.fieldValue || (result.error ? 'Cannot generate binding.' : 'Enter an expression to generate a binding.');
    const error = session.dialog.querySelector('[data-expression-error]');
    error.textContent = result.error || '';
    error.hidden = !result.error;
    session.dialog.querySelector('[data-expression-bytes]').textContent = result.bytes + ' / ' + byteLimit + ' bytes';
    session.dialog.querySelector('[data-expression-apply]').disabled = !result.valid;
    session.result = result;
    expressionBuilderPreviewSchedule(session, result);
}

function expressionBuilderPreviewSet(session, state, value) {
    const output = session.dialog.querySelector('[data-expression-result]');
    const swatch = session.dialog.querySelector('[data-expression-result-swatch]');
    output.dataset.state = state;
    output.textContent = value === undefined
        ? ({ empty: 'Your result will appear here.',
            invalid: 'Preview unavailable.',
            resolving: 'Resolving...', unavailable: 'No preview available.', failed: 'Preview request failed.' }[state] || '')
        : value === '' ? '(empty)' : String(value);
    swatch.hidden = true;
    swatch.style.backgroundColor = '';
    if (state === 'resolved' && typeof value === 'string' && /^#[0-9a-f]{3}(?:[0-9a-f]{3})?$/i.test(value.trim())) {
        const color = padColorNormalizeLiteral(value);
        if (color) {
            swatch.hidden = false;
            swatch.style.backgroundColor = color;
        }
    }
}

function expressionBuilderPreviewCancel(session) {
    if (session.previewTimer != null) clearTimeout(session.previewTimer);
    session.previewTimer = null;
    if (session.previewController) session.previewController.abort();
    session.previewController = null;
}

function expressionBuilderPreviewDispose(session) {
    expressionBuilderPreviewCancel(session);
    session.previewGeneration++;
    session.previewBinding = '';
}

function expressionBuilderPreviewIsCurrent(session, generation, binding) {
    return expressionBuilderSession === session && session.previewGeneration === generation &&
        session.previewBinding === binding && session.result?.valid === true &&
        session.result.binding === binding && session.dialog.isConnected && session.target.isConnected &&
        expressionBuilderContextMatches(session);
}

function expressionBuilderPreviewSchedule(session, result) {
    expressionBuilderPreviewCancel(session);
    const generation = (session.previewGeneration || 0) + 1;
    session.previewGeneration = generation;
    session.previewBinding = result.valid ? result.binding : '';
    if (!session.supported || !result.valid) {
        expressionBuilderPreviewSet(session, session.supported && result.body === '' ? 'empty' : 'invalid');
        return;
    }
    const page = session.context ? session.context.page :
        typeof padState !== 'undefined' ? padState.page : null;
    if (page == null) {
        expressionBuilderPreviewSet(session, 'failed');
        return;
    }
    const binding = result.binding;
    const screen = 'pad_' + page;
    expressionBuilderPreviewSet(session, 'resolving');
    session.previewTimer = setTimeout(() => {
        session.previewTimer = null;
        if (!expressionBuilderPreviewIsCurrent(session, generation, binding)) return;
        const controller = new AbortController();
        session.previewController = controller;
        Promise.resolve().then(() => fetch('/api/pad/resolve', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ screen, bindings: [binding] }),
            signal: controller.signal
        })).then(response => {
            if (!response || !response.ok) throw new Error('Preview request failed.');
            return response.json();
        }).then(payload => {
            if (!expressionBuilderPreviewIsCurrent(session, generation, binding)) return;
            if (!payload || !Array.isArray(payload.resolved)) throw new Error('Malformed preview response.');
            const entry = payload.resolved[0];
            if (!entry) {
                expressionBuilderPreviewSet(session, 'unavailable');
                return;
            }
            if (entry.binding !== undefined && entry.binding !== binding) throw new Error('Mismatched preview response.');
            if (!Object.prototype.hasOwnProperty.call(entry, 'value')) throw new Error('Missing preview value.');
            if (entry.value == null || entry.value === '---') {
                expressionBuilderPreviewSet(session, 'unavailable');
                return;
            }
            if (!['string', 'number', 'boolean'].includes(typeof entry.value)) {
                throw new Error('Malformed preview value.');
            }
            expressionBuilderPreviewSet(session, 'resolved', entry.value);
        }).catch(error => {
            if (error?.name !== 'AbortError' && expressionBuilderPreviewIsCurrent(session, generation, binding)) {
                expressionBuilderPreviewSet(session, 'failed');
            }
        }).finally(() => {
            if (session.previewController === controller) session.previewController = null;
        });
    }, 250);
}

function expressionBuilderCaptureContext() {
    if (typeof padState === 'undefined') return null;
    const context = {
        page: padState.page, layer: padState.selectedLayer,
        col: padState.editCol, row: padState.editRow, button: null
    };
    if (typeof padFindButton === 'function') context.button = padFindButton(context.col, context.row) || null;
    return context;
}

function expressionBuilderContextMatches(session) {
    const context = session.context;
    if (!context) return session.target.isConnected;
    if (typeof padState === 'undefined' || padState.page !== context.page ||
        padState.selectedLayer !== context.layer || padState.editCol !== context.col || padState.editRow !== context.row) return false;
    return typeof padFindButton !== 'function' || (padFindButton(context.col, context.row) || null) === context.button;
}

function expressionBuilderOpenBindingPicker(session) {
    if (!session.target.isConnected || !expressionBuilderContextMatches(session)) {
        expressionBuilderClose();
        return;
    }
    expressionBuilderCaptureSelection(session);
    bindingValidationOptions.set(session.input, { isWidgetBinding: true });
    _bindingHelp.target = session.input;
    _bindingHelp.targetFocused = true;
    _bindingHelp.opener = session.input;
    _bindingHelp.pointer = null;
    showBindingBuilder();
}

function expressionBuilderOpenColorPicker(session) {
    if (!session.target.isConnected || !expressionBuilderContextMatches(session)) {
        expressionBuilderClose();
        return;
    }
    expressionBuilderCaptureSelection(session);
    const input = session.dialog.querySelector('[data-expression-color-staging]');
    input.value = '';
    padColorPopoverOpen(session.dialog.querySelector('[data-expression-color]'), input);
    if (typeof _cpPopover !== 'undefined' && _cpPopover) _cpPopover.returnFocus = session.input;
}

function expressionBuilderInsertText(session, text) {
    const range = session.selection || { start: session.input.selectionStart, end: session.input.selectionEnd };
    const value = session.input.value;
    session.input.value = value.slice(0, range.start) + text + value.slice(range.end);
    const caret = range.start + text.length;
    session.input.focus();
    session.input.setSelectionRange(caret, caret);
    expressionBuilderCaptureSelection(session);
    expressionBuilderRender(session);
}

function expressionBuilderApply(session) {
    const target = session.target;
    if (!target.isConnected || !expressionBuilderContextMatches(session)) {
        expressionBuilderClose();
        return;
    }
    if (target.value !== session.initialValue) {
        session.guardError = 'The field changed while this editor was open. Close and reopen before replacing it.';
        expressionBuilderRender(session);
        return;
    }
    expressionBuilderRender(session);
    if (!session.result.valid) return;
    target.value = session.result.fieldValue;
    target.dispatchEvent(new Event('input', { bubbles: true }));
    target.dispatchEvent(new Event('change', { bubbles: true }));
    expressionBuilderClose();
}

function expressionBuilderOpen(target, opener) {
    if (expressionBuilderSession || !expressionBuilderEligible(target) || !target.isConnected) return;
    const validationOptions = bindingValidationOptions.get(target) || {};
    const loaded = expressionBuilderLoad(target.value, validationOptions);
    const dialog = document.createElement('dialog');
    dialog.className = 'pad-button-builder portal-form expression-builder-dialog';
    dialog.setAttribute('aria-labelledby', 'expression-builder-title');
    dialog.innerHTML = expressionBuilderDialogHTML();
    document.body.appendChild(dialog);
    const session = {
        dialog, target, opener: opener || document.activeElement,
        initialValue: target.value, supported: loaded.supported,
        loadError: loaded.supported ? '' : 'Cannot edit the existing field value. ' + loaded.error +
            ' Start a new expression to replace the entire field, or cancel to keep it. The original value is unchanged until you choose Replace field.',
        prefix: loaded.prefix || '', suffix: loaded.suffix || '',
        input: dialog.querySelector('#expression-builder-input'),
        selection: { start: 0, end: 0 }, context: expressionBuilderCaptureContext(),
        colorInput: dialog.querySelector('[data-expression-color-staging]'), guardError: '',
        previewGeneration: 0, previewTimer: null, previewController: null, previewBinding: ''
    };
    expressionBuilderSession = session;
    session.input.value = loaded.supported ? loaded.expression : '';
    session.input.disabled = !loaded.supported;
    dialog.querySelector('[data-expression-format]').checked = loaded.formatting.enabled;
    dialog.querySelector('[data-expression-decimals]').value = loaded.formatting.decimals;
    dialog.querySelector('[data-expression-unit]').value = loaded.formatting.unit;
    expressionBuilderRender(session);
    const close = () => expressionBuilderClose();
    dialog.querySelector('[data-expression-close]').addEventListener('click', close);
    dialog.querySelector('[data-expression-cancel]').addEventListener('click', close);
    dialog.querySelector('[data-expression-apply]').addEventListener('click', () => expressionBuilderApply(session));
    dialog.querySelector('[data-expression-start]').addEventListener('click', () => {
        session.supported = true;
        expressionBuilderRender(session);
        session.input.focus();
    });
    dialog.querySelector('[data-expression-binding]').addEventListener('click', () => expressionBuilderOpenBindingPicker(session));
    dialog.querySelector('[data-expression-color]').addEventListener('click', () => expressionBuilderOpenColorPicker(session));
    dialog.querySelector('[data-expression-reference]').addEventListener('click', () => {
        const resume = () => {
            if (expressionBuilderSession !== session || !session.dialog.isConnected) return;
            session.dialog.showModal();
            session.dialog.querySelector('[data-expression-reference]').focus();
        };
        session.dialog.close();
        if (typeof showBindingHelp !== 'function' || !showBindingHelp(resume)) resume();
    });
    dialog.addEventListener('cancel', event => { event.preventDefault(); close(); });
    dialog.addEventListener('keydown', event => {
        if (event.key !== 'Escape') return;
        if (event.target.closest('.cdd-wrap.cdd-open')) return;
        event.preventDefault();
        event.stopPropagation();
        close();
    });
    session.input.addEventListener('select', () => expressionBuilderCaptureSelection(session));
    session.input.addEventListener('click', () => expressionBuilderCaptureSelection(session));
    session.input.addEventListener('keyup', () => expressionBuilderCaptureSelection(session));
    session.input.addEventListener('blur', () => expressionBuilderCaptureSelection(session));
    dialog.addEventListener('pointerdown', event => {
        if (event.target.closest('[data-expression-function], [data-expression-insert]')) expressionBuilderCaptureSelection(session);
    }, true);
    dialog.addEventListener('input', event => {
        if (event.target === session.colorInput) {
            const color = padColorNormalizeLiteral(session.colorInput.value);
            if (color) expressionBuilderInsertText(session, JSON.stringify(color));
            return;
        }
        if (event.target === session.input) expressionBuilderCaptureSelection(session);
        if (event.target.matches('[data-expression-format], [data-expression-decimals], [data-expression-unit], #expression-builder-input')) {
            expressionBuilderRender(session);
        }
    });
    dialog.addEventListener('change', event => {
        if (event.target.matches('[data-expression-function]')) {
            const snippet = expressionBuilderSnippet(event.target.value);
            event.target.value = '';
            if (snippet) expressionBuilderInsertSnippet(session, snippet);
        } else if (event.target.matches('[data-expression-format], [data-expression-decimals], [data-expression-unit]')) {
            expressionBuilderRender(session);
        }
    });
    dialog.showModal();
    if (loaded.supported) session.input.focus();
    else dialog.querySelector('[data-expression-cancel]').focus();
    session.observer = new MutationObserver(() => {
        if (!session.target.isConnected || !expressionBuilderContextMatches(session)) expressionBuilderClose();
    });
    session.observer.observe(document.body, {
        childList: true, subtree: true, attributes: true, attributeFilter: ['class', 'hidden']
    });
}

function expressionBuilderClose() {
    const session = expressionBuilderSession;
    if (!session) return;
    expressionBuilderPreviewDispose(session);
    if (session.observer) session.observer.disconnect();
    if (typeof _cpPopover !== 'undefined' && _cpPopover && _cpPopover.target === session.colorInput) {
        padColorPopoverClose();
    }
    if (document.querySelector('.binding-builder-dialog[open]') && typeof closeBindingBuilder === 'function') {
        closeBindingBuilder();
    }
    if (bindingValidationOptions.has(session.input)) bindingValidationOptions.delete(session.input);
    if (_bindingHelp.target === session.input) {
        _bindingHelp.target = null;
        _bindingHelp.targetFocused = false;
        _bindingHelp.opener = null;
    }
    session.dialog.close();
    session.dialog.remove();
    expressionBuilderSession = null;
    const restore = [session.opener, session.target].find(candidate =>
        candidate && candidate !== document.body && candidate.isConnected && typeof candidate.focus === 'function');
    if (restore) restore.focus();
}