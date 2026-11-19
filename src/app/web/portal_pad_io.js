// portal_pad_io.js - Pad clipboard, import/export, and device config I/O
// Part of the ESP32 Macropad configuration portal.

function padStripPosition(btn) {
    const copy = padCloneJson(btn);
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

    let btn;
    try { btn = padDialogBuildButton(); padDialogValidateButton(btn); }
    catch (error) { padDialogShowValidationError(error.message); return; }
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
    const existing = padFindButton(col, row);
    if (!existing && padState.mainButtons.length + padState.overlayButtons.length >= padMaxButtons()) {
        showMessage('Combined Main and Overlay button limit reached (' + padMaxButtons() + ')', 'error');
        return;
    }
    if (typeof padWorkspaceClearButton === 'function') padWorkspaceClearButton(col, row);

    // Remove existing button at this position
    padState.buttons = padState.buttons.filter(b => !(b.col === col && b.row === row));

    // Paste clipboard with new position — best-effort span fitting
    const btn = Object.assign(padCloneJson(padState.btnClipboard), { col: col, row: row });
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
}

// --- Fill pad with clipboard ---

function padFillWithClipboard() {
    if (!padState.btnClipboard) return;
    const otherLayerCount = padState.selectedLayer === 'overlay'
        ? padState.mainButtons.length : padState.overlayButtons.length;
    const fillCount = padState.cols * padState.rows;
    if (otherLayerCount + fillCount > padMaxButtons()) {
        showMessage('Filling this layer would exceed the combined button limit (' + padMaxButtons() + ')', 'error');
        return;
    }
    if (!confirm('Fill all cells with the copied button settings?')) return;
    if (typeof padWorkspaceReset === 'function') padWorkspaceReset(padState.selectedLayer);

    for (let r = 0; r < padState.rows; r++) {
        for (let c = 0; c < padState.cols; c++) {
            // Remove existing button
            padState.buttons = padState.buttons.filter(b => !(b.col === c && b.row === r));
            // Add copy (strip spans — every cell gets a 1×1 button)
            const btn = Object.assign(padCloneJson(padState.btnClipboard), { col: c, row: r });
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
    const tracks = padTrackValidate(true);
    if (!tracks.valid) {
        const axis = tracks.rows.status !== 'valid' ? 'rows' : 'columns';
        const section = document.getElementById('pad-custom-track-sizes-section');
        if (section) section.open = true;
        const field = padTrackField(axis);
        if (field && field.focus) field.focus();
        showMessage(tracks[axis].message, 'error');
        return;
    }
    try { if (typeof padWorkspaceValidate === 'function') padWorkspaceValidate(); }
    catch (error) { showMessage(error.message, 'error'); return; }
    // Snapshot current pad and button defaults from UI
    padCollectButtonDefaults();
    padState.padClipboard = {
        cols: padState.cols,
        rows: padState.rows,
        column_widths_px: tracks.columns.sizes,
        row_heights_px: tracks.rows.sizes,
        name: document.getElementById('pad-name').value.trim(),
        wake_screen: document.getElementById('pad-wake-screen').value,
        bg_color: padGetBindableColor('pad-edit-page-bg-color') || padGetEffectiveDefault('default_pad_bg_color'),
        buttons: padCloneJson(padState.mainButtons),
        overlay_buttons: padCloneJson(padState.overlayButtons),
        bindings: padState.bindings ? padState.bindings.map(b => Object.assign({}, b)) : [],
        padActions: padBuildLevelActions().map(a => Object.assign({}, a)),
        buttonDefaults: Object.assign({}, padState.buttonDefaults),
        templatePad: padState.templatePad,
        buttonShadow: document.getElementById('pad-button-shadow').value || 'inherit',
    };
    document.getElementById('pad-paste-btn').disabled = false;
    showMessage('Pad ' + (padState.page + 1) + ' copied', 'success');
}

function padPastePad() {
    if (!padState.padClipboard) return;
    if (typeof padWorkspaceReset === 'function') padWorkspaceReset();

    padState.cols = padState.padClipboard.cols;
    padState.rows = padState.padClipboard.rows;
    padState.mainButtons = padCloneJson(padState.padClipboard.buttons);
    padState.overlayButtons = padCloneJson(padState.padClipboard.overlay_buttons || []);

    document.getElementById('pad-cols').value = padState.cols;
    document.getElementById('pad-rows').value = padState.rows;
    document.getElementById('pad-name').value = padState.padClipboard.name || '';
    document.getElementById('pad-wake-screen').value = padState.padClipboard.wake_screen || '';
    padInitBindableColor(document.getElementById('pad-page-bg-color-wrap'));
    padSetBindableColor('pad-edit-page-bg-color', padState.padClipboard.bg_color, padGetEffectiveDefault('default_pad_bg_color'));
    document.getElementById('pad-page-bg-mode').value = padState.padClipboard.bg_color ? 'override' : 'inherit';
    padPageBackgroundModeChanged();
    document.getElementById('pad-button-shadow').value = padState.padClipboard.buttonShadow || 'inherit';

    padState.bindings = padState.padClipboard.bindings ? padState.padClipboard.bindings.map(b => Object.assign({}, b)) : [];
    padRenderBindings();
    padLoadLevelActions(padState.padClipboard.padActions);

    // Paste pad and button defaults
    padLoadButtonDefaults(padState.padClipboard.buttonDefaults || {});
    padTrackLoad({
        column_widths_px: padState.padClipboard.column_widths_px || [],
        row_heights_px: padState.padClipboard.row_heights_px || [],
    });

    // Paste template pad (clear if it would reference self)
    padState.templatePad = (padState.padClipboard.templatePad !== undefined &&
        padState.padClipboard.templatePad !== padState.page) ? padState.padClipboard.templatePad : -1;
    padPopulateTemplateDropdown(padState.page);
    padLoadTemplateButtons().then(() => padRenderGrid());

    padMarkDirty();
    showMessage('Pad pasted (unsaved)', 'success');
}

// --- Export/import single pad ---

function padCheckImportedKeys(value) {
    const pending = [value];
    while (pending.length) {
        const current = pending.pop();
        if (!current || typeof current !== 'object') continue;
        for (const [key, child] of Object.entries(current)) {
            if (key.includes('\0')) throw new Error('JSON field names must not contain NUL characters.');
            if (child && typeof child === 'object') pending.push(child);
        }
    }
}

function padCheckImportedActionFormat(pad) {
    if (pad.action_format_version !== undefined && pad.action_format_version !== 1)
        throw new Error('Unsupported action format version.');
    const buttons = (pad.buttons || []).concat(pad.overlay_buttons || []);
    const arrays = [pad.pad_actions];
    for (const button of buttons) {
        if (['action', 'lp_action', 'action_screen', 'lp_action_screen'].some(key => Object.prototype.hasOwnProperty.call(button, key)))
            throw new Error('Legacy pad actions are not supported.');
        arrays.push(button.actions, button.lp_actions);
        if (button.widget_numericrocker_action !== undefined) arrays.push([button.widget_numericrocker_action]);
    }
    for (const actions of arrays) {
        if (actions === undefined || actions === null) continue;
        if (!Array.isArray(actions)) throw new Error('Pad actions must be arrays.');
        if (actions.length > MAX_ACTIONS) throw new Error('Pad action limit exceeded (' + MAX_ACTIONS + ').');
        for (const action of actions) {
            if (pad.action_format_version !== 1 || !action || typeof action !== 'object' ||
                Array.isArray(action) || Object.keys(action).some(key => key !== 'op' && key !== 'args') ||
                typeof action.op !== 'string' || !action.args || typeof action.args !== 'object' ||
                Array.isArray(action.args) || !actionEditorOperation(action.op))
                throw new Error('Pad actions require action format version 1 and supported operation envelopes.');
            const operation = actionEditorOperation(action.op);
            if (!Array.isArray(operation.arguments))
                throw new Error('Operation argument information is unavailable: ' + action.op);
            const argumentsByName = new Map(operation.arguments.map(argument => [argument.name, argument]));
            if (Object.keys(action.args).some(name => !argumentsByName.has(name)) ||
                operation.arguments.some(argument => !Object.prototype.hasOwnProperty.call(action.args, argument.name)))
                throw new Error('Operation arguments do not match ' + action.op + '.');
            for (const argument of operation.arguments) {
                const value = action.args[argument.name];
                if (value && typeof value === 'object' && !Array.isArray(value)) {
                    if (!argument.dynamic || Object.keys(value).length !== 1 ||
                        typeof value.$template !== 'string' || !value.$template ||
                        value.$template.includes('\0') || new TextEncoder().encode(value.$template).length >= 192 ||
                        !validateBinding(value.$template, { requireKnownScheme: true }).valid)
                        throw new Error('Invalid dynamic argument for ' + action.op + '.');
                } else if (argument.type === 'boolean' ? typeof value !== 'boolean'
                    : argument.type === 'integer' ? !Number.isInteger(value) || value < argument.min || value > argument.max ||
                        (argument.nonzero_min && value !== 0 && value < argument.nonzero_min)
                    : argument.type === 'number' ? typeof value !== 'number' || !Number.isFinite(value) ||
                        value < argument.min || value > argument.max ||
                        !Number.isFinite(Math.fround(value)) || Math.fround(value) < argument.min || Math.fround(value) > argument.max
                    : typeof value !== 'string' || value.includes('\0') ||
                        (!value && !argument.allow_empty) || new TextEncoder().encode(value).length > argument.max_length ||
                        (argument.type === 'enum' && !(argument.choices || []).includes(value))) {
                    throw new Error('Invalid argument for ' + action.op + '.');
                }
            }
        }
    }
}

async function padExportPad() {
    if (padState.loadError) {
        showMessage(padState.loadError, 'error');
        return;
    }
    const tracks = padTrackValidate(true);
    if (!tracks.valid) {
        const axis = tracks.rows.status !== 'valid' ? 'rows' : 'columns';
        const section = document.getElementById('pad-custom-track-sizes-section');
        if (section) section.open = true;
        const field = padTrackField(axis);
        if (field && field.focus) field.focus();
        showMessage(tracks[axis].message, 'error');
        return;
    }
    try { if (typeof padWorkspaceValidate === 'function') padWorkspaceValidate(); }
    catch (error) { showMessage(error.message, 'error'); return; }
    padCollectButtonDefaults();
    const payload = {
        action_format_version: 1,
        layout: 'grid',
        cols: padState.cols,
        rows: padState.rows,
        buttons: padState.mainButtons.map(b => {
            const copy = padCloneJson(b);
            padColorsToHex(copy);
            return copy;
        }),
        overlay_buttons: padState.overlayButtons.map(b => {
            const copy = padCloneJson(b);
            padColorsToHex(copy);
            return copy;
        }),
    };
    if (tracks.columns.sizes.some(size => size > 0)) payload.column_widths_px = tracks.columns.sizes;
    if (tracks.rows.sizes.some(size => size > 0)) payload.row_heights_px = tracks.rows.sizes;
    const padName = document.getElementById('pad-name').value.trim();
    if (padName) payload.name = padName;
    const wakeScreen = document.getElementById('pad-wake-screen').value;
    if (wakeScreen) payload.wake_screen = wakeScreen;
    const expBgC = padGetBindableColor('pad-edit-page-bg-color');
    if (document.getElementById('pad-page-bg-mode').value === 'override' && expBgC) {
        payload.bg_color = expBgC.startsWith('#') ? expBgC.slice(1) : expBgC;
    }
    if (padState.bindings && padState.bindings.length > 0) {
        var bd = padBindingsToDict(padState.bindings);
        if (bd) payload.bindings = bd;
    }
    const padActions = padBuildLevelActions();
    if (padActions.length > 0) payload.pad_actions = padActions;
    if (padState.templatePad >= 0) {
        payload.template_pad = padState.templatePad;
    }
    const padShadow = document.getElementById('pad-button-shadow').value || 'inherit';
    if (padShadow !== 'inherit') payload.button_shadow = padShadow;

    try {
        payload._icon_sources = await padCollectExportIconSources(
            [{ page: padState.page, payload }], [padState.page],
            Number(deviceInfoCache && deviceInfoCache.max_pads) || 8, {
                byDraftId: padState.draftIconSources || {},
                bySourceId: padState.importedIconSources || {},
            });
        const blob = new Blob([JSON.stringify(payload, null, 2)], { type: 'application/json' });
        const a = document.createElement('a');
        a.href = URL.createObjectURL(blob);
        a.download = 'pad_' + (padState.page + 1) + (padName ? '_' + padName.replace(/[^a-zA-Z0-9_-]/g, '_') : '') + '.json';
        a.click();
        URL.revokeObjectURL(a.href);
    } catch (error) {
        showMessage('Export failed: ' + error.message, 'error');
    }
}

async function padImportPad(evt) {
    const file = evt.target.files[0];
    evt.target.value = '';
    if (!file) return;
    const page = padState.page;
    const section = document.getElementById('pad-config-section');

    try {
        if (padState.loadPending) throw new Error('Pad is still loading. Wait before importing.');
        if (Number(file.size) > 16 * 1024 * 1024) throw new Error('Import file exceeds the 16 MiB limit.');
        const text = await file.text();
        const json = JSON.parse(text);
        if (padState.page !== page || document.getElementById('pad-config-section') !== section) return;

        padCheckImportedKeys(json);
        if (!json.cols || !json.rows || !Array.isArray(json.buttons) ||
            (json.overlay_buttons !== undefined && !Array.isArray(json.overlay_buttons))) {
            throw new Error('Invalid pad JSON: missing cols, rows, or buttons');
        }
        padCheckImportedActionFormat(json);
        const importedSources = padValidateImportedIconSources([json], json._icon_sources);
        if (json.buttons.length + (json.overlay_buttons || []).length > padMaxButtons())
            throw new Error('Combined Main and Overlay button limit exceeded (' + padMaxButtons() + ')');
        const maxCols = (deviceInfoCache && deviceInfoCache.max_grid_cols) || 8;
        const maxRows = (deviceInfoCache && deviceInfoCache.max_grid_rows) || 8;
        if (json.cols < 1 || json.cols > maxCols || json.rows < 1 || json.rows > maxRows) {
            throw new Error('Invalid grid size (maximum ' + maxCols + ' columns and ' + maxRows + ' rows)');
        }
        if (padState.loadError &&
            !confirm('The saved pad could not be loaded. Importing and saving this draft will replace its settings. Continue?')) return;

        // Load into editor
        if (typeof padWorkspaceReset === 'function') padWorkspaceReset();
        padState.selectedLayer = 'main';
        padState.layerSelections = { main: null, overlay: null };
        padState.importedIconSources = importedSources;
        padState.importedIconSourceIds = {};
        padState.draftIconSources = {};
        padState.rawJson = null;
        padState.cols = json.cols;
        padState.rows = json.rows;
        padTrackLoad(json);
        padState.mainButtons = json.buttons.map(b => Object.assign({}, b));
        padState.overlayButtons = (json.overlay_buttons || []).map(b => Object.assign({}, b));

        document.getElementById('pad-cols').value = padState.cols;
        document.getElementById('pad-rows').value = padState.rows;
        document.getElementById('pad-name').value = json.name || '';
        document.getElementById('pad-wake-screen').value = json.wake_screen || '';
        padInitBindableColor(document.getElementById('pad-page-bg-color-wrap'));
        padSetBindableColor('pad-edit-page-bg-color', json.bg_color || padGetEffectiveDefault('default_pad_bg_color'));
        document.getElementById('pad-page-bg-mode').value = json.bg_color ? 'override' : 'inherit';
        padPageBackgroundModeChanged();
        document.getElementById('pad-button-shadow').value = json.button_shadow || 'inherit';

        padState.bindings = padBindingsFromJson(json.bindings);
        padRenderBindings();
        padLoadLevelActions(json.pad_actions);

        // Load template pad (clear if it would reference self)
        padState.templatePad = (json.template_pad !== undefined && json.template_pad !== null &&
            json.template_pad !== padState.page) ? json.template_pad : -1;
        padPopulateTemplateDropdown(padState.page);
        await padLoadTemplateButtons();
        if (padState.page !== page || document.getElementById('pad-config-section') !== section) return;

        padRenderGrid();
        padState.loadError = null;
        padMarkDirty();
        showMessage('Pad imported (unsaved) — click Save Pad to apply', 'success');
    } catch (err) {
        showMessage('Import failed: ' + err.message, 'error');
    }
}

// --- Export device config ---

function devicePortableSettings(config) {
    const omitted = new Set(['device_name', 'device_name_sanitized', 'wifi_ssid', 'fixed_ip',
        'subnet_mask', 'gateway', 'dns1', 'dns2', 'basic_auth_username', 'basic_auth_enabled', 'mqtt_username',
        'save_current_brightness', 'save_current_volume']);
    return Object.fromEntries(Object.entries(config).filter(([key]) =>
        !omitted.has(key) && !/password|token|api_key/.test(key)));
}

async function deviceExportConfig() {
    try {
        showMessage('Exporting device config...', 'info');

        // Fetch device config
        const cfgResp = await fetch('/api/config');
        if (!cfgResp.ok) throw new Error('Failed to fetch device config');
        const config = devicePortableSettings(await cfgResp.json());

        // Fetch all pad configs
        const maxPads = (deviceInfoCache && deviceInfoCache.max_pads) || 8;
        const pads = [];
        for (let i = 0; i < maxPads; i++) {
            const resp = await fetch('/api/pad?page=' + i);
            if (resp.status === 404) pads.push(null);
            else if (resp.ok) pads.push(await resp.json());
            else throw new Error('Failed to fetch Pad ' + (i + 1) + ': HTTP ' + resp.status);
        }

        // Fetch device-level pad and button defaults
        const bdResp = await fetch('/api/component/button-defaults/config');
        if (!bdResp.ok) throw new Error('Failed to fetch pad and button defaults: HTTP ' + bdResp.status);
        const buttonDefaults = await bdResp.json();
        if (!buttonDefaults || typeof buttonDefaults !== 'object' || Array.isArray(buttonDefaults))
            throw new Error('Invalid pad and button defaults response');
        delete buttonDefaults.firmware_defaults;
        delete buttonDefaults.typography_status;

        const exportData = {
            _format: DEVICE_CONFIG_FORMAT,
            _version: DEVICE_CONFIG_VERSION,
            config: config,
            button_defaults: buttonDefaults,
            pads: pads,
            _icon_sources: await padCollectExportIconSources(
                pads.map((payload, page) => ({ page, payload })).filter(entry => entry.payload),
                pads.map((payload, page) => payload ? page : -1).filter(page => page >= 0), maxPads),
        };

        const deviceName = (window.deviceConfig && window.deviceConfig.device_name) || 'device';
        const blob = new Blob([JSON.stringify(exportData, null, 2)], { type: 'application/json' });
        const a = document.createElement('a');
        a.href = URL.createObjectURL(blob);
        a.download = deviceName.replace(/[^a-zA-Z0-9_-]/g, '_') + '_config.json';
        a.click();
        URL.revokeObjectURL(a.href);
    } catch (err) {
        showMessage('Export failed: ' + err.message, 'error');
    }
}

async function padDeletePage() {
    if (padState.loadPending) {
        showMessage('Pad is still loading. Wait before clearing it.', 'error');
        return;
    }
    const prompt = padState.loadError
        ? 'The saved pad could not be loaded. Clearing and saving will replace its settings. Continue?'
        : 'Clear Pad ' + (padState.page + 1) + ' in the editor? Save Pad to apply.';
    if (!confirm(prompt)) return;

    try {
        if (typeof padWorkspaceReset === 'function') padWorkspaceReset();
        padState.rawJson = null;
        padState.selectedLayer = 'main';
        padState.layerSelections = { main: null, overlay: null };
        padState.mainButtons = [];
        padState.overlayButtons = [];
        padState.importedIconSources = {};
        padState.importedIconSourceIds = {};
        padState.dragSource = null;
        padState.cols = 3;
        padState.rows = 2;
        padTrackLoad({});
        padState.trackValidationError = false;
        padDialogClearValidationError();
        document.getElementById('pad-cols').value = padState.cols;
        document.getElementById('pad-rows').value = padState.rows;
        document.getElementById('pad-name').value = '';
        document.getElementById('pad-wake-screen').value = '';
        padInitBindableColor(document.getElementById('pad-page-bg-color-wrap'));
        padSetBindableColor('pad-edit-page-bg-color', padGetEffectiveDefault('default_pad_bg_color'));
        padState.bindings = [];
        padRenderBindings();
        padLoadLevelActions([]);
        padState.templatePad = -1;
        padState.templateButtons = [];
        padPruneDraftIconSources();
        padPopulateTemplateDropdown(padState.page);
        document.getElementById('pad-page-bg-mode').value = 'inherit';
        padPageBackgroundModeChanged();
        document.getElementById('pad-button-shadow').value = 'inherit';
        if (padState.placingBlock) padExitPlacementMode();
        if (typeof padWorkspaceRefresh === 'function') padWorkspaceRefresh();
        padRenderGrid();
        padState.loadError = null;
        padMarkDirty();
        showMessage('Pad cleared (unsaved)', 'success');
    } catch (err) {
        console.error('padDeletePage error:', err);
        showMessage('Clear failed: ' + err.message, 'error');
    }
}

async function padShowOnDevice() {
    const screenId = 'pad_' + padState.page;
    try {
        const resp = await fetch('/api/component/display/screen', {
            method: 'PUT',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ screen: screenId }),
        });
        if (!resp.ok) throw new Error('HTTP ' + resp.status);
        showMessage('Showing ' + screenId + ' on device', 'info');
    } catch (err) {
        console.error('padShowOnDevice error:', err);
        showMessage('Failed to switch screen', 'error');
    }
}