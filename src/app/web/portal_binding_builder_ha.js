const _haBuilderDefaults = {
    query: '', entity: '', name: '', manual: false, path: '', customPath: false,
    mode: 'raw', decimals: 1, unit: '', fallback: '',
    detail: null
};
const _haBuilder = { ..._haBuilderDefaults, controller: null, timer: null, generation: 0 };

function haBindingValidEntity(entity) {
    return entity.length < 96 && /^[a-z0-9_]+\.[a-z0-9_]+$/.test(entity);
}

function haBindingBuilderStop() {
    clearTimeout(_haBuilder.timer);
    _haBuilder.timer = null;
    if (_haBuilder.controller) _haBuilder.controller.abort();
    _haBuilder.controller = null;
    ++_haBuilder.generation;
}

function haBindingBuilderReset() {
    haBindingBuilderStop();
    Object.assign(_haBuilder, _haBuilderDefaults);
}

function haBindingBuilderHTML() {
    const b = _haBuilder;
    const esc = _bhEsc;
    return '<section id="ha-binding-builder" class="binding-builder" aria-labelledby="ha-builder-title">' +
        '<header class="binding-builder-header"><h5 id="ha-builder-title">Build a binding</h5>' +
        '<p>Choose your data, adjust its appearance, then use it in your pad.</p></header>' +
        '<div class="binding-builder-step" aria-labelledby="ha-builder-choose-title">' +
        '<div class="binding-builder-step-heading"><span class="binding-builder-number" aria-hidden="true">1</span>' +
        '<h6 id="ha-builder-choose-title">Choose an entity</h6></div>' +
        '<div id="ha-builder-search-panel">' +
        '<div class="binding-builder-source-heading">' +
        '<label id="ha-builder-entity-label" for="ha-builder-entity">Search entities</label>' +
        '<span>or <button type="button" class="binding-docs-link" data-ha-manual></button></span></div>' +
        '<div class="binding-builder-source">' +
        '<input id="ha-builder-entity" class="form-control" type="text" maxlength="95" autocomplete="off" spellcheck="false" ' +
        'aria-describedby="ha-builder-help ha-builder-status" value="' + esc(b.query) + '">' +
        '<button type="button" class="binding-copy-btn binding-builder-primary" data-ha-use>Use entity</button></div>' +
        '<p id="ha-builder-help" class="binding-builder-note"></p>' +
        '<div id="ha-builder-suggestions" class="binding-builder-suggestions" aria-label="Entity suggestions"></div></div>' +
        '<div id="ha-builder-selection" class="binding-builder-selection" hidden>' +
        '<div class="binding-builder-selection-info"><span class="binding-docs-badge">Selected entity</span>' +
        '<strong id="ha-builder-selected-name"></strong><code id="ha-builder-selected-id"></code>' +
        '<span id="ha-builder-selected-state" class="binding-builder-note"></span></div>' +
        '<div class="binding-builder-selection-actions">' +
        '<button type="button" class="binding-copy-btn" data-ha-change>Change entity</button>' +
        '<button type="button" class="binding-docs-link" data-ha-refresh>Refresh details</button></div></div>' +
        '<p id="ha-builder-status" class="binding-builder-status" role="status" aria-live="polite" hidden></p></div>' +
        '<div class="binding-builder-step" data-ha-format-step aria-labelledby="ha-builder-format-title">' +
        '<div class="binding-builder-step-heading"><span class="binding-builder-number" aria-hidden="true">2</span>' +
        '<h6 id="ha-builder-format-title" tabindex="-1">Format the value</h6></div>' +
        '<p class="binding-builder-note" data-ha-format-pending>Choose an entity to see value and formatting options.</p>' +
        '<div data-ha-format-controls hidden><div class="binding-builder-fields">' +
        '<label for="ha-builder-value">Value<select id="ha-builder-value" class="form-control"></select></label>' +
        '<label for="ha-builder-path" data-ha-custom-path hidden>Custom path<input id="ha-builder-path" class="form-control" ' +
        'type="text" maxlength="127" placeholder="attributes.brightness" value="' + esc(b.path) + '"></label>' +
        '<label for="ha-builder-mode">Display as<select id="ha-builder-mode" class="form-control">' +
        [['raw', 'As received'], ['number', 'Number'], ['text', 'Text']].map(([value, label]) =>
            '<option value="' + value + '"' + (value === b.mode ? ' selected' : '') + '>' + label + '</option>').join('') +
        '</select></label>' +
        '<label for="ha-builder-decimals" data-ha-number>Decimal places<select id="ha-builder-decimals" class="form-control">' +
        [0, 1, 2, 3, 4, 5, 6].map(n => '<option' + (n === b.decimals ? ' selected' : '') + '>' + n + '</option>').join('') +
        '</select></label>' +
        '<label for="ha-builder-unit" data-ha-formatted>Unit (optional)<input id="ha-builder-unit" class="form-control" ' +
        'type="text" maxlength="60" placeholder="e.g. \u00b0C or %" value="' + esc(b.unit) + '"></label></div>' +
        '<p class="binding-builder-note" data-ha-raw-note>Use As received for numeric widget inputs.</p>' +
        '<p class="binding-builder-note" data-ha-formatted>Unit spacing is automatic. Adding a unit does not convert the value.</p>' +
        '<details class="binding-builder-options"><summary>When unavailable (optional)</summary>' +
        '<label for="ha-builder-fallback">Show this instead<input id="ha-builder-fallback" class="form-control" type="text" ' +
        'placeholder="Default: ---" maxlength="80" value="' + esc(b.fallback) + '"></label>' +
        '<p class="binding-builder-note">Used when the entity or value is unavailable. Leave empty for ---.</p></details></div></div>' +
        '<div class="binding-builder-step binding-builder-step-result" data-ha-result-step aria-labelledby="ha-builder-result-title">' +
        '<div class="binding-builder-step-heading"><span class="binding-builder-number" aria-hidden="true">3</span>' +
        '<h6 id="ha-builder-result-title">Insert the binding</h6></div>' +
        '<div id="ha-builder-output"></div></div></section>';
}

// A bounded, polled job keeps HA I/O off the web-server and display tasks.
async function haBindingLookup(params, signal) {
    const deadline = Date.now() + 12000;
    let job = null;
    while (Date.now() < deadline) {
        if (signal.aborted) throw new DOMException('Lookup cancelled', 'AbortError');
        const response = await fetch('/api/ha/lookup?' + (job == null ? params : 'job=' + job),
            { cache: 'no-store', signal });
        const payload = await response.json();
        if (response.status === 409 && job == null) {
            await new Promise(resolve => setTimeout(resolve, 200));
            continue;
        }
        if (!response.ok) throw new Error(payload.error || 'Home Assistant lookup failed (HTTP ' + response.status + ').');
        if (response.status !== 202) return payload;
        if (!Number.isInteger(payload.job) || payload.job <= 0) throw new Error('Invalid HA lookup job response.');
        job = payload.job;
        await new Promise(resolve => setTimeout(resolve, 150));
    }
    throw new Error('Home Assistant lookup timed out. Retry or enter the entity ID manually.');
}

function _haBuilderStatus(root, message, kind = 'info') {
    const status = root.querySelector('#ha-builder-status');
    status.textContent = message;
    status.dataset.kind = kind;
    status.hidden = !message;
}

function _haBuilderPaths(root) {
    const b = _haBuilder;
    if (b.path && !b.customPath && !(b.detail && b.detail.paths.some(item => item.path === b.path))) b.customPath = true;
    root.querySelector('#ha-builder-value').innerHTML =
        '<option value="">State</option>' + (b.detail ? b.detail.paths : []).map(item =>
            '<option value="' + _bhEsc(item.path) + '">Attribute: ' +
            _bhEsc(item.path.replace(/^attributes\./, '')) + '</option>').join('') +
        '<option value="custom">Custom path...</option>';
    root.querySelector('#ha-builder-value').value = b.customPath ? 'custom' : b.path;
}

function _haBuilderUpdate(root) {
    const b = _haBuilder;
    const chosen = !!b.entity;
    root.querySelector('#ha-builder-search-panel').hidden = chosen;
    root.querySelector('#ha-builder-selection').hidden = !chosen;
    root.querySelector('#ha-builder-entity-label').textContent = b.manual ? 'Entity ID' : 'Search entities';
    root.querySelector('#ha-builder-entity').placeholder = b.manual
        ? 'sensor.living_room_temperature' : 'Type at least 3 characters of an entity name';
    root.querySelector('[data-ha-use]').hidden = !b.manual;
    root.querySelector('#ha-builder-help').textContent = b.manual
        ? 'Enter the exact ID and choose Use entity. Details and a sample are optional.'
        : '';
    root.querySelector('#ha-builder-help').hidden = !b.manual;
    root.querySelector('[data-ha-manual]').textContent = b.manual ? 'search entities instead' : 'enter an entity ID manually';
    root.querySelector('[data-ha-format-pending]').hidden = chosen;
    root.querySelector('[data-ha-format-controls]').hidden = !chosen;
    root.querySelector('[data-ha-format-step]').classList.toggle('is-pending', !chosen);
    root.querySelector('[data-ha-result-step]').classList.toggle('is-pending', !chosen);
    root.querySelector('[data-ha-custom-path]').hidden = !b.customPath;
    root.querySelector('[data-ha-number]').hidden = b.mode !== 'number';
    root.querySelectorAll('[data-ha-formatted]').forEach(element => { element.hidden = b.mode === 'raw'; });
    root.querySelector('[data-ha-raw-note]').hidden = b.mode !== 'raw';
    root.querySelector('#ha-builder-selected-name').textContent = b.name || 'Home Assistant entity';
    root.querySelector('#ha-builder-selected-id').textContent = b.entity;
    root.querySelector('#ha-builder-selected-state').textContent = b.detail
        ? 'Current state: ' + b.detail.state + (b.detail.unit ? ' ' + b.detail.unit : '') +
            (b.detail.state_truncated ? ' (truncated)' : '') : 'Details not fetched yet. You can already build the binding.';
    const output = root.querySelector('#ha-builder-output');
    if (!chosen) {
        output.innerHTML = '<p class="binding-builder-note">Choose an entity to build your binding.</p>';
        return;
    }
    try {
        if (b.customPath && !b.path.trim()) throw new Error('Enter a custom path, or choose State.');
        if (new TextEncoder().encode(b.path).length >= 128) throw new Error('The path must be shorter than 128 UTF-8 bytes.');
        const suffix = bindingBuilderUnitSuffix(b.unit);
        const format = bindingBuilderFormat(b.mode, b.decimals, suffix);
        const token = bindingBuilderToken('ha', b.entity, b.path, format, b.fallback);
        const detail = b.detail;
        const path = b.path === '.' || !b.path ? 'state' : b.path;
        const attribute = detail ? detail.paths.find(p => p.path === path) : null;
        const raw = detail ? path === 'state' ? detail.state : attribute ? attribute.value : null : null;
        const unavailable = detail && ['unknown', 'unavailable'].includes(detail.state);
        const sample = unavailable ? (b.fallback || '---') : bindingBuilderSample(raw, b.mode, b.decimals, suffix);
        const truncated = detail && (path === 'state' ? detail.state_truncated : attribute && attribute.truncated);
        output.innerHTML = (sample == null ? '<p class="binding-builder-note">' +
            (detail ? 'No sample for this path; it may be missing or outside lookup limits.' :
                'Sample unavailable. You can still copy or insert the binding.') + '</p>' :
            '<div class="binding-builder-sample"><span>Binding result</span><output aria-live="polite">' +
            _bhEsc(sample) + (truncated ? ' <small>(truncated)</small>' : '') +
            '</output></div>') +
            '<span class="binding-builder-code-label">Generated binding</span>' +
            '<pre class="binding-example-code"><code>' + _bhEsc(token) + '</code></pre>' +
            '<div class="binding-example-actions">' +
            _bhActions(token, { copy: 'Copy binding', insert: 'Insert binding' }) + '</div>';
    } catch (error) {
        output.innerHTML = '<p class="binding-builder-status" data-kind="error" role="alert">' + _bhEsc(error.message) + '</p>';
    }
}

async function _haBuilderLookup(root, entity) {
    haBindingBuilderStop();
    const generation = _haBuilder.generation;
    const value = entity ? _haBuilder.entity : _haBuilder.query.trim();
    const suggestions = root.querySelector('#ha-builder-suggestions');
    suggestions.innerHTML = '';
    const controller = new AbortController();
    _haBuilder.controller = controller;
    let timedOut = false;
    const timeout = setTimeout(() => { timedOut = true; controller.abort(); }, 15000);
    _haBuilderStatus(root, entity ? 'Fetching attributes and a sample...' : 'Searching Home Assistant...');
    try {
        const payload = await haBindingLookup((entity ? 'entity=' : 'query=') + encodeURIComponent(value), controller.signal);
        if (generation !== _haBuilder.generation || !root.isConnected) return;
        if (entity) {
            if (payload.entity_id !== value || typeof payload.state !== 'string' || !Array.isArray(payload.paths)) {
                throw new Error('Invalid HA entity details.');
            }
            _haBuilder.detail = payload;
            const friendlyName = payload.paths.find(item => item.path === 'attributes.friendly_name');
            if (friendlyName) _haBuilder.name = friendlyName.value;
            if (!_haBuilder.unit && ['', '.', 'state'].includes(_haBuilder.path)) _haBuilder.unit = payload.unit || '';
            root.querySelector('#ha-builder-unit').value = _haBuilder.unit;
            _haBuilderPaths(root);
            _haBuilderStatus(root, payload.more
                ? 'Some attribute suggestions are omitted. Use Custom path for other values.' : '');
            _haBuilderUpdate(root);
        } else {
            if (!Array.isArray(payload.entities)) throw new Error('Invalid HA entity suggestions.');
            suggestions.innerHTML = payload.entities.map(item =>
                '<button type="button" class="binding-builder-suggestion" data-ha-entity="' + _bhEsc(item.entity_id) +
                '" data-ha-name="' + _bhEsc(item.name) + '">' +
                '<span class="binding-builder-suggestion-info"><strong>' + _bhEsc(item.name) +
                '</strong><code>' + _bhEsc(item.entity_id) + '</code></span>' +
                '<span class="binding-builder-suggestion-select">Select <span aria-hidden="true">&rarr;</span></span></button>').join('');
            _haBuilderStatus(root, payload.more ? 'Showing 20 matches. Keep typing to narrow the results.' :
                payload.entities.length ? '' : 'No matching entities. Try another name or enter an entity ID manually.');
        }
    } catch (error) {
        if (generation !== _haBuilder.generation || !root.isConnected) return;
        if (timedOut) _haBuilderStatus(root, 'Home Assistant lookup timed out. You can still enter an entity ID manually.', 'error');
        else if (error.name !== 'AbortError') _haBuilderStatus(root,
            error.message || 'Could not reach Home Assistant. You can still enter an entity ID manually.', 'error');
    } finally {
        clearTimeout(timeout);
    }
}

function _haBuilderChoose(root, entity, name = '') {
    if (!haBindingValidEntity(entity.trim())) {
        _haBuilderStatus(root, 'Enter an exact entity ID, such as sensor.living_room_temperature.', 'error');
        return;
    }
    haBindingBuilderStop();
    Object.assign(_haBuilder, { entity: entity.trim(), name, detail: null, path: '', customPath: false, unit: '' });
    root.querySelector('#ha-builder-path').value = '';
    root.querySelector('#ha-builder-unit').value = '';
    _haBuilderPaths(root);
    _haBuilderUpdate(root);
    _haBuilderLookup(root, true);
    root.querySelector('#ha-builder-format-title').focus({ preventScroll: true });
}

function haBindingBuilderMount() {
    const root = document.getElementById('ha-binding-builder');
    if (!root) return;
    _haBuilderPaths(root);
    _haBuilderUpdate(root);
    const updateField = event => {
        const key = event.target.id.replace('ha-builder-', '');
        const b = _haBuilder;
        if (key === 'entity') {
            b.query = event.target.value;
            haBindingBuilderStop();
            root.querySelector('#ha-builder-suggestions').innerHTML = '';
            _haBuilderStatus(root, '');
            if (!b.manual && b.query.trim().length >= 3) {
                b.timer = setTimeout(() => _haBuilderLookup(root, false), 350);
            }
            return;
        }
        if (key === 'value') {
            b.customPath = event.target.value === 'custom';
            b.path = b.customPath ? '' : event.target.value;
            root.querySelector('#ha-builder-path').value = b.path;
        } else if (['path', 'mode', 'decimals', 'unit', 'fallback'].includes(key)) {
            b[key] = key === 'decimals' ? Number(event.target.value) : event.target.value;
        } else return;
        if (key === 'value' || key === 'path') {
            b.unit = b.detail && ['', '.', 'state'].includes(b.path) ? b.detail.unit || '' : '';
            root.querySelector('#ha-builder-unit').value = b.unit;
        }
        _haBuilderUpdate(root);
        if (key === 'value' && b.customPath) root.querySelector('#ha-builder-path').focus();
    };
    root.addEventListener('input', updateField);
    root.addEventListener('change', event => {
        if (event.target.tagName === 'SELECT') updateField(event);
    });
    root.addEventListener('click', event => {
        const suggestion = event.target.closest('[data-ha-entity]');
        if (suggestion) _haBuilderChoose(root, suggestion.dataset.haEntity, suggestion.dataset.haName);
        else if (event.target.closest('[data-ha-use]')) _haBuilderChoose(root, _haBuilder.query);
        else if (event.target.closest('[data-ha-refresh]')) _haBuilderLookup(root, true);
        else if (event.target.closest('[data-ha-manual], [data-ha-change]')) {
            haBindingBuilderStop();
            if (event.target.closest('[data-ha-manual]')) _haBuilder.manual = !_haBuilder.manual;
            else Object.assign(_haBuilder, { entity: '', name: '', detail: null, path: '', customPath: false, unit: '' });
            _haBuilderStatus(root, '');
            root.querySelector('#ha-builder-suggestions').innerHTML = '';
            _haBuilderPaths(root);
            _haBuilderUpdate(root);
            const input = root.querySelector('#ha-builder-entity');
            input.focus();
            input.select();
        }
    });
    root.addEventListener('keydown', event => {
        if (event.target.id === 'ha-builder-entity' && event.key === 'Enter') {
            event.preventDefault();
            if (_haBuilder.manual || haBindingValidEntity(_haBuilder.query.trim())) _haBuilderChoose(root, _haBuilder.query);
            else {
                const first = root.querySelector('[data-ha-entity]');
                if (first) first.focus();
            }
        } else if (event.target.id === 'ha-builder-entity' && event.key === 'ArrowDown') {
            const first = root.querySelector('[data-ha-entity]');
            if (first) { event.preventDefault(); first.focus(); }
        } else if (event.target.closest('[data-ha-entity]') && ['ArrowDown', 'ArrowUp'].includes(event.key)) {
            const next = event.key === 'ArrowDown' ? event.target.nextElementSibling : event.target.previousElementSibling;
            if (next) { event.preventDefault(); next.focus(); }
        }
    });
}
