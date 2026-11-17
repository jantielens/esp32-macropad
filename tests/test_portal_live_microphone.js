const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

const elements = {};
for (const id of ['live-microphone-start', 'live-microphone-stop', 'live-microphone-status']) {
    elements[id] = {
        listeners: {},
        addEventListener(name, fn) { this.listeners[name] = fn; },
        click() { return this.listeners.click(); }
    };
}
const listeners = {};
const played = [];
class AudioContext {
    constructor() { this.currentTime = 0; this.destination = {}; }
    resume() { return Promise.resolve(); }
    close() { this.closed = true; return Promise.resolve(); }
    createBuffer(channels, frames, rate) {
        const samples = Array.from({length: channels}, () => new Float32Array(frames));
        return {samples, duration: frames / rate, getChannelData: channel => samples[channel]};
    }
    createBufferSource() {
        return {
            connect() {},
            start() { played.push(this.buffer.samples.map(channel => [...channel])); },
            stop() { this.stopped = true; }
        };
    }
}
let chunks;
let signal;
const context = {
    window: {
        AudioContext,
        addEventListener(name, fn) { listeners[name] = fn; },
        fetch(url, options) {
            signal = options.signal;
            assert.equal(url, '/api/component/live-microphone/stream');
            assert.equal(options.headers['X-Live-Microphone'], '1');
            return Promise.resolve({
                ok: true,
                body: {getReader: () => ({read: () => chunks.length
                    ? Promise.resolve({value: chunks.shift(), done: false})
                    : new Promise(() => {})})}
            });
        }
    },
    document: {getElementById: id => elements[id]},
    AbortController, Uint8Array, Float32Array, DataView, Set, console
};
vm.runInNewContext(fs.readFileSync('src/app/web/portal_live_microphone.js', 'utf8'), context);
context.window.fetch = () => { throw new Error('Streaming must bypass the portal fetch limiter'); };
context.window.init_live_microphone_fragment();

const tick = () => new Promise(resolve => setImmediate(resolve));
async function run() {
    const header = [76, 77, 1, 1, 128, 62, 0, 0, 1, 16, 0, 0];
    chunks = [Uint8Array.from(header.slice(0, 5)),
        Uint8Array.from([...header.slice(5), 0]),
        Uint8Array.from([64, 0, 192])];
    elements['live-microphone-start'].click();
    await tick();
    assert.equal(elements['live-microphone-status'].textContent, 'Listening');
    assert.deepEqual(played.flat(2), [0.5, -0.5]);
    elements['live-microphone-stop'].click();
    assert.equal(signal.aborted, true);
    assert.equal(elements['live-microphone-status'].textContent, 'Not listening');

    played.length = 0;
    chunks = [Uint8Array.from([76, 77, 1, 1, 128, 187, 0, 0, 2, 16, 0, 0,
        0, 128, 255, 127])];
    elements['live-microphone-start'].click();
    await tick();
    assert.deepEqual(played[0], [[-1], [32767 / 32768]]);
    elements['live-microphone-stop'].click();

    chunks = [Uint8Array.from([76, 77, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0])];
    elements['live-microphone-start'].click();
    await tick();
    assert.equal(elements['live-microphone-status'].textContent, 'Microphone is busy or unavailable');
    assert.equal(elements['live-microphone-start'].disabled, false);

    chunks = [Uint8Array.from(header)];
    elements['live-microphone-start'].click();
    await tick();
    listeners['portal-fragment-leave']({detail: 'live-microphone'});
    assert.equal(signal.aborted, true);
    assert.equal(elements['live-microphone-status'].textContent, 'Not listening');
}
run().catch(error => { console.error(error); process.exitCode = 1; });
