/**
 * @typedef {Object} ButtonBuilderSourceProvider
 * @property {string} id
 * @property {string} name
 * @property {(config: Object) => string} availability
 * @property {(config: Object) => boolean} [supported]
 * @property {(query: string) => boolean} acceptsReference
 * @property {(query: string) => Object} referenceFromInput
 * @property {(query: string, signal: AbortSignal, bindingOnly?: boolean) => Promise<{candidates: BindingSourceCandidate[], more: boolean}>} [search]
 * @property {(reference: Object, signal: AbortSignal, screen?: string) => Promise<BindingSourceCandidate>} [inspect]
 * @property {(reference: Object, signal: AbortSignal, screen?: string) => BindingSourceCandidate|Promise<BindingSourceCandidate>} [prepare]
 * @property {(reference: Object) => boolean} [requiresParameters]
 * @property {(candidate: BindingSourceCandidate, selection: Object) => string} buildBinding
 * @property {(candidate: BindingSourceCandidate, path: string) => BindingSourceCandidate} [selectField]
 * @property {(candidate: BindingSourceCandidate, options: Object, includeUnit: boolean) => string} [buildLabel]
 * @property {(candidate: BindingSourceCandidate, options: Object, includeUnit: boolean) => string} [previewLabel]
 * @property {(candidate: BindingSourceCandidate) => boolean} [supportsHistory]
 * @property {(candidate: BindingSourceCandidate, suffix: string) => Object} [buildHistoryFields]
 * @property {(candidate: BindingSourceCandidate) => Object[]} [controlFields]
 * @property {(candidate: BindingSourceCandidate, options: Object) => Object} [buildControl]
 * @property {(candidate: BindingSourceCandidate) => Object} [buildToggleWidget]
 * @property {(candidate: BindingSourceCandidate) => void|Promise<void>} dispose
 * @property {(root: HTMLElement, reference: Object, onUse: (candidate: BindingSourceCandidate) => void, screen: string, selected?: BindingSourceCandidate, onChange?: () => void, rawOnly?: boolean) => (() => void)} [configureSelection]
 */

/**
 * @typedef {Object} BindingButtonScenario
 * @property {string} id
 * @property {(candidate: BindingSourceCandidate, runtime: Object) => boolean} available
 * @property {(candidate: BindingSourceCandidate, draft: Object) => Object[]} [fields]
 * @property {(draft: Object) => Object} build Returns an ordinary, unsaved button object.
 */

let padButtonBuilderSession = null;

function bindingSourceSessionActive(session) {
    return session.picker ? !session.disposed : padButtonBuilderSession === session;
}

function bindingSourceProviders(config) {
    return BUTTON_BUILDER_PROVIDERS.filter(provider => !provider.supported || provider.supported(config));
}

function bindingSourceProviderOptions(config, name) {
    return bindingSourceProviders(config).map(provider => {
        const unavailable = provider.availability(config);
        return '<label' + (unavailable ? ' class="is-unconfigured"' : '') + '>' +
            '<input type="radio" name="' + _bhEsc(name) + '" data-bb-provider value="' +
            _bhEsc(provider.id) + '"><span>' + _bhEsc(provider.name) +
            (unavailable ? '<small>Setup needed</small>' : '') + '</span></label>';
    }).join('');
}

function bindingSourcePickerMount(session) {
    session.picker = true;
    const root = session.dialog;
    const search = root.querySelector('#button-builder-search');
    search.oninput = () => {
        padButtonBuilderStopLookup(session);
        root.querySelector('[data-bb-matches]').replaceChildren();
        root.querySelector('[data-bb-retry]').hidden = true;
        padButtonBuilderStatus('', false, session);
        if (session.provider?.search && search.value.trim().length >= 3) {
            session.timer = setTimeout(() => padButtonBuilderLookup(false, undefined, session), 300);
        }
    };
    search.onkeydown = event => {
        if (event.key === 'Enter') {
            event.preventDefault();
            if (session.provider.acceptsReference(search.value.trim())) {
                padButtonBuilderLookup(true, undefined, session);
            } else if (session.provider.search && search.value.trim().length >= 3) {
                padButtonBuilderLookup(false, undefined, session);
            }
        } else if (event.key === 'ArrowDown') {
            const first = root.querySelector('[data-bb-matches] button');
            if (first) { event.preventDefault(); first.focus(); }
        }
    };
    root.querySelector('[data-bb-matches]').onkeydown = event => {
        if (!['ArrowDown', 'ArrowUp'].includes(event.key)) return;
        const next = event.key === 'ArrowDown' ? event.target.nextElementSibling : event.target.previousElementSibling;
        if (next) { event.preventDefault(); next.focus(); }
    };
    root.querySelector('[data-bb-use]').onclick = () => padButtonBuilderLookup(true, undefined, session);
    root.querySelector('[data-bb-value-back]').onclick = () => {
        padButtonBuilderStopLookup(session);
        root.querySelector('[data-bb-catalog-search-panel]').hidden = false;
        padButtonBuilderStatus('', false, session);
        search.focus();
    };
    root.querySelector('[data-bb-provider-picker]').onchange = event => {
        if (event.target.matches('[data-bb-provider]')) {
            padButtonBuilderChooseProvider(event.target.value, session, false);
        }
    };
    root.querySelector('[data-bb-confirm-value]').onclick = () => {
        try {
            const candidate = session.provider.selectField(session.pendingCandidate,
                root.querySelector('#button-builder-path').value);
            padButtonBuilderAcceptCandidate(session, candidate);
        } catch (error) {
            padButtonBuilderStatus(error.message, true, session);
        }
    };
    root.querySelector('[data-bb-picked-change]').onclick = () => {
        padButtonBuilderStopLookup(session);
        if (session.bindingOnly) {
            root.querySelector('#binding-builder-title').textContent = 'Build a binding from...';
            root.querySelector('[data-binding-insert]').hidden = true;
        }
        padButtonBuilderStatus('', false, session);
        root.querySelector('[data-bb-retry]').hidden = true;
        root.querySelector('[data-bb-provider-picker]').hidden = false;
        root.querySelector('[data-bb-picked]').hidden = true;
        root.querySelector('[data-bb-source]').hidden = false;
        root.querySelector('[data-bb-catalog-search-panel]').hidden = false;
        if (session.bindingOnly && session.provider.configureSelection) {
            padButtonBuilderLookup(true, session.picked, session, true);
            return;
        }
        search.focus();
    };
    root.querySelector('[data-bb-continue]').onclick = () => {
        try {
            bindingSourceToken(session.picked, session.selection, session.rawOnly);
            const candidate = { ...session.picked, selection: { ...session.selection } };
            if (!session.bindingOnly) padButtonBuilderAcceptCandidate(session, candidate, true);
        } catch (error) {
            padButtonBuilderStatus(error.message, true, session);
        }
    };
    return () => {
        session.disposed = true;
        padButtonBuilderStopLookup(session);
    };
}

function bindingSourcePickerSelect(session, candidate) {
    const root = session.dialog;
    root.querySelector('[data-bb-path-options]')?.remove();
    session.picked = candidate;
    session.selection = bindingSourceSelection(candidate);
    root.querySelector('[data-bb-source]').hidden = true;
    root.querySelector('[data-bb-provider-picker]').hidden = true;
    root.querySelector('[data-bb-picked]').hidden = false;
    root.querySelector('[data-bb-picked-name]').textContent = candidate.displayName;
    root.querySelector('[data-bb-picked-name]').tabIndex = -1;
    root.querySelector('[data-bb-picked-id]').textContent = candidate.sourceId;
    if (session.bindingOnly) {
        root.querySelector('#binding-builder-title').textContent = 'Customize your binding';
    }
    const modes = session.rawOnly ? ['raw'] : candidate.formatModes;
    root.querySelector('[data-bb-format]').innerHTML = bindingBuilderFormatControlsHTML({
        idPrefix: 'source-format', dataPrefix: 'source',
        ...session.selection,
        formatModes: modes.map(mode => [mode, {
            raw: 'As received', number: 'Number', text: 'Text', integer: 'Whole number'
        }[mode]])
    });
    const mode = root.querySelector('#source-format-mode');
    mode.closest('label').hidden = modes.length < 2;
    const update = () => {
        const selection = session.selection;
        for (const key of ['mode', 'decimals', 'unit', 'fallback']) {
            selection[key] = root.querySelector('#source-format-' + key).value;
        }
        root.querySelector('[data-source-number]').hidden = selection.mode !== 'number';
        root.querySelectorAll('[data-source-formatted]').forEach(node =>
            node.hidden = session.rawOnly || node.tagName !== 'LABEL');
        root.querySelector('[data-source-raw-note]').hidden = true;
        const result = root.querySelector('[data-bb-picked-result]');
        const next = root.querySelector('[data-bb-continue]');
        const insert = session.bindingOnly ? root.querySelector('[data-binding-insert]') : null;
        if (insert) {
            insert.disabled = true;
            delete insert.dataset.bhInsert;
        }
        next.hidden = !!session.bindingOnly;
        try {
            const token = bindingSourceToken(candidate, selection, session.rawOnly);
            const sample = bindingSourceSample(candidate, selection, session.rawOnly);
            result.innerHTML = sample == null ? '<p class="binding-builder-note">No sample available.</p>' :
                '<div class="binding-builder-sample"><span>Sample value' +
                (candidate.sampleIsExample ? ' (example)' : candidate.cached ? ' (cached)' : '') +
                '</span><output aria-live="polite">' +
                _bhEsc(sample) + (candidate.sampleTruncated ? ' <small>(truncated)</small>' : '') +
                '</output></div>';
            if (session.bindingOnly) {
                result.innerHTML = '<h3>Preview</h3>' + bindingBuilderResultHTML(token, sample, {
                    insert: false, truncated: candidate.sampleTruncated,
                    sampleLabel: 'Sample value' + (candidate.sampleIsExample ? ' (example)' :
                        candidate.cached ? ' (cached)' : '')
                });
                insert.hidden = !_bindingHelp.target;
                insert.disabled = false;
                insert.dataset.bhInsert = token;
            }
            next.hidden = !!session.bindingOnly;
            next.disabled = false;
            padButtonBuilderStatus('', false, session);
        } catch (error) {
            result.replaceChildren();
            next.disabled = true;
            padButtonBuilderStatus(error.message, true, session);
        }
    };
    root.querySelector('[data-bb-format]').oninput = update;
    root.querySelector('[data-bb-format]').onchange = update;
    if (candidate.providerId === 'ha' && candidate.fields?.length > 1) {
        const values = document.createElement('details');
        values.dataset.bbPathOptions = '';
        values.className = 'binding-builder-options';
        values.innerHTML = '<summary>Change value</summary><div class="binding-builder-suggestions"></div>';
        for (const field of candidate.fields) {
            const button = document.createElement('button');
            button.type = 'button';
            button.className = 'binding-builder-suggestion';
            button.textContent = field.displayName + (field.sample !== undefined ? ' · ' + field.sample : '');
            button.onclick = () => bindingSourcePickerSelect(session,
                session.provider.selectField(candidate, field.path));
            values.lastElementChild.appendChild(button);
        }
        const label = document.createElement('label');
        label.textContent = 'Custom path';
        const input = document.createElement('input');
        input.className = 'form-control';
        input.maxLength = 127;
        input.placeholder = 'attributes.brightness';
        const use = document.createElement('button');
        use.type = 'button';
        use.className = 'btn btn-secondary';
        use.textContent = 'Use path';
        use.onclick = () => {
            try {
                bindingSourcePickerSelect(session, session.provider.selectField(candidate, input.value));
            } catch (error) { padButtonBuilderStatus(error.message, true, session); }
        };
        label.appendChild(input);
        values.append(label, use);
        root.querySelector('[data-bb-display]').before(values);
    }
    root.querySelector('[data-bb-display]').open = !!session.bindingOnly;
    update();
    root.querySelector('[data-bb-picked-name]').focus({ preventScroll: true });
}

function padButtonBuilderProvider(candidate) {
    const provider = BUTTON_BUILDER_PROVIDERS.find(item => item.id === candidate.providerId);
    if (!provider) throw new Error('The selected source provider is no longer available.');
    return provider;
}

function padButtonBuilderCandidateValueText(candidate) {
    if (candidate.sample !== undefined) {
        return (candidate.sampleIsExample ? 'Sample value (example): ' : 'Sample value: ') + candidate.sample +
            (candidate.sampleTruncated ? ' (truncated sample)' : '');
    }
    if (candidate.acquisitionState === 'unavailable') return 'Sample value: ---';
    if (candidate.exampleValue !== undefined) return '';
    return 'No sample available';
}

function padButtonBuilderBinding(source, format = '') {
    return padButtonBuilderProvider(source.candidate).buildBinding(source.candidate, { format, fallback: '' });
}

function padButtonBuilderSetScenario(session, builder, title = session.draft.sources[0].candidate.displayName,
    preserveOptions = false) {
    const draft = session.draft;
    const previousOptions = draft.options;
    session.scenario = builder;
    session.fields = [
        { name: 'title', label: 'Title', type: 'text', maxlength: 191, default: title },
        ...builder.fields(draft)
    ];
    draft.options = {
        ...Object.fromEntries(session.fields.map(field => [field.name, field.default])),
        ...(preserveOptions ? previousOptions : {}),
        title
    };
    if (builder.sourceFields) {
        draft.sources = draft.sources.map((source, index) =>
            padButtonBuilderSource(builder, source.candidate, index, source));
    }
}

function padButtonBuilderSource(builder, candidate, index, previous) {
    const source = {
        candidate,
        name: candidate.displayName,
        options: {}
    };
    const fields = builder?.sourceFields?.(source, index) || [];
    source.options = {
        ...Object.fromEntries(fields.map(field => [field.name, field.default])),
        ...Object.fromEntries(fields.filter(field => previous?.options[field.name] !== undefined)
            .map(field => [field.name, previous.options[field.name]])),
        ...builder?.sourceOptions?.(source, previous)
    };
    return source;
}

function padButtonBuilderRenderSelection() {
    const session = padButtonBuilderSession;
    const { candidate } = session.draft.sources[0];
    session.dialog.querySelector('[data-bb-name]').textContent = candidate.displayName;
    session.dialog.querySelector('[data-bb-id]').textContent = candidate.sourceId;
    const state = session.dialog.querySelector('[data-bb-state]');
    const valueText = padButtonBuilderCandidateValueText(candidate);
    state.textContent = valueText + (valueText ? bindingBuilderUnitSuffix(candidate.unit || '') : '');
    state.hidden = !!candidate.selection || !valueText;
}

function padButtonBuilderValidateFields(session) {
    const draft = session.draft;
    padButtonBuilderValidateOptions(session.fields, draft.options);
    const sources = session.scenario.maxSources ? draft.sources : draft.sources.slice(0, 1);
    const maxSources = session.scenario.maxSources || 1;
    if (!sources.length || sources.length > maxSources) {
        throw new Error('Choose between 1 and ' + maxSources + ' values.');
    }
    sources.forEach((source, index) => {
        session.scenario.validateSource?.(source);
        padButtonBuilderValidateOptions(session.scenario.sourceFields?.(source, index) || [], source.options);
    });
}

function padButtonBuilderValidateOptions(fields, options) {
    for (const field of fields) {
        const value = options[field.name];
        const text = String(value ?? '');
        let valid = value !== undefined && value !== null && (!field.required || text.trim() !== '');
        if (field.type === 'number') {
            const number = Number(text);
            const step = field.step ?? 1;
            const steps = (number - (field.min ?? 0)) / Number(step);
            valid = valid && bindingBuilderIsNumeric(text) &&
                (field.min === undefined || number >= field.min) &&
                (field.max === undefined || number <= field.max) &&
                (step === 'any' || Math.abs(steps - Math.round(steps)) <=
                    Number.EPSILON * Math.max(1, Math.abs(steps)) * 4);
        } else if (field.type === 'select') {
            valid = valid && field.options.some(([option]) => String(option) === text);
        } else if (field.type === 'color') {
            valid = valid && /^#[0-9a-f]{6}$/i.test(text);
        } else if (field.type !== 'text') {
            throw new Error('Unsupported button builder field type: ' + field.type);
        }
        if (field.maxlength !== undefined) valid = valid && text.length <= field.maxlength;
        if (!valid) throw new Error(field.error || 'Enter a valid ' + field.label.toLowerCase() + '.');
    }
}

function padButtonBuilderCanCreate(col, row) {
    if (!Number.isInteger(col) || !Number.isInteger(row) || col < 0 || row < 0 ||
        col >= padState.cols || row >= padState.rows || padState.placingBlock ||
        padBuildOccupancySet().has(col + ',' + row)) return false;
    if (padWorkspace && padWorkspace.errors.has(col + ',' + row)) return false;
    return !padState.templateButtons.some(button => padCanPlaceTemplateButton(button) &&
        col >= button.col && col < button.col + (button.col_span || 1) &&
        row >= button.row && row < button.row + (button.row_span || 1));
}

function padButtonBuilderAfterSelect() {
    if (window.matchMedia('(max-width: 980px)').matches &&
        padButtonBuilderCanCreate(padState.editCol, padState.editRow)) {
        padWorkspace.root.classList.remove('pad-workspace-inspecting');
    }
}

function padButtonBuilderUpdateEntry() {
    const canvas = document.getElementById('pad-workspace-canvas');
    let actions = padWorkspace.root.querySelector('.pad-button-builders');
    if (!actions) {
        actions = document.createElement('div');
        actions.className = 'pad-button-builders';
        const button = document.createElement('button');
        button.type = 'button';
        button.className = 'pad-button-builder-entry';
        button.innerHTML = '<span>Build a button from a source ...</span>';
        button.onclick = padButtonBuilderOpen;
        actions.appendChild(button);
        const manual = document.createElement('button');
        manual.type = 'button';
        manual.className = 'pad-button-builder-manual';
        manual.textContent = 'Edit manually';
        manual.onclick = () => padWorkspaceSetScope('button');
        actions.appendChild(manual);
        canvas.parentElement.insertBefore(actions, canvas);
    }
    const show = padWorkspace.selected && padButtonBuilderCanCreate(padState.editCol, padState.editRow);
    Array.from(actions.children).forEach(button => { button.hidden = !show; });
}

function padButtonBuilderClose() {
    const session = padButtonBuilderSession;
    if (!session) return;
    session.disposed = true;
    padButtonBuilderStopLookup();
    session.draft.sources.forEach(padButtonBuilderDisposeSource);
    padColorPopoverClose();
    session.dialog.close();
    session.dialog.remove();
    padButtonBuilderSession = null;
    const cell = document.querySelector('#pad-grid .pad-cell-selected');
    const entry = padWorkspace.root.querySelector('.pad-button-builder-entry:not([hidden])');
    if (entry || cell) (entry || cell).focus();
}

function padButtonBuilderStatus(message, error = false, session = padButtonBuilderSession) {
    const status = session.dialog.querySelector('[data-bb-status]');
    status.textContent = message;
    status.hidden = !message;
    status.dataset.kind = error ? 'error' : 'info';
}

function padButtonBuilderDisposeSource(source) {
    if (!source) return;
    const provider = BUTTON_BUILDER_PROVIDERS.find(item => item.id === source.candidate.providerId);
    if (!provider?.dispose) return;
    try {
        const result = provider.dispose(source.candidate);
        if (result && typeof result.catch === 'function') {
            result.catch(error => console.error('Button source cleanup failed:', error));
        }
    } catch (error) {
        console.error('Button source cleanup failed:', error);
    }
}

function padButtonBuilderOpen() {
    if (padButtonBuilderSession || !padWorkspace || !padWorkspace.selected ||
        !padButtonBuilderCanCreate(padState.editCol, padState.editRow)) return;
    const dialog = document.createElement('dialog');
    dialog.className = 'pad-button-builder portal-form';
    dialog.dataset.stage = 'source';
    dialog.setAttribute('aria-labelledby', 'button-builder-title');
    dialog.innerHTML = `
        <header><div><h2 id="button-builder-title">Build a button from...</h2></div>
        <button type="button" class="btn btn-sm" data-bb-close aria-label="Close Button Builder">&#215;</button></header>
        <div class="pad-button-builder-body">
            ${bindingSourcePickerHTML()}
            <section data-bb-selection hidden class="binding-builder-selection">
                <div class="binding-builder-selection-info"><strong data-bb-name></strong><code data-bb-id></code>
                <span class="binding-builder-note" data-bb-state></span></div>
                <button type="button" class="binding-docs-link" data-bb-change>Edit value</button>
            </section>
            <div data-bb-design hidden>
                <fieldset class="pad-button-builder-choices"><legend>What should this button do?</legend>
                    <div data-bb-choices></div></fieldset>
                <div class="pad-button-builder-design">
                    <div><h3>Customize</h3><div class="binding-builder-fields" data-bb-fields></div>
                        <section data-bb-sources hidden aria-label="Button values"></section>
                        <p class="binding-builder-note" data-bb-note></p></div>
                    <section class="pad-button-builder-preview-panel" aria-labelledby="button-builder-preview-title">
                        <h3 id="button-builder-preview-title">Preview</h3><div class="pad-cell pad-button-builder-preview" data-bb-preview></div>
                        <div class="pad-button-builder-states" data-bb-states></div>
                        <p class="binding-builder-note" data-bb-preview-note>Preview only. Does not control the entity.</p></section>
                </div>
            </div>
            <p class="binding-builder-status" data-bb-validation role="alert" data-kind="error" hidden></p>
        </div>
        <footer><span>Creates a draft. Save Pad to keep your changes.</span><div>
            <button type="button" class="btn btn-secondary" data-bb-cancel>Cancel</button>
            <button type="button" class="btn btn-primary" data-bb-create disabled>Create button</button></div></footer>`;
    padWorkspace.root.appendChild(dialog);
    const session = { dialog, page: padState.page, col: padState.editCol, row: padState.editRow,
        controller: null, timer: null, draft: { sources: [], options: {}, previewState: null } };
    padButtonBuilderSession = session;
    for (const selector of ['[data-bb-close]', '[data-bb-cancel]']) {
        dialog.querySelector(selector).onclick = padButtonBuilderClose;
    }
    dialog.oncancel = event => { event.preventDefault(); padButtonBuilderClose(); };
    dialog.onkeydown = event => {
        if (event.key === 'Escape') {
            event.preventDefault();
            event.stopPropagation();
            padButtonBuilderClose();
        }
    };
    bindingSourcePickerMount(session);
    dialog.querySelector('[data-bb-change]').onclick = () => padButtonBuilderPickSource(0);
    dialog.querySelector('[data-bb-create]').onclick = padButtonBuilderCreate;
    dialog.showModal();
    padButtonBuilderLoadConfig(session);
}

function padButtonBuilderPickSource(index) {
    const session = padButtonBuilderSession;
    padButtonBuilderStopLookup();
    padColorPopoverClose();
    session.sourceIndex = index;
    const dialog = session.dialog;
    dialog.dataset.stage = 'source';
    dialog.querySelector('#button-builder-title').textContent = 'Build a button from...';
    const search = dialog.querySelector('#button-builder-search');
    search.value = '';
    dialog.querySelector('[data-bb-matches]').replaceChildren();
    dialog.querySelector('[data-bb-retry]').hidden = true;
    dialog.querySelector('[data-bb-source]').hidden = false;
    dialog.querySelector('[data-bb-provider-picker]')?.removeAttribute('hidden');
    dialog.querySelector('[data-bb-design]').hidden = true;
    dialog.querySelector('[data-bb-picked]')?.setAttribute('hidden', '');
    dialog.querySelector('[data-bb-create]').disabled = true;
    if (session.config) padButtonBuilderChooseProvider(session.provider.id);
    const current = session.draft.sources[index]?.candidate;
    if (session.picker && current?.selection) {
        padButtonBuilderChooseProvider(current.providerId, session);
        bindingSourcePickerSelect(session, current);
        return;
    }
    padButtonBuilderStatus('Choose a source for value ' + (index + 1) + '.');
    search.focus();
}

function padButtonBuilderStopLookup(session = padButtonBuilderSession) {
    clearTimeout(session.timer);
    if (session.controller) session.controller.abort();
    session.controller = null;
    session.stopConfiguration?.();
    session.stopConfiguration = null;
    session.dialog.querySelector('[data-bb-parameters]')?.setAttribute('hidden', '');
    delete session.pendingCandidate;
    const values = session.dialog.querySelector('[data-bb-value]');
    if (values) values.hidden = true;
}

function padButtonBuilderChooseProvider(id, session = padButtonBuilderSession, focusSearch = true) {
    padButtonBuilderStopLookup(session);
    const provider = BUTTON_BUILDER_PROVIDERS.find(item => item.id === id);
    if (!provider) throw new Error('Choose an available source provider.');
    session.provider = provider;
    const dialog = session.dialog;
    dialog.querySelectorAll('[data-bb-provider]').forEach(input => { input.checked = input.value === id; });
    const unavailable = provider.availability(session.config);
    const setup = dialog.querySelector('[data-bb-setup]');
    setup.hidden = !unavailable;
    setup.firstChild.textContent = unavailable + ' ';
    const settings = dialog.querySelector('[data-bb-settings]');
    settings.hidden = !provider.setupHref;
    settings.href = provider.setupHref || '#';
    dialog.querySelector('[data-bb-source-label]').textContent = provider.sourceLabel || 'Source';
    const search = dialog.querySelector('#button-builder-search');
    search.value = '';
    search.maxLength = provider.inputLimit || 191;
    search.placeholder = provider.searchPlaceholder || 'Search or enter an exact value';
    dialog.querySelector('#button-builder-search-help').textContent =
        provider.searchHelp || 'Search for a value or enter its exact identifier.';
    dialog.querySelector('[data-bb-use]').textContent = provider.useLabel || 'Use value';
    dialog.querySelector('[data-bb-use]').hidden = !!provider.search;
    dialog.querySelector('[data-bb-catalog-search-panel]')?.removeAttribute('hidden');
    dialog.querySelector('[data-bb-picked]')?.setAttribute('hidden', '');
    dialog.querySelector('[data-bb-selection]')?.setAttribute('hidden', '');
    dialog.querySelector('#button-builder-search-help').hidden = true;
    dialog.querySelector('[data-bb-matches]').replaceChildren();
    dialog.querySelector('[data-bb-retry]').hidden = true;
    dialog.querySelector('[data-bb-source]').hidden = !!unavailable;
    padButtonBuilderStatus('', false, session);
    if (!unavailable) {
        if (focusSearch) search.focus();
        if (provider.listOnOpen) padButtonBuilderLookup(false, undefined, session);
    }
}

function padButtonBuilderOfferValues(session, candidate) {
    session.pendingCandidate = candidate;
    const dialog = session.dialog;
    const fields = candidate.fields || [];
    if (fields.length === 1 && !candidate.payloadTruncated) {
        padButtonBuilderAcceptCandidate(session, session.provider.selectField(candidate, fields[0].path));
        return;
    }
    const matches = dialog.querySelector('[data-bb-value-matches]');
    matches.replaceChildren();
    for (const field of fields) {
        const button = document.createElement('button');
        button.type = 'button';
        button.className = 'binding-builder-suggestion';
        button.innerHTML = '<span class="binding-builder-suggestion-info"><strong>' +
            _bhEsc(field.displayName) + '</strong><code>' +
            _bhEsc(mqttBindingVisibleSample(field.sample ?? '')) + '</code></span>';
        button.onclick = () => {
            try {
                padButtonBuilderAcceptCandidate(session, session.provider.selectField(candidate, field.path));
            } catch (error) { padButtonBuilderStatus(error.message, true, session); }
        };
        matches.appendChild(button);
    }
    dialog.querySelector('[data-bb-value]').hidden = false;
    dialog.querySelector('[data-bb-catalog-search-panel]').hidden = true;
    dialog.querySelector('[data-bb-value] details').hidden =
        candidate.payloadTruncated || candidate.malformedJson;
    dialog.querySelector('#button-builder-path').value = '';
    dialog.querySelector('[data-bb-value-note]').textContent = candidate.payloadTruncated
        ? 'The message is truncated. JSON fields are unavailable.' : '';
    padButtonBuilderStatus('', false, session);
}

async function padButtonBuilderLoadConfig(session = padButtonBuilderSession) {
    const retry = session.dialog.querySelector('[data-bb-retry]');
    retry.hidden = true;
    padButtonBuilderStatus('Loading sources...', false, session);
    const controller = new AbortController();
    session.controller = controller;
    const timeout = setTimeout(() => controller.abort(), 15000);
    try {
        const response = await fetch('/api/config', { cache: 'no-store', signal: controller.signal });
        if (!response.ok) throw new Error('Could not load source configuration (HTTP ' + response.status + ').');
        const config = await response.json();
        await _bhLoad();
        if (_bindingHelp.error) throw new Error(_bindingHelp.error);
        if (!bindingSourceSessionActive(session) || controller !== session.controller || controller.signal.aborted) return;
        const providers = bindingSourceProviders(config);
        const provider = providers.find(item => item.preferred && !item.availability(config)) ||
            providers.find(item => !item.availability(config)) || providers[0];
        if (!provider) {
            padButtonBuilderStatus('No sources are available. Use the binding reference or enter a binding manually.', true, session);
            return;
        }
        session.config = config;
        session.dialog.querySelector('[data-bb-provider-picker]').innerHTML =
            bindingSourceProviderOptions(config, session.bindingOnly ? 'binding-source-provider' : 'button-source-provider');
        const initial = providers.find(item => item.id === session.initialProvider);
        padButtonBuilderChooseProvider((initial || provider).id, session);
    } catch (error) {
        if (!bindingSourceSessionActive(session) || controller !== session.controller) return;
        padButtonBuilderStatus(controller.signal.aborted ? 'Source availability lookup timed out. Retry to continue.' : error.message, true, session);
        retry.hidden = false;
        retry.onclick = () => padButtonBuilderLoadConfig(session);
    } finally {
        clearTimeout(timeout);
        if (session.controller === controller) session.controller = null;
    }
}

async function padButtonBuilderLookup(exact, selected, session = padButtonBuilderSession, editParameters = false) {
    const dialog = session.dialog;
    const provider = session.provider;
    const query = dialog.querySelector('#button-builder-search').value.trim();
    padButtonBuilderStopLookup(session);
    const controller = new AbortController();
    session.controller = controller;
    dialog.querySelector('[data-bb-matches]').replaceChildren();
    const retry = dialog.querySelector('[data-bb-retry]');
    retry.hidden = true;
    const reference = selected?.reference || (exact && provider.acceptsReference(query)
        ? provider.referenceFromInput(query) : null);
    if (exact && !reference) {
        padButtonBuilderStatus(provider.invalidReferenceMessage || 'Enter a valid source identifier.', true, session);
        session.controller = null;
        return;
    }
    if (reference && provider.configureSelection &&
        (editParameters || !session.picker || provider.requiresParameters?.(reference))) {
        const root = dialog.querySelector('[data-bb-parameters]');
        root.hidden = false;
        const catalogSearchPanel = dialog.querySelector('[data-bb-catalog-search-panel]');
        if (catalogSearchPanel) catalogSearchPanel.hidden = true;
        session.stopConfiguration = provider.configureSelection(root, reference, candidate => {
            padButtonBuilderAcceptCandidate(session, candidate);
            session.stopConfiguration?.();
            session.stopConfiguration = null;
            root.hidden = true;
        }, session.page === undefined ? undefined : 'pad_' + session.page, selected, () => {
            padButtonBuilderStopLookup(session);
            if (catalogSearchPanel) catalogSearchPanel.hidden = false;
            padButtonBuilderLookup(false, undefined, session);
        }, session.rawOnly);
        session.controller = null;
        padButtonBuilderStatus('', false, session);
        return;
    }
    padButtonBuilderStatus(reference ? (provider.loadingMessage || 'Loading value...') : 'Searching...', false, session);
    const timeout = setTimeout(() => controller.abort(), 15000);
    try {
        const result = reference
            ? { candidate: await (session.picker && provider.prepare
                ? provider.prepare(reference, controller.signal,
                    session.page === undefined ? undefined : 'pad_' + session.page)
                : provider.inspect(reference, controller.signal)) }
            : await provider.search(query, controller.signal, session.bindingOnly);
        if (!bindingSourceSessionActive(session) || controller !== session.controller || controller.signal.aborted) {
            if (result.candidate) padButtonBuilderDisposeSource({ candidate: result.candidate });
            return;
        }
        if (result.candidate) {
            const candidate = result.candidate;
            if (provider.selectField && candidate.providerId !== 'ha') padButtonBuilderOfferValues(session, candidate);
            else padButtonBuilderAcceptCandidate(session, candidate);
        } else {
            if (!Array.isArray(result.candidates)) throw new Error('Invalid source suggestions.');
            for (const candidate of result.candidates) {
                const button = document.createElement('button');
                button.type = 'button';
                button.className = 'binding-builder-suggestion';
                button.innerHTML = '<span class="binding-builder-suggestion-info"><strong></strong><code></code></span>';
                button.querySelector('strong').textContent = candidate.displayName;
                button.querySelector('code').textContent = candidate.sourceId;
                button.onclick = () => padButtonBuilderLookup(true, candidate, session);
                dialog.querySelector('[data-bb-matches]').appendChild(button);
            }
            padButtonBuilderStatus(result.more ? (provider.moreResultsMessage || 'More results are available. Keep typing to narrow them.') :
                result.candidates.length ? '' : (provider.noResultsMessage || 'No matching values. Try another search.'), false, session);
        }
    } catch (error) {
        if (!bindingSourceSessionActive(session) || controller !== session.controller) return;
        padButtonBuilderStatus(controller.signal.aborted ? 'Source lookup timed out. Retry to continue.' : error.message, true, session);
        retry.hidden = false;
        retry.onclick = () => padButtonBuilderLookup(exact, selected, session);
    } finally {
        clearTimeout(timeout);
        if (session.controller === controller) session.controller = null;
    }
}

function padButtonBuilderAcceptCandidate(session, candidate, confirmed = false) {
    const dialog = session.dialog;
    if (candidate.providerId !== session.provider.id || !candidate.sourceId || !candidate.displayName ||
        !candidate.capabilities?.display) {
        throw new Error('The source provider returned an invalid candidate.');
    }
    if (session.picker && !confirmed) {
        bindingSourcePickerSelect(session, candidate);
        return;
    }
    if (session.sourceIndex !== undefined) {
        const index = session.sourceIndex;
        const draft = session.draft;
        if (draft.sources.some((source, other) => other !== index &&
            source.candidate.providerId === candidate.providerId &&
            source.candidate.sourceId === candidate.sourceId)) {
            throw new Error('This value is already included. Choose another value.');
        }
        const previous = draft.sources[index];
        const nextSource = padButtonBuilderSource(session.scenario, candidate, index, previous);
        const nextSources = [...draft.sources];
        nextSources[index] = nextSource;
        const nextDraft = { ...draft, sources: nextSources };
        const available = padButtonBuilders.filter(builder => builder.available(nextDraft, deviceInfoCache));
        const scenario = session.scenario?.available(nextDraft, deviceInfoCache) ? session.scenario :
            available.find(builder => builder.preferred) || available[0];
        if (!scenario) throw new Error('No button scenarios are available for this value.');
        scenario.validateSource?.(nextSource);
        draft.sources[index] = nextSource;
        if (previous && previous.candidate !== candidate) padButtonBuilderDisposeSource(previous);
        if (index === 0) draft.previewState = candidate.sample;
        const titleEdited = draft.titleEdited ||
            (!!previous && draft.options.title !== previous.candidate.displayName);
        const title = titleEdited ? draft.options.title :
            (index === 0 ? candidate.displayName : draft.options.title);
        padButtonBuilderSetScenario(session, scenario, title, scenario === session.scenario);
        draft.titleEdited = titleEdited;
    } else {
        session.draft.sources = [padButtonBuilderSource(null, candidate, 0)];
        session.draft.previewState = candidate.sample;
        const available = padButtonBuilders.filter(builder => builder.available(session.draft, deviceInfoCache));
        const scenario = available.find(builder => builder.preferred) || available[0];
        if (!scenario) throw new Error('No button scenarios are available for this value.');
        padButtonBuilderSetScenario(session, scenario, candidate.displayName);
    }
    padButtonBuilderStopLookup(session);
    delete session.sourceIndex;
    dialog.querySelector('[data-bb-picked]')?.setAttribute('hidden', '');
    dialog.querySelector('[data-bb-source]').hidden = true;
    dialog.querySelector('[data-bb-provider-picker]')?.setAttribute('hidden', '');
    const setup = dialog.querySelector('[data-bb-setup]');
    if (setup) setup.hidden = true;
    dialog.querySelector('[data-bb-matches]').replaceChildren();
    dialog.querySelector('[data-bb-retry]').hidden = true;
    dialog.dataset.stage = 'design';
    dialog.querySelector('#button-builder-title').textContent = 'Customize your button';
    dialog.querySelector('[data-bb-selection]').hidden = false;
    dialog.querySelector('[data-bb-design]').hidden = false;
    padButtonBuilderRenderChoices();
    padButtonBuilderRenderFields();
    dialog.querySelector('[data-bb-choices] input:checked').focus();
    padButtonBuilderStatus('');
}

function padButtonBuilderRenderChoices() {
    const session = padButtonBuilderSession;
    const choices = session.dialog.querySelector('[data-bb-choices]');
    choices.replaceChildren();
    const available = padButtonBuilders.filter(item => item.available(session.draft, deviceInfoCache));
    choices.closest('fieldset').hidden = available.length === 1 && !!session.draft.sources[0].candidate.selection;
    for (const builder of available) {
        const label = document.createElement('label');
        const radio = document.createElement('input');
        radio.type = 'radio';
        radio.name = 'button-builder-scenario';
        radio.value = builder.id;
        radio.checked = builder === session.scenario;
        radio.onchange = () => {
            padButtonBuilderSetScenario(session, builder, session.draft.options.title);
            padButtonBuilderRenderFields();
        };
        const text = document.createElement('span');
        const heading = document.createElement('strong');
        heading.textContent = builder.label;
        const description = document.createElement('small');
        description.textContent = builder.description;
        text.append(heading, description);
        label.append(radio, text);
        choices.appendChild(label);
    }
}

function padButtonBuilderRenderFields() {
    const session = padButtonBuilderSession;
    const builder = session.scenario;
    const draft = session.draft;
    const fields = session.dialog.querySelector('[data-bb-fields]');
    padColorPopoverClose();
    fields.replaceChildren();
    padButtonBuilderRenderSelection();
    padButtonBuilderRenderOptions(fields, session.fields, draft.options);
    padButtonBuilderRenderSources();
    const states = session.dialog.querySelector('[data-bb-states]');
    states.replaceChildren();
    if (builder.previewStates?.length) {
        const caption = document.createElement('span');
        caption.className = 'binding-builder-note';
        caption.textContent = 'Simulate state:';
        states.appendChild(caption);
    }
    for (const [state, label] of builder.previewStates || []) {
        const button = document.createElement('button');
        button.type = 'button';
        button.className = 'btn btn-sm btn-secondary';
        button.textContent = label;
        button.dataset.state = state;
        button.onclick = () => { draft.previewState = state; padButtonBuilderUpdatePreview(); };
        states.appendChild(button);
    }
    session.dialog.querySelector('[data-bb-note]').textContent = builder.note || '';
    padButtonBuilderUpdatePreview();
}

function padButtonBuilderRenderSources() {
    const session = padButtonBuilderSession;
    const sources = session.draft.sources;
    const host = session.dialog.querySelector('[data-bb-sources]');
    host.hidden = !session.scenario.maxSources;
    session.dialog.querySelector('[data-bb-selection]').hidden = !!session.scenario.maxSources;
    host.replaceChildren();
    if (!session.scenario.maxSources) return;
    const heading = document.createElement('h3');
    heading.textContent = 'Values';
    host.appendChild(heading);
    sources.forEach((source, index) => {
        const group = document.createElement('fieldset');
        group.className = 'pad-button-builder-source';
        const legend = document.createElement('legend');
        legend.textContent = 'Value ' + (index + 1) + ': ' + source.name;
        const entity = document.createElement('code');
        entity.textContent = source.candidate.sourceId;
        const state = document.createElement('p');
        state.className = 'binding-builder-note';
        const valueText = padButtonBuilderCandidateValueText(source.candidate);
        state.textContent = valueText + (valueText ? bindingBuilderUnitSuffix(source.candidate.unit || '') : '');
        state.hidden = !valueText;
        group.appendChild(legend);
        group.appendChild(entity);
        group.appendChild(state);
        const controls = document.createElement('div');
        controls.className = 'binding-builder-fields';
        padButtonBuilderRenderOptions(controls, session.scenario.sourceFields?.(source, index) || [], source.options);
        group.appendChild(controls);
        const actions = document.createElement('div');
        actions.className = 'pad-button-builder-source-actions';
        for (const [text, action] of [
            ['Edit value', () => padButtonBuilderPickSource(index)],
            ...(sources.length > 1 ? [['Remove value', () => {
                padColorPopoverClose();
                padButtonBuilderDisposeSource(sources.splice(index, 1)[0]);
                if (index === 0) {
                    session.draft.previewState = sources[0].candidate.sample;
                    padButtonBuilderRenderChoices();
                }
                padButtonBuilderRenderFields();
                session.dialog.querySelector('[data-bb-add-source]').focus();
            }]] : [])
        ]) {
            const button = document.createElement('button');
            button.type = 'button';
            button.className = 'btn btn-sm btn-secondary';
            button.textContent = text;
            button.onclick = action;
            actions.appendChild(button);
        }
        group.appendChild(actions);
        host.appendChild(group);
    });
    const add = document.createElement('button');
    add.type = 'button';
    add.className = 'btn btn-sm btn-secondary';
    add.textContent = 'Add another value';
    add.dataset.bbAddSource = '';
    add.disabled = sources.length >= session.scenario.maxSources;
    add.onclick = () => padButtonBuilderPickSource(sources.length);
    host.appendChild(add);
}

function padButtonBuilderRenderOptions(host, fields, options) {
    for (const field of fields) {
        const label = document.createElement('label');
        label.textContent = field.label;
        if (field.type === 'color') label.className = 'pad-button-builder-color';
        const input = document.createElement(field.type === 'select' ? 'select' : 'input');
        input.className = field.type === 'color' ? 'bc-input' : 'form-control';
        for (const key of ['type', 'min', 'max', 'maxlength']) {
            if (field[key] !== undefined && !(key === 'type' && field.type === 'select')) input.setAttribute(key, field[key]);
        }
        for (const [value, text] of field.options || []) {
            const option = document.createElement('option');
            option.value = value;
            option.textContent = text;
            input.appendChild(option);
        }
        if (field.type === 'number') input.step = field.step || '1';
        input.required = !!field.required;
        input.name = field.name;
        input.value = options[field.name];
        input[field.type === 'select' ? 'onchange' : 'oninput'] = () => {
            options[field.name] = input.value;
            if (field.name === 'title') padButtonBuilderSession.draft.titleEdited = true;
            padButtonBuilderUpdatePreview();
        };
        if (field.type === 'color') {
            input.type = 'text';
            input.maxLength = 7;
            input.dataset.allowBindings = 'false';
            const container = document.createElement('span');
            container.className = 'bindable-color';
            const swatch = document.createElement('button');
            swatch.type = 'button';
            swatch.className = 'bc-swatch';
            swatch.setAttribute('aria-label', 'Choose ' + field.label.toLowerCase());
            container.append(swatch, input);
            label.appendChild(container);
            padInitBindableColor(container);
        } else {
            label.appendChild(input);
        }
        if (field.link) {
            const link = document.createElement('a');
            link.className = 'binding-builder-note';
            link.textContent = field.link.label;
            link.href = field.link.href;
            link.target = '_blank';
            link.rel = 'noopener noreferrer';
            label.appendChild(link);
        }
        host.appendChild(label);
    }
}

function padButtonBuilderBuild(session) {
    padButtonBuilderValidateFields(session);
    const draft = session.draft;
    const button = { col: session.col, row: session.row, label_top: draft.options.title.trim(),
        ...session.scenario.build(draft) };
    for (const [key, value] of Object.entries(button)) {
        if (typeof value !== 'string') continue;
        if (['label_top', 'label_center', 'label_bottom'].includes(key)) {
            padButtonBuilderValidateText(value, 191);
        } else if (value.includes('[')) {
            const limit = /^widget_sparkline_current_label(?:_\d+)?$/.test(key) ? 63 : 191;
            padButtonBuilderValidateText(value, limit);
        }
    }
    session.scenario.validateOutput?.(draft, button);
    return button;
}

function padButtonBuilderValidateText(value, limit) {
    const text = value || '';
    if (new TextEncoder().encode(text).length > limit) throw new Error('Button text or binding exceeds the ' + limit + ' UTF-8 byte limit.');
    if (text.includes('[')) {
        const result = validateBinding(text);
        if (!result.valid) throw new Error(result.errors[0].message);
    }
}

function padButtonBuilderUpdatePreview() {
    const session = padButtonBuilderSession;
    const draft = session.draft;
    const validation = session.dialog.querySelector('[data-bb-validation]');
    const create = session.dialog.querySelector('[data-bb-create]');
    const preview = session.dialog.querySelector('[data-bb-preview]');
    preview.replaceChildren();
    try {
        const button = padButtonBuilderBuild(session);
        const source = draft.sources[0]?.candidate;
        const provider = source ? padButtonBuilderProvider(source) : null;
        session.dialog.querySelector('[data-bb-preview-note]').textContent =
            session.scenario.previewNote?.(draft) || provider?.previewNote?.(source) ||
            'Preview only. Does not control the entity.';
        const overrides = session.scenario.preview(draft);
        const sample = { ...button, ...overrides };
        padRenderCellContent(preview, sample);
        if (session.scenario.renderPreview) session.scenario.renderPreview(draft, preview, sample);
        const title = preview.querySelector('.pad-cell-label-top');
        if (title && button.label_top_style?.startsWith('color:')) title.style.color = button.label_top_style.slice(6);
        session.dialog.querySelectorAll('[data-bb-states] button').forEach(button => {
            button.setAttribute('aria-pressed', String(button.dataset.state === draft.previewState));
        });
        validation.hidden = true;
        create.disabled = false;
    } catch (error) {
        validation.textContent = error.message;
        validation.hidden = false;
        create.disabled = true;
    }
}

function padButtonBuilderCreate() {
    const session = padButtonBuilderSession;
    let button;
    try {
        if (!session.draft.sources.length) throw new Error('Choose a source and load its details before creating a button.');
        if (session.sourceIndex !== undefined) throw new Error('Finish value selection before creating the button.');
        if (session.page !== padState.page || !padButtonBuilderCanCreate(session.col, session.row)) {
            throw new Error('This position is no longer empty. Cancel and select an empty position.');
        }
        button = padButtonBuilderBuild(session);
    } catch (error) {
        const validation = session.dialog.querySelector('[data-bb-validation]');
        validation.textContent = error.message;
        validation.hidden = false;
        return;
    }
    padWorkspaceClearButton(session.col, session.row);
    padState.buttons.push(button);
    padMarkDirty();
    padButtonBuilderClose();
    padDialogOpen(button.col, button.row);
    showMessage('Button created. Save the pad to keep your changes.', 'success');
}
