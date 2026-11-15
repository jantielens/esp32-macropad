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
    'voice-clips-status': new Element('div'),
    'voice-clips-list': new Element('div'),
    'voice-clips-refresh': new Element('button')
};
const entries = [
    { id: '0000000100000002', created_at: 1791370589, duration_ms: 65000 },
    { id: '0000000100000003', created_at: 1791366989, duration_ms: 3000 }
];
const context = {
    document: {
        createElement(tag) { return new Element(tag); },
        getElementById(id) { return elements[id]; }
    },
    window: { confirm() { return false; } },
    fetch(url) {
        return Promise.resolve({
            ok: true,
            json: () => Promise.resolve(url.endsWith('list') ? { entries } : { error: '' })
        });
    }
};
vm.createContext(context);
vm.runInContext(fs.readFileSync('src/app/web/portal_voice_clips.js', 'utf8'), context);

(async function () {
    await context.window.init_voice_clips_fragment();
    const list = elements['voice-clips-list'];
    const audios = list.querySelectorAll('audio');
    const buttons = list.querySelectorAll('button');
    const ranges = list.querySelectorAll('input');
    const plays = buttons.filter(button => button.getAttribute('aria-label').startsWith('Play'));
    assert.strictEqual(audios.length, 2);
    assert.strictEqual(list.querySelectorAll('h3').length, 1);
    assert.strictEqual(audios[0].preload, 'none');

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
    assert.strictEqual(elements['voice-clips-status'].textContent, 'Could not play this clip. Try again.');
    audios[0].failPlayback = false;
    plays[0].emit('click');
    assert.strictEqual(elements['voice-clips-status'].textContent, '');
    plays[0].emit('click');
    audios[0].failPlayback = Object.assign(new Error('interrupted'), { name: 'AbortError' });
    plays[0].emit('click');
    await Promise.resolve();
    assert.strictEqual(elements['voice-clips-status'].textContent, '');
    console.log('PASS: voice clip playback, seeking, and errors');
})().catch(error => {
    console.error(error);
    process.exitCode = 1;
});
