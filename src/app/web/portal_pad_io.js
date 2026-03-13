// portal_pad_io.js - Pad clipboard, import/export, and device config I/O
// Part of the ESP32 Macropad configuration portal.

async function copyTextToClipboard(text) {
    if (navigator.clipboard && navigator.clipboard.writeText) {
        await navigator.clipboard.writeText(text);
        return;
    }

    const temp = document.createElement('textarea');
    temp.value = text;
    temp.setAttribute('readonly', '');
    temp.style.position = 'absolute';
    temp.style.left = '-9999px';
    document.body.appendChild(temp);
    temp.select();
    document.execCommand('copy');
    document.body.removeChild(temp);
}

function copyBindingExample(button) {
    const example = button.closest('.binding-example');
    const code = example ? example.querySelector('code') : null;
    if (!code) return;

    copyTextToClipboard(code.textContent.trim())
        .then(() => {
            const original = button.dataset.label || button.textContent;
            button.dataset.label = original;
            button.textContent = 'Copied';
            button.classList.add('is-copied');
            window.setTimeout(() => {
                button.textContent = original;
                button.classList.remove('is-copied');
            }, 1200);
        })
        .catch(() => {
            showMessage('Could not copy binding', 'error');
        });
}

function padStripPosition(btn) {
    const copy = Object.assign({}, btn);
    delete copy.col;
    delete copy.row;
    // Keep col_span/row_span in clipboard for best-effort paste
    return copy;
}

// Check whether a col_span × row_span rectangle fits at (col, row)
// without exceeding grid bounds or overlapping existing buttons.
function padCanSpanFit(col, row, cs, rs, buttons) {
    if (col + cs > padState.cols || row + rs > padState.rows) return false;
    for (let dc = 0; dc < cs; dc++) {
        for (let dr = 0; dr < rs; dr++) {
            if (dc === 0 && dr === 0) continue; // origin is ours
            for (const b of buttons) {
                const bcs = b.col_span || 1, brs = b.row_span || 1;
                if ((col + dc) >= b.col && (col + dc) < b.col + bcs &&
                    (row + dr) >= b.row && (row + dr) < b.row + brs) {
                    return false;
                }
            }
        }
    }
    return true;
}

function padDialogCopyBtn() {
    const col = padState.editCol;
    const row = padState.editRow;

    // Save dialog state to model without closing the dialog
    padDialogOk(true);
    const btn = padFindButton(col, row);
    padState.btnClipboard = btn ? padStripPosition(btn) : null;

    if (padState.btnClipboard) {
        document.getElementById('pad-edit-paste').disabled = false;
        document.getElementById('pad-fill-btn').disabled = false;
        showMessage('Button copied', 'success');
    }
}

function padDialogPasteBtn() {
    if (!padState.btnClipboard) return;
    const col = padState.editCol;
    const row = padState.editRow;

    // Remove existing button at this position
    padState.buttons = padState.buttons.filter(b => !(b.col === col && b.row === row));

    // Paste clipboard with new position — best-effort span fitting
    const btn = Object.assign({}, padState.btnClipboard, { col: col, row: row });
    const srcCs = btn.col_span || 1;
    const srcRs = btn.row_span || 1;
    if (srcCs > 1 || srcRs > 1) {
        if (!padCanSpanFit(col, row, srcCs, srcRs, padState.buttons)) {
            delete btn.col_span;
            delete btn.row_span;
        }
    }
    padState.buttons.push(btn);

    padMarkDirty();
    padRenderGrid();

    // Reopen dialog to show pasted content
    padDialogOpen(col, row);
    showMessage('Button pasted', 'success');
}

// --- Fill pad with clipboard ---

function padFillWithClipboard() {
    if (!padState.btnClipboard) return;
    if (!confirm('Fill all cells with the copied button settings?')) return;

    for (let r = 0; r < padState.rows; r++) {
        for (let c = 0; c < padState.cols; c++) {
            // Remove existing button
            padState.buttons = padState.buttons.filter(b => !(b.col === c && b.row === r));
            // Add copy (strip spans — every cell gets a 1×1 button)
            const btn = Object.assign({}, padState.btnClipboard, { col: c, row: r });
            delete btn.col_span;
            delete btn.row_span;
            padState.buttons.push(btn);
        }
    }
    padRenderGrid();
    padMarkDirty();
    showMessage('Pad filled with copied button', 'success');
}

// --- Pad clipboard (copy/paste entire pad) ---

function padCopyPad() {
    padState.padClipboard = {
        cols: padState.cols,
        rows: padState.rows,
        name: document.getElementById('pad-name').value.trim(),
        wake_screen: document.getElementById('pad-wake-screen').value,
        bg_color: padGetBindableColor('pad-edit-page-bg-color') || '#000000',
        buttons: padState.buttons.map(b => Object.assign({}, b)),
        bindings: padState.bindings ? padState.bindings.map(b => Object.assign({}, b)) : [],
    };
    document.getElementById('pad-paste-btn').disabled = false;
    showMessage('Pad ' + (padState.page + 1) + ' copied', 'success');
}

function padPastePad() {
    if (!padState.padClipboard) return;

    padState.cols = padState.padClipboard.cols;
    padState.rows = padState.padClipboard.rows;
    padState.buttons = padState.padClipboard.buttons.map(b => Object.assign({}, b));

    document.getElementById('pad-cols').value = padState.cols;
    document.getElementById('pad-rows').value = padState.rows;
    document.getElementById('pad-name').value = padState.padClipboard.name || '';
    document.getElementById('pad-wake-screen').value = padState.padClipboard.wake_screen || '';
    padInitBindableColor(document.getElementById('pad-page-bg-color-wrap'));
    padSetBindableColor('pad-edit-page-bg-color', padState.padClipboard.bg_color, '#000000');

    padState.bindings = padState.padClipboard.bindings ? padState.padClipboard.bindings.map(b => Object.assign({}, b)) : [];
    padRenderBindings();

    padRenderGrid();
    padMarkDirty();
    showMessage('Pad pasted (unsaved)', 'success');
}

// --- Export/import single pad ---

function padExportPad() {
    const payload = {
        layout: 'grid',
        cols: padState.cols,
        rows: padState.rows,
        buttons: padState.buttons.map(b => {
            const copy = Object.assign({}, b);
            padColorsToHex(copy);
            return copy;
        }),
    };
    const padName = document.getElementById('pad-name').value.trim();
    if (padName) payload.name = padName;
    const wakeScreen = document.getElementById('pad-wake-screen').value;
    if (wakeScreen) payload.wake_screen = wakeScreen;
    const expBgC = padGetBindableColor('pad-edit-page-bg-color');
    if (expBgC && expBgC !== '#000000') {
        payload.bg_color = expBgC.startsWith('#') ? expBgC.slice(1) : expBgC;
    }
    if (padState.bindings && padState.bindings.length > 0) {
        var bd = padBindingsToDict(padState.bindings);
        if (bd) payload.bindings = bd;
    }

    const blob = new Blob([JSON.stringify(payload, null, 2)], { type: 'application/json' });
    const a = document.createElement('a');
    a.href = URL.createObjectURL(blob);
    a.download = 'pad_' + (padState.page + 1) + (padName ? '_' + padName.replace(/[^a-zA-Z0-9_-]/g, '_') : '') + '.json';
    a.click();
    URL.revokeObjectURL(a.href);
}

async function padImportPad(evt) {
    const file = evt.target.files[0];
    evt.target.value = '';
    if (!file) return;

    try {
        const text = await file.text();
        const json = JSON.parse(text);

        if (!json.cols || !json.rows || !Array.isArray(json.buttons)) {
            throw new Error('Invalid pad JSON: missing cols, rows, or buttons');
        }
        if (json.cols < 1 || json.cols > 8 || json.rows < 1 || json.rows > 8) {
            throw new Error('Invalid grid size (1-8 cols/rows)');
        }

        // Load into editor
        padState.cols = json.cols;
        padState.rows = json.rows;
        padState.buttons = json.buttons.map(b => Object.assign({}, b));

        document.getElementById('pad-cols').value = padState.cols;
        document.getElementById('pad-rows').value = padState.rows;
        document.getElementById('pad-name').value = json.name || '';
        document.getElementById('pad-wake-screen').value = json.wake_screen || '';
        padInitBindableColor(document.getElementById('pad-page-bg-color-wrap'));
        padSetBindableColor('pad-edit-page-bg-color', json.bg_color, '#000000');

        padState.bindings = padBindingsFromJson(json.bindings);
        padRenderBindings();

        padRenderGrid();
        padMarkDirty();
        showMessage('Pad imported (unsaved) — click Save Pad to apply', 'success');
    } catch (err) {
        showMessage('Import failed: ' + err.message, 'error');
    }
}

// --- Export/import full device config ---

async function deviceExportConfig() {
    try {
        showMessage('Exporting device config...', 'success');

        // Fetch device config
        const cfgResp = await fetch('/api/config');
        if (!cfgResp.ok) throw new Error('Failed to fetch device config');
        const config = await cfgResp.json();

        // Remove fields we don't want to export (network-specific)
        delete config.device_name;
        delete config.device_name_sanitized;
        delete config.fixed_ip;
        delete config.subnet_mask;
        delete config.gateway;
        delete config.dns1;
        delete config.dns2;
        delete config.wifi_ssid;
        delete config.wifi_password;

        // Fetch all 8 pad configs
        const pads = [];
        for (let i = 0; i < 8; i++) {
            try {
                const resp = await fetch('/api/pad?page=' + i);
                if (resp.ok) {
                    pads.push(await resp.json());
                } else {
                    pads.push(null);
                }
            } catch (e) {
                pads.push(null);
            }
        }

        const exportData = {
            _format: DEVICE_CONFIG_FORMAT,
            _version: DEVICE_CONFIG_VERSION,
            config: config,
            pads: pads,
        };

        const deviceName = (window.deviceConfig && window.deviceConfig.device_name) || 'device';
        const blob = new Blob([JSON.stringify(exportData, null, 2)], { type: 'application/json' });
        const a = document.createElement('a');
        a.href = URL.createObjectURL(blob);
        a.download = deviceName.replace(/[^a-zA-Z0-9_-]/g, '_') + '_config.json';
        a.click();
        URL.revokeObjectURL(a.href);
        showMessage('Device config exported', 'success');
    } catch (err) {
        showMessage('Export failed: ' + err.message, 'error');
    }
}

async function deviceImportConfig(evt) {
    const file = evt.target.files[0];
    evt.target.value = '';
    if (!file) return;

    try {
        const text = await file.text();
        const data = JSON.parse(text);

        if (data._format !== DEVICE_CONFIG_FORMAT || data._version !== DEVICE_CONFIG_VERSION) {
            throw new Error('Unrecognized file format');
        }

        if (!confirm('Import device configuration? This will overwrite current settings and all pad configs. The device will reboot.')) return;

        showMessage('Importing device config...', 'success');

        // Step 1: Import device settings (excl. network fields which were stripped on export)
        if (data.config && typeof data.config === 'object') {
            const resp = await fetch('/api/config?no_reboot=1', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(data.config),
            });
            if (!resp.ok) throw new Error('Config import failed: HTTP ' + resp.status);
        }

        // Step 2: Import all pad configs (save each to trigger icon rendering)
        if (Array.isArray(data.pads)) {
            for (let i = 0; i < data.pads.length && i < 8; i++) {
                const padJson = data.pads[i];
                if (!padJson) {
                    // Delete pad if it was null in export
                    await fetch('/api/icons/page?page=' + i, { method: 'DELETE' }).catch(() => {});
                    await fetch('/api/pad?page=' + i, { method: 'DELETE' }).catch(() => {});
                    continue;
                }

                // Load pad into editor state, save it (which triggers icon upload)
                padState.page = i;
                padState.rawJson = null;
                padState.cols = padJson.cols || 3;
                padState.rows = padJson.rows || 2;
                padState.buttons = (padJson.buttons || []).map(b => Object.assign({}, b));
                document.getElementById('pad-page-select').value = i;
                document.getElementById('pad-cols').value = padState.cols;
                document.getElementById('pad-rows').value = padState.rows;
                document.getElementById('pad-name').value = padJson.name || '';
                document.getElementById('pad-wake-screen').value = padJson.wake_screen || '';
                padInitBindableColor(document.getElementById('pad-page-bg-color-wrap'));
                padSetBindableColor('pad-edit-page-bg-color', padJson.bg_color, '#000000');

                // Delete old icons first
                await fetch('/api/icons/page?page=' + i, { method: 'DELETE' }).catch(() => {});

                // Save pad (includes icon upload)
                await padSavePage();
            }
        }

        showMessage('Import complete — rebooting device...', 'success');

        // Reboot to apply NVS config
        setTimeout(() => {
            fetch('/api/reboot', { method: 'POST' }).catch(() => {});
        }, 500);
    } catch (err) {
        showMessage('Import failed: ' + err.message, 'error');
    }
}

async function padDeletePage() {
    if (!confirm('Clear Pad ' + (padState.page + 1) + '? This will remove all buttons. This cannot be undone.')) return;

    try {
        // Delete page icons
        await fetch('/api/icons/page?page=' + padState.page, { method: 'DELETE' });

        const resp = await fetch('/api/pad?page=' + padState.page, { method: 'DELETE' });
        if (!resp.ok) {
            const err = await resp.json().catch(() => ({}));
            throw new Error(err.error || 'HTTP ' + resp.status);
        }
        showMessage('Pad ' + (padState.page + 1) + ' deleted', 'success');
        padState.rawJson = null;
        padState.buttons = [];
        padState.cols = 3;
        padState.rows = 2;
        document.getElementById('pad-cols').value = padState.cols;
        document.getElementById('pad-rows').value = padState.rows;
        document.getElementById('pad-name').value = '';
        document.getElementById('pad-wake-screen').value = '';
        padInitBindableColor(document.getElementById('pad-page-bg-color-wrap'));
        padSetBindableColor('pad-edit-page-bg-color', '#000000');
        padState.bindings = [];
        padRenderBindings();
        padUpdateDropdownLabel(padState.page, '');
        padRenderGrid();
    } catch (err) {
        console.error('padDeletePage error:', err);
        showMessage('Delete failed: ' + err.message, 'error');
    }
}

async function padShowOnDevice() {
    const screenId = 'pad_' + padState.page;
    try {
        const resp = await fetch('/api/display/screen', {
            method: 'PUT',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ screen: screenId }),
        });
        if (!resp.ok) throw new Error('HTTP ' + resp.status);
        showMessage('Showing ' + screenId + ' on device', 'success');
    } catch (err) {
        console.error('padShowOnDevice error:', err);
        showMessage('Failed to switch screen', 'error');
    }
}