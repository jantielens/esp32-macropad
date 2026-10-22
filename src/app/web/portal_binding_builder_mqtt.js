const mqttBindingBuilderDefaults = {
    topic: '', candidate: null, path: '', mode: 'raw', decimals: 1, unit: '', fallback: '',
    rawOnly: false, error: ''
};
const mqttBindingBuilderState = {
    ...mqttBindingBuilderDefaults, controller: null, generation: 0
};

function mqttBindingTopicValid(topic) {
    const text = String(topic || '');
    for (let index = 0; index < text.length; index++) {
        const code = text.charCodeAt(index);
        if (code >= 0xd800 && code <= 0xdbff) {
            const next = text.charCodeAt(index + 1);
            if (!(next >= 0xdc00 && next <= 0xdfff)) return false;
            index++;
        } else if (code >= 0xdc00 && code <= 0xdfff) {
            return false;
        }
    }
    const bytes = new TextEncoder().encode(text);
    return bytes.length > 0 && bytes.length <= 127 &&
        !/[+#;[\]|]/.test(text) && !text.includes('\0');
}

function mqttBindingScalarFields(payload) {
    const fields = [];
    const encoder = new TextEncoder();
    const addScalar = (path, value) => {
        if (!path || encoder.encode(path).length > 47 || /[;[\]|]/.test(path)) return;
        if (value === null || typeof value === 'object') return;
        const kind = typeof value === 'number'
            ? (Number.isFinite(value) ? 'number' : null)
            : typeof value === 'boolean' ? 'boolean'
                : typeof value === 'string' ? 'text' : null;
        if (!kind || fields.length >= 32) return;
        fields.push({ path, displayName: path, sample: String(value), sampleTruncated: false, valueKind: kind });
    };
    const visit = (value, path, depth) => {
        if (fields.length >= 32 || depth > 8 || value === null) return;
        if (Array.isArray(value)) return;
        if (typeof value !== 'object') {
            addScalar(path, value);
            return;
        }
        for (const [key, child] of Object.entries(value)) {
            if (!/^[A-Za-z0-9_-]+$/.test(key)) continue;
            const childPath = path ? path + '.' + key : key;
            if (encoder.encode(childPath).length > 47) continue;
            if (child && typeof child === 'object' && !Array.isArray(child)) {
                visit(child, childPath, depth + 1);
            } else {
                addScalar(childPath, child);
            }
            if (fields.length >= 32) return;
        }
    };
    try {
        visit(JSON.parse(payload), '', 0);
    } catch (_) {
        return { fields, malformed: true };
    }
    return { fields, malformed: false };
}

function mqttBindingScalarTruncated(value) {
    return new TextEncoder().encode(String(value)).length > 127;
}

function mqttBindingVisibleSample(value) {
    const text = String(value);
    let bytes = 0;
    let visible = '';
    for (const character of text) {
        const length = new TextEncoder().encode(character).length;
        if (bytes + length > 127) break;
        bytes += length;
        visible += character;
    }
    return visible;
}

function mqttBindingApiError(payload, fallback) {
    const reasons = {
        ota_active: 'MQTT inspection is paused during a firmware update. Retry afterward.',
        broker_unconfigured: 'Configure an MQTT broker before inspecting a topic.',
        broker_disconnected: 'The MQTT broker is disconnected. Reconnect and retry.',
        capacity_exhausted: 'All MQTT inspection slots are in use. Close another picker and retry.',
        inspection_capacity_exhausted: 'All MQTT inspection slots are in use. Close another picker and retry.',
        subscribe_failed: 'The MQTT client could not subscribe to this topic.',
        timeout: 'No message arrived before the inspection timed out. Edit the topic or retry.',
        unsupported_encoding: 'This topic returned a payload that cannot be displayed as UTF-8 text.',
        expired: 'This MQTT inspection expired. Inspect the topic again to continue.'
    };
    return reasons[payload && payload.error] || (payload && payload.error) || fallback;
}

async function mqttBindingWait(delay, signal) {
    if (signal.aborted) throw new DOMException('Inspection cancelled', 'AbortError');
    await new Promise((resolve, reject) => {
        const timer = setTimeout(resolve, delay);
        signal.addEventListener('abort', () => {
            clearTimeout(timer);
            reject(new DOMException('Inspection cancelled', 'AbortError'));
        }, { once: true });
    });
}

const mqttBindingProvider = {
    id: 'mqtt',
    async inspect(reference, signal) {
        const topic = String(reference && reference.topic || '');
        if (!mqttBindingTopicValid(topic)) throw new Error('Enter an exact MQTT topic of at most 127 UTF-8 bytes. Wildcards are not allowed.');
        let handle = 0;
        try {
            const post = await fetch('/api/mqtt/inspect', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ topic }),
                cache: 'no-store',
                signal
            });
            const postPayload = await post.json();
            if (!post.ok) throw new Error(mqttBindingApiError(postPayload, 'MQTT inspection could not start (HTTP ' + post.status + ').'));
            if (post.status !== 202 || !Number.isInteger(postPayload.handle) || postPayload.handle <= 0) {
                throw new Error('The device returned an invalid MQTT inspection handle.');
            }
            handle = postPayload.handle;
            const deadline = Date.now() + 10500;
            while (Date.now() < deadline) {
                if (signal.aborted) throw new DOMException('Inspection cancelled', 'AbortError');
                const response = await fetch('/api/mqtt/inspect?handle=' + encodeURIComponent(handle), {
                    cache: 'no-store', signal
                });
                const payload = await response.json();
                if (response.status === 410) throw new Error(mqttBindingApiError(payload, 'MQTT inspection expired.'));
                if (!response.ok) throw new Error(mqttBindingApiError(payload, 'MQTT inspection failed (HTTP ' + response.status + ').'));
                if (payload.status === 'received' && payload.received === true) {
                    if (payload.error === 'unsupported_encoding' || typeof payload.sample !== 'string') {
                        throw new Error(mqttBindingApiError(payload, 'The received payload cannot be used by the guided builder.'));
                    }
                    const parsed = payload.truncated ? { fields: [], malformed: false } :
                        mqttBindingScalarFields(payload.sample);
                    const rawNumeric = !payload.truncated && !mqttBindingScalarTruncated(payload.sample) &&
                        bindingBuilderIsNumeric(payload.sample);
                    const rawField = {
                        path: '',
                        displayName: 'Whole payload (raw)',
                        sample: payload.sample,
                        sampleTruncated: payload.truncated === true || mqttBindingScalarTruncated(payload.sample),
                        valueKind: rawNumeric ? 'number' : 'text',
                        formatModes: rawNumeric ? ['raw', 'number', 'text'] : ['raw'],
                        capabilities: { display: true, numericWidget: rawNumeric, liveSampling: true }
                    };
                    const fields = [rawField].concat(parsed.fields.map(field => {
                        const sampleTruncated = mqttBindingScalarTruncated(field.sample);
                        return {
                            ...field,
                            sampleTruncated,
                            formatModes: field.valueKind !== 'boolean' && bindingBuilderIsNumeric(field.sample)
                                ? ['raw', 'number', 'text']
                                : field.valueKind === 'text' ? ['raw', 'text'] : ['raw'],
                            capabilities: {
                                display: true,
                                numericWidget: field.valueKind !== 'boolean' && !payload.truncated && !sampleTruncated &&
                                    bindingBuilderIsNumeric(field.sample),
                                liveSampling: true
                            }
                        };
                    }));
                    return {
                        providerId: 'mqtt',
                        sourceId: 'mqtt:' + topic,
                        displayName: topic,
                        reference: { topic, handle, disposePromise: null },
                        acquisitionState: 'received',
                        sample: payload.sample,
                        sampleTruncated: rawField.sampleTruncated,
                        valueKind: 'json',
                        fields,
                        formatModes: ['raw'],
                        limits: { tokenBytes: 191, labelBytes: 191, formatBytes: 23, pathBytes: 47, topicBytes: 127, sampleBytes: 2047 },
                        capabilities: { display: true, numericWidget: false, liveSampling: true },
                        cached: payload.cached === true,
                        malformedJson: parsed.malformed,
                        payloadTruncated: payload.truncated === true
                    };
                }
                if (payload.status === 'failed' || payload.status === 'cleanup_failed') {
                    throw new Error(mqttBindingApiError(payload, 'MQTT inspection failed.'));
                }
                if (payload.status === 'released') {
                    throw new Error('This MQTT inspection was released before a message arrived.');
                }
                await mqttBindingWait(500, signal);
            }
            throw new Error('MQTT inspection timed out. Edit the topic or retry.');
        } catch (error) {
            if (handle) {
                await mqttBindingProvider.dispose({ reference: { topic, handle } });
            }
            throw error;
        }
    },
    buildBinding(candidate, selection) {
        const reference = candidate && candidate.reference;
        const topic = reference && reference.topic;
        if (!mqttBindingTopicValid(topic)) throw new Error('Enter a valid exact MQTT topic.');
        const path = String(selection && selection.path || '');
        if (new TextEncoder().encode(path).length > 47 || /[;[\]|]/.test(path)) {
            throw new Error('The JSON path must be at most 47 UTF-8 bytes and cannot contain binding delimiters.');
        }
        const mode = selection && selection.mode || 'raw';
        if (!['raw', 'number', 'text'].includes(mode)) throw new Error('Choose a supported MQTT format.');
        const suffix = mode === 'raw' ? '' :
            bindingBuilderUnitSuffix(String(selection && selection.unit || ''));
        const format = bindingBuilderFormat(mode, selection && selection.decimals, suffix);
        if (new TextEncoder().encode(format).length >= 24) {
            throw new Error('The MQTT format must be shorter than 24 UTF-8 bytes.');
        }
        const fallback = String(selection && selection.fallback || '');
        return bindingBuilderToken('mqtt', topic, path, format, fallback);
    },
    async dispose(candidate) {
        const reference = candidate && candidate.reference;
        if (!reference || !reference.handle) return;
        if (reference.disposePromise) return reference.disposePromise;
        reference.disposePromise = fetch('/api/mqtt/inspect?handle=' +
            encodeURIComponent(reference.handle), { method: 'DELETE', cache: 'no-store' })
            .then(async response => {
                const payload = await response.json();
                if (!response.ok || payload.status === 'cleanup_failed') {
                    throw new Error(mqttBindingApiError(payload, 'Could not release MQTT inspection.'));
                }
                return payload;
            });
        return reference.disposePromise;
    }
};

function mqttBindingBuilderStop() {
    if (mqttBindingBuilderState.controller) mqttBindingBuilderState.controller.abort();
    mqttBindingBuilderState.controller = null;
    const candidate = mqttBindingBuilderState.candidate;
    mqttBindingBuilderState.candidate = null;
    mqttBindingBuilderState.generation++;
    if (candidate) mqttBindingProvider.dispose(candidate).catch(error =>
        console.error('MQTT inspection release failed; server-side expiry remains active.', error));
}

function mqttBindingBuilderReset() {
    mqttBindingBuilderStop();
    Object.assign(mqttBindingBuilderState, mqttBindingBuilderDefaults, {
        rawOnly: bindingValidationOptions.get(_bindingHelp.target)?.isWidgetBinding === true
    });
}

function mqttBindingBuilderHTML() {
    const state = mqttBindingBuilderState;
    return '<div id="mqtt-binding-builder">' +
        '<div class="binding-builder-step">' +
        '<label for="mqtt-builder-topic">Exact MQTT topic<input id="mqtt-builder-topic" class="form-control" ' +
        'type="text" maxlength="127" autocomplete="off" spellcheck="false" value="' +
        _bhEsc(state.topic) + '" placeholder="sensors/room/state"></label>' +
        '<button type="button" class="binding-copy-btn binding-builder-primary" data-mqtt-inspect>Inspect topic</button>' +
        '<p class="binding-builder-note">Enter one exact topic. Wildcards are not supported. Inspection waits for a message and releases its temporary subscription when you change source or close this dialog.</p>' +
        '<p data-mqtt-status class="binding-builder-status" role="status" aria-live="polite"></p>' +
        '<div data-mqtt-result hidden>' +
        '<label for="mqtt-builder-path">Value<select id="mqtt-builder-path" class="form-control"></select></label>' +
        '<div data-mqtt-format></div>' +
        '<div id="mqtt-builder-output"></div></div></div></div>';
}

function mqttBindingBuilderSelectedField(candidate, path) {
    const field = (candidate.fields || []).find(item => item.path === path);
    if (!field) return null;
    return {
        ...candidate,
        sourceId: candidate.sourceId + '|' + path,
        sample: field.sample,
        sampleTruncated: field.sampleTruncated,
        valueKind: field.valueKind,
        formatModes: field.formatModes,
        capabilities: field.capabilities,
        reference: { ...candidate.reference, path }
    };
}

function mqttButtonBuilderSelectedField(candidate, path) {
    if (path === '.') path = '';
    let selected = mqttBindingBuilderSelectedField(candidate, path);
    if (!selected) {
        if (candidate.payloadTruncated || candidate.malformedJson ||
            !path || new TextEncoder().encode(path).length > 47 || /[;[\]|\0]/.test(path) ||
            !path.split('.').some(Boolean)) {
            throw new Error('Enter a dotted scalar path of at most 47 UTF-8 bytes from a complete JSON message.');
        }
        let value = JSON.parse(candidate.sample);
        for (const key of path.split('.').filter(Boolean)) {
            value = value && typeof value === 'object' && !Array.isArray(value) &&
                Object.hasOwn(value, key) ? value[key] : undefined;
        }
        if (value === null || value === undefined || typeof value === 'object') {
            throw new Error('The selected path does not contain a scalar value in this message.');
        }
        const sample = String(value);
        const numeric = typeof value !== 'boolean' && bindingBuilderIsNumeric(sample);
        const field = {
            path, displayName: path, sample,
            sampleTruncated: mqttBindingScalarTruncated(sample),
            valueKind: typeof value === 'boolean' ? 'boolean' : numeric ? 'number' : 'text',
            formatModes: numeric ? ['raw', 'number', 'text'] : ['raw'],
            capabilities: { display: true, numericWidget: numeric && !mqttBindingScalarTruncated(sample), liveSampling: true }
        };
        selected = mqttBindingBuilderSelectedField({ ...candidate, fields: [field] }, path);
    }
    if (selected.sampleTruncated) {
        throw new Error('The selected value exceeds the resolver limit of 127 UTF-8 bytes. Choose a shorter scalar path or use manual binding entry.');
    }
    return { ...selected, displayName: candidate.displayName + (path ? ' / ' + path : '') };
}

const mqttButtonBuilderProvider = {
    id: 'mqtt',
    name: 'MQTT',
    sourceLabel: 'Exact MQTT topic',
    inputLimit: 127,
    searchPlaceholder: 'sensors/room/state',
    searchHelp: 'Enter one exact topic and inspect it. No wildcard search or commands. A received message is required; temporary inspection is released after its sample is copied.',
    useLabel: 'Read topic',
    loadingMessage: 'Waiting for a received MQTT message (up to 10 seconds)...',
    invalidReferenceMessage: 'Enter an exact MQTT topic of at most 127 UTF-8 bytes, without wildcards or binding delimiters.',
    setupHref: '#mqtt',
    supported: config => config.caps?.mqtt === true,
    availability: config => mqttBindingConfigured(config) ? '' : 'Configure an MQTT broker to inspect topics.',
    acceptsReference: mqttBindingTopicValid,
    referenceFromInput: topic => ({ topic }),
    async inspect(reference, signal) {
        const candidate = await mqttBindingProvider.inspect(reference, signal);
        await mqttBindingProvider.dispose(candidate);
        return { ...candidate, reference: { topic: candidate.reference.topic } };
    },
    selectField: mqttButtonBuilderSelectedField,
    buildBinding(candidate, { format = '', fallback = '' } = {}) {
        const { topic, path = '' } = candidate.reference;
        if (new TextEncoder().encode(format).length > 23) {
            throw new Error('The MQTT format must be shorter than 24 UTF-8 bytes.');
        }
        return bindingBuilderToken('mqtt', topic, path, format, fallback);
    },
    dispose: candidate => mqttBindingProvider.dispose(candidate)
};

BUTTON_BUILDER_PROVIDERS.push(mqttButtonBuilderProvider);

function mqttBindingBuilderUpdate(root) {
    const state = mqttBindingBuilderState;
    const status = root.querySelector('[data-mqtt-status]');
    const result = root.querySelector('[data-mqtt-result]');
    if (!state.candidate) {
        result.hidden = true;
        status.textContent = state.error || 'Enter an exact topic, then inspect it. No binding is generated until a message is received.';
        return;
    }
    result.hidden = false;
    const candidate = state.candidate;
    const pathSelect = root.querySelector('#mqtt-builder-path');
    pathSelect.innerHTML = candidate.fields.map(field => '<option value="' + _bhEsc(field.path) + '">' +
        _bhEsc(field.displayName) + (field.path && field.sample != null ? ' — ' + _bhEsc(mqttBindingVisibleSample(field.sample)) : '') +
        '</option>').join('');
    if (!(candidate.fields || []).some(field => field.path === state.path)) state.path = '';
    pathSelect.value = state.path;
    const field = (candidate.fields || []).find(item => item.path === state.path);
    const modes = state.rawOnly ? ['raw'] : field ? field.formatModes : ['raw'];
    if (!modes.includes(state.mode)) state.mode = 'raw';
    const formatModes = modes.map(mode => [mode,
        mode === 'raw' ? 'As received' : mode === 'number' ? 'Number' : 'Text']);
    root.querySelector('[data-mqtt-format]').innerHTML = bindingBuilderFormatControlsHTML({
        idPrefix: 'mqtt-builder',
        dataPrefix: 'mqtt',
        mode: state.mode,
        decimals: state.decimals,
        unit: state.unit,
        fallback: state.fallback,
        formatModes,
        rawNote: state.rawOnly ? 'This field uses the raw value; formatting and units are omitted.' :
            'Use As received for numeric widget inputs.'
    });
    const selected = mqttBindingBuilderSelectedField(candidate, state.path);
    try {
        const token = mqttBindingProvider.buildBinding(selected, {
            path: state.path,
            mode: state.mode,
            decimals: state.decimals,
            unit: state.unit,
            fallback: state.fallback
        });
        const sample = selected.sample == null ? null :
            mqttBindingVisibleSample(bindingBuilderSample(selected.sample, state.mode, state.decimals,
                state.mode === 'raw' ? '' : bindingBuilderUnitSuffix(state.unit)));
        const notes = [
            candidate.cached ? 'Using a cached received message.' : '',
            candidate.sample === '' ? 'A message was received with an empty payload.' : '',
            candidate.malformedJson ? 'Payload is not valid JSON; only the raw value is available.' : '',
            candidate.payloadTruncated ? 'The stored payload was truncated; JSON fields are not suggested.' : '',
            selected.sampleTruncated ? 'The resolver can return at most 127 UTF-8 bytes for this value.' : ''
        ].filter(Boolean);
        status.dataset.kind = candidate.payloadTruncated ? 'warning' : 'info';
        status.textContent = notes.join(' ');
        root.querySelector('#mqtt-builder-output').innerHTML = bindingBuilderResultHTML(token, sample, {
            truncated: selected.sampleTruncated,
            message: 'A message was received, but this selected value has no displayable sample.'
        });
    } catch (error) {
        root.querySelector('#mqtt-builder-output').innerHTML = bindingBuilderErrorHTML(error.message);
    }
}

async function mqttBindingBuilderInspect(root) {
    mqttBindingBuilderStop();
    const state = mqttBindingBuilderState;
    const generation = state.generation;
    const controller = new AbortController();
    state.controller = controller;
    state.error = '';
    state.topic = root.querySelector('#mqtt-builder-topic').value;
    state.path = '';
    state.mode = 'raw';
    state.unit = '';
    state.fallback = '';
    if (!mqttBindingTopicValid(state.topic)) {
        state.error = 'Enter an exact MQTT topic of at most 127 UTF-8 bytes. Wildcards are not allowed.';
        state.controller = null;
        mqttBindingBuilderUpdate(root);
        return;
    }
    const status = root.querySelector('[data-mqtt-status]');
    status.dataset.kind = 'info';
    status.textContent = 'Requesting a temporary subscription…';
    try {
        const candidate = await mqttBindingProvider.inspect({ topic: state.topic }, controller.signal);
        if (generation !== state.generation || !root.isConnected) {
            mqttBindingProvider.dispose(candidate).catch(error =>
                console.error('MQTT inspection release failed; server-side expiry remains active.', error));
            return;
        }
        state.candidate = candidate;
        if (candidate.malformedJson) state.error = 'Payload is not valid JSON; the raw value remains usable.';
        mqttBindingBuilderUpdate(root);
    } catch (error) {
        if (generation !== state.generation || !root.isConnected ||
            (error && error.name === 'AbortError')) return;
        state.error = error.message || 'MQTT inspection failed. Edit the topic or retry.';
        state.candidate = null;
        mqttBindingBuilderUpdate(root);
        status.dataset.kind = 'error';
    } finally {
        if (generation === state.generation) state.controller = null;
    }
}

function mqttBindingBuilderMount() {
    const root = document.getElementById('mqtt-binding-builder');
    if (!root) return;
    mqttBindingBuilderState.rawOnly =
        bindingValidationOptions.get(_bindingHelp.target)?.isWidgetBinding === true;
    mqttBindingBuilderUpdate(root);
    root.addEventListener('input', event => {
        if (event.target.id !== 'mqtt-builder-topic') return;
        mqttBindingBuilderStop();
        mqttBindingBuilderState.topic = event.target.value;
        mqttBindingBuilderState.path = '';
        mqttBindingBuilderState.error = '';
        mqttBindingBuilderUpdate(root);
    });
    root.addEventListener('change', event => {
        const state = mqttBindingBuilderState;
        if (event.target.id === 'mqtt-builder-path') {
            state.path = event.target.value;
            state.mode = 'raw';
            state.unit = '';
            mqttBindingBuilderUpdate(root);
        } else if (event.target.id === 'mqtt-builder-mode') {
            state.mode = event.target.value;
            mqttBindingBuilderUpdate(root);
        } else if (event.target.id === 'mqtt-builder-decimals') {
            state.decimals = Number(event.target.value);
            mqttBindingBuilderUpdate(root);
        }
    });
    root.addEventListener('input', event => {
        const state = mqttBindingBuilderState;
        if (event.target.id === 'mqtt-builder-unit') state.unit = event.target.value;
        else if (event.target.id === 'mqtt-builder-fallback') state.fallback = event.target.value;
        else return;
        mqttBindingBuilderUpdate(root);
    });
    root.addEventListener('click', event => {
        if (event.target.closest('[data-mqtt-inspect]')) mqttBindingBuilderInspect(root);
    });
}

function mqttBindingConfigured(config) {
    return config.caps?.mqtt === true && !!config.mqtt_host?.trim();
}

BINDING_BUILDERS.push({
    id: 'mqtt',
    scheme: 'mqtt',
    label: 'MQTT',
    name: 'MQTT',
    description: 'an exact MQTT topic and its received message.',
    instructions: field => bindingValidationOptions.get(field)?.isWidgetBinding === true
        ? 'Inspect one exact topic and use a received value as-is.'
        : 'Inspect one exact topic, then optionally choose a JSON scalar and format.',
    tooltip: 'Build an MQTT binding',
    configured: mqttBindingConfigured,
    availability: config => {
        if (!config.caps) return '';
        if (config.caps.mqtt !== true) return 'MQTT is not available on this device. You can still enter a binding manually.';
        if (!config.mqtt_host?.trim()) return 'Configure an MQTT broker to inspect topics. Manual topic entry remains available.';
        return '';
    },
    provider: mqttBindingProvider,
    render: mqttBindingBuilderHTML,
    mount: mqttBindingBuilderMount,
    reset: mqttBindingBuilderReset,
    stop: mqttBindingBuilderStop
});
