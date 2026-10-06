(function () {
    'use strict';

    var disposeCurrent = null;

    window.init_logs_fragment = function () {
        if (disposeCurrent) disposeCurrent();
        var root = document.getElementById('logs-root');
        if (!root) return;
        var output = document.getElementById('logs-output');
        var status = document.getElementById('logs-status');
        var errorBox = document.getElementById('logs-error');
        var pauseButton = document.getElementById('logs-pause');
        var follow = document.getElementById('logs-follow');
        var crashStatus = document.getElementById('logs-crash-status');
        var crashDetails = document.getElementById('logs-crash-details');
        var crashDownload = document.getElementById('logs-crash-download');
        var crashCopy = document.getElementById('logs-crash-copy');
        var crashRegisters = document.getElementById('logs-crash-registers');
        var registerSection = document.getElementById('logs-crash-register-section');
        var crashData = null;
        var crashPending = true;
        var crashRetryAt = 0;
        var crashRetryDelay = 3000;
        var crashAvailable = false;
        var downloading = false;
        var lines = [];
        var cursor = null;
        var bootId = null;
        var source = 'boot';
        var catchingUp = true;
        var missed = 0;
        var paused = false;
        var stopped = false;
        var disposed = false;
        var timer = null;
        var controller = null;
        var generation = 0;
        var latest = null;
        var previousScroll = 0;

        function showError(message) {
            errorBox.textContent = message;
            errorBox.classList.toggle('d-none', !message);
        }

        function updateStatus() {
            var state = downloading ? 'Downloading crash dump' : (paused ? 'Paused' : (document.hidden ? 'Hidden' :
                (catchingUp ? 'Catching up' : 'Live polling')));
            if (!latest) { status.textContent = state; return; }
            status.textContent = state + ' | Device buffer: ' + latest.capacity + ' records | Latest record ID: ' + latest.newest +
                ' | Browser history: ' + lines.length + ' entries | Records missed before retrieval: ' + missed +
                ' | Records dropped during capture: ' + latest.dropped;
        }

        function exceptionCause(data) {
            if (typeof data.exception_cause !== 'number') return 'Unavailable';
            var cause = data.exception_cause;
            var names;
            var register;
            if (data.architecture === 'riscv') {
                register = 'MCAUSE';
                if (cause >= 0x80000000) return 'Interrupt (' + register + '=' + cause + ')';
                names = { 0: 'Instruction address misaligned', 1: 'Instruction access fault',
                    2: 'Illegal instruction', 3: 'Breakpoint', 4: 'Load address misaligned', 5: 'Load access fault',
                    6: 'Store/AMO address misaligned', 7: 'Store/AMO access fault', 8: 'Environment call from U-mode',
                    9: 'Environment call from S-mode', 11: 'Environment call from M-mode', 12: 'Instruction page fault',
                    13: 'Load page fault', 15: 'Store/AMO page fault' };
            } else if (data.architecture === 'xtensa') {
                register = 'EXCCAUSE';
                names = { 0: 'Illegal instruction', 1: 'System call', 2: 'Instruction fetch error', 3: 'Load/store error',
                    4: 'Level-1 interrupt', 5: 'Alloca', 6: 'Integer divide by zero', 8: 'Privileged instruction',
                    9: 'Unaligned access', 12: 'Instruction PIF data error', 13: 'Load/store PIF data error',
                    14: 'Instruction PIF address error', 15: 'Load/store PIF address error', 16: 'Instruction TLB miss',
                    17: 'Instruction TLB multi-hit', 18: 'Instruction fetch privilege violation', 20: 'Instruction fetch prohibited',
                    24: 'Load/store TLB miss', 25: 'Load/store TLB multi-hit', 26: 'Load/store privilege violation',
                    28: 'Load prohibited', 29: 'Store prohibited' };
            } else return 'Unknown architecture (cause=' + cause + ')';
            return (names[cause] || 'Unknown exception') + ' (' + register + '=' + cause + ')';
        }

        function resetReason(data) {
            if (typeof data.current_reset_reason !== 'number') return 'Unavailable';
            var names = ['Unknown', 'Power-on', 'External reset', 'Software restart', 'Panic', 'Interrupt watchdog', 'Task watchdog',
                'Other watchdog', 'Deep sleep wake', 'Brownout', 'SDIO', 'USB', 'JTAG', 'eFuse', 'Power glitch', 'CPU lockup'];
            return (names[data.current_reset_reason] || 'Unknown') + ' (' + data.current_reset_reason + ')';
        }

        function active() { return !disposed && root.isConnected && !document.hidden && !paused && !stopped && !downloading; }

        function cancel() {
            ++generation;
            clearTimeout(timer);
            timer = null;
            if (controller) controller.abort();
        }

        function schedule(delay) {
            clearTimeout(timer);
            if (active()) timer = setTimeout(poll, delay);
        }

        async function loadCrash(requestController, requestGeneration) {
            if (!crashPending || Date.now() < crashRetryAt) return true;
            try {
                var response = await fetch('/api/logs/crash', { signal: requestController.signal, cache: 'no-store' });
                if (!response.ok) throw new Error('Crash summary unavailable (HTTP ' + response.status + ').');
                var data = await response.json();
                if (disposed || requestGeneration !== generation) return true;
                crashPending = false;
                crashRetryAt = 0;
                crashRetryDelay = 3000;
                crashAvailable = !!data.available;
                crashData = crashAvailable ? data : null;
                crashDownload.disabled = !crashAvailable;
                crashCopy.disabled = !crashAvailable;
                crashDetails.classList.toggle('d-none', !crashAvailable);
                registerSection.classList.toggle('d-none', !crashAvailable || !Object.keys(data.registers || {}).length);
                if (!crashAvailable) {
                    var messages = {
                        not_found: 'No saved crash dump.',
                        no_partition: 'No crash dump partition on this build.',
                        disabled: 'Flash crash capture is disabled on this build.',
                        invalid_dump: 'Saved crash dump is incomplete or corrupt.'
                    };
                    crashStatus.textContent = messages[data.reason] || 'Crash dump unavailable.';
                    return true;
                }
                crashStatus.textContent = 'Retained crash | ' + data.size + ' bytes | Crash age unknown';
                document.getElementById('logs-crash-reason').textContent = data.panic_reason || 'Unavailable';
                document.getElementById('logs-crash-task').textContent = data.task || 'Unavailable';
                document.getElementById('logs-crash-pc').textContent = data.pc || 'Unavailable';
                document.getElementById('logs-crash-elf').textContent = data.elf_sha256 || 'Unavailable';
                document.getElementById('logs-crash-cause').textContent = exceptionCause(data);
                var memoryCause = [0, 1, 4, 5, 6, 7, 12, 13, 15].includes(data.exception_cause);
                document.getElementById('logs-crash-address-label').textContent = data.architecture === 'riscv' ?
                    (memoryCause ? 'Faulting address (MTVAL)' : 'Trap value (MTVAL)') :
                    (data.architecture === 'xtensa' ? 'Faulting address (EXCVADDR)' : 'Faulting address / trap value');
                document.getElementById('logs-crash-address').textContent = data.trap_value || data.fault_address || 'Unavailable';
                var registers = data.registers || {};
                document.getElementById('logs-crash-ra').textContent =
                    (data.architecture === 'xtensa' ? registers.A0 : registers.RA) || 'Unavailable';
                document.getElementById('logs-crash-sp').textContent =
                    (data.architecture === 'xtensa' ? registers.A1 : registers.SP) || 'Unavailable';
                document.getElementById('logs-crash-reset').textContent = resetReason(data);
                document.getElementById('logs-crash-current-elf').textContent = data.current_elf_sha256 || 'Unavailable';
                crashRegisters.textContent = '';
                Object.entries(registers).forEach(function (entry) {
                    var name = document.createElement('dt');
                    var value = document.createElement('dd');
                    name.className = 'col-4 col-sm-3 font-monospace';
                    value.className = 'col-8 col-sm-9 font-monospace';
                    name.textContent = entry[0];
                    value.textContent = entry[1];
                    crashRegisters.appendChild(name);
                    crashRegisters.appendChild(value);
                });
                return true;
            } catch (error) {
                if (disposed || requestGeneration !== generation) return true;
                crashPending = true;
                crashRetryAt = Date.now() + crashRetryDelay;
                crashRetryDelay = Math.min(crashRetryDelay * 2, 30000);
                crashAvailable = false;
                crashData = null;
                crashDownload.disabled = true;
                crashCopy.disabled = true;
                crashDetails.classList.toggle('d-none', true);
                registerSection.classList.toggle('d-none', true);
                crashStatus.textContent = error.name === 'AbortError' ? 'Crash summary request timed out.' : error.message;
                return false;
            }
        }

        async function poll() {
            if (!active() || controller) return;
            var nextDelay = 3000;
            var requestGeneration = generation;
            var requestController = new AbortController();
            controller = requestController;
            var timeout = setTimeout(function () { requestController.abort(); }, 10000);
            var params = new URLSearchParams({ source: source });
            if (cursor !== null) params.set('after', String(cursor));
            if (bootId !== null) params.set('boot_id', String(bootId));
            try {
                var response = await fetch('/api/logs?' + params, { signal: requestController.signal, cache: 'no-store' });
                if (disposed || requestGeneration !== generation || !active()) return;
                if (!response.ok) {
                    if (response.status === 401 || response.status === 403) stopped = true;
                    throw new Error(response.status === 401 ? 'Authentication is required.' :
                        response.status === 403 ? 'Full portal access is required.' : 'Logs unavailable (HTTP ' + response.status + ').');
                }
                var data = await response.json();
                if (disposed || requestGeneration !== generation || !active()) return;
                if (!data.available) {
                    await loadCrash(requestController, requestGeneration);
                    if (disposed || requestGeneration !== generation) return;
                    stopped = true;
                    status.textContent = 'Unavailable';
                    showError(data.reason === 'disabled' ? 'Remote capture is disabled on this build.' : 'PSRAM log storage is unavailable.');
                    return;
                }
                showError('');
                if (bootId !== null && bootId !== data.boot_id) {
                    lines = [];
                    output.textContent = '';
                    missed = 0;
                    bootId = data.boot_id;
                    source = 'boot';
                    cursor = null;
                    latest = null;
                    catchingUp = true;
                    crashPending = true;
                    crashRetryAt = 0;
                    crashRetryDelay = 3000;
                    crashAvailable = false;
                    crashData = null;
                    crashDownload.disabled = true;
                    crashCopy.disabled = true;
                    crashDetails.classList.toggle('d-none', true);
                    registerSection.classList.toggle('d-none', true);
                    crashStatus.textContent = 'Checking saved crash...';
                    nextDelay = 250;
                    updateStatus();
                    return;
                }
                bootId = data.boot_id;
                latest = data;
                missed += data.missed;
                var batch = data.records.map(function (record) {
                    return record.line.endsWith('\n') ? record.line : record.line + '\n';
                });
                if (data.missed) {
                    batch.unshift('--- Log history gap: ' + data.missed + ' records are no longer available ---\n');
                } else if (source === 'recent' && data.reset) {
                    batch.unshift('--- Log history gap: older records are no longer available ---\n');
                }
                lines.push.apply(lines, batch);
                if (batch.length) output.appendChild(document.createTextNode(batch.join('')));
                if (lines.length > 10000) {
                    lines.splice(0, lines.length - 9000);
                    output.textContent = lines.join('');
                }
                cursor = data.next;
                catchingUp = source === 'boot' || !!data.has_more;
                if (source === 'boot' && !data.has_more) source = 'recent';
                if (follow.checked) output.scrollTop = output.scrollHeight;
                previousScroll = output.scrollTop;
                nextDelay = catchingUp ? 250 : 3000;
                updateStatus();
                if (!await loadCrash(requestController, requestGeneration)) nextDelay = 3000;
            } catch (error) {
                if (!disposed && requestGeneration === generation) {
                    showError(error.name === 'AbortError' ? 'Log request timed out.' : error.message);
                    updateStatus();
                }
            } finally {
                clearTimeout(timeout);
                if (controller === requestController) controller = null;
                schedule(nextDelay);
            }
        }

        pauseButton.addEventListener('click', function () {
            paused = !paused;
            pauseButton.textContent = paused ? 'Resume' : 'Pause';
            pauseButton.title = paused ? 'Resume polling' : 'Pause polling';
            pauseButton.setAttribute('aria-pressed', String(paused));
            cancel();
            updateStatus();
            if (!paused) poll();
        });
        document.getElementById('logs-clear').addEventListener('click', function () {
            lines = [];
            output.textContent = '';
            updateStatus();
        });
        document.getElementById('logs-copy').addEventListener('click', async function () {
            try {
                await copyTextToClipboard(lines.join(''));
                showError('');
            } catch (error) { showError('Clipboard unavailable. Use Download instead.'); }
        });
        document.getElementById('logs-download').addEventListener('click', function () {
            var url = URL.createObjectURL(new Blob([lines.join('')], { type: 'text/plain;charset=utf-8' }));
            var link = document.createElement('a');
            link.href = url;
            link.download = 'device-logs.txt';
            link.click();
            setTimeout(function () { URL.revokeObjectURL(url); }, 1000);
        });
        crashCopy.addEventListener('click', async function () {
            if (!crashAvailable || !crashData || disposed) return;
            var fields = [
                ['Panic reason', 'reason'], ['Task', 'task'], ['Exception cause', 'cause'], ['Exception PC', 'pc'],
                [document.getElementById('logs-crash-address-label').textContent, 'address'],
                ['Return address (RA / A0)', 'ra'], ['Stack pointer (SP / A1)', 'sp'], ['Crashed firmware ELF SHA256', 'elf']
            ];
            var report = ['Retained crash - age unknown'];
            fields.forEach(function (field) {
                report.push(field[0] + ': ' + document.getElementById('logs-crash-' + field[1]).textContent);
            });
            report.push('', 'Current boot - not necessarily the boot immediately after this crash',
                'Reset reason: ' + resetReason(crashData), 'Firmware ELF SHA256: ' + (crashData.current_elf_sha256 || 'Unavailable'));
            var registers = Object.entries(crashData.registers || {});
            if (registers.length) {
                report.push('', 'SDK-saved registers:');
                registers.forEach(function (entry) { report.push(entry[0] + ': ' + entry[1]); });
            }
            try {
                await copyTextToClipboard(report.join('\n') + '\n');
                showError('');
            } catch (error) { showError('Clipboard unavailable.'); }
        });
        crashDownload.addEventListener('click', async function () {
            if (!crashAvailable || downloading || disposed || document.hidden) return;
            downloading = true;
            cancel();
            var requestGeneration = generation;
            var requestController = new AbortController();
            controller = requestController;
            crashDownload.disabled = true;
            updateStatus();
            var timeout = setTimeout(function () { requestController.abort(); }, 30000);
            try {
                var response = await fetch('/api/logs/crash/download', { signal: requestController.signal, cache: 'no-store' });
                if (!response.ok) throw new Error('Crash download unavailable (HTTP ' + response.status + ').');
                var blob = await response.blob();
                if (disposed || requestGeneration !== generation) return;
                var url = URL.createObjectURL(blob);
                var link = document.createElement('a');
                link.href = url;
                link.download = 'device-coredump.bin';
                link.click();
                setTimeout(function () { URL.revokeObjectURL(url); }, 1000);
                showError('');
            } catch (error) {
                if (!disposed && requestGeneration === generation) {
                    crashPending = true;
                    crashAvailable = false;
                    showError(error.name === 'AbortError' ? 'Crash download timed out.' : error.message);
                }
            } finally {
                clearTimeout(timeout);
                if (controller === requestController) controller = null;
                downloading = false;
                if (!disposed) {
                    crashDownload.disabled = !crashAvailable;
                    crashCopy.disabled = !crashAvailable;
                    updateStatus();
                    schedule(250);
                }
            }
        });
        output.addEventListener('scroll', function () {
            if (output.scrollTop < previousScroll && output.scrollHeight - output.scrollTop - output.clientHeight > 4)
                follow.checked = false;
            previousScroll = output.scrollTop;
        });
        follow.addEventListener('change', function () {
            if (follow.checked) output.scrollTop = output.scrollHeight;
        });
        function onVisibility() {
            cancel();
            updateStatus();
            if (!document.hidden) poll();
        }
        function dispose() {
            disposed = true;
            cancel();
            document.removeEventListener('visibilitychange', onVisibility);
            window.removeEventListener('portal-fragment-leave', dispose);
            disposeCurrent = null;
        }
        disposeCurrent = dispose;
        document.addEventListener('visibilitychange', onVisibility);
        window.addEventListener('portal-fragment-leave', dispose);
        poll();
    };
}());