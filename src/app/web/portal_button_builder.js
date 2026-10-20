let padButtonBuilderSession = null;

function padButtonBuilderSetScenario(session, builder, title = session.draft.sources[0].name) {
    const draft = session.draft;
    session.scenario = builder;
    session.fields = [
        { name: 'title', label: 'Title', type: 'text', maxlength: 191, default: title },
        ...builder.fields(draft)
    ];
    draft.options = Object.fromEntries(session.fields.map(field => [field.name, field.default]));
    if (builder.sourceFields) {
        draft.sources = draft.sources.map((source, index) =>
            padButtonBuilderSource(builder, source.detail, source.name, index, source));
    }
}

function padButtonBuilderSource(builder, detail, name, index, previous) {
    const source = { detail, name };
    const fields = builder.sourceFields?.(source, index) || [];
    source.options = { ...Object.fromEntries(fields.map(field => [field.name, field.default])),
        ...previous?.options, ...builder.sourceOptions?.(source, previous) };
    return source;
}

function padButtonBuilderRenderSelection() {
    const session = padButtonBuilderSession;
    const { detail, name } = session.draft.sources[0];
    session.dialog.querySelector('[data-bb-name]').textContent = name;
    session.dialog.querySelector('[data-bb-id]').textContent = detail.entity_id;
    session.dialog.querySelector('[data-bb-state]').textContent = 'Current state: ' + detail.state +
        (detail.unit ? ' ' + detail.unit : '') + (detail.state_truncated ? ' (truncated sample)' : '');
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
        button.innerHTML = '<span class="binding-builder-badge" aria-hidden="true">HA</span> <span>Build a button with Home Assistant ...</span>';
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
    clearTimeout(session.timer);
    if (session.controller) session.controller.abort();
    padColorPopoverClose();
    session.dialog.close();
    session.dialog.remove();
    padButtonBuilderSession = null;
    const cell = document.querySelector('#pad-grid .pad-cell-selected');
    const entry = padWorkspace.root.querySelector('.pad-button-builder-entry:not([hidden])');
    if (entry || cell) (entry || cell).focus();
}

function padButtonBuilderStatus(message, error = false) {
    const status = padButtonBuilderSession.dialog.querySelector('[data-bb-status]');
    status.textContent = message;
    status.hidden = !message;
    status.dataset.kind = error ? 'error' : 'info';
}

function padButtonBuilderOpen() {
    if (padButtonBuilderSession || !padWorkspace || !padWorkspace.selected ||
        !padButtonBuilderCanCreate(padState.editCol, padState.editRow)) return;
    const dialog = document.createElement('dialog');
    dialog.className = 'pad-button-builder';
    dialog.setAttribute('aria-labelledby', 'button-builder-title');
    dialog.innerHTML = `
        <header><div><h2 id="button-builder-title">Home Assistant Button Builder</h2>
        <p>Create a button from a Home Assistant entity. Fine-tune it in the editor afterward.</p></div>
        <button type="button" class="btn btn-sm" data-bb-close aria-label="Close Home Assistant Button Builder">&#215;</button></header>
        <div class="pad-button-builder-body">
            <p class="pad-button-builder-target"></p>
            <p data-bb-setup hidden>Set up Home Assistant before creating a button.
                <a href="#ha-discovery" data-bb-settings>Open Home Assistant settings</a></p>
            <section data-bb-source hidden>
                <label for="button-builder-search">Home Assistant entity</label>
                <div class="binding-builder-source">
                    <input id="button-builder-search" class="form-control" autocomplete="off" spellcheck="false" maxlength="95"
                        placeholder="Search by name or enter an exact entity ID" aria-describedby="button-builder-search-help">
                    <button type="button" class="btn btn-secondary" data-bb-use>Use entity</button>
                </div>
                <p id="button-builder-search-help" class="binding-builder-note">Type at least 3 characters to search.
                    An administrator token is required for search; other tokens can look up an exact entity ID.</p>
                <div class="binding-builder-suggestions" data-bb-matches aria-label="Entity suggestions"></div>
                <button type="button" class="btn btn-sm btn-secondary" data-bb-source-cancel hidden>Cancel value selection</button>
            </section>
            <div class="binding-builder-status" id="button-builder-status" data-bb-status role="status" aria-live="polite" hidden></div>
            <button type="button" class="btn btn-sm btn-secondary" data-bb-retry hidden>Retry</button>
            <section data-bb-selection hidden class="binding-builder-selection">
                <div class="binding-builder-selection-info"><strong data-bb-name></strong><code data-bb-id></code>
                <span class="binding-builder-note" data-bb-state></span></div>
                <button type="button" class="btn btn-sm btn-secondary" data-bb-change>Change entity</button>
            </section>
            <div data-bb-design hidden>
                <fieldset class="pad-button-builder-choices"><legend>What should this button do?</legend><div data-bb-choices></div></fieldset>
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
    dialog.querySelector('.pad-button-builder-target').textContent =
        'Pad ' + (session.page + 1) + ' / Row ' + (session.row + 1) + ', column ' + (session.col + 1);
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
    dialog.querySelector('[data-bb-settings]').onclick = padButtonBuilderClose;
    const search = dialog.querySelector('#button-builder-search');
    search.oninput = () => {
        padButtonBuilderStopLookup();
        dialog.querySelector('[data-bb-matches]').replaceChildren();
        dialog.querySelector('[data-bb-retry]').hidden = true;
        padButtonBuilderStatus('');
        if (search.value.trim().length >= 3) session.timer = setTimeout(() => padButtonBuilderLookup(false), 300);
    };
    search.onkeydown = event => {
        if (event.key === 'Enter') { event.preventDefault(); padButtonBuilderLookup(true); }
    };
    dialog.querySelector('[data-bb-use]').onclick = () => padButtonBuilderLookup(true);
    dialog.querySelector('[data-bb-change]').onclick = () => {
        padButtonBuilderStopLookup();
        padColorPopoverClose();
        session.draft.sources = [];
        dialog.querySelector('[data-bb-selection]').hidden = true;
        dialog.querySelector('[data-bb-design]').hidden = true;
        dialog.querySelector('[data-bb-source]').hidden = false;
        dialog.querySelector('[data-bb-create]').disabled = true;
        dialog.querySelector('[data-bb-validation]').hidden = true;
        padButtonBuilderStatus('');
        search.focus();
    };
    dialog.querySelector('[data-bb-source-cancel]').onclick = padButtonBuilderCancelSource;
    dialog.querySelector('[data-bb-create]').onclick = padButtonBuilderCreate;
    dialog.showModal();
    padButtonBuilderLoadConfig();
}

function padButtonBuilderPickSource(index) {
    const session = padButtonBuilderSession;
    padButtonBuilderStopLookup();
    padColorPopoverClose();
    session.sourceIndex = index;
    const dialog = session.dialog;
    const search = dialog.querySelector('#button-builder-search');
    search.value = '';
    dialog.querySelector('[data-bb-matches]').replaceChildren();
    dialog.querySelector('[data-bb-retry]').hidden = true;
    dialog.querySelector('[data-bb-source]').hidden = false;
    dialog.querySelector('[data-bb-source-cancel]').hidden = false;
    dialog.querySelector('[data-bb-design]').hidden = true;
    dialog.querySelector('[data-bb-create]').disabled = true;
    padButtonBuilderStatus('Choose an entity for value ' + (index + 1) + '.');
    search.focus();
}

function padButtonBuilderCancelSource() {
    const session = padButtonBuilderSession;
    padButtonBuilderStopLookup();
    delete session.sourceIndex;
    session.dialog.querySelector('[data-bb-source]').hidden = true;
    session.dialog.querySelector('[data-bb-source-cancel]').hidden = true;
    session.dialog.querySelector('[data-bb-matches]').replaceChildren();
    session.dialog.querySelector('[data-bb-retry]').hidden = true;
    session.dialog.querySelector('[data-bb-design]').hidden = false;
    padButtonBuilderStatus('');
    padButtonBuilderRenderFields();
    const add = session.dialog.querySelector('[data-bb-add-source]');
    (add?.disabled ? session.dialog.querySelector('.pad-button-builder-source-actions button') : add)?.focus();
}

function padButtonBuilderStopLookup() {
    const session = padButtonBuilderSession;
    clearTimeout(session.timer);
    if (session.controller) session.controller.abort();
    session.controller = null;
}

async function padButtonBuilderLoadConfig() {
    const session = padButtonBuilderSession;
    const retry = session.dialog.querySelector('[data-bb-retry]');
    retry.hidden = true;
    padButtonBuilderStatus('Checking Home Assistant configuration...');
    const controller = new AbortController();
    session.controller = controller;
    const timeout = setTimeout(() => controller.abort(), 15000);
    try {
        const response = await fetch('/api/config', { cache: 'no-store', signal: controller.signal });
        if (!response.ok) throw new Error('Could not load Home Assistant configuration (HTTP ' + response.status + ').');
        const config = await response.json();
        if (padButtonBuilderSession !== session) return;
        const configured = config.caps?.ha_live === true && !!config.ha_url?.trim() && config.ha_token_set === true;
        session.dialog.querySelector('[data-bb-setup]').hidden = configured;
        session.dialog.querySelector('[data-bb-source]').hidden = !configured;
        padButtonBuilderStatus('');
        if (configured) session.dialog.querySelector('#button-builder-search').focus();
    } catch (error) {
        if (padButtonBuilderSession !== session) return;
        padButtonBuilderStatus(controller.signal.aborted ? 'Configuration lookup timed out. Retry to continue.' : error.message, true);
        retry.hidden = false;
        retry.onclick = padButtonBuilderLoadConfig;
    } finally {
        clearTimeout(timeout);
    }
}

async function padButtonBuilderLookup(entity, selected) {
    const session = padButtonBuilderSession;
    const dialog = session.dialog;
    const query = selected?.entity_id || dialog.querySelector('#button-builder-search').value.trim();
    padButtonBuilderStopLookup();
    const controller = new AbortController();
    session.controller = controller;
    dialog.querySelector('[data-bb-matches]').replaceChildren();
    const retry = dialog.querySelector('[data-bb-retry]');
    retry.hidden = true;
    if (entity && !haBindingValidEntity(query)) {
        padButtonBuilderStatus('Enter an exact entity ID, such as sensor.living_room_temperature.', true);
        return;
    }
    padButtonBuilderStatus(entity ? 'Loading entity details...' : 'Searching Home Assistant...');
    const timeout = setTimeout(() => controller.abort(), 15000);
    try {
        const payload = await haBindingLookup((entity ? 'entity=' : 'query=') + encodeURIComponent(query), controller.signal);
        if (padButtonBuilderSession !== session || controller !== session.controller || controller.signal.aborted) return;
        if (entity) {
            if (payload.entity_id !== query || typeof payload.state !== 'string' || !Array.isArray(payload.paths)) {
                throw new Error('Invalid Home Assistant entity details. Retry to continue.');
            }
            const name = payload.paths.find(path => path.path === 'attributes.friendly_name' && !path.truncated)?.value ||
                selected?.name || query;
            if (session.sourceIndex !== undefined) {
                session.scenario.validateSource?.({ detail: payload, name });
                const index = session.sourceIndex;
                const draft = session.draft;
                if (draft.sources.some((source, other) => other !== index && source.detail.entity_id === query)) {
                    throw new Error('This entity is already included. Choose another value.');
                }
                draft.sources[index] = padButtonBuilderSource(session.scenario, payload, name, index, draft.sources[index]);
                if (index === 0) {
                    draft.previewState = payload.state;
                    padButtonBuilderRenderChoices();
                }
                padButtonBuilderCancelSource();
                return;
            }
            session.draft.sources = [{ detail: payload, name, options: {} }];
            session.draft.previewState = payload.state;
            padButtonBuilderSetScenario(session,
                padButtonBuilders.find(builder => builder.preferred && builder.available(session.draft)) ||
                padButtonBuilders.find(builder => builder.available(session.draft)));
            dialog.querySelector('[data-bb-source]').hidden = true;
            dialog.querySelector('[data-bb-selection]').hidden = false;
            dialog.querySelector('[data-bb-design]').hidden = false;
            padButtonBuilderRenderChoices();
            padButtonBuilderRenderFields();
            dialog.querySelector('[data-bb-choices] input:checked').focus();
            padButtonBuilderStatus(payload.more ? 'Some entity attributes were omitted by lookup limits.' : '');
        } else {
            if (!Array.isArray(payload.entities)) throw new Error('Invalid Home Assistant entity suggestions.');
            for (const item of payload.entities) {
                const button = document.createElement('button');
                button.type = 'button';
                button.className = 'binding-builder-suggestion';
                button.innerHTML = '<span class="binding-builder-suggestion-info"><strong></strong><code></code></span>' +
                    '<span class="binding-builder-suggestion-select">Select &rarr;</span>';
                button.querySelector('strong').textContent = item.name;
                button.querySelector('code').textContent = item.entity_id;
                button.onclick = () => padButtonBuilderLookup(true, item);
                dialog.querySelector('[data-bb-matches]').appendChild(button);
            }
            padButtonBuilderStatus(payload.more ? 'Showing 20 matches. Keep typing to narrow the results.' :
                payload.entities.length ? '' : 'No matching entities. Try another name or enter an exact entity ID.');
        }
    } catch (error) {
        if (padButtonBuilderSession !== session || controller !== session.controller) return;
        padButtonBuilderStatus(controller.signal.aborted ? 'Home Assistant lookup timed out. Retry to continue.' : error.message, true);
        retry.hidden = false;
        retry.onclick = () => padButtonBuilderLookup(entity, selected);
    } finally {
        clearTimeout(timeout);
    }
}

function padButtonBuilderRenderChoices() {
    const session = padButtonBuilderSession;
    const choices = session.dialog.querySelector('[data-bb-choices]');
    choices.replaceChildren();
    for (const builder of padButtonBuilders.filter(item => item.available(session.draft))) {
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
        entity.textContent = source.detail.entity_id;
        const state = document.createElement('p');
        state.className = 'binding-builder-note';
        state.textContent = 'Current state: ' + source.detail.state + bindingBuilderUnitSuffix(source.detail.unit || '');
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
            ['Change entity', () => padButtonBuilderPickSource(index)],
            ...(sources.length > 1 ? [['Remove value', () => {
                padColorPopoverClose();
                sources.splice(index, 1);
                if (index === 0) {
                    session.draft.previewState = sources[0].detail.state;
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
    for (const field of Object.keys(button).filter(key =>
        ['label_top', 'label_center', 'label_bottom', 'fg_color'].includes(key) ||
        /^widget_data_binding(?:_\d+)?$/.test(key))) {
        padButtonBuilderValidateText(button[field], 191);
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
        session.dialog.querySelector('[data-bb-preview-note]').textContent = session.scenario.previewNote ?
            session.scenario.previewNote(draft) : 'Preview only. Does not control the entity.';
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
        if (!session.draft.sources.length) throw new Error('Choose an entity and load its details before creating a button.');
        if (session.sourceIndex !== undefined) throw new Error('Finish or cancel value selection before creating the button.');
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
