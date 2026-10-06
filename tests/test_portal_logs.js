const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

class Node {
    constructor() {
        this.listeners = {};
        this.children = [];
        this.text = '';
        this.checked = true;
        this.isConnected = true;
        this.scrollTop = 0;
        this.scrollHeight = 1000;
        this.clientHeight = 200;
        this.style = {};
        this.classList = { toggle() {} };
    }
    addEventListener(name, callback) { this.listeners[name] = callback; }
    removeEventListener(name) { delete this.listeners[name]; }
    emit(name) { return this.listeners[name] && this.listeners[name](); }
    set textContent(value) { this.text = value; this.children = []; }
    get textContent() { return this.text; }
    set innerHTML(value) { throw new Error('Unsafe HTML rendering: ' + value); }
    appendChild(child) { this.children.push(child); this.text += child.textContent; }
    removeChild(child) { this.children = this.children.filter(item => item !== child); }
    select() {}
    setAttribute() {}
    click() { this.clicked = true; }
}

async function main() {
    const nodes = new Map();
    const get = id => {
        if (!nodes.has(id)) nodes.set(id, new Node());
        return nodes.get(id);
    };
    const document = new Node();
    document.hidden = false;
    document.getElementById = get;
    document.createTextNode = text => ({ textContent: text });
    document.createElement = () => new Node();
    document.body = new Node();
    const window = new Node();
    const timers = new Map();
    const requests = [];
    const crashRequests = [];
    let crashData = { available: true, size: 1024, task: '<script>task', pc: '0x40012345',
        panic_reason: 'panic <script>&', elf_sha256: 'crashed-elf-hash', architecture: 'riscv',
        exception_cause: 7, trap_value: '0x500d2000', current_reset_reason: 3, current_elf_sha256: 'current-elf-hash',
        registers: { MEPC: '0x40012345', RA: '0x400cfc8c', SP: '0x4ff41350', MCAUSE: '0x00000007', MTVAL: '0x500d2000' } };
    let crashStatusCode = 200;
    let timerId = 0;
    let clipboard = '';
    let fallbackCopies = 0;
    document.execCommand = command => {
        assert.strictEqual(command, 'copy');
        clipboard = document.body.children.at(-1).value;
        ++fallbackCopies;
        return true;
    };
    let downloaded;
    class TestURL extends URL {}
    TestURL.createObjectURL = blob => { downloaded = blob; return 'blob:test'; };
    TestURL.revokeObjectURL = () => {};
    const context = {
        window, document, AbortController, URLSearchParams, URL: TestURL, Blob,
        navigator: { clipboard: { async writeText(text) { clipboard = text; } } },
        setTimeout(callback, delay) { timers.set(++timerId, { callback, delay }); return timerId; },
        clearTimeout(id) { timers.delete(id); },
        fetch(url, options) {
            if (url === '/api/logs/crash') {
                crashRequests.push({ url, options });
                return Promise.resolve({ ok: crashStatusCode === 200, status: crashStatusCode, json: async () => crashData });
            }
            return new Promise((resolve, reject) => {
                requests.push({ url, options, resolve });
                options.signal.addEventListener('abort', () => reject(Object.assign(new Error('aborted'), { name: 'AbortError' })));
            });
        }
    };
    vm.createContext(context);
    const copyHelper = fs.readFileSync('src/app/web/portal_core.js', 'utf8')
        .match(/async function copyTextToClipboard\(text\) \{[\s\S]*?\n\}/);
    assert(copyHelper, 'shared clipboard helper must exist');
    vm.runInContext(copyHelper[0], context);
    vm.runInContext(fs.readFileSync('src/app/web/portal_logs.js', 'utf8'), context);
    async function flush() { for (let index = 0; index < 12; ++index) await Promise.resolve(); }
    function tick(delay = 3000) {
        const entry = [...timers.entries()].find(([, timer]) => timer.delay === delay);
        assert(entry, 'one poll must be scheduled');
        timers.delete(entry[0]);
        entry[1].callback();
    }
    let sequence = 0;
    function answer(count = 1, extra = {}) {
        const records = Array.from({ length: count }, () => ({ sequence: ++sequence, line: '<script>& log ' + sequence + '\n' }));
        requests.at(-1).resolve({ ok: true, json: async () => ({
            available: true, boot_id: 123, capacity: 256, newest: sequence,
            next: sequence, missed: 0, dropped: 0, boot_complete: true,
            records, ...extra
        }) });
    }
    window.init_logs_fragment();
    assert.strictEqual(requests.length, 1);
    assert(requests[0].url.includes('source=boot'));
    assert(!requests[0].url.includes('after='));
    answer(32, { has_more: true });
    await flush();
    assert.strictEqual(crashRequests.length, 1);
    assert.strictEqual(get('logs-crash-reason').textContent, 'panic <script>&');
    assert.strictEqual(get('logs-crash-task').textContent, '<script>task');
    assert.strictEqual(get('logs-crash-pc').textContent, '0x40012345');
    assert.strictEqual(get('logs-crash-elf').textContent, 'crashed-elf-hash');
    assert.strictEqual(get('logs-crash-download').disabled, false);
    assert.strictEqual(get('logs-crash-copy').disabled, false);
    assert.strictEqual(get('logs-crash-cause').textContent, 'Store/AMO access fault (MCAUSE=7)');
    assert.strictEqual(get('logs-crash-address').textContent, '0x500d2000');
    assert.strictEqual(get('logs-crash-ra').textContent, '0x400cfc8c');
    assert.strictEqual(get('logs-crash-sp').textContent, '0x4ff41350');
    assert.strictEqual(get('logs-crash-reset').textContent, 'Software restart (3)');
    assert(get('logs-crash-status').textContent.includes('Crash age unknown'));
    assert(get('logs-crash-registers').textContent.includes('MTVAL0x500d2000'));
    await get('logs-crash-copy').emit('click');
    assert(clipboard.includes('Exception PC: 0x40012345'));
    assert(clipboard.includes('Faulting address (MTVAL): 0x500d2000'));
    assert(clipboard.includes('Exception cause: Store/AMO access fault (MCAUSE=7)'));
    assert(clipboard.includes('Crashed firmware ELF SHA256: crashed-elf-hash'));
    assert(clipboard.includes('Current boot - not necessarily the boot immediately after this crash'));
    assert(clipboard.includes('Firmware ELF SHA256: current-elf-hash'));
    assert(clipboard.includes('Reset reason: Software restart (3)'));
    assert(get('logs-output').textContent.includes('<script>& log 1'));
    assert.strictEqual(get('logs-output').children.length, 1);
    assert(get('logs-status').textContent.startsWith('Catching up'));
    assert(![...timers.values()].some(timer => timer.delay === 3000));
    tick(250);
    assert(requests.at(-1).url.includes('after=32'));
    assert.strictEqual(requests.length, 2, 'catch-up must keep one request in flight');
    requests.at(-1).resolve({ ok: false, status: 429 });
    await flush();
    assert(![...timers.values()].some(timer => timer.delay === 250), 'busy responses must use normal retry cadence');
    tick();
    assert(requests.at(-1).url.includes('after=32'), 'failed catch-up must retain cursor');
    answer(0);
    await flush();
    assert(get('logs-status').textContent.startsWith('Catching up'), 'recent history must follow boot history');
    tick(250);
    assert(requests.at(-1).url.includes('source=recent'));
    assert(requests.at(-1).url.includes('after=32'), 'boot cursor must skip overlapping recent records');
    answer(0);
    await flush();
    assert(get('logs-status').textContent.startsWith('Live'));
    assert(get('logs-status').textContent.includes('Device buffer: 256 records | Latest record ID: 32'));
    assert(get('logs-status').textContent.includes('Browser history: 32 entries'));
    assert(![...timers.values()].some(timer => timer.delay === 250));
    assert.strictEqual(get('logs-output').children.length, 1, 'empty polls must not grow DOM');
    get('logs-pause').emit('click');
    assert(![...timers.values()].some(timer => timer.delay === 3000));
    assert(get('logs-status').textContent.startsWith('Paused'));
    get('logs-pause').emit('click');
    answer(1, { missed: 2, dropped: 3 });
    await flush();
    assert(get('logs-status').textContent.includes('Records missed before retrieval: 2 | Records dropped during capture: 3'));
    assert(get('logs-output').textContent.includes('--- Log history gap: 2 records are no longer available ---'));
    await get('logs-copy').emit('click');
    assert.strictEqual(clipboard, get('logs-output').textContent);
    context.navigator.clipboard = undefined;
    await get('logs-copy').emit('click');
    assert.strictEqual(fallbackCopies, 1, 'HTTP pages must use the shared clipboard fallback');
    assert.strictEqual(clipboard, get('logs-output').textContent);
    assert.strictEqual(document.body.children.length, 0, 'temporary textarea must be removed');
    context.navigator.clipboard = { async writeText() { throw new Error('Permission denied'); } };
    await get('logs-copy').emit('click');
    assert.strictEqual(fallbackCopies, 2, 'rejected clipboard API must also fall back');
    const copyCommand = document.execCommand;
    document.execCommand = () => false;
    await get('logs-copy').emit('click');
    assert(get('logs-error').textContent.includes('Clipboard unavailable'));
    assert.strictEqual(document.body.children.length, 0, 'failed fallback must clean up');
    document.execCommand = copyCommand;
    await get('logs-copy').emit('click');
    assert.strictEqual(get('logs-error').textContent, '');
    get('logs-download').emit('click');
    assert.strictEqual(await downloaded.text(), clipboard);
    get('logs-clear').emit('click');
    assert.strictEqual(get('logs-output').textContent, '');
    tick();
    assert(requests.at(-1).url.includes('after=33'), 'clear must retain cursor');
    answer(1);
    await flush();
    document.hidden = true;
    document.emit('visibilitychange');
    assert(![...timers.values()].some(timer => timer.delay === 3000));
    document.hidden = false;
    document.emit('visibilitychange');
    answer(1);
    await flush();
    for (let index = 0; index < 320; ++index) {
        tick();
        answer(32);
        await flush();
    }
    const lineCount = get('logs-output').textContent.split('\n').length - 1;
    assert(lineCount >= 9000 && lineCount <= 10000, 'history must trim in bounded batches');
    assert(get('logs-output').children.length < 40, 'trimming must also bound DOM nodes');
    get('logs-output').scrollTop = 50;
    get('logs-output').emit('scroll');
    assert.strictEqual(get('logs-follow').checked, false);
    tick();
    answer(1, { boot_id: 456, reset: true });
    await flush();
    assert.strictEqual(get('logs-output').textContent, '', 'reboot must clear stale history before loading startup logs');
    tick(250);
    assert(requests.at(-1).url.includes('source=boot'));
    assert(!requests.at(-1).url.includes('after='));
    sequence = 0;
    answer(2, { boot_id: 456 });
    await flush();
    tick(250);
    assert(requests.at(-1).url.includes('source=recent'));
    assert(requests.at(-1).url.includes('after=2'));
    answer(1, { boot_id: 456 });
    await flush();
    assert.strictEqual(get('logs-output').textContent.split('\n').length - 1, 3, 'new boot logs must precede new recent logs');
    assert.strictEqual(crashRequests.length, 2, 'reboot must refresh retained crash metadata');
    get('logs-crash-download').emit('click');
    assert.strictEqual(requests.at(-1).url, '/api/logs/crash/download');
    assert(get('logs-status').textContent.startsWith('Downloading'));
    assert(![...timers.values()].some(timer => timer.delay === 3000));
    requests.at(-1).resolve({ ok: true, blob: async () => new Blob(['crash bytes']) });
    await flush();
    assert.strictEqual(await downloaded.text(), 'crash bytes');
    assert.strictEqual(get('logs-crash-download').disabled, false);
    tick(250);
    answer(0, { boot_id: 456 });
    await flush();
    get('logs-crash-download').emit('click');
    const pendingDownload = requests.at(-1);
    window.emit('portal-fragment-leave');
    await flush();
    assert(pendingDownload.options.signal.aborted, 'navigation must cancel crash downloads');
    window.init_logs_fragment();
    crashData = { available: false, reason: 'invalid_dump' };
    answer(0);
    await flush();
    assert.strictEqual(get('logs-crash-download').disabled, true);
    assert.strictEqual(get('logs-crash-copy').disabled, true);
    assert(get('logs-crash-status').textContent.includes('incomplete or corrupt'));
    tick(250);
    const pending = requests.at(-1);
    window.emit('portal-fragment-leave');
    await flush();
    assert(pending.options.signal.aborted);
    assert(![...timers.values()].some(timer => timer.delay === 3000));
    assert(!document.listeners.visibilitychange);
    window.init_logs_fragment();
    requests.at(-1).resolve({ ok: true, json: async () => ({ available: false, reason: 'disabled' }) });
    await flush();
    assert(get('logs-error').textContent.includes('disabled'));
    assert(![...timers.values()].some(timer => timer.delay === 3000));
    window.emit('portal-fragment-leave');
    window.init_logs_fragment();
    requests.at(-1).resolve({ ok: false, status: 403 });
    await flush();
    assert(get('logs-error').textContent.includes('Full portal'));
    assert(![...timers.values()].some(timer => timer.delay === 3000));
    window.emit('portal-fragment-leave');
    crashStatusCode = 503;
    window.init_logs_fragment();
    answer(1, { has_more: true });
    await flush();
    assert(get('logs-crash-status').textContent.includes('HTTP 503'));
    assert(![...timers.values()].some(timer => timer.delay === 250), 'summary errors must not cause fast retries');
    crashStatusCode = 200;
    crashData = { available: false, reason: 'not_found' };
    tick();
    answer(0);
    await flush();
    assert.strictEqual(get('logs-crash-status').textContent, 'No saved crash dump.');
    assert.strictEqual(get('logs-crash-copy').disabled, true);
    window.emit('portal-fragment-leave');
    crashData = { available: true, size: 1024, architecture: 'xtensa', exception_cause: 29,
        fault_address: '0x500d2000', registers: { A0: '0x400cfc8c', A1: '0x4ff41350' } };
    window.init_logs_fragment();
    answer(0);
    await flush();
    assert.strictEqual(get('logs-crash-cause').textContent, 'Store prohibited (EXCCAUSE=29)');
    assert.strictEqual(get('logs-crash-ra').textContent, '0x400cfc8c');
    assert.strictEqual(get('logs-crash-sp').textContent, '0x4ff41350');
    assert.strictEqual(get('logs-crash-reset').textContent, 'Unavailable');
    window.emit('portal-fragment-leave');
    crashData = { available: true, size: 1024, architecture: 'riscv', exception_cause: 2, trap_value: '0xdeadbeef' };
    window.init_logs_fragment();
    answer(0);
    await flush();
    assert.strictEqual(get('logs-crash-cause').textContent, 'Illegal instruction (MCAUSE=2)');
    assert.strictEqual(get('logs-crash-address-label').textContent, 'Trap value (MTVAL)');
    assert.strictEqual(get('logs-crash-ra').textContent, 'Unavailable');
    for (const [cause, description] of [[0, 'Instruction address misaligned (MCAUSE=0)'],
        [99, 'Unknown exception (MCAUSE=99)'], [0x80000007, 'Interrupt (MCAUSE=2147483655)']]) {
        window.emit('portal-fragment-leave');
        crashData = { available: true, size: 1024, architecture: 'riscv', exception_cause: cause };
        window.init_logs_fragment();
        answer(0);
        await flush();
        assert.strictEqual(get('logs-crash-cause').textContent, description);
    }
    for (const [reason, description] of [[0, 'Unknown'], [2, 'External reset'], [4, 'Panic'],
        [5, 'Interrupt watchdog'], [15, 'CPU lockup'], [99, 'Unknown']]) {
        window.emit('portal-fragment-leave');
        crashData = { available: true, size: 1024, current_reset_reason: reason };
        window.init_logs_fragment();
        answer(0);
        await flush();
        assert.strictEqual(get('logs-crash-reset').textContent, description + ' (' + reason + ')');
    }
    window.emit('portal-fragment-leave');
    window.init_logs_fragment();
    requests.at(-1).resolve({ ok: false, status: 401 });
    await flush();
    assert(get('logs-error').textContent.includes('Authentication'));
    assert(![...timers.values()].some(timer => timer.delay === 3000));
    console.log('PASS: log polling, history bounds, safe rendering, controls, and cleanup');
}

main().catch(error => { console.error(error); process.exitCode = 1; });