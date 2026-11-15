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
    'voice-notes-list': new Element('div'),
    'voice-notes-refresh': new Element('button'),
    'voice-notes-catalog': new Element('div'),
    'voice-notes-detail': new Element('div'),
    'voice-notes-detail-title': new Element('h3'),
    'voice-notes-detail-meta': new Element('p'),
    'voice-notes-detail-player': new Element('div'),
    'voice-notes-detail-actions': new Element('div'),
    'voice-notes-detail-transcript': new Element('div'),
    'voice-notes-back': new Element('button'),
    'voice-notes-setup': new Element('div')
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
const context = {
    document: {
        createElement(tag) { return new Element(tag); },
        getElementById(id) { return elements[id]; }
    },
    window: { confirm() { return false; }, addEventListener() {}, removeEventListener() {} },
    AbortController,
    setTimeout(callback) { nextPoll = callback; return 1; },
    clearTimeout() {},
    fetch(url) {
        if (url.endsWith('status') && failStatus) return Promise.reject(new Error('Status unavailable'));
        if (url.endsWith('status') && firstStatus) {
            firstStatus = false;
            return initialStatus.then(() => ({ ok: true, json: () => Promise.resolve(currentState) }));
        }
        return Promise.resolve({
            ok: true,
            json: () => Promise.resolve(url.endsWith('list') ? { entries } :
                url.includes('detail?') ? { note: entries[0], transcript: { text: '<script>not markup</script>' } } :
                currentState)
        });
    }
};
vm.createContext(context);
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
    const noteActions = buttons.filter(button => ['View details', 'Transcribe', 'Transcribe again'].includes(button.textContent));
    assert.deepStrictEqual(noteActions.map(button => button.textContent),
        ['View details', 'Transcribe again', 'View details', 'Transcribe']);
    currentState = { transcription_configured: true,
        transcription: { state: 'transcribing', note_id: entries[1].id, error: '' } };
    nextPoll();
    await new Promise(resolve => setImmediate(resolve));
    assert.strictEqual(indicators[1].hidden, true);
    assert.strictEqual(noteStatuses[1].textContent, 'Transcribing...');
    assert.strictEqual(noteStatuses[1].hidden, false);
    currentState = { transcription_configured: true,
        transcription: { state: 'ready', note_id: entries[1].id, error: '' } };
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
        .filter(button => button.textContent === 'Transcribe').every(button => button.disabled));
    failStatus = false;
    currentState = { transcription_configured: false, transcription: { state: 'idle', note_id: '', error: '' } };
    nextPoll();
    await new Promise(resolve => setImmediate(resolve));
    assert.strictEqual(elements['voice-notes-status'].hidden, true);
    assert.strictEqual(elements['voice-notes-setup'].hidden, false);
    assert(list.querySelectorAll('button')
        .filter(button => button.textContent === 'Transcribe').every(button => button.disabled));
    console.log('PASS: voice note playback, seeking, and errors');
})().catch(error => {
    console.error(error);
    process.exitCode = 1;
});
