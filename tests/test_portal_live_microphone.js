const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');

const elements = {};
for (const id of ['live-microphone-status']) {
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
            assert.equal(url, '/api/component/microphone/stream');
            assert.equal(options.headers['X-Microphone-Stream'], '1');
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

const tick = () => new Promise(resolve => setImmediate(resolve));
async function run() {
    const header = [76, 77, 1, 1, 128, 62, 0, 0, 1, 16, 0, 0];
    chunks = [Uint8Array.from(header.slice(0, 5)),
        Uint8Array.from([...header.slice(5), 0]),
        Uint8Array.from([64, 0, 192])];
    context.window.microphoneStream.start();
    await tick();
    assert.equal(elements['live-microphone-status'].textContent, 'Listening');
    assert.deepEqual(played.flat(2), [0.5, -0.5]);
    context.window.microphoneStream.stop();
    assert.equal(signal.aborted, true);
    assert.equal(elements['live-microphone-status'].textContent, 'Not listening');

    played.length = 0;
    chunks = [Uint8Array.from([76, 77, 1, 1, 128, 187, 0, 0, 2, 16, 0, 0,
        0, 128, 255, 127])];
    context.window.microphoneStream.start();
    await tick();
    assert.deepEqual(played[0], [[-1], [32767 / 32768]]);
    context.window.microphoneStream.stop();

    chunks = [Uint8Array.from([76, 77, 1, 2, 0, 0, 0, 0, 0, 0, 0, 0])];
    context.window.microphoneStream.start();
    await tick();
    assert.equal(elements['live-microphone-status'].textContent, 'Microphone is busy or unavailable');

    chunks = [Uint8Array.from(header)];
    context.window.microphoneStream.start();
    await tick();
    context.window.microphoneStream.stop();
    assert.equal(signal.aborted, true);
    assert.equal(elements['live-microphone-status'].textContent, 'Not listening');
}
async function testMicrophoneControls() {
    const ids = ['microphone_gain_db', 'microphone_reduce_rumble', 'microphone-gain-value',
        'microphone-unsaved', 'microphone-defaults', 'microphone-save', 'microphone-test',
        'microphone-level', 'microphone-peak', 'microphone-test-status',
        'microphone-warning', 'microphone-undo', 'microphone-hear', 'microphone-level-bar'];
    const controls = Object.fromEntries(ids.map(id => [id, {
        listeners: {}, value: '', checked: false, disabled: false, hidden: false, style: {},
        setAttribute(name, value) { this[name] = value; },
        addEventListener(name, fn) { this.listeners[name] = fn; },
        fire(name) { return this.listeners[name].call(this); }
    }]));
    const handlers = {};
    let writes = 0, applies = 0, polls = 0, saved = false, applyOk = true, aborted;
    let applied = {microphone_gain_db: 0, microphone_reduce_rumble: false};
    let streamStarts = 0, streamStops = 0;
    let finishApply;
    let deferApply = false;
    const errors = [];
    const timers = [];
    let levels = {available: true, fresh: true, in_use: true, ota: false,
        rms: 10000, peak: 32768, input_full_scale: true, gain_clipped: true};
    const ui = {
        window: {
            addEventListener(name, fn) { handlers[name] = fn; },
            microphoneStream: {
                start() { ++streamStarts; this.onStatus('Listening', true); },
                stop() { ++streamStops; this.onStatus('Not listening', false); }
            }
        },
        document: { getElementById: id => controls[id] || null },
        registerConfigFields(names) { assert.deepEqual([...names], ['microphone_gain_db', 'microphone_reduce_rumble']); },
        async loadConfig() {
            controls.microphone_gain_db.value = '0';
            return {microphone_gain_db: 0, microphone_reduce_rumble: false,
                current_microphone_gain_db: applied.microphone_gain_db,
                current_microphone_reduce_rumble: applied.microphone_reduce_rumble};
        },
        async saveFragmentConfig() {
            assert.equal(controls.microphone_gain_db.disabled, false, 'Save must collect enabled form fields');
            ++writes;
            return saved;
        },
        showMessage(message) { errors.push(message); },
        fetch(url, options) {
            if (url === '/api/component/microphone/settings') {
                assert.equal(options.method, 'PUT');
                ++applies;
                const candidate = JSON.parse(options.body);
                const response = () => {
                    if (applyOk) applied = candidate;
                    return {ok: applyOk, json: async () => ({success: applyOk, message: 'Apply failed'})};
                };
                return deferApply ? new Promise(resolve => { finishApply = () => resolve(response()); })
                    : Promise.resolve(response());
            }
            ++polls;
            assert.equal(url, '/api/component/microphone/levels');
            assert.equal(options.headers['X-Microphone-Test'], '1');
            aborted = options.signal;
            return Promise.resolve({ok: true, json: async () => levels});
        },
        setTimeout(fn) { timers.push(fn); return timers.length; },
        clearTimeout() {}, AbortController, Date, Math
    };
    vm.runInNewContext(fs.readFileSync('src/app/web/portal_microphone.js', 'utf8'), ui);
    const initializing = ui.window.init_microphone_fragment();
    assert.equal(controls['microphone-test'].disabled, true, 'Testing must wait for configuration');
    assert.equal(controls['microphone-defaults'].disabled, true);
    await initializing;
    assert.equal(controls['microphone-test'].disabled, false);
    controls.microphone_gain_db.value = '6';
    controls.microphone_gain_db.fire('input');
    assert.equal(writes, 0);
    assert.equal(applies, 0, 'Dragging must not send requests');
    assert.equal(controls['microphone-unsaved'].hidden, false);
    await controls.microphone_gain_db.fire('change');
    assert.equal(applies, 1);
    assert.equal(applied.microphone_gain_db, 6);
    assert.equal(controls['microphone-save'].disabled, false);
    await controls['microphone-defaults'].fire('click');
    assert.equal(writes, 0);
    assert.equal(applied.microphone_gain_db, 0);
    assert.equal(controls['microphone-unsaved'].hidden, true);
    controls.microphone_gain_db.value = '6';
    await controls.microphone_gain_db.fire('change');
    controls.microphone_reduce_rumble.checked = true;
    await controls.microphone_reduce_rumble.fire('change');
    assert.equal(applied.microphone_reduce_rumble, true);
    await controls['microphone-undo'].fire('click');
    assert.deepEqual(applied, {microphone_gain_db: 0, microphone_reduce_rumble: false});
    controls.microphone_gain_db.value = '6';
    await controls.microphone_gain_db.fire('change');
    controls['microphone-test'].checked = true;
    controls['microphone-test'].fire('change');
    await tick();
    assert.equal(polls, 1);
    assert.match(controls['microphone-test-status'].textContent, /in use/);
    assert.match(controls['microphone-warning'].textContent, /Digital gain clipped/);
    assert.equal(controls['microphone-level-bar'].style.width, '100%');
    assert.equal(controls['microphone-level-bar'].className, 'progress-bar bg-danger');
    assert.equal(controls['microphone-hear'].disabled, false);
    controls['microphone-hear'].checked = true;
    controls['microphone-hear'].fire('change');
    assert.equal(streamStarts, 1);
    assert.equal(polls, 1, 'Listening must not start a second test loop');
    ui.window.microphoneStream.onStatus('Microphone is busy', false);
    assert.equal(controls['microphone-hear'].checked, false);
    await controls['microphone-save'].fire('click');
    assert.equal(writes, 1);
    assert.equal(controls['microphone-unsaved'].hidden, false);
    saved = true;
    await controls['microphone-save'].fire('click');
    assert.equal(writes, 2);
    assert.equal(controls['microphone-unsaved'].hidden, true);
    assert.equal(controls['microphone-test'].checked, true);
    applyOk = false;
    controls.microphone_gain_db.value = '12';
    await controls.microphone_gain_db.fire('change');
    assert.equal(controls.microphone_gain_db.value, '6');
    assert.match(errors.pop(), /Apply failed/);
    applyOk = true;
    deferApply = true;
    controls.microphone_gain_db.value = '9';
    const applying = controls.microphone_gain_db.fire('change');
    const saving = controls['microphone-save'].fire('click');
    await tick();
    assert.equal(writes, 2, 'Save must wait for pending input changes');
    finishApply();
    await applying;
    await saving;
    assert.equal(writes, 3);
    assert.equal(applied.microphone_gain_db, 9);
    deferApply = false;
    await controls['microphone-defaults'].fire('click');
    assert.equal(applied.microphone_gain_db, 0);
    assert.equal(controls['microphone-unsaved'].hidden, false);
    assert.equal(controls['microphone-unsaved'].textContent,
        'Factory values applied. Select Save settings to keep them.');
    assert.equal(writes, 3, 'Resetting to factory values must not persist');
    await controls['microphone-undo'].fire('click');
    assert.equal(applied.microphone_gain_db, 9, 'Revert must restore saved values, not factory values');
    assert.equal(controls['microphone-save'].disabled, true);
    handlers['portal-fragment-leave']({detail: 'microphone'});
    assert.equal(controls['microphone-test'].checked, false);
    assert.equal(controls['microphone-hear'].checked, false);
    assert.equal(controls['microphone-hear'].disabled, true);
    assert.equal(streamStops, 1);
    assert.equal(applied.microphone_gain_db, 9, 'Leaving must not restore earlier settings');
    timers.forEach(fn => fn());
    await tick();
    assert.equal(polls, 1);
    assert.equal(aborted.aborted, false);
    for (const [db, expectedWidth, color] of [
        [-30, 50, 'bg-success'], [-0.5, 99.1666667, 'bg-danger'], [-Infinity, 0, 'bg-success']
    ]) {
        levels = {...levels, rms: 1, peak: db === -Infinity ? 0 : 32768 * Math.pow(10, db / 20),
            input_full_scale: false, gain_clipped: false};
        controls['microphone-test'].checked = true;
        controls['microphone-test'].fire('change');
        await tick();
        assert.ok(Math.abs(parseFloat(controls['microphone-level-bar'].style.width) - expectedWidth) < 0.001);
        assert.equal(controls['microphone-level-bar'].className, 'progress-bar ' + color);
        assert.equal(Number(controls['microphone-level']['aria-valuenow']), Math.max(-60, db));
        controls['microphone-test'].checked = false;
        controls['microphone-test'].fire('change');
        assert.equal(controls['microphone-level'].hidden, true);
        assert.equal(controls['microphone-hear'].disabled, true);
    }
    for (const config of [undefined, {microphone_gain_db: 0, microphone_reduce_rumble: false}]) {
        ui.loadConfig = async () => config;
        await ui.window.init_microphone_fragment();
        for (const id of ['microphone-test', 'microphone-defaults', 'microphone-save',
            'microphone-undo', 'microphone_gain_db', 'microphone_reduce_rumble']) {
            assert.equal(controls[id].disabled, true, id + ' must stay disabled without settings');
        }
        assert.match(controls['microphone-test-status'].textContent, /settings unavailable/);
    }
}
run().then(testMicrophoneControls).catch(error => { console.error(error); process.exitCode = 1; });
