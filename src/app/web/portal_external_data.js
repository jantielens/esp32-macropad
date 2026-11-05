const externalDataBuilderProvider = {
    id: 'external', name: 'External Data',
    sourceLabel: 'Source',
    searchPlaceholder: 'Filter configured sources',
    listOnOpen: true,
    setupHref: '#external-data',
    supported: config => Array.isArray(config.external_data),
    availability: config => config.external_data?.length ? '' : 'Configure an external data source first.',
    acceptsReference: () => false,
    referenceFromInput: () => null,
    async search(query, signal) {
        const response = await fetch('/api/config', { cache: 'no-store', signal });
        if (!response.ok) throw new Error('Could not load external sources.');
        const config = await response.json();
        if (!Array.isArray(config.external_data)) throw new Error('External data is unavailable.');
        const filter = query.trim().toLowerCase();
        return {
            candidates: config.external_data.filter(source => source.name.toLowerCase().includes(filter))
                .map(source => ({
                    displayName: source.name, sourceId: source.name,
                    reference: { source: source.name, state: source.state }
                })),
            more: false
        };
    },
    requiresParameters: () => true,
    configureSelection(root, reference, onUse, screen, selected, onChange) {
        let disposed = false;
        let timer = null;
        let controller = null;
        let generation = 0;
        let sample;
        let acquisitionState = 'pending';
        const initialPath = selected?.reference.path || '';
        root.innerHTML = '<button type="button" class="binding-docs-link" data-ext-change>Change source</button>' +
            '<p>Source: <strong data-ext-name></strong></p>' +
            '<label for="external-builder-kind">Value<select id="external-builder-kind" class="form-control">' +
            '<option value="">JSON path</option><option value=".@state">State</option>' +
            '<option value=".@age">Age (seconds)</option><option value=".@next">Next refresh (seconds)</option>' +
            '<option value=".@duration">Last duration (ms)</option><option value=".@size">Response size (bytes)</option>' +
            '<option value=".@http_status">HTTP status</option><option value=".@error">Last error</option></select></label>' +
            '<label data-ext-path-label for="external-builder-path">JSON path<input id="external-builder-path" ' +
            'class="form-control" maxlength="47" placeholder="current.temperature"></label>' +
            '<p class="binding-builder-note" data-ext-path-help>Leave the path empty for the JSON root. Use dots for nested keys and array indices.</p>' +
            '<div role="status" aria-live="polite"><div class="binding-builder-sample" data-ext-preview hidden>' +
            '<span data-ext-preview-label>Preview (cached)</span><output data-ext-preview-value></output></div>' +
            '<p class="binding-builder-note" data-ext-preview-status></p></div>' +
            '<button type="button" class="btn btn-primary" data-ext-use>Use value</button>';
        root.querySelector('[data-ext-name]').textContent = reference.source;
        const kind = root.querySelector('#external-builder-kind');
        const path = root.querySelector('#external-builder-path');
        const preview = root.querySelector('[data-ext-preview]');
        const previewLabel = root.querySelector('[data-ext-preview-label]');
        const previewValue = root.querySelector('[data-ext-preview-value]');
        const previewStatus = root.querySelector('[data-ext-preview-status]');
        const use = root.querySelector('[data-ext-use]');
        if ([...kind.options].some(option => option.value && option.value === initialPath)) kind.value = initialPath;
        else path.value = initialPath;
        const currentPath = () => kind.value || path.value.trim();
        function showStatus(message) {
            preview.hidden = true;
            previewStatus.hidden = false;
            previewStatus.textContent = message;
        }
        function token() {
            const value = currentPath();
            if (new TextEncoder().encode(value).length >= 48) throw new Error('The path must be shorter than 48 UTF-8 bytes.');
            return bindingBuilderToken('ext', reference.source, value, '', '__ext_missing_a__');
        }
        function update() {
            clearTimeout(timer);
            controller?.abort();
            controller = null;
            const current = ++generation;
            sample = undefined;
            acquisitionState = 'pending';
            root.querySelector('[data-ext-path-label]').hidden = !!kind.value;
            root.querySelector('[data-ext-path-help]').hidden = !!kind.value;
            let binding;
            try {
                binding = token();
            } catch (error) {
                showStatus(error.message);
                use.disabled = true;
                return;
            }
            use.disabled = true;
            showStatus('Loading preview...');
            timer = setTimeout(async () => {
                const request = new AbortController();
                controller = request;
                let timedOut = false;
                const timeout = setTimeout(() => { timedOut = true; request.abort(); }, 15000);
                try {
                    const response = await fetch('/api/pad/resolve', {
                        method: 'POST', headers: { 'Content-Type': 'application/json' },
                        body: JSON.stringify({
                            screen: screen || (typeof padState !== 'undefined' && padState.page != null
                                ? 'pad_' + padState.page : 'pad_0'),
                            bindings: [binding,
                                bindingBuilderToken('ext', reference.source, currentPath(), '', '__ext_missing_b__'),
                                bindingBuilderToken('ext', reference.source, '.@state', '', '__ext_state_missing__')]
                        }),
                        signal: request.signal
                    });
                    if (!response.ok) throw new Error('Preview request failed (HTTP ' + response.status + ').');
                    const result = await response.json();
                    if (!Array.isArray(result.resolved) || result.resolved.length !== 3 ||
                        result.resolved.some(entry => typeof entry.value !== 'string')) {
                        throw new Error('Invalid preview response.');
                    }
                    if (disposed || current !== generation || !root.isConnected) return;
                    use.disabled = false;
                    const value = result.resolved[0].value;
                    const statistic = currentPath().startsWith('.@');
                    if (value === '__ext_missing_a__' && result.resolved[1].value === '__ext_missing_b__') {
                        acquisitionState = 'unavailable';
                        showStatus(statistic ? 'This statistic is not available yet.' :
                            'No cached value is available for this path.');
                    } else if (value !== result.resolved[1].value) {
                        acquisitionState = 'error';
                        showStatus('The value changed during preview. Change the path or selection to retry.');
                    } else {
                        acquisitionState = 'received';
                        sample = value;
                        previewLabel.textContent = statistic ? 'Preview' : result.resolved[2].value === 'stale'
                            ? 'Preview (cached, stale)' : 'Preview (cached)';
                        previewValue.textContent = value === '' ? '(empty)' : value;
                        previewStatus.hidden = true;
                        preview.hidden = false;
                    }
                } catch (error) {
                    if (disposed || current !== generation || !root.isConnected ||
                        (error.name === 'AbortError' && !timedOut)) return;
                    acquisitionState = 'error';
                    use.disabled = false;
                    showStatus(timedOut ? 'Preview request timed out.' :
                        error.message || 'Preview request failed.');
                } finally {
                    clearTimeout(timeout);
                    if (controller === request) controller = null;
                }
            }, 300);
        }
        kind.onchange = update;
        path.oninput = update;
        root.querySelector('[data-ext-change]').onclick = onChange;
        use.onclick = () => {
            const value = currentPath();
            const truncated = sample !== undefined && new TextEncoder().encode(sample).length >= 127;
            const numeric = acquisitionState === 'received' && !truncated && bindingBuilderIsNumeric(sample);
            onUse({
                providerId: 'external',
                sourceId: reference.source + (value ? ';' + value : ''),
                displayName: value ? reference.source + ': ' + value : reference.source,
                reference: { ...reference, path: value },
                acquisitionState, sample, sampleTruncated: truncated, cached: !value.startsWith('.@'),
                formatModes: ['raw', 'number', 'text'],
                limits: { tokenBytes: 191, labelBytes: 191, formatBytes: 23, pathBytes: 47, sampleBytes: 127 },
                capabilities: { display: true, numericWidget: numeric, liveSampling: numeric }
            });
        };
        update();
        return () => {
            disposed = true;
            generation++;
            clearTimeout(timer);
            controller?.abort();
        };
    },
    buildBinding(candidate, selection) {
        if (new TextEncoder().encode(selection.format || '').length >= 24) {
            throw new Error('The format must be shorter than 24 UTF-8 bytes.');
        }
        const token = bindingBuilderToken('ext', candidate.reference.source, candidate.reference.path,
            selection.format || '', selection.fallback || '');
        const validation = validateBinding(token, { isWidgetBinding: selection.isWidgetBinding === true });
        if (!validation.valid) throw new Error(validation.errors[0].message);
        return token;
    },
    dispose() {}
};
BUTTON_BUILDER_PROVIDERS.push(externalDataBuilderProvider);

window.init_external_data_fragment = function () {
    const editor = document.getElementById('external-data-sources');
    const status = document.getElementById('external-data-status');
    const message = document.getElementById('external-data-message');
    const save = document.getElementById('external-data-save');

    function render(sources) {
        status.replaceChildren();
        if (!sources.length) {
            status.textContent = 'No sources configured.';
            return;
        }
        sources.forEach(function (source) {
            const line = document.createElement('p');
            let text = source.name + ': ' + source.state;
            if (source.age_seconds !== undefined) text += ' (last success ' + source.age_seconds + ' seconds ago)';
            if (source.error) text += ' — ' + source.error;
            line.textContent = text;
            status.appendChild(line);
        });
    }

    async function refresh(updateEditor) {
        const response = await fetch('/api/config', { cache: 'no-store' });
        if (!response.ok) throw new Error('Unable to load source configuration.');
        const config = await response.json();
        if (!Array.isArray(config.external_data)) throw new Error('External data is unavailable.');
        render(config.external_data);
        if (updateEditor) {
            editor.value = JSON.stringify(config.external_data.map(function (source) {
                return { name: source.name, url: source.url, refresh_seconds: source.refresh_seconds };
            }), null, 2);
        }
    }

    async function poll() {
        if (!status.isConnected) return;
        try {
            await refresh(false);
        } catch (error) {
            status.textContent = error.message;
        }
        if (status.isConnected) setTimeout(poll, 10000);
    }

    save.addEventListener('click', async function () {
        let sources;
        try {
            sources = JSON.parse(editor.value);
        } catch (error) {
            message.textContent = 'Enter a valid JSON array.';
            return;
        }
        if (!Array.isArray(sources)) {
            message.textContent = 'Enter a JSON array.';
            return;
        }
        save.disabled = true;
        try {
            const response = await fetch('/api/config?no_reboot=1', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ external_data: sources })
            });
            if (!response.ok) throw new Error('Save failed. Check source names, URLs, intervals, and device availability.');
            await refresh(false).catch(function (error) {
                status.textContent = error.message;
            });
            message.textContent = '';
            showMessage('Sources saved.', 'success');
        } catch (error) {
            message.textContent = error.message;
        } finally {
            save.disabled = false;
        }
    });

    refresh(true).then(function () {
        setTimeout(poll, 10000);
    }).catch(function (error) {
        message.textContent = error.message;
    });
};
