// Each scenario owns its fields, defaults, configuration, and preview overrides.
const padButtonBuilders = [
    {
        id: 'value', label: 'Show a value',
        description: 'Show the entity name, current value, and unit.',
        note: 'The unit is display text only; changing it does not convert the value.',
        available: () => true,
        fields: detail => [
            ...(bindingBuilderIsNumeric(detail.state) || detail.unit ? [
                { name: 'decimals', label: 'Decimal places', type: 'number', min: 0, max: 6 }
            ] : []),
            { name: 'unit', label: 'Unit', type: 'text', maxlength: 60 }
        ],
        defaults: detail => ({ decimals: 1, unit: detail.unit || '' }),
        build(detail, options) {
            const numeric = bindingBuilderIsNumeric(detail.state) || !!detail.unit;
            const format = bindingBuilderFormat(numeric ? 'number' : 'text', Number(options.decimals), '');
            return {
                label_center: bindingBuilderToken('ha', detail.entity_id, '', format, ''),
                label_bottom: options.unit.trim()
            };
        },
        preview(detail, options) {
            return { label_center: ['unknown', 'unavailable'].includes(detail.state) ? '---' :
                bindingBuilderSample(detail.state, bindingBuilderIsNumeric(detail.state) ? 'number' : 'text',
                    Number(options.decimals), '') };
        }
    },
    {
        id: 'bar', label: 'Show a bar',
        description: 'Show a numeric value as a single bar within a fixed range.',
        note: 'Check the suggested range before creating the button. The unit changes display text, not the value. Fine-tune styling in the pad editor.',
        available: detail => !detail.state_truncated && bindingBuilderIsNumeric(detail.state) &&
            !!deviceInfoCache?.widget_catalog?.some(widget => widget.type === 'bar_chart'),
        fields: () => [
            { name: 'min', label: 'Minimum', type: 'number', step: 'any' },
            { name: 'max', label: 'Maximum', type: 'number', step: 'any' },
            { name: 'orientation', label: 'Orientation', type: 'select',
                options: [['vertical', 'Vertical'], ['horizontal', 'Horizontal']] },
            { name: 'decimals', label: 'Decimal places', type: 'number', min: 0, max: 6 },
            { name: 'unit', label: 'Unit', type: 'text', maxlength: 60 }
        ],
        defaults(detail) {
            const value = Number(detail.state);
            const percentage = detail.unit?.trim() === '%';
            let max = 100;
            if (!percentage && value !== 0) {
                const target = Math.abs(value) * 2;
                const scale = 10 ** Math.floor(Math.log10(target));
                max = [1, 2, 5, 10].find(step => step * scale >= target) * scale;
            }
            return { min: !percentage && value < 0 ? -max : 0, max,
                orientation: 'vertical', decimals: 1, unit: detail.unit || '' };
        },
        build(detail, options) {
            if (!bindingBuilderIsNumeric(options.min) || !bindingBuilderIsNumeric(options.max)) {
                throw new Error('Enter a numeric minimum and maximum.');
            }
            const min = Number(options.min), max = Number(options.max);
            const range = Math.fround(Math.fround(max) - Math.fround(min));
            if (!Number.isFinite(range) || range <= 0) {
                throw new Error('Maximum must be greater than minimum within the supported numeric range.');
            }
            if (!['vertical', 'horizontal'].includes(options.orientation)) throw new Error('Choose a valid orientation.');
            const format = bindingBuilderFormat('number', Number(options.decimals), bindingBuilderUnitSuffix(options.unit));
            return {
                widget_type: 'bar_chart',
                widget_data_binding: bindingBuilderToken('ha', detail.entity_id, '', '', ''),
                widget_bar_min: min, widget_bar_max: max,
                widget_orientation: options.orientation,
                widget_bar_zero_centered: min < 0 && max > 0,
                label_bottom: bindingBuilderToken('ha', detail.entity_id, '', format, '')
            };
        },
        preview(detail, options) {
            return { label_bottom: bindingBuilderIsNumeric(detail.state) ?
                bindingBuilderSample(detail.state, 'number', Number(options.decimals), bindingBuilderUnitSuffix(options.unit)) : '---' };
        },
        renderPreview(cell, button, detail) {
            const bar = document.createElement('div');
            bar.className = 'pad-button-builder-bar';
            bar.dataset.orientation = button.widget_orientation;
            const track = document.createElement('div');
            track.className = 'pad-button-builder-bar-track';
            track.style.background = PAD_FIRMWARE_DEFAULTS.widget_track_color;
            const fill = document.createElement('div');
            fill.className = 'pad-button-builder-bar-fill';
            fill.style.background = PAD_FIRMWARE_DEFAULTS.widget_series_color;
            if (bindingBuilderIsNumeric(detail.state)) {
                const min = button.widget_bar_min, max = button.widget_bar_max;
                const value = Math.max(min, Math.min(max, Number(detail.state)));
                const baseline = button.widget_bar_zero_centered ? 0 : min;
                bar.setAttribute('role', 'meter');
                bar.setAttribute('aria-label', button.label_top || 'Entity value');
                bar.setAttribute('aria-valuemin', String(min));
                bar.setAttribute('aria-valuemax', String(max));
                bar.setAttribute('aria-valuenow', String(value));
                bar.setAttribute('aria-valuetext', button.label_bottom);
                track.style.setProperty('--bar-start', (Math.min(value, baseline) - min) / (max - min) * 100 + '%');
                track.style.setProperty('--bar-size', Math.abs(value - baseline) / (max - min) * 100 + '%');
            } else {
                bar.setAttribute('role', 'img');
                bar.setAttribute('aria-label', 'Bar value unavailable');
                fill.hidden = true;
            }
            track.appendChild(fill);
            bar.appendChild(track);
            cell.insertBefore(bar, cell.querySelector('.pad-cell-label-bottom'));
        }
    },
    {
        id: 'history', label: 'Show a value with history',
        description: 'Show the current numeric value with a sparkline.',
        note: 'The unit changes display text, not the value. Without Home Assistant history support, the device collects live values while it runs. Fine-tune sampling and styling in the pad editor.',
        available: detail => !detail.state_truncated && bindingBuilderIsNumeric(detail.state) &&
            !!deviceInfoCache?.widget_catalog?.some(widget => widget.type === 'sparkline'),
        fields: () => [
            { name: 'decimals', label: 'Decimal places', type: 'number', min: 0, max: 6 },
            { name: 'unit', label: 'Unit', type: 'text', maxlength: 60 },
            { name: 'window', label: 'Time range', type: 'select',
                options: [['3600', '1 hour'], ['21600', '6 hours'], ['86400', '24 hours'], ['604800', '7 days']] },
            { name: 'color', label: 'Line color', type: 'color' }
        ],
        defaults: detail => ({ decimals: 1, unit: detail.unit || '', window: '86400', color: '#4CAF50' }),
        build(detail, options) {
            const windowSeconds = Number(options.window);
            if (![3600, 21600, 86400, 604800].includes(windowSeconds)) throw new Error('Choose a valid time range.');
            if (!/^#[0-9a-f]{6}$/i.test(options.color)) throw new Error('Choose a valid line color.');
            const format = bindingBuilderFormat('number', Number(options.decimals), bindingBuilderUnitSuffix(options.unit));
            return {
                label_center: bindingBuilderToken('ha', detail.entity_id, '', format, ''),
                widget_type: 'sparkline',
                widget_data_binding: bindingBuilderToken('ha', detail.entity_id, '', '', ''),
                widget_sparkline_line_color: options.color,
                widget_sparkline_line_width: 2,
                widget_sparkline_current_dot: 5,
                widget_sparkline_smooth: 0,
                widget_sparkline_unified_scale: true,
                widget_sparkline_window: windowSeconds,
                widget_sparkline_slots: Math.min(1024, Math.max(120, Math.ceil(windowSeconds / 300))),
                ...((window.__device_caps || {}).ha_history === true ? {
                    widget_sparkline_ha_entity: detail.entity_id,
                    widget_sparkline_ha_stat: 'mean'
                } : {})
            };
        },
        preview(detail, options) {
            return { widget_type: '', label_center: bindingBuilderIsNumeric(detail.state) ?
                bindingBuilderSample(detail.state, 'number', Number(options.decimals), bindingBuilderUnitSuffix(options.unit)) : '---' };
        },
        previewNote(options) {
            const hours = Number(options.window) / 3600;
            const range = hours === 168 ? '7 days' : hours + (hours === 1 ? ' hour' : ' hours');
            return 'Last ' + range + '. Illustrative trend, not actual history. Preview only. Does not control the entity.';
        },
        renderPreview(cell, button) {
            const chart = document.createElementNS('http://www.w3.org/2000/svg', 'svg');
            chart.setAttribute('class', 'pad-button-builder-sparkline');
            chart.setAttribute('viewBox', '0 0 280 70');
            chart.setAttribute('role', 'img');
            chart.setAttribute('aria-label', 'Illustrative trend, not actual history');
            const line = document.createElementNS('http://www.w3.org/2000/svg', 'polyline');
            line.setAttribute('points', '8,54 28,50 48,52 68,30 88,26 108,40 128,44 148,24 168,18 188,22 208,38 228,36 248,20 272,16');
            line.setAttribute('fill', 'none');
            line.setAttribute('stroke', button.widget_sparkline_line_color);
            line.setAttribute('stroke-width', String(button.widget_sparkline_line_width));
            const dot = document.createElementNS('http://www.w3.org/2000/svg', 'circle');
            dot.setAttribute('cx', '272');
            dot.setAttribute('cy', '16');
            dot.setAttribute('r', String(button.widget_sparkline_current_dot / 2));
            dot.setAttribute('fill', button.widget_sparkline_line_color);
            chart.appendChild(line);
            chart.appendChild(dot);
            cell.appendChild(chart);
        }
    },
    {
        id: 'toggle', label: 'Toggle an entity',
        description: 'Toggle a light or switch and show whether it is on or off.',
        note: 'Icon color follows the entity state. Unavailable data uses a pink-red icon.',
        preferred: true,
        available: detail => /^(light|switch)\./.test(detail.entity_id) && !!actionEditorCatalogEntry('ha_service'),
        fields: () => [
            { name: 'icon', label: 'Material Symbol name', type: 'text', maxlength: 28,
                link: { label: 'Browse Material Symbols', href: 'https://fonts.google.com/icons' } },
            { name: 'on', label: 'On color', type: 'color' },
            { name: 'off', label: 'Off color', type: 'color' }
        ],
        defaults: detail => ({ icon: detail.entity_id.startsWith('light.') ? 'lightbulb' : 'toggle_on',
            on: '#FFC857', off: '#64748B' }),
        build(detail, options) {
            if (detail.entity_id.length >= 48) throw new Error('Entity IDs must be shorter than 48 characters for service actions.');
            if (!/^[a-z][a-z0-9_]{0,27}$/.test(options.icon)) throw new Error('Enter a Material Symbol name, such as lightbulb.');
            for (const color of [options.on, options.off]) {
                if (!/^#[0-9a-f]{6}$/i.test(color)) throw new Error('Choose valid on and off colors.');
            }
            const state = bindingBuilderToken('ha', detail.entity_id, '', '', '');
            return {
                icon_id: 'mi_' + options.icon, icon_position: 'center',
                label_top_style: 'color:' + (padColorNormalizeLiteral(padGetEffectiveDefault('fg_color')) || '#FFFFFF'),
                fg_color: '[expr:' + state + '=="on"?"' + options.on + '":"' + options.off + '"|#EF6B73]',
                actions: [{ type: 'ha_service', entity_id: detail.entity_id, service: 'toggle' }]
            };
        },
        preview(detail, options, state) {
            return { fg_color: state === 'on' ? options.on : state === 'off' ? options.off : '#EF6B73' };
        },
        previewStates: ['on', 'off', 'unavailable']
    }
];

let padButtonBuilderSession = null;

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

function padButtonBuilderUpdateEntry(cell, selected) {
    const show = selected && padButtonBuilderCanCreate(Number(cell.dataset.col), Number(cell.dataset.row));
    const existing = cell.querySelector('.pad-button-builder-entry');
    if (!show) {
        if (existing) cell.textContent = '+';
        return;
    }
    cell.setAttribute('role', 'group');
    cell.removeAttribute('aria-pressed');
    if (existing) return;
    cell.textContent = '';
    const button = document.createElement('button');
    button.type = 'button';
    button.className = 'pad-button-builder-entry';
    button.title = 'Open the button builder';
    button.innerHTML = '<span class="portal-icon" aria-hidden="true">touch_app</span><span>Build button...</span>';
    button.addEventListener('click', event => {
        event.stopPropagation();
        padButtonBuilderOpen();
    });
    cell.appendChild(button);
    const manual = document.createElement('button');
    manual.type = 'button';
    manual.className = 'pad-button-builder-manual';
    manual.textContent = 'Edit manually';
    manual.onclick = event => {
        event.stopPropagation();
        padWorkspaceSetScope('button');
    };
    cell.appendChild(manual);
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
    if (cell) (cell.querySelector('.pad-button-builder-entry') || cell).focus();
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
        <header><div><h2 id="button-builder-title">Button builder</h2>
        <p>Create a button from a Home Assistant entity. Fine-tune it in the editor afterward.</p></div>
        <button type="button" class="btn btn-sm" data-bb-close aria-label="Close button builder">&#215;</button></header>
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
        controller: null, timer: null, detail: null };
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
        padColorPopoverClose();
        session.detail = null;
        dialog.querySelector('[data-bb-selection]').hidden = true;
        dialog.querySelector('[data-bb-design]').hidden = true;
        dialog.querySelector('[data-bb-source]').hidden = false;
        dialog.querySelector('[data-bb-create]').disabled = true;
        dialog.querySelector('[data-bb-validation]').hidden = true;
        padButtonBuilderStatus('');
        search.focus();
    };
    dialog.querySelector('[data-bb-create]').onclick = padButtonBuilderCreate;
    dialog.showModal();
    padButtonBuilderLoadConfig();
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
            session.detail = payload;
            session.name = payload.paths.find(path => path.path === 'attributes.friendly_name' && !path.truncated)?.value ||
                selected?.name || query;
            session.scenario = padButtonBuilders.find(builder => builder.preferred && builder.available(payload)) ||
                padButtonBuilders.find(builder => builder.available(payload));
            session.options = null;
            session.previewState = payload.state;
            dialog.querySelector('[data-bb-source]').hidden = true;
            dialog.querySelector('[data-bb-selection]').hidden = false;
            dialog.querySelector('[data-bb-design]').hidden = false;
            dialog.querySelector('[data-bb-name]').textContent = session.name;
            dialog.querySelector('[data-bb-id]').textContent = query;
            dialog.querySelector('[data-bb-state]').textContent = 'Current state: ' + payload.state +
                (payload.unit ? ' ' + payload.unit : '') + (payload.state_truncated ? ' (truncated sample)' : '');
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
    for (const builder of padButtonBuilders.filter(item => item.available(session.detail))) {
        const label = document.createElement('label');
        const radio = document.createElement('input');
        radio.type = 'radio';
        radio.name = 'button-builder-scenario';
        radio.value = builder.id;
        radio.checked = builder === session.scenario;
        radio.onchange = () => {
            session.scenario = builder;
            const title = session.options.title;
            session.options = { ...builder.defaults(session.detail), title };
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
    if (!session.options) session.options = { ...builder.defaults(session.detail), title: session.name };
    const fields = session.dialog.querySelector('[data-bb-fields]');
    padColorPopoverClose();
    fields.replaceChildren();
    for (const field of [{ name: 'title', label: 'Title', type: 'text', maxlength: 191 }, ...builder.fields(session.detail)]) {
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
        input.name = field.name;
        input.value = session.options[field.name];
        input[field.type === 'select' ? 'onchange' : 'oninput'] = () => {
            session.options[field.name] = input.value;
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
        fields.appendChild(label);
    }
    const states = session.dialog.querySelector('[data-bb-states]');
    states.replaceChildren();
    if (builder.previewStates?.length) {
        const caption = document.createElement('span');
        caption.className = 'binding-builder-note';
        caption.textContent = 'Simulate state:';
        states.appendChild(caption);
    }
    for (const state of builder.previewStates || []) {
        const button = document.createElement('button');
        button.type = 'button';
        button.className = 'btn btn-sm btn-secondary';
        button.textContent = state === 'unavailable' ? 'Unavailable' : state === 'on' ? 'On' : 'Off';
        button.dataset.state = state;
        button.onclick = () => { session.previewState = state; padButtonBuilderUpdatePreview(); };
        states.appendChild(button);
    }
    session.dialog.querySelector('[data-bb-note]').textContent = builder.note || '';
    padButtonBuilderUpdatePreview();
}

function padButtonBuilderBuild(session) {
    const button = { col: session.col, row: session.row, label_top: session.options.title.trim(),
        ...session.scenario.build(session.detail, session.options) };
    for (const field of ['label_top', 'label_center', 'label_bottom', 'fg_color', 'widget_data_binding']) {
        const value = button[field] || '';
        if (new TextEncoder().encode(value).length >= 192) throw new Error('Button text or binding exceeds the 191 UTF-8 byte limit.');
        if (value.includes('[')) {
            const result = validateBinding(value);
            if (!result.valid) throw new Error(result.errors[0].message);
        }
    }
    return button;
}

function padButtonBuilderUpdatePreview() {
    const session = padButtonBuilderSession;
    const validation = session.dialog.querySelector('[data-bb-validation]');
    const create = session.dialog.querySelector('[data-bb-create]');
    const preview = session.dialog.querySelector('[data-bb-preview]');
    preview.replaceChildren();
    try {
        const button = padButtonBuilderBuild(session);
        session.dialog.querySelector('[data-bb-preview-note]').textContent = session.scenario.previewNote ?
            session.scenario.previewNote(session.options) : 'Preview only. Does not control the entity.';
        const overrides = session.scenario.preview(session.detail, session.options, session.previewState);
        const sample = { ...button, ...overrides };
        padRenderCellContent(preview, sample);
        if (session.scenario.renderPreview) session.scenario.renderPreview(preview, sample, session.detail);
        const title = preview.querySelector('.pad-cell-label-top');
        if (title && button.label_top_style?.startsWith('color:')) title.style.color = button.label_top_style.slice(6);
        session.dialog.querySelectorAll('[data-bb-states] button').forEach(button => {
            button.setAttribute('aria-pressed', String(button.dataset.state === session.previewState));
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
        if (!session.detail) throw new Error('Choose an entity and load its details before creating a button.');
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
