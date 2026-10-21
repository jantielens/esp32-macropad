function catalogBindingSchemes() {
    return (_bindingHelp.schemes || []).filter(scheme => typeof scheme.name === 'string');
}

function catalogBindingScheme(name) {
    const scheme = catalogBindingSchemes().find(item => item.name === name);
    if (!scheme) throw new Error('This scheme is not registered on this device.');
    return scheme;
}

function catalogBindingMetadata(reference) {
    const scheme = catalogBindingScheme(reference.scheme);
    const keys = (scheme.authoring?.keys || []).filter(item =>
        item.read_only === true && ['integer', 'number', 'text'].includes(item.kind) &&
        item.formats?.includes('raw'));
    const key = reference.params?.[0] || '';
    return keys.find(item => item.key === key) ||
        keys.find(item => bindingHelpKeyMatches(item.key, key));
}

function catalogBindingDefaultReference(reference) {
    const scheme = catalogBindingScheme(reference.scheme);
    const params = Array.from(reference.params || [], value => String(value ?? ''));
    for (let index = 0; index < scheme.min_params; index++) {
        if (!params[index] && typeof scheme.params?.[index]?.default === 'string') {
            params[index] = scheme.params[index].default;
        }
    }
    return { ...reference, params };
}

function catalogBindingReferenceRows(scheme) {
    return scheme.reference?.formats ? scheme.reference.rows || [] : [];
}

function catalogBindingToken(reference, selection = {}) {
    const scheme = catalogBindingScheme(reference.scheme);
    const params = Array.from(reference.params || [], value => String(value ?? ''));
    const metadata = catalogBindingMetadata(reference);
    if (selection.mode === 'integer') {
        if (!metadata?.formats.includes('integer') || typeof metadata.integer_format !== 'string') {
            throw new Error('This value does not declare integer formatting.');
        }
        if (metadata.integer_format) {
            if (scheme.format_param < 0) throw new Error('This value does not accept a native format.');
            while (params.length <= scheme.format_param) params.push('');
            params[scheme.format_param] = metadata.integer_format;
        }
    }
    if (selection.mode === 'number') {
        if (!metadata?.formats.includes('number') || scheme.format_param < 0) {
            throw new Error('This value does not declare number formatting.');
        }
        while (params.length <= scheme.format_param) params.push('');
        params[scheme.format_param] = bindingBuilderFormat('number', Number(selection.decimals), '');
    }
    while (params.length > scheme.min_params && params[params.length - 1] === '') params.pop();
    if (params.length < scheme.min_params || params.length > scheme.max_params) {
        throw new Error('Complete the required parameters for this scheme.');
    }
    for (let index = 0; index < params.length; index++) {
        const value = params[index];
        if (index < scheme.min_params && !value.trim()) {
            throw new Error('Enter ' + (scheme.params?.[index]?.name || 'parameter ' + (index + 1)) + '.');
        }
        if (bindingSplitParams(value).length !== 1 || bindingSplitFallback(value).fallback !== null) {
            throw new Error('A parameter cannot contain a top-level semicolon or pipe. Nested bindings are allowed.');
        }
    }
    if (params.length && !scheme.free_form && Array.isArray(scheme.keys) && !scheme.keys.includes(params[0])) {
        throw new Error('Choose a registered key for this scheme.');
    }
    const fallback = String(selection.fallback ?? reference.fallback ?? '');
    if (/[;[\]|]/.test(fallback)) throw new Error('The fallback cannot contain binding delimiters (; [ ] |).');
    const token = '[' + scheme.name + ':' + params.join(';') + (fallback ? '|' + fallback : '') + ']';
    if (new TextEncoder().encode(token).length >= 192) {
        throw new Error('The binding must be shorter than 192 UTF-8 bytes.');
    }
    const tokens = bindingTokenize(token);
    if (tokens.length !== 1 || tokens[0].raw !== token || tokens[0].unclosed) {
        throw new Error('Check the binding brackets.');
    }
    const validation = validateBinding(token, { isWidgetBinding: selection.isWidgetBinding === true });
    if (!validation.valid) throw new Error(validation.errors[0].message);
    return token;
}

function catalogBindingReference(text) {
    const token = text.startsWith('[') ? text : '[' + text + ']';
    const tokens = bindingTokenize(token);
    if (tokens.length !== 1 || tokens[0].raw !== token || tokens[0].unclosed) {
        throw new Error('Enter a complete catalog binding, such as health:cpu.');
    }
    catalogBindingScheme(tokens[0].scheme);
    const split = bindingSplitFallback(tokens[0].params);
    return { scheme: tokens[0].scheme, params: bindingSplitParams(split.params), fallback: split.fallback || '' };
}

function catalogBindingCandidate(reference) {
    const completeReference = catalogBindingDefaultReference(reference);
    const token = catalogBindingToken(completeReference);
    const canonical = catalogBindingReference(token);
    const metadata = catalogBindingMetadata(canonical);
    const scheme = catalogBindingScheme(canonical.scheme);
    const keyDoc = (scheme.key_docs || []).find(item => bindingHelpKeyMatches(item.key, canonical.params[0] || ''));
    const presetToken = '[' + canonical.scheme + ':' + canonical.params.join(';') + ']';
    const preset = catalogBindingReferenceRows(scheme).find(row => row.example === presetToken);
    const exampleValue = completeReference.exampleValue ?? preset?.sample ?? metadata?.example_value;
    const valueKind = preset?.kind || metadata?.kind;
    const unit = preset?.unit ?? metadata?.unit;
    const normalizedReference = {
        ...canonical,
        ...(typeof exampleValue === 'string' ? { exampleValue } : {}),
        ...(completeReference.displayName ? { displayName: completeReference.displayName } : {})
    };
    return {
        providerId: 'catalog',
        sourceId: token,
        displayName: normalizedReference.displayName ||
            keyDoc?.desc || canonical.scheme + ':' + canonical.params[0],
        reference: normalizedReference,
        acquisitionState: 'not_required',
        sampleTruncated: false,
        ...(typeof exampleValue === 'string' ? { exampleValue } : {}),
        ...(valueKind ? { valueKind } : {}),
        ...(typeof unit === 'string' ? { unit } : {}),
        formatModes: preset && preset.kind !== metadata?.kind ? ['raw'] : metadata?.formats.slice() || ['raw'],
        ...(scheme.format_param >= 0 && canonical.params[scheme.format_param]
            ? { selection: { mode: 'raw', decimals: 1, unit: unit || '',
                fallback: canonical.fallback } } : {}),
        limits: { tokenBytes: 191, labelBytes: 191, formatBytes: 191, sampleBytes: 127 },
        capabilities: { display: true, numericWidget: false, liveSampling: false }
    };
}

const catalogBindingProvider = {
    id: 'catalog',
    async list(query, signal) {
        if (signal?.aborted) throw new DOMException('Lookup cancelled', 'AbortError');
        const normalized = String(query || '').trim().toLowerCase();
        const candidates = [];
        for (const scheme of catalogBindingSchemes()) {
            const addCandidate = (reference, displayName, description, exampleValue) => {
                const completeReference = catalogBindingDefaultReference({
                    ...reference,
                    ...(displayName ? { displayName } : {}),
                    ...(typeof exampleValue === 'string' ? { exampleValue } : {})
                });
                const sourceId = completeReference.scheme +
                    (completeReference.params.length ? ':' + completeReference.params.join(';') : '');
                if (normalized && ![displayName, sourceId, description, exampleValue, scheme.summary, scheme.category]
                    .some(value => String(value || '').toLowerCase().includes(normalized))) return;
                candidates.push({
                    providerId: 'catalog', sourceId, displayName: displayName || sourceId,
                    reference: completeReference,
                    ...(typeof exampleValue === 'string' ? { exampleValue } : {})
                });
            };
            if (!scheme.free_form && Array.isArray(scheme.keys)) {
                for (const key of scheme.keys) {
                    const doc = (scheme.key_docs || []).find(item => bindingHelpKeyMatches(item.key, key));
                    const name = scheme.name + ':' + key;
                    const displayName = doc?.desc || name;
                    addCandidate({ scheme: scheme.name, params: [key], fallback: '' },
                        displayName, doc?.desc);
                }
            } else if (scheme.authoring?.keys?.length && scheme.key_docs?.length) {
                for (const metadata of scheme.authoring.keys) {
                    const reference = catalogBindingReference(bindingHelpKeyToken(scheme, metadata.key));
                    const doc = (scheme.key_docs || []).find(item =>
                        bindingHelpKeyMatches(item.key, reference.params[0]));
                    addCandidate(reference, doc?.desc || scheme.name + ':' + reference.params[0], doc?.desc);
                }
            } else if (!catalogBindingReferenceRows(scheme).length) {
                addCandidate({ scheme: scheme.name, params: [], fallback: '' },
                    scheme.category ? scheme.category + ' — ' + scheme.name : scheme.name,
                    scheme.summary);
            }
            for (const row of catalogBindingReferenceRows(scheme)) {
                if (!row.example) continue;
                try {
                    const reference = catalogBindingReference(row.example);
                    const displayName = (scheme.category ? scheme.category + ' — ' : '') + row.desc;
                    addCandidate(reference, displayName, row.desc, row.sample);
                } catch (error) {
                    console.warn('Skipping invalid catalog example for ' + scheme.name + ':', error);
                }
            }
        }
        return candidates;
    },
    async inspect(reference, signal, screen) {
        const candidate = catalogBindingCandidate(reference);
        const response = await fetch('/api/pad/resolve', {
            method: 'POST', headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                bindings: [candidate.sourceId],
                ...(screen ? { screen } : {})
            }),
            cache: 'no-store', signal
        });
        const payload = await response.json();
        if (!response.ok) throw new Error(payload.error || 'Device preview failed (HTTP ' + response.status + ').');
        const value = payload?.resolved?.[0]?.value;
        if (typeof value !== 'string') throw new Error('The device returned an invalid binding preview.');
        const unavailable = value === '?' || value === '---' || /^ERR:/.test(value);
        const metadata = catalogBindingMetadata(candidate.reference);
        const scheme = catalogBindingScheme(candidate.reference.scheme);
        const truncated = new TextEncoder().encode(value).length >= 127;
        const numeric = !unavailable && !truncated && bindingBuilderIsNumeric(value) &&
            ['integer', 'number'].includes(metadata?.kind) && !candidate.reference.fallback &&
            candidate.reference.params.length <= scheme.widget_max_params;
        return {
            ...candidate, acquisitionState: unavailable ? 'unavailable' : 'received',
            sampleTruncated: truncated,
            ...(!unavailable ? { sample: value } : {}),
            capabilities: {
                display: true,
                numericWidget: !!numeric && metadata.capabilities.includes('numeric_widget'),
                liveSampling: !!numeric && metadata.capabilities.includes('live_sampling')
            }
        };
    },
    buildBinding: (candidate, selection = {}) => catalogBindingToken(candidate.reference, selection) +
        (selection.mode === 'number' ? bindingBuilderUnitSuffix(selection.unit || '') : ''),
    dispose() {}
};

function catalogBindingFormHTML(rawOnly = false, unified = false) {
    return '<div class="binding-builder-step">' +
        (unified ? '<div class="binding-builder-selection"><div class="binding-builder-selection-info">' +
            '<strong data-catalog-selection></strong></div>' +
            '<button type="button" class="btn btn-sm btn-secondary" data-catalog-change>Change</button></div>' : '') +
        (unified ? '' : '<label>Scheme<select data-catalog-scheme class="form-control">' +
            catalogBindingSchemes().map(scheme => '<option value="' + _bhEsc(scheme.name) + '">' +
                _bhEsc(scheme.name) + '</option>').join('') + '</select></label>') +
        '<div data-catalog-fields></div>' +
        '<details class="binding-builder-options"><summary>When unavailable (optional)</summary>' +
        '<label>Show this instead' +
        '<input data-catalog-fallback class="form-control" type="text" ' +
        'maxlength="80" placeholder="Default: ---">' +
        '</label></details>' +
        (unified ? '' : '<p class="binding-builder-note">' +
            'Parameters use the registered syntax, without guessed types or units. ' +
            (rawOnly ? 'Widget inputs omit display formatting and extra parameters.' :
                'Leave optional formatting empty for raw output.') +
            ' Preview may require a configured integration or saved pad context.</p>') +
        '<p data-catalog-status class="binding-builder-status" role="status" aria-live="polite"></p>' +
        '<div data-catalog-output></div></div>';
}

function catalogBindingMountForm(root, initial = {}, onUse, screen, rawOnly = false, selected = null, onChange) {
    const initialReference = catalogBindingDefaultReference({
        ...initial,
        scheme: initial.scheme || catalogBindingSchemes()[0]?.name || '',
        params: initial.params?.slice() || [],
        fallback: initial.fallback || ''
    });
    const unified = !!onUse;
    root.innerHTML = catalogBindingFormHTML(rawOnly, unified);
    const state = {
        scheme: initialReference.scheme,
        params: initialReference.params,
        fallback: initialReference.fallback,
        exampleValue: initialReference.exampleValue ?? selected?.exampleValue,
        displayName: initialReference.displayName ?? selected?.displayName,
        candidate: null, controller: null, generation: 0
    };
    let disposed = false;
    const stop = () => {
        state.controller?.abort();
        state.controller = null;
        state.generation++;
    };
    const reference = () => ({
        scheme: state.scheme, params: state.params.slice(), fallback: state.fallback,
        ...(state.exampleValue !== undefined ? { exampleValue: state.exampleValue } : {}),
        ...(state.displayName ? { displayName: state.displayName } : {})
    });
    const status = root.querySelector('[data-catalog-status]');
    const output = root.querySelector('[data-catalog-output]');
    const renderOutput = () => {
        output.replaceChildren();
        status.dataset.kind = 'info';
        const scheme = catalogBindingSchemes().find(item => item.name === state.scheme);
        const keyDoc = (scheme?.key_docs || []).find(item => bindingHelpKeyMatches(item.key, state.params[0] || ''));
        const keyDescription = root.querySelector('[data-catalog-key-description]');
        if (keyDescription) keyDescription.textContent = keyDoc?.desc || '';
        const selectionLabel = root.querySelector('[data-catalog-selection]');
        if (selectionLabel) selectionLabel.textContent = state.displayName || state.scheme;
        try {
            const candidate = state.candidate || catalogBindingCandidate(reference());
            const token = catalogBindingProvider.buildBinding(candidate, { isWidgetBinding: rawOnly });
            status.textContent = state.candidate?.acquisitionState === 'unavailable'
                ? 'No current value is available. The binding can still be used.' : '';
            if (onUse) {
                output.replaceChildren();
            } else {
                const sample = state.candidate?.sample ?? candidate.exampleValue ?? null;
                output.innerHTML = bindingBuilderResultHTML(token, sample, {
                    sampleLabel: state.candidate?.sample !== undefined ? 'Device result' : 'Example value'
                });
                output.querySelector('[data-binding-preview]')?.remove();
            }
            let actions = output.querySelector('.binding-example-actions');
            if (!actions) {
                actions = document.createElement('div');
                actions.className = 'binding-example-actions';
                output.appendChild(actions);
            }
            if (!onUse) {
                const preview = document.createElement('button');
                preview.type = 'button';
                preview.className = 'binding-copy-btn';
                preview.textContent = 'Preview on device';
                preview.onclick = async () => {
                    stop();
                    const generation = state.generation;
                    const controller = new AbortController();
                    state.controller = controller;
                    preview.disabled = true;
                    status.textContent = 'Resolving on this device...';
                    const timeout = setTimeout(() => controller.abort(), 15000);
                    try {
                        const candidate = await catalogBindingProvider.inspect(reference(), controller.signal, screen);
                        if (disposed || generation !== state.generation || !root.isConnected) return;
                        state.candidate = candidate;
                        renderOutput();
                    } catch (error) {
                        if (disposed || generation !== state.generation || !root.isConnected) return;
                        status.dataset.kind = 'error';
                        status.textContent = controller.signal.aborted
                            ? 'Device preview timed out. Retry to continue.' : error.message;
                        preview.disabled = false;
                    } finally {
                        clearTimeout(timeout);
                        if (generation === state.generation) state.controller = null;
                    }
                };
                actions.appendChild(preview);
            }
            if (onUse) {
                const use = document.createElement('button');
                use.type = 'button';
                use.className = 'btn btn-secondary';
                use.textContent = 'Continue';
                use.onclick = () => {
                    try { onUse(state.candidate || catalogButtonBuilderProvider.prepare(reference())); }
                    catch (error) { status.dataset.kind = 'error'; status.textContent = error.message; }
                };
                actions.appendChild(use);
            }
        } catch (error) {
            status.dataset.kind = 'error';
            status.textContent = error.message;
        }
    };
    const renderFields = () => {
        const scheme = catalogBindingSchemes().find(item => item.name === state.scheme);
        if (!scheme) {
            status.textContent = 'No binding schemes are registered on this device.';
            return;
        }
        const schemeSelect = root.querySelector('[data-catalog-scheme]');
        if (schemeSelect) schemeSelect.value = state.scheme;
        root.querySelector('[data-catalog-fallback]').value = state.fallback;
        const selectionLabel = root.querySelector('[data-catalog-selection]');
        if (selectionLabel) selectionLabel.textContent = state.displayName || state.scheme;
        const params = Array.from({ length: rawOnly ? scheme.widget_max_params : scheme.max_params }, (_, index) =>
            scheme.params?.[index] || { name: 'Parameter ' + (index + 1), desc: 'Use this scheme\'s parameter syntax.',
                required: index < scheme.min_params });
        root.querySelector('[data-catalog-fields]').innerHTML =
            (unified ? '' : '<p class="binding-builder-note">' + _bhEsc(scheme.summary) + '</p>' +
                (scheme.status ? '<p class="binding-docs-status">' + _bhEsc(scheme.status) + '</p>' : '')) +
            (scheme.name === 'pad' ? '<p class="binding-builder-note">' +
                (screen ? 'Preview uses saved bindings from ' + _bhEsc(screen) + ', not unsaved definitions.' :
                    'This dialog has no pad context. Preview named pad bindings in the button editor or Button Builder.') + '</p>' : '') +
            params.map((param, index) => {
                if (unified && index === 0 && !scheme.free_form) return '';
                if (unified && index === scheme.format_param && index >= scheme.min_params) return '';
                const control = index === 0 && !scheme.free_form && Array.isArray(scheme.keys)
                    ? '<select data-catalog-param="' + index + '" class="form-control">' +
                        '<option value="">Choose a registered key</option>' +
                        (scheme.keys || []).map(key => '<option value="' + _bhEsc(key) + '"' +
                            (key === state.params[index] ? ' selected' : '') + '>' + _bhEsc(key) + '</option>').join('') + '</select>'
                    : '<input data-catalog-param="' + index + '" class="form-control" type="text" maxlength="191" ' +
                        'spellcheck="false" value="' + _bhEsc(state.params[index] || '') + '">';
                return '<label>' + _bhEsc(param.name) + (index < scheme.min_params ? ' (required)' : ' (optional)') +
                    control + '<span class="binding-builder-note">' + _bhEsc(param.desc) + '</span></label>';
            }).join('') +
            (unified ? '' : '<p data-catalog-key-description class="binding-builder-note"></p>') +
            (scheme.note ? '<p class="binding-builder-note">' + _bhEsc(scheme.note) + '</p>' : '') +
            '<details><summary>Syntax and examples</summary><p>' + _bhEsc(bindingHelpSignature({ ...scheme, params })) + '</p>' +
            (scheme.examples || []).map(example => '<p><code>' + _bhEsc(example.code) + '</code> ' +
                _bhEsc(example.desc) + '</p>').join('') + '</details>';
        renderOutput();
    };
    const changeReference = next => {
        stop();
        Object.assign(state, catalogBindingDefaultReference(next), {
            candidate: null,
            exampleValue: next.exampleValue,
            displayName: next.displayName
        });
        renderFields();
    };
    root.querySelector('[data-catalog-change]')?.addEventListener('click', () => onChange?.());
    root.querySelector('[data-catalog-scheme]')?.addEventListener('change', event =>
        changeReference({ scheme: event.target.value, params: [], fallback: '' }));
    root.querySelector('[data-catalog-fallback]').oninput = event => {
        stop();
        state.fallback = event.target.value;
        state.candidate = null;
        renderOutput();
    };
    const updateParameter = event => {
        if (!event.target.hasAttribute('data-catalog-param')) return;
        stop();
        const index = Number(event.target.dataset.catalogParam);
        while (state.params.length <= index) state.params.push('');
        state.params[index] = event.target.value;
        state.candidate = null;
        state.exampleValue = undefined;
        renderOutput();
    };
    root.querySelector('[data-catalog-fields]').oninput = event => {
        if (event.target.tagName === 'INPUT') updateParameter(event);
    };
    root.querySelector('[data-catalog-fields]').onchange = event => {
        if (event.target.tagName === 'SELECT') updateParameter(event);
    };
    renderFields();
    return () => { disposed = true; stop(); };
}

let catalogBindingBuilderDispose = null;
function catalogBindingBuilderStop() {
    catalogBindingBuilderDispose?.();
    catalogBindingBuilderDispose = null;
}

BINDING_BUILDERS.push({
    id: 'catalog', scheme: null, label: 'On-device bindings', name: 'On-device bindings',
    description: 'any scheme registered on this device.',
    instructions: 'Complete the registered parameters. Preview is optional.',
    tooltip: 'Build a binding from the device catalog',
    configured: () => false, availability: () => '', provider: catalogBindingProvider,
    render: () => '<div id="catalog-binding-builder"></div>',
    mount(scheme) {
        catalogBindingBuilderStop();
        const root = document.getElementById('catalog-binding-builder');
        if (root) catalogBindingBuilderDispose = catalogBindingMountForm(root, { scheme }, undefined, undefined,
            bindingValidationOptions.get(_bindingHelp.target)?.isWidgetBinding === true);
    },
    reset: catalogBindingBuilderStop, stop: catalogBindingBuilderStop
});

const catalogButtonBuilderProvider = {
    id: 'catalog', name: 'On-device bindings',
    sourceLabel: 'Catalog binding',
    searchPlaceholder: 'Search a scheme, key, or description',
    searchHelp: 'Type at least 3 characters to search. Enter a complete binding and choose Use value to select it directly.',
    noResultsMessage: 'No matching bindings. Keep typing or enter a complete binding.',
    availability: () => catalogBindingSchemes().length ? '' : 'No binding schemes are registered on this device.',
    acceptsReference: query => {
        try { catalogBindingReference(query); return true; } catch { return false; }
    },
    referenceFromInput: catalogBindingReference,
    async search(query, signal) {
        if (String(query || '').trim().length < 3) return { candidates: [], more: false };
        const candidates = (await catalogBindingProvider.list(query, signal))
            .filter(candidate => !['ha', 'mqtt', 'expr', 'pad'].includes(candidate.reference.scheme));
        return { candidates: candidates.slice(0, 40), more: candidates.length > 40 };
    },
    inspect: (reference, signal, screen) => catalogBindingProvider.inspect(reference, signal, screen),
    prepare(reference, signal) {
        if (signal?.aborted) throw new DOMException('Lookup cancelled', 'AbortError');
        const candidate = catalogBindingCandidate(reference);
        if (typeof candidate.exampleValue !== 'string') return candidate;
        const metadata = catalogBindingMetadata(candidate.reference);
        const scheme = catalogBindingScheme(candidate.reference.scheme);
        const sample = candidate.exampleValue;
        const truncated = new TextEncoder().encode(sample).length >= 127;
        const numeric = !truncated && bindingBuilderIsNumeric(sample) &&
            ['integer', 'number'].includes(metadata?.kind) && !candidate.reference.fallback &&
            candidate.reference.params.length <= scheme.widget_max_params;
        return { ...candidate, sample, sampleIsExample: true, sampleTruncated: truncated,
            capabilities: {
                display: true,
                numericWidget: numeric && metadata.capabilities.includes('numeric_widget'),
                liveSampling: numeric && metadata.capabilities.includes('live_sampling')
            } };
    },
    requiresParameters(reference) {
        try { catalogBindingCandidate(reference); return false; }
        catch { return true; }
    },
    canEnableNumeric: candidate => {
        const metadata = catalogBindingMetadata(candidate.reference);
        return metadata?.capabilities?.includes('numeric_widget') === true;
    },
    configureSelection: (root, reference, onUse, screen, selected, onChange) =>
        catalogBindingMountForm(root, reference, onUse, screen, false, selected, onChange),
    previewNote(candidate) {
        return (candidate.sampleIsExample || candidate.sample === undefined && candidate.exampleValue !== undefined) &&
            candidate.acquisitionState !== 'unavailable'
            ? 'Illustrative sample preview. This is not the current device value.'
            : 'Preview only. Does not control the entity.';
    },
    buildBinding: (candidate, selection) => catalogBindingProvider.buildBinding(candidate, selection),
    buildLabel(candidate, options, includeUnit) {
        const scheme = catalogBindingScheme(candidate.reference.scheme);
        const metadata = catalogBindingMetadata(candidate.reference);
        const mode = metadata?.formats.includes('integer') && !candidate.reference.params[scheme.format_param]
            ? 'integer' : 'raw';
        const suffix = includeUnit ? bindingBuilderUnitSuffix(options.unit) : '';
        if (/[;[\]|]/.test(suffix)) throw new Error('The unit cannot contain binding delimiters (; [ ] |).');
        return catalogBindingProvider.buildBinding(candidate, { mode }) + suffix;
    },
    previewLabel: (candidate, options, includeUnit) =>
        (candidate.sample ?? (candidate.acquisitionState === 'unavailable' ? '---' :
            candidate.exampleValue ?? '---')) + (includeUnit ? bindingBuilderUnitSuffix(options.unit) : ''),
    dispose() {}
};
BUTTON_BUILDER_PROVIDERS.push(catalogButtonBuilderProvider);
