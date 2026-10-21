/**
 * @typedef {Object} BindingSourceCapabilities
 * @property {boolean} display
 * @property {boolean} numericWidget Requires declared support and a complete numeric authoring sample.
 * @property {boolean} liveSampling
 * @property {boolean} [historyBackfill]
 * @property {boolean} [control]
 */

/**
 * @typedef {Object} BindingSourceField
 * @property {string} path Bounded provider path for this selectable value.
 * @property {string} displayName
 * @property {string} [sample] Absent means no sample; an empty string is a received value.
 * @property {boolean} sampleTruncated
 * @property {'integer'|'number'|'text'|'boolean'|'json'} [valueKind]
 * @property {string} [unit] Absent means unknown; '' means known to have no unit.
 * @property {string[]} formatModes
 * @property {BindingSourceCapabilities} capabilities
 */

/**
 * @typedef {Object} BindingSourceLimits
 * @property {number} tokenBytes
 * @property {number} labelBytes
 * @property {number} formatBytes
 * @property {number} [pathBytes]
 * @property {number} [topicBytes]
 * @property {number} [sampleBytes]
 */

/**
 * @typedef {Object} BindingSourceCandidate
 * @property {string} providerId
 * @property {string} sourceId Stable within its provider; include a selected field path.
 * @property {string} displayName
 * @property {Object} reference Opaque value owned by the provider.
 * @property {'not_required'|'pending'|'waiting'|'received'|'unavailable'|'error'} acquisitionState
 * @property {string} [sample] Absent means no sample; '' and '0' are received values.
 * @property {boolean} sampleTruncated
 * @property {boolean} [sampleIsExample] The sample is illustrative, not a device reading.
 * @property {'integer'|'number'|'text'|'boolean'|'json'} [valueKind]
 * @property {string} [unit] Absent means unknown; '' means known to have no unit.
 * @property {BindingSourceField[]} [fields]
 * @property {string[]} formatModes
 * @property {BindingSourceLimits} limits Effective provider-specific byte limits.
 * @property {BindingSourceCapabilities} capabilities
 * @property {{mode: string, decimals: number|string, unit: string, fallback: string}} [selection] Draft display options, never numeric widget input.
 */

/**
 * @typedef {Object} BindingSourceProvider
 * @property {string} id
 * @property {(query: string, signal: AbortSignal) => Promise<BindingSourceCandidate[]>} [list]
 * @property {(reference: Object) => BindingSourceCandidate} [prepare] Build a candidate from declared samples.
 * @property {(reference: Object, signal: AbortSignal) => Promise<BindingSourceCandidate>} [inspect] Acquire an initial source value.
 * @property {(candidate: BindingSourceCandidate, selection: Object) => string} buildBinding
 * @property {(candidate: BindingSourceCandidate) => void|Promise<void>} dispose
 */

const BUTTON_BUILDER_PROVIDERS = [];

function bindingSourcePickerHTML() {
    return `<div class="binding-source-picker">
        <div class="binding-source-options" data-bb-provider-picker role="radiogroup" aria-label="Source"></div>
        <p data-bb-setup hidden><a data-bb-settings>Open source settings</a></p>
        <section data-bb-source hidden>
            <div data-bb-catalog-search-panel>
                <label class="binding-builder-input-label" for="button-builder-search" data-bb-source-label>Find a value</label>
                <div class="binding-builder-source">
                    <input id="button-builder-search" class="form-control" autocomplete="off" spellcheck="false"
                        maxlength="191" aria-describedby="button-builder-search-help">
                    <button type="button" class="btn btn-secondary" data-bb-use hidden>Read topic</button>
                </div>
                <p id="button-builder-search-help" class="binding-builder-note" hidden></p>
                <div class="binding-builder-suggestions" data-bb-matches aria-label="Source suggestions"></div>
            </div>
            <div data-bb-value hidden>
                <button type="button" class="binding-docs-link" data-bb-value-back>Change topic</button>
                <div data-bb-value-matches class="binding-builder-suggestions" aria-label="Available values"></div>
                <details><summary>Custom path</summary>
                    <label for="button-builder-path">Dotted path<input id="button-builder-path" class="form-control"
                        maxlength="127" placeholder="nested.temperature"></label>
                    <button type="button" class="btn btn-secondary" data-bb-confirm-value>Use path</button>
                </details>
                <p data-bb-value-note class="binding-builder-note" role="status"></p>
            </div>
            <div data-bb-parameters hidden></div>
        </section>
        <div class="binding-builder-status" data-bb-status role="status" aria-live="polite" hidden></div>
        <button type="button" class="btn btn-sm btn-secondary" data-bb-retry hidden>Retry</button>
        <section data-bb-picked hidden>
            <div class="binding-builder-selection">
                <div class="binding-builder-selection-info"><strong data-bb-picked-name></strong>
                    <code data-bb-picked-id></code></div>
                <button type="button" class="binding-docs-link" data-bb-picked-change>Edit value</button>
            </div>
            <div class="binding-source-customize pad-button-builder-design">
            <div>
            <details class="binding-builder-options" data-bb-display><summary>Display options</summary>
                <div data-bb-format></div>
            </details>
            </div>
            <div class="pad-button-builder-preview-panel" data-bb-picked-result></div>
            </div>
            <button type="button" class="btn btn-primary" data-bb-continue>Continue to button</button>
        </section>
    </div>`;
}

function bindingSourceSelection(candidate) {
    const numeric = ['integer', 'number'].includes(candidate.valueKind) ||
        bindingBuilderIsNumeric(candidate.sample);
    const mode = numeric && candidate.formatModes.includes('number') ? 'number' :
        numeric && candidate.formatModes.includes('integer') ? 'integer' : 'raw';
    return { mode, decimals: 1, unit: candidate.unit || '',
        fallback: candidate.reference.fallback || '', ...candidate.selection };
}

function bindingSourceToken(candidate, selection, rawOnly = false) {
    const provider = BUTTON_BUILDER_PROVIDERS.find(item => item.id === candidate.providerId);
    if (!provider) throw new Error('The selected source is no longer available.');
    const mode = rawOnly ? 'raw' : selection.mode;
    if (!candidate.formatModes.includes(mode)) throw new Error('Choose a supported display format.');
    const format = ['number', 'text'].includes(mode)
        ? bindingBuilderFormat(mode, Number(selection.decimals), bindingBuilderUnitSuffix(selection.unit)) : '';
    const suffix = !rawOnly && ['raw', 'integer'].includes(mode)
        ? bindingBuilderUnitSuffix(selection.unit) : '';
    if (/[;[\]|]/.test(suffix)) throw new Error('The unit cannot contain binding delimiters (; [ ] |).');
    const token = provider.buildBinding(candidate, {
        ...selection, mode, format, isWidgetBinding: rawOnly
    }) + suffix;
    if (new TextEncoder().encode(token).length > 191) {
        throw new Error('The binding and its unit must fit within 191 UTF-8 bytes.');
    }
    return token;
}

function bindingSourceSample(candidate, selection, rawOnly = false) {
    const unavailable = candidate.acquisitionState === 'unavailable';
    const mode = rawOnly ? 'raw' : selection.mode;
    const sample = unavailable ? selection.fallback || '---' :
        bindingBuilderSample(candidate.sample, mode === 'integer' ? 'raw' : mode,
        Number(selection.decimals), mode === 'raw' || mode === 'integer' ? '' :
            bindingBuilderUnitSuffix(selection.unit));
    return sample == null ? null : sample + (!rawOnly && ['raw', 'integer'].includes(mode)
        ? bindingBuilderUnitSuffix(selection.unit) : '');
}

// Shared by source-specific builders; discovery stays with the owning scheme.
function bindingBuilderUnitSuffix(unit) {
    return unit.trim() ? ' ' + unit.trim() : '';
}

function bindingBuilderFormat(mode, decimals, suffix) {
    if (mode === 'raw') return '';
    if (!['number', 'text'].includes(mode)) throw new Error('Choose a valid formatting mode.');
    if (mode === 'number' && (!Number.isInteger(decimals) || decimals < 0 || decimals > 6)) {
        throw new Error('Choose 0 to 6 decimal places.');
    }
    if (/[;[\]|]/.test(suffix)) throw new Error('The suffix cannot contain binding delimiters (; [ ] |).');
    const format = (mode === 'number' ? '%.' + decimals + 'f' : '%s') + suffix.replace(/%/g, '%%');
    if (new TextEncoder().encode(format).length >= 64) throw new Error('The format must be shorter than 64 UTF-8 bytes.');
    return format;
}

function bindingBuilderToken(scheme, source, path, format, fallback) {
    if (!/^[a-z][a-z0-9_]*$/.test(scheme) || !source || /[;[\]|]/.test(source)) {
        throw new Error('Enter a valid binding source.');
    }
    if (/[;[\]|]/.test(path)) throw new Error('The path cannot contain binding delimiters (; [ ] |).');
    if (/[;[\]|]/.test(fallback)) throw new Error('The fallback cannot contain binding delimiters (; [ ] |).');
    const params = [source];
    if (path || format) params.push(path);
    if (format) params.push(format);
    const token = '[' + scheme + ':' + params.join(';') + (fallback ? '|' + fallback : '') + ']';
    if (new TextEncoder().encode(token).length >= 192) throw new Error('The binding must be shorter than 192 UTF-8 bytes.');
    return token;
}

function bindingBuilderIsNumeric(value) {
    const text = String(value == null ? '' : value).trim();
    return /^[+-]?(?:\d+\.?\d*|\.\d+)(?:e[+-]?\d+)?$/i.test(text) && Number.isFinite(Number(text));
}

function bindingBuilderSample(value, mode, decimals, suffix) {
    if (value == null) return null;
    if (mode === 'raw') return String(value);
    if (mode === 'text') return String(value) + suffix;
    // Do not turn an empty string, boolean, or partially numeric text into a number.
    return bindingBuilderIsNumeric(value) ? Number(value).toFixed(decimals) + suffix : String(value);
}

function bindingBuilderFormatControlsHTML(options) {
    const {
        idPrefix, dataPrefix, mode, decimals, unit, fallback, formatModes, extraFields = '',
        rawNote = 'Use As received for numeric widget inputs.'
    } = options;
    const esc = _bhEsc;
    const modes = formatModes.map(([value, label]) =>
        '<option value="' + value + '"' + (value === mode ? ' selected' : '') + '>' + esc(label) + '</option>').join('');
    const numberAvailable = formatModes.some(([value]) => value === 'number');
    return '<div class="binding-builder-fields">' + extraFields +
        '<label for="' + idPrefix + '-mode" data-' + dataPrefix + '-format-choice>Display as<select id="' +
        idPrefix + '-mode" class="form-control">' + modes + '</select></label>' +
        '<label for="' + idPrefix + '-decimals" data-' + dataPrefix + '-number' + (numberAvailable ? '' : ' hidden') +
        '>Decimal places<select id="' + idPrefix + '-decimals" class="form-control">' +
        [0, 1, 2, 3, 4, 5, 6].map(n => '<option' + (n === Number(decimals) ? ' selected' : '') + '>' + n + '</option>').join('') +
        '</select></label>' +
        '<label for="' + idPrefix + '-unit" data-' + dataPrefix + '-formatted' + (mode === 'raw' ? ' hidden' : '') +
        '>Unit (optional)<input id="' + idPrefix + '-unit" class="form-control" type="text" maxlength="60" ' +
        'placeholder="e.g. \u00b0C or %" value="' + esc(unit) + '"></label></div>' +
        '<p class="binding-builder-note" data-' + dataPrefix + '-raw-note' + (mode === 'raw' ? '' : ' hidden') + '>' +
        esc(rawNote) + '</p>' +
        '<p class="binding-builder-note" data-' + dataPrefix + '-formatted' + (mode === 'raw' ? ' hidden' : '') +
        '>Unit spacing is automatic. Adding a unit does not convert the value.</p>' +
        '<details class="binding-builder-options"><summary>When unavailable (optional)</summary>' +
        '<label for="' + idPrefix + '-fallback">Show this instead<input id="' + idPrefix +
        '-fallback" class="form-control" type="text" placeholder="Default: ---" maxlength="80" value="' +
        esc(fallback) + '"></label><p class="binding-builder-note">Used when the value is unavailable. Leave empty for ---.</p></details>';
}

function bindingBuilderResultHTML(token, sample, options = {}) {
    const esc = _bhEsc;
    const message = options.message || 'No sample is available yet. You can still copy or insert the binding.';
    return '<div data-binding-builder-result>' +
        (sample == null ? '<p class="binding-builder-note">' + esc(message) + '</p>' :
        '<div class="binding-builder-sample"><span>' + esc(options.sampleLabel || 'Binding result') +
        '</span><output aria-live="polite">' +
        esc(sample) + (options.truncated ? ' <small>(truncated)</small>' : '') + '</output></div>') +
        '<span class="binding-builder-code-label">Generated binding</span>' +
        '<pre class="binding-example-code"><code>' + esc(token) + '</code></pre>' +
        '<div class="binding-example-actions">' +
        _bhActions(token, { copy: 'Copy binding', insert: options.insert === false ? false : 'Insert binding' }) +
        '</div></div>';
}

function bindingBuilderErrorHTML(message) {
    return '<p class="binding-builder-status" data-kind="error" role="alert">' +
        _bhEsc(message || 'Could not build this binding. Retry or enter it manually.') + '</p>';
}

let bindingBuilderSession = null;

function showBindingBuilder(id) {
    if (bindingBuilderSession) return;
    const dialog = document.createElement('dialog');
    dialog.className = 'pad-button-builder portal-form binding-builder-dialog';
    dialog.setAttribute('aria-labelledby', 'binding-builder-title');
    dialog.innerHTML = `<header><h2 id="binding-builder-title">Build a binding from...</h2>
        <button type="button" class="btn btn-sm" data-binding-close aria-label="Close Binding Builder">&#215;</button></header>
        <div class="pad-button-builder-body">${bindingSourcePickerHTML()}</div>
        <footer><span>Changes this field only. Save your changes in the editor.</span><div>
            <button type="button" class="btn btn-secondary" data-binding-cancel>Cancel</button>
            <button type="button" class="btn btn-primary" data-binding-insert disabled hidden>Insert binding</button>
        </div></footer>`;
    if (!_bindingHelp.target) _bindingHelp.opener = document.activeElement;
    document.body.appendChild(dialog);
    const session = {
        dialog, initialProvider: id || undefined,
        rawOnly: bindingValidationOptions.get(_bindingHelp.target)?.isWidgetBinding === true,
        page: undefined, controller: null, timer: null, bindingOnly: true
    };
    bindingBuilderSession = session;
    session.dispose = bindingSourcePickerMount(session);
    dialog.querySelector('[data-binding-close]').onclick = closeBindingBuilder;
    dialog.querySelector('[data-binding-cancel]').onclick = closeBindingBuilder;
    dialog.oncancel = event => { event.preventDefault(); closeBindingBuilder(); };
    dialog.onkeydown = event => {
        if (event.key === 'Escape') {
            event.preventDefault();
            event.stopPropagation();
            closeBindingBuilder();
        }
    };
    dialog.showModal();
    padButtonBuilderLoadConfig(session);
}

function closeBindingBuilder() {
    const session = bindingBuilderSession;
    if (!session) return;
    session.dispose();
    session.dialog.close();
    session.dialog.remove();
    bindingBuilderSession = null;
    const restore = _bindingHelp.target || _bindingHelp.opener;
    _bindingHelp.target = null;
    _bindingHelp.opener = null;
    if (restore?.isConnected) restore.focus();
}
