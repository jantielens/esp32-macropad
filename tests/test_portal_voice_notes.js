const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

class Element {
    constructor(tag) {
        this.tag = tag;
        this.children = [];
        this.attributes = {};
        this.listeners = {};
        this.classList = { add() {} };
        this.paused = true;
        this.ended = false;
        this.currentTime = 0;
    }
    appendChild(child) { this.children.push(child); return child; }
    get firstChild() { return this.children[0]; }
    get lastChild() { return this.children[this.children.length - 1]; }
    set textContent(value) { this.text = value; this.children = []; }
    get textContent() { return this.text || ''; }
    set src(value) { this.setAttribute('src', value); }
    get src() { return this.getAttribute('src'); }
    setAttribute(name, value) { this.attributes[name] = String(value); }
    getAttribute(name) { return this.attributes[name] || null; }
    removeAttribute(name) { delete this.attributes[name]; }
    addEventListener(name, callback) { this.listeners[name] = callback; }
    emit(name) { if (this.listeners[name]) this.listeners[name](); }
    querySelectorAll(tag) {
        return this.children.flatMap(child => [
            ...(child.tag === tag ? [child] : []),
            ...child.querySelectorAll(tag)
        ]);
    }
    querySelector(tag) { return this.querySelectorAll(tag)[0] || null; }
    play() {
        if (this.failPlayback) return Promise.reject(this.failPlayback);
        this.paused = false;
        this.emit('play');
        return Promise.resolve();
    }
    pause() { this.paused = true; this.emit('pause'); }
    load() {}
}

const elements = {
    'voice-notes-status': new Element('div'),
    'voice-notes-processing-status': new Element('div'),
    'voice-notes-list': new Element('div'),
    'voice-notes-refresh': new Element('button'),
    'voice-notes-catalog': new Element('div'),
    'voice-notes-detail': new Element('div'),
    'voice-notes-detail-title': new Element('h3'),
    'voice-notes-detail-meta': new Element('p'),
    'voice-notes-detail-player': new Element('div'),
    'voice-notes-detail-actions': new Element('div'),
    'voice-notes-detail-transcript': new Element('div'),
    'voice-notes-detail-summary': new Element('div'),
    'voice-notes-summary-section': new Element('section'),
    'voice-notes-back': new Element('button'),
    'voice-notes-setup': new Element('div'),
    'voice-notes-settings-save': new Element('button'),
    'voice-notes-transcription-key-status': new Element('div'),
    'voice-notes-llm-key-status': new Element('div'),
    'voice_notes_transcription_endpoint': new Element('input'),
    'voice_notes_transcription_deployment': new Element('input'),
    'voice_notes_transcription_api_key': new Element('input'),
    'voice_notes_llm_endpoint': new Element('input'),
    'voice_notes_llm_deployment': new Element('input'),
    'voice_notes_llm_api_key': new Element('input'),
    'voice_notes_auto_process': new Element('input')
};
const entries = [
    { id: '0000000100000002', created_at: 1791370589, duration_ms: 65000, has_transcript: true },
    { id: '0000000100000003', created_at: 1791366989, duration_ms: 3000, has_transcript: false }
];
let nextPoll;
let currentState = { transcription_configured: true, transcription: { state: 'idle', note_id: '', error: '' } };
let releaseInitialStatus;
const initialStatus = new Promise(resolve => { releaseInitialStatus = resolve; });
let firstStatus = true;
let failStatus = false;
let failList = false;
let listGate = null;
let listRequests = 0;
let detailRequests = 0;
let detailById = false;
let detailGate = null;
let registered = [];
let config = { voice_notes_transcription_endpoint: 'https://example.test/', voice_notes_transcription_deployment: 'deployment',
    voice_notes_transcription_api_key_set: true, voice_notes_llm_api_key_set: true, voice_notes_auto_process: true };
let savedConfig;
let leave;
elements.voice_notes_auto_process.type = 'checkbox';
elements.voice_notes_transcription_api_key.value = '';
elements.voice_notes_llm_api_key.value = '';
const context = {
    document: {
        createElement(tag) { return new Element(tag); },
        createTextNode(text) { const node = new Element('#text'); node.textContent = text; return node; },
        getElementById(id) { return elements[id]; },
        querySelector() { return null; },
        querySelectorAll() {
            return [...elements['voice-notes-list'].querySelectorAll('audio'),
                ...elements['voice-notes-detail-player'].querySelectorAll('audio')];
        }
    },
    window: {
        confirm() { return false; },
        __extra_config_fields: registered,
        registerConfigFields(fields) { registered.push(...fields); },
        addEventListener(name, callback) { if (name === 'portal-fragment-leave') leave = callback; },
        removeEventListener() {}
    },
    API_CONFIG: '/api/config',
    showMessage() {},
    console,
    AbortController,
    setTimeout(callback) { nextPoll = callback; return 1; },
    clearTimeout() {},
    fetch(url, options) {
        if (url.endsWith('list')) listRequests++;
        if (url.startsWith('/api/config')) {
            if (options && options.method === 'POST') {
                savedConfig = JSON.parse(options.body);
                config = { ...config, ...savedConfig };
            }
            return Promise.resolve({ ok: true, json: () => Promise.resolve({ ...config }) });
        }
        if (url.endsWith('status') && failStatus) return Promise.reject(new Error('Status unavailable'));
        if (url.endsWith('list') && failList) return Promise.reject(new Error('List unavailable'));
        if (url.endsWith('list') && listGate) return listGate;
        if (url.endsWith('status') && firstStatus) {
            firstStatus = false;
            return initialStatus.then(() => ({ ok: true, json: () => Promise.resolve({ enrichment_configured: true, ...currentState }) }));
        }
        if (url.includes('detail?')) detailRequests++;
        if (url.includes('detail?') && detailGate) return detailGate;
        const detailEntry = entries.find(entry => url.includes(entry.id)) || entries[0];
        return Promise.resolve({
            ok: true,
            json: () => Promise.resolve(url.endsWith('list') ? { entries: entries.map(entry => ({ ...entry })) } :
                url.includes('detail?') ? { note: { ...detailEntry }, transcript:
                    !detailById || detailEntry.has_transcript ? { text: '<script>not markup</script>' } : null,
                    enrichment: detailEntry.has_enrichment ? { title: detailEntry.title, summary: detailEntry.summary } : null,
                    enrichment_error: detailEntry.enrichment_error || '' } :
                { enrichment_configured: true, ...currentState })
        });
    }
};
vm.createContext(context);
vm.runInContext(fs.readFileSync('src/app/web/portal_config.js', 'utf8'), context);
vm.runInContext(fs.readFileSync('src/app/web/portal_voice_notes.js', 'utf8'), context);

(async function () {
    const loading = context.window.init_voice_notes_fragment();
    await new Promise(resolve => setImmediate(resolve));
    assert.strictEqual(elements['voice-notes-list'].children.length, 0);
    releaseInitialStatus();
    await loading;
    const list = elements['voice-notes-list'];
    elements['voice-notes-refresh'].emit('click');
    assert.strictEqual(elements['voice-notes-status'].hidden, true);
    await new Promise(resolve => setImmediate(resolve));
    const audios = list.querySelectorAll('audio');
    const buttons = list.querySelectorAll('button');
    const ranges = list.querySelectorAll('input');
    const plays = buttons.filter(button => (button.getAttribute('aria-label') || '').startsWith('Play'));
    assert.strictEqual(audios.length, 2);
    assert.strictEqual(list.querySelectorAll('h3').length, 1);
    assert.strictEqual(audios[0].preload, 'none');
    const noteStatuses = list.querySelectorAll('span').filter(node => node.getAttribute('role') === 'status');
    const indicators = list.querySelectorAll('span').filter(node => node.getAttribute('aria-label') === 'Transcript available');
    assert.deepStrictEqual(indicators.map(node => node.hidden), [false, true]);
    assert(noteStatuses.every(node => node.hidden));
    const noteActions = buttons.filter(button => ['View details', 'Process note'].includes(button.textContent));
    assert.deepStrictEqual(noteActions.map(button => button.textContent),
        ['View details', 'Process note', 'View details', 'Process note']);
    currentState = { transcription_configured: true,
        transcription: { state: 'transcribing', note_id: entries[1].id, error: '' } };
    nextPoll();
    await new Promise(resolve => setImmediate(resolve));
    assert.strictEqual(indicators[1].hidden, true);
    assert.strictEqual(noteStatuses[1].textContent, 'Transcribing...');
    assert.strictEqual(noteStatuses[1].hidden, false);
    currentState = { transcription_configured: true,
        transcription: { state: 'ready', note_id: entries[1].id, error: '' } };
    entries[1].has_transcript = true;
    nextPoll();
    await new Promise(resolve => setImmediate(resolve));
    assert.strictEqual(indicators[1].hidden, false);
    assert.strictEqual(noteStatuses[1].hidden, true);

    plays[0].emit('click');
    assert.strictEqual(audios[0].paused, false);
    assert(plays[0].getAttribute('aria-label').startsWith('Pause'));
    plays[1].emit('click');
    assert.strictEqual(audios[0].paused, true);
    assert(plays[0].getAttribute('aria-label').startsWith('Play'));
    assert.strictEqual(audios[1].paused, false);

    const seek = ranges.find(range => range.getAttribute('aria-label').startsWith('Seek'));
    seek.value = '32';
    seek.emit('input');
    assert.strictEqual(seek.getAttribute('aria-valuetext'), '0:32 of 1:05');
    seek.emit('change');
    assert.strictEqual(audios[0].currentTime, 32);
    audios[0].duration = 64;
    audios[0].emit('loadedmetadata');
    assert.strictEqual(seek.max, '64');

    assert.strictEqual(ranges.length, 2);

    audios[0].failPlayback = new Error('playback failed');
    plays[0].emit('click');
    await Promise.resolve();
    assert.strictEqual(elements['voice-notes-status'].textContent, 'Could not play this recording. Try again.');
    audios[0].failPlayback = false;
    plays[0].emit('click');
    assert.strictEqual(elements['voice-notes-status'].textContent, '');
    plays[0].emit('click');
    audios[0].failPlayback = Object.assign(new Error('interrupted'), { name: 'AbortError' });
    plays[0].emit('click');
    await Promise.resolve();
    assert.strictEqual(elements['voice-notes-status'].textContent, '');
    const details = buttons.find(button => button.textContent === 'View details');
    elements['voice-notes-back'].hidden = true;
    details.emit('click');
    await new Promise(resolve => setImmediate(resolve));
    assert.strictEqual(elements['voice-notes-back'].hidden, false);
    const transcript = elements['voice-notes-detail-transcript'];
    assert.strictEqual(transcript.textContent, '<script>not markup</script>');
    assert.strictEqual(transcript.children.length, 0);
    const detailAudio = elements['voice-notes-detail-player'].querySelector('audio');
    currentState = { transcription_configured: true,
        transcription: { state: 'ready', note_id: entries[0].id, error: '' } };
    nextPoll();
    await new Promise(resolve => setImmediate(resolve));
    assert.strictEqual(elements['voice-notes-detail-player'].querySelector('audio'), detailAudio);
    currentState = { transcription_configured: true,
        transcription: { state: 'error', note_id: entries[0].id, error: 'Connection failed' } };
    nextPoll();
    await new Promise(resolve => setImmediate(resolve));
    const detailStatus = elements['voice-notes-detail-actions'].querySelectorAll('span')
        .find(node => node.getAttribute('role') === 'status');
    assert.strictEqual(detailStatus.textContent, 'Transcription failed: Connection failed Previous transcript saved.');
    assert.strictEqual(transcript.textContent, '<script>not markup</script>');
    elements['voice-notes-back'].emit('click');
    assert.strictEqual(elements['voice-notes-back'].hidden, true);
    await new Promise(resolve => setImmediate(resolve));
    list.textContent = '';
    failStatus = true;
    await context.window.init_voice_notes_fragment();
    assert.strictEqual(elements['voice-notes-status'].textContent, 'Status unavailable');
    assert.strictEqual(list.querySelectorAll('audio').length, 2);
    assert(list.querySelectorAll('button')
        .filter(button => button.textContent === 'Process note').every(button => button.disabled));
    failStatus = false;
    currentState = { transcription_configured: false, transcription: { state: 'idle', note_id: '', error: '' } };
    nextPoll();
    await new Promise(resolve => setImmediate(resolve));
    assert.strictEqual(elements['voice-notes-status'].hidden, true);
    assert.strictEqual(elements['voice-notes-setup'].hidden, true);
    assert.deepStrictEqual(list.querySelectorAll('button')
        .filter(button => button.textContent === 'Process note').map(button => button.hidden), [false, false]);

    context.window.init_voice_notes_settings_fragment();
    await new Promise(resolve => setImmediate(resolve));
    assert(registered.includes('voice_notes_auto_process'));
    assert.strictEqual(elements.voice_notes_auto_process.checked, true);
    elements.voice_notes_auto_process.checked = false;
    elements['voice-notes-settings-save'].emit('click');
    await new Promise(resolve => setImmediate(resolve));
    assert.strictEqual(savedConfig.voice_notes_auto_process, false);
    assert.strictEqual(savedConfig.voice_notes_transcription_api_key, '');
    assert.strictEqual(savedConfig.voice_notes_llm_api_key, '');
    config.voice_notes_auto_process = true;
    context.window.init_voice_notes_settings_fragment();
    await new Promise(resolve => setImmediate(resolve));
    assert.strictEqual(elements.voice_notes_auto_process.checked, true);

    entries.forEach(entry => { entry.has_transcript = false; });
    detailById = true;
    currentState = { transcription_configured: true, processing: { state: 'idle', reason: 'none' },
        transcription: { state: 'idle', note_id: '', error: '' } };
    await context.window.init_voice_notes_fragment();
    for (const [reason, text] of [
        ['offline', 'Waiting for Wi-Fi'], ['recording', 'Waiting for recording to finish'],
        ['update', 'Waiting for device update or storage availability'], ['retry', 'Retrying later'],
        ['retries_exhausted', 'Automatic retries paused. Save a new note or retry manually.'],
        ['configuration', 'Check Voice Notes Settings'], ['none', 'Waiting to process']
    ]) {
        currentState.processing = {
            state: ['configuration', 'retries_exhausted'].includes(reason) ? 'blocked' : 'waiting', reason };
        nextPoll();
        await new Promise(resolve => setImmediate(resolve));
        assert.strictEqual(elements['voice-notes-processing-status'].textContent, text);
        assert.strictEqual(elements['voice-notes-processing-status'].hidden, false);
    }
    assert.strictEqual(elements['voice-notes-setup'].hidden, true);
    const newAudios = list.querySelectorAll('audio');
    const newIndicators = list.querySelectorAll('span').filter(node => node.getAttribute('aria-label') === 'Transcript available');
    const detailButtons = list.querySelectorAll('button').filter(button => button.textContent === 'View details');
    detailButtons[1].emit('click');
    await new Promise(resolve => setImmediate(resolve));
    const newDetailAudio = elements['voice-notes-detail-player'].querySelector('audio');
    await newDetailAudio.play();
    assert.strictEqual(elements['voice-notes-detail-transcript'].textContent, 'No transcript yet.');
    currentState.processing = { state: 'running', reason: 'none' };
    currentState.transcription = { state: 'transcribing', note_id: entries[0].id, error: '' };
    nextPoll();
    await new Promise(resolve => setImmediate(resolve));
    assert.strictEqual(elements['voice-notes-processing-status'].hidden, true);
    entries[0].has_transcript = true;
    nextPoll();
    await new Promise(resolve => setImmediate(resolve));
    assert.deepStrictEqual(newIndicators.map(node => node.hidden), [false, true]);
    entries[1].has_transcript = true;
    currentState.processing = { state: 'idle', reason: 'none' };
    currentState.transcription = { state: 'ready', note_id: entries[0].id, error: '' };
    const priorDetails = detailRequests;
    nextPoll();
    await new Promise(resolve => setImmediate(resolve));
    assert.deepStrictEqual(newIndicators.map(node => node.hidden), [false, false]);
    assert.strictEqual(detailRequests, priorDetails + 1);
    assert.strictEqual(elements['voice-notes-detail-transcript'].textContent, '<script>not markup</script>');
    assert.strictEqual(elements['voice-notes-detail-player'].querySelector('audio'), newDetailAudio);
    assert.strictEqual(newDetailAudio.paused, false);
    assert.strictEqual(list.querySelectorAll('audio')[0], newAudios[0]);
    const idleListRequests = listRequests;
    nextPoll();
    await new Promise(resolve => setImmediate(resolve));
    assert.strictEqual(listRequests, idleListRequests);
    assert.strictEqual(detailRequests, priorDetails + 1);
    currentState.transcription = { state: 'error', note_id: entries[0].id, error: 'Connection failed' };
    nextPoll();
    await new Promise(resolve => setImmediate(resolve));
    assert.strictEqual(listRequests, idleListRequests + 1);
    nextPoll();
    await new Promise(resolve => setImmediate(resolve));
    assert.strictEqual(listRequests, idleListRequests + 1);

    currentState.processing = { state: 'running', reason: 'none' };
    failList = true;
    nextPoll();
    await new Promise(resolve => setImmediate(resolve));
    assert.strictEqual(elements['voice-notes-status'].textContent, 'List unavailable');
    currentState.processing = { state: 'idle', reason: 'none' };
    failList = false;
    nextPoll();
    await new Promise(resolve => setImmediate(resolve));
    assert.strictEqual(elements['voice-notes-status'].hidden, true);
    currentState.processing = { state: 'running', reason: 'none' };
    let releaseList;
    listGate = new Promise(resolve => { releaseList = resolve; });
    nextPoll();
    await new Promise(resolve => setImmediate(resolve));
    leave();
    releaseList({ ok: true, json: () => Promise.resolve({ entries: [] }) });
    await new Promise(resolve => setImmediate(resolve));
    assert.strictEqual(newDetailAudio.paused, true);
    assert.strictEqual(newDetailAudio.getAttribute('src'), null);
    assert.deepStrictEqual(newIndicators.map(node => node.hidden), [false, false]);

    listGate = null;
    entries.forEach(entry => { entry.has_transcript = false; });
    currentState = { transcription_configured: true, processing: { state: 'idle', reason: 'none' },
        transcription: { state: 'idle', note_id: '', error: '' } };
    await context.window.init_voice_notes_fragment();
    const missedPassAudio = list.querySelectorAll('audio')[0];
    entries.forEach(entry => { entry.has_transcript = true; });
    currentState.transcription = { state: 'ready', note_id: entries[1].id, error: '' };
    nextPoll();
    await new Promise(resolve => setImmediate(resolve));
    const missedPassIndicators = list.querySelectorAll('span')
        .filter(node => node.getAttribute('aria-label') === 'Transcript available');
    assert.deepStrictEqual(missedPassIndicators.map(node => node.hidden), [false, false]);
    assert.strictEqual(list.querySelectorAll('audio')[0], missedPassAudio);
    list.querySelectorAll('button').find(button => button.textContent === 'View details').emit('click');
    await new Promise(resolve => setImmediate(resolve));
    const enrichmentAudio = elements['voice-notes-detail-player'].querySelector('audio');
    entries[0].has_enrichment = true;
    entries[0].title = '<title>literal</title>';
    entries[0].summary = '<script>summary</script>';
    currentState.enrichment = { state: 'ready', note_id: entries[0].id, error: '' };
    nextPoll();
    await new Promise(resolve => setImmediate(resolve));
    assert.strictEqual(elements['voice-notes-detail-title'].textContent, '<title>literal</title>');
    assert.strictEqual(elements['voice-notes-detail-summary'].textContent, '<script>summary</script>');
    assert.strictEqual(elements['voice-notes-detail-summary'].children.length, 0);
    assert.strictEqual(elements['voice-notes-detail-player'].querySelector('audio'), enrichmentAudio);
    assert(elements['voice-notes-detail-actions'].querySelectorAll('button')
        .some(button => button.textContent === 'Regenerate title and summary'));
    entries[0].summary = 'Updated summary';
    currentState.transcription = { state: 'error', note_id: entries[0].id, error: 'Earlier transcription failure' };
    currentState.processing = { state: 'running', reason: 'none' };
    currentState.enrichment = { state: 'generating', note_id: entries[0].id, error: '' };
    nextPoll();
    await new Promise(resolve => setImmediate(resolve));
    const generationStatus = elements['voice-notes-detail-actions'].querySelectorAll('span')
        .find(node => node.getAttribute('role') === 'status');
    assert.strictEqual(generationStatus.textContent, 'Generating title and summary...');
    assert.strictEqual(elements['voice-notes-detail-summary'].textContent, '<script>summary</script>');
    currentState.processing = { state: 'idle', reason: 'none' };
    currentState.enrichment = { state: 'ready', note_id: entries[0].id, error: '' };
    nextPoll();
    await new Promise(resolve => setImmediate(resolve));
    assert.strictEqual(elements['voice-notes-detail-summary'].textContent, 'Updated summary');
    assert.strictEqual(elements['voice-notes-detail-player'].querySelector('audio'), enrichmentAudio);
    currentState.enrichment = { state: 'error', note_id: entries[0].id, error: 'Generation failed' };
    nextPoll();
    await new Promise(resolve => setImmediate(resolve));
    const failedStatus = elements['voice-notes-detail-actions'].querySelectorAll('span')
        .find(node => node.getAttribute('role') === 'status');
    assert(failedStatus.textContent.includes('Earlier transcription failure'));
    assert(failedStatus.textContent.includes('Title and summary generation failed: Generation failed'));
    assert.strictEqual(elements['voice-notes-detail-summary'].textContent, 'Updated summary');
    entries[0].title = 'No speech detected';
    entries[0].summary = '';
    currentState.enrichment = { state: 'ready', note_id: entries[0].id, error: '' };
    nextPoll();
    await new Promise(resolve => setImmediate(resolve));
    assert.strictEqual(elements['voice-notes-detail-title'].textContent, 'No speech detected');
    assert.strictEqual(elements['voice-notes-detail-summary'].textContent, '');
    assert.strictEqual(elements['voice-notes-summary-section'].hidden, true);
    assert.strictEqual(elements['voice-notes-detail-player'].querySelector('audio'), enrichmentAudio);

    let rejectDetail;
    detailGate = new Promise((resolve, reject) => { rejectDetail = reject; });
    list.querySelectorAll('button').filter(button => button.textContent === 'View details')[1].emit('click');
    assert.strictEqual(elements['voice-notes-detail-transcript'].textContent, 'Loading...');
    assert.strictEqual(elements['voice-notes-detail-summary'].textContent, '');
    assert.strictEqual(elements['voice-notes-summary-section'].hidden, true);
    rejectDetail(new Error('Detail unavailable'));
    await new Promise(resolve => setImmediate(resolve));
    assert.strictEqual(elements['voice-notes-status'].textContent, 'Detail unavailable');
    assert.strictEqual(elements['voice-notes-summary-section'].hidden, true);
    detailGate = null;

    entries[0].title = null;
    entries[0].enrichment_error = 'Saved title and summary are invalid';
    currentState = { transcription_configured: true, enrichment_configured: true,
        processing: { state: 'idle', reason: 'none' },
        transcription: { state: 'idle', note_id: '', error: '' },
        enrichment: { state: 'idle', note_id: '', error: '' } };
    await context.window.init_voice_notes_fragment();
    assert(list.querySelectorAll('span').some(node => node.textContent === entries[0].enrichment_error));
    assert(!list.querySelectorAll('span').some(node => node.textContent === 'Title and summary ready'));
    assert.strictEqual(list.querySelectorAll('button')
        .find(button => button.textContent === 'Process note').hidden, true);
    leave();
    console.log('PASS: Voice Notes settings, guidance, reconciliation, playback, and errors');
})().catch(error => {
    console.error(error);
    process.exitCode = 1;
});
