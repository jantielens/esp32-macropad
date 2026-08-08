// portal_pad_editor.js - Pad editor state, initialization, page load/save, and dropdowns
// Part of the ESP32 Macropad configuration portal.
//
// Fragment modules (bundled during minification via portal_pad_editor.js.bundle):
//   portal_pad_blocks.js   - Building block catalog and placement
//   portal_pad_icons.js    - Icon rendering, cell content, utility helpers
//   portal_pad_defaults.js - Button defaults, bindings, color helpers
//   portal_pad_grid.js     - Grid rendering, resize, drag-and-drop
//   portal_pad_dialog.js   - Button edit dialog

// ===== PAD CONFIGURATION =====

const padState = {
    page: 0,
    rawJson: null,   // Original GET response (for merge-on-save)
    cols: 3,
    rows: 2,
    buttons: [],     // Working copy: array of button objects (by grid position key "col,row")
    editCol: 0,
    editRow: 0,
    btnClipboard: null,  // Copied button settings (position-independent)
    padClipboard: null,  // Copied pad settings { cols, rows, buttons, name }
    bindings: [],        // Page-level named bindings [{name, value}]
    padActions: [],      // Full-screen pad tap actions
    colorCache: {},      // page → hex[] — colors from visited pads
    buttonDefaults: {},  // Device-level button defaults (loaded from /api/button-defaults)
    templatePad: -1,     // Template pad index (-1 = none)
    templateButtons: [], // Buttons loaded from template pad (for ghost rendering)
    placingBlock: null,  // Block being placed (from catalog), or null
    dragSource: null,    // {col, row} of button being dragged, or null
};

let padDirty = false;

function padMarkDirty() {
    padDirty = true;
}

function padClearDirty() {
    padDirty = false;
}

const DEVICE_CONFIG_FORMAT = 'esp32-macropad-config';
const DEVICE_CONFIG_VERSION = 1;

async function padInit() {
    const section = document.getElementById('pad-config-section');
    if (!section) return;

    // The action-type picker renders from the firmware catalog cached on
    // deviceInfoCache; wait for it before building any action editor markup
    // so the picker is complete and correct on first paint.
    await getDeviceInfo();

    // Generate action editor HTML from shared module — three fixed action
    // slots per gesture. An unused slot collapses as its own "Add ..."
    // placeholder; labels are set here for the non-widget default and
    // re-applied by padWidgetTypeChanged() for rocker/list widgets.
    const aeContainer = document.getElementById('pad-action-editors');
    if (aeContainer) {
        var aeOpts = { showBleHint: true, showKeyHelp: true };
        aeContainer.innerHTML =
            '<div class="action-group-heading">Tap actions</div>' +
            '<div id="pad-edit-tap-actions"></div>' +
            '<div class="action-group-heading" id="pad-edit-lp-heading">Long-press actions</div>' +
            '<div id="pad-edit-lp-actions"></div>';
        actionEditorListRender('pad-edit-tap-actions', padActionPrefixes('tap'), padDefaultActionLabels('Tap action'), { actionOptions: aeOpts });
        actionEditorListRender('pad-edit-lp-actions', padActionPrefixes('lp'), padDefaultActionLabels('Long-press action'), { actionOptions: aeOpts });
    }

    const padActionContainer = document.getElementById('pad-level-action-editors');
    if (padActionContainer) {
        actionEditorListRender('pad-level-action-editors', padLevelActionPrefixes(), padDefaultActionLabels('Tap action'),
            { actionOptions: { showBleHint: true, showKeyHelp: true } });
        padActionContainer.addEventListener('input', padMarkDirty);
        padActionContainer.addEventListener('change', padMarkDirty);
    }

    // Generate numeric rocker adjustment action editor
    var nrAdjContainer = document.getElementById('pad-edit-nr-adjust-container');
    if (nrAdjContainer) {
        nrAdjContainer.innerHTML = actionEditorHTML('pad-edit-nr-adjust', 'Adjustment Action', { showBleHint: true, showKeyHelp: true });
    }

    document.getElementById('pad-page-select').addEventListener('change', (e) => {
        const newPage = parseInt(e.target.value);
        if (padDirty) {
            if (!confirm('You have unsaved changes. Discard and switch pad?')) {
                e.target.value = padState.page;
                return;
            }
        }
        padClearDirty();
        padState.page = newPage;
        padLoadPage(padState.page);
    });
    document.getElementById('pad-cols').addEventListener('change', (e) => {
        padState.cols = parseInt(e.target.value);
        padMarkDirty();
        padRenderGrid();
    });
    document.getElementById('pad-rows').addEventListener('change', (e) => {
        padState.rows = parseInt(e.target.value);
        padMarkDirty();
        padRenderGrid();
    });
    document.getElementById('pad-template-pad').addEventListener('change', async (e) => {
        padState.templatePad = parseInt(e.target.value);
        padMarkDirty();
        await padLoadTemplateButtons();
        padRenderGrid();
    });

    document.getElementById('pad-save-btn').addEventListener('click', padSavePage);
    document.getElementById('pad-delete-btn').addEventListener('click', padDeletePage);
    document.getElementById('pad-show-btn').addEventListener('click', padShowOnDevice);
    document.getElementById('pad-binding-add').addEventListener('click', padAddBinding);
    var btnDefSaveBtn = document.getElementById('btn-defaults-save-btn');
    if (btnDefSaveBtn) btnDefSaveBtn.addEventListener('click', padSaveButtonDefaults);

    // More menu toggle
    const moreBtn = document.getElementById('pad-more-btn');
    const moreMenu = document.getElementById('pad-more-menu');
    moreBtn.addEventListener('click', (e) => {
        e.stopPropagation();
        moreMenu.style.display = moreMenu.style.display === 'none' ? 'block' : 'none';
    });
    // Close menu on outside click
    document.addEventListener('click', () => { moreMenu.style.display = 'none'; });
    // Close menu when any menu item is clicked
    moreMenu.addEventListener('click', (e) => {
        if (e.target.tagName === 'BUTTON' && !e.target.disabled) moreMenu.style.display = 'none';
    });

    // Dialog buttons
    document.getElementById('pad-edit-ok').addEventListener('click', () => padDialogOk());
    document.getElementById('pad-edit-copy').addEventListener('click', padDialogCopyBtn);
    document.getElementById('pad-edit-paste').addEventListener('click', padDialogPasteBtn);
    document.getElementById('pad-edit-clear').addEventListener('click', padDialogClear);
    document.getElementById('pad-edit-cancel').addEventListener('click', padDialogClose);

    // Wire appearance field input listeners for reset-hint visibility (once)
    PAD_APPEARANCE_FIELDS.forEach(function(f) {
        var el = document.getElementById(f.input);
        if (el) el.addEventListener('input', padUpdateResetHints);
    });

    // Pad-level actions
    document.getElementById('pad-fill-btn').addEventListener('click', padFillWithClipboard);
    document.getElementById('pad-copy-btn').addEventListener('click', padCopyPad);
    document.getElementById('pad-paste-btn').addEventListener('click', padPastePad);
    document.getElementById('pad-export-btn').addEventListener('click', padExportPad);
    document.getElementById('pad-import-btn').addEventListener('click', () => document.getElementById('pad-import-file').click());
    document.getElementById('pad-import-file').addEventListener('change', padImportPad);

    // Device export/import
    document.getElementById('device-export-btn').addEventListener('click', deviceExportConfig);
    document.getElementById('device-import-btn').addEventListener('click', () => document.getElementById('device-import-file').click());
    document.getElementById('device-import-file').addEventListener('change', deviceImportConfig);

    // Icon input live preview
    const iconEmoji = document.getElementById('pad-edit-icon-emoji');
    if (iconEmoji) iconEmoji.addEventListener('input', padUpdateIconPreview);
    const iconMi = document.getElementById('pad-edit-icon-mi');
    if (iconMi) iconMi.addEventListener('input', padUpdateIconPreview);

    // Close dialog on overlay click
    document.getElementById('pad-edit-overlay').addEventListener('click', (e) => {
        if (e.target.id === 'pad-edit-overlay') padDialogClose();
    });

    // Block placement cancel + Escape key
    const blockCancelBtn = document.getElementById('pad-block-cancel-btn');
    if (blockCancelBtn) blockCancelBtn.addEventListener('click', padExitPlacementMode);
    document.addEventListener('keydown', (e) => {
        if (e.key === 'Escape' && padState.placingBlock) {
            padExitPlacementMode();
            e.stopImmediatePropagation();
        }
    });

    // Track unsaved changes on name and other inputs
    document.getElementById('pad-name').addEventListener('input', padMarkDirty);

    // Warn before leaving with unsaved changes
    window.addEventListener('beforeunload', (e) => {
        if (padDirty) {
            e.preventDefault();
        }
    });

    // deviceInfoCache is already populated (awaited at the top of padInit).
    if (deviceInfoCache.has_display === true) {
        section.style.display = 'block';
        const padFooter = document.getElementById('pad-floating-footer');
        if (padFooter) padFooter.style.display = '';
        // Show button defaults section
        var btnDefSec = document.getElementById('btn-defaults-section');
        if (btnDefSec) btnDefSec.style.display = 'block';
        padPopulateGridDropdowns();
        padPopulatePadDropdown();
        padPopulateScreenDropdown();
        padFetchSoundList();
        padLoadButtonDefaultsFromDevice();
        padLoadPage(0);
        padLoadBlockCatalog();
        padRefreshDropdownLabels();
    } else {
        const noDisp = document.getElementById('pad-no-display-section');
        if (noDisp) noDisp.style.display = 'block';
    }
}

// Prefix lists for the fixed action slots. Order is execution order.
function padActionPrefixes(gesture) {
    var base = (gesture === 'lp') ? 'pad-edit-lp-action-' : 'pad-edit-action-';
    var out = [];
    for (var i = 0; i < MAX_ACTIONS; i++) out.push(base + i);
    return out;
}
function padLevelActionPrefixes() {
    var out = [];
    for (var i = 0; i < MAX_ACTIONS; i++) out.push('pad-level-action-' + i);
    return out;
}
function padDefaultActionLabels(base) {
    var out = [];
    for (var i = 0; i < MAX_ACTIONS; i++) out.push(base + ' ' + (i + 1));
    return out;
}

function padLoadLevelActions(actions) {
    padState.padActions = (actions || []).filter(function(action) {
        return action && typeof action === 'object' && action.type && action.type !== 'none';
    }).slice(0, MAX_ACTIONS);
    actionEditorListLoad(padLevelActionPrefixes(), padState.padActions);
}

function padBuildLevelActions() {
    padState.padActions = actionEditorListBuild(padLevelActionPrefixes());
    return padState.padActions;
}

function padPopulateGridDropdowns() {
    const maxCols = (deviceInfoCache && deviceInfoCache.max_grid_cols) || 8;
    const maxRows = (deviceInfoCache && deviceInfoCache.max_grid_rows) || 8;
    const colSel = document.getElementById('pad-cols');
    const rowSel = document.getElementById('pad-rows');
    if (colSel) {
        colSel.innerHTML = '';
        for (let i = 1; i <= maxCols; i++) {
            const o = document.createElement('option');
            o.value = i; o.textContent = i;
            colSel.appendChild(o);
        }
    }
    if (rowSel) {
        rowSel.innerHTML = '';
        for (let i = 1; i <= maxRows; i++) {
            const o = document.createElement('option');
            o.value = i; o.textContent = i;
            rowSel.appendChild(o);
        }
    }
}

function padPopulatePadDropdown() {
    const sel = document.getElementById('pad-page-select');
    if (!sel) return;
    const maxPads = (deviceInfoCache && deviceInfoCache.max_pads) || 8;
    sel.innerHTML = '';
    for (let i = 0; i < maxPads; i++) {
        const opt = document.createElement('option');
        opt.value = i;
        opt.textContent = 'Pad ' + (i + 1);
        sel.appendChild(opt);
    }
}

// Populate the template pad dropdown, excluding the current page
function padPopulateTemplateDropdown(currentPage) {
    const sel = document.getElementById('pad-template-pad');
    if (!sel) return;
    const maxPads = (deviceInfoCache && deviceInfoCache.max_pads) || 8;
    sel.innerHTML = '';
    const noneOpt = document.createElement('option');
    noneOpt.value = -1;
    noneOpt.textContent = '(none)';
    sel.appendChild(noneOpt);
    const screens = (deviceInfoCache && deviceInfoCache.available_screens) || [];
    for (let i = 0; i < maxPads; i++) {
        if (i === currentPage) continue;
        const opt = document.createElement('option');
        opt.value = i;
        // Try to find a friendly name from available_screens
        const scr = screens.find(s => s.id === 'pad_' + i);
        opt.textContent = scr ? scr.name : 'Pad ' + (i + 1);
        sel.appendChild(opt);
    }
    sel.value = padState.templatePad;
}

// Load template buttons asynchronously for ghost rendering
async function padLoadTemplateButtons() {
    padState.templateButtons = [];
    if (padState.templatePad < 0) return;
    try {
        const resp = await fetch('/api/pad?page=' + padState.templatePad);
        if (!resp.ok) return;
        const json = await resp.json();
        padState.templateButtons = (json.buttons && Array.isArray(json.buttons)) ? json.buttons : [];
    } catch (e) {
        // Silently ignore — template just won't show ghosts
    }
}

function padPopulateScreenDropdown() {
    var prefixes = padActionPrefixes('tap').concat(padActionPrefixes('lp'), padLevelActionPrefixes());
    prefixes.push('pad-edit-nr-adjust');
    prefixes.push('pad-edit-list-select');
    actionEditorPopulateScreens(
        prefixes,
        deviceInfoCache ? deviceInfoCache.available_screens : null
    );
    // Populate wake-screen dropdown (keep first "(stay on this screen)" option)
    const wakeSel = document.getElementById('pad-wake-screen');
    if (wakeSel && deviceInfoCache && deviceInfoCache.available_screens) {
        while (wakeSel.options.length > 1) wakeSel.remove(1);
        deviceInfoCache.available_screens.forEach(s => {
            const opt = document.createElement('option');
            opt.value = s.id;
            opt.textContent = s.name;
            wakeSel.appendChild(opt);
        });
    }
}

// Cached sound file list (populated at init, used synchronously on dialog open)
var padSoundListCache = [];

// Fetch sound list from device and update cache
function padFetchSoundList() {
    fetch('/api/sounds/list')
        .then(function(r) { return r.ok ? r.json() : []; })
        .then(function(sounds) { padSoundListCache = sounds; })
        .catch(function() {});
}

// Populate sound file dropdowns in action editors (synchronous, uses cache)
function padPopulateSoundDropdown() {
    var prefixes = padActionPrefixes('tap').concat(padActionPrefixes('lp'), padLevelActionPrefixes());
    prefixes.push('pad-edit-nr-adjust');
    prefixes.push('pad-edit-list-select');
    actionEditorPopulateSounds(prefixes, padSoundListCache);
}

const WIDGET_SECTIONS = ['bar_chart', 'gauge', 'sparkline', 'table', 'rocker', 'numericrocker', 'list'];

function padWidgetTypeChanged() {
    const wtype = document.getElementById('pad-edit-widget-type').value;
    WIDGET_SECTIONS.forEach(s => {
        const el = document.getElementById('pad-edit-' + s.replace('_', '-') + '-section');
        if (el) { el.style.display = (wtype === s) ? '' : 'none'; if (wtype === s) el.open = true; }
    });
    var confirmGroup = document.getElementById('pad-edit-confirm-group');
    var confirmInput = document.getElementById('pad-edit-confirm');
    if (confirmGroup) confirmGroup.style.display = wtype ? 'none' : '';
    if (wtype && confirmInput) {
        confirmInput.checked = false;
        padConfirmChanged();
    }
    // Refresh icon position visibility (widgets may override layout)
    padIconTypeChanged();

    // Rocker/list widgets: relabel Tap/LP action slots contextually. The
    // slot label is set here rather than mutating a <label> element — the
    // slot's own label now lives on its <details> wrapper (see
    // actionEditorListSetLabels), since summary text doubles as the
    // collapsed "Add action" placeholder.
    var isRocker = (wtype === 'rocker');
    var isNumericRocker = (wtype === 'numericrocker');
    var axis = 'vertical';
    if (isRocker) {
        var axSel = document.getElementById('pad-edit-rocker-axis');
        if (axSel) axis = axSel.value;
    }
    var isList = (wtype === 'list');
    var tapBase = isRocker ? ((axis === 'horizontal') ? 'Left action' : 'Up action')
        : isList ? 'Select action' : 'Tap action';
    var lpBase = isRocker ? ((axis === 'horizontal') ? 'Right action' : 'Down action') : 'Long-press action';
    actionEditorListSetLabels(padActionPrefixes('tap'), padDefaultActionLabels(tapBase));
    actionEditorListSetLabels(padActionPrefixes('lp'), padDefaultActionLabels(lpBase));

    // Numeric rocker uses a dedicated adjustment action instead of long-press actions.
    var lpActionsContainer = document.getElementById('pad-edit-lp-actions');
    if (lpActionsContainer) lpActionsContainer.style.display = isNumericRocker ? 'none' : '';
    var lpHeading = document.getElementById('pad-edit-lp-heading');
    if (lpHeading) lpHeading.style.display = isNumericRocker ? 'none' : '';

    // Numeric rocker: show adjustment action editor in widget settings
    var adjSection = document.getElementById('pad-edit-numericrocker-adjust-section');
    if (adjSection) adjSection.style.display = isNumericRocker ? '' : 'none';
    // List widget: inject/remove synthetic [list:provider.selected] option in tap/lp dropdowns
    if (typeof listRefreshSyntheticOptions === 'function') listRefreshSyntheticOptions();
}

async function padLoadPage(page) {
    padState.page = page;
    padState.rawJson = null;
    padState.buttons = [];
    padState.bindings = [];
    padState.padActions = [];
    padState.templatePad = -1;
    padState.templateButtons = [];
    padClearDirty();

    try {
        const resp = await fetch('/api/pad?page=' + page);
        if (resp.status === 404) {
            // No config for this page — show empty grid
            padState.cols = 3;
            padState.rows = 2;
            document.getElementById('pad-cols').value = padState.cols;
            document.getElementById('pad-rows').value = padState.rows;
            document.getElementById('pad-name').value = '';
            document.getElementById('pad-wake-screen').value = '';
            padInitBindableColor(document.getElementById('pad-page-bg-color-wrap'));
            padSetBindableColor('pad-edit-page-bg-color', '#000000');
            padState.bindings = [];
            padLoadLevelActions([]);
            padState.templatePad = -1;
            padState.templateButtons = [];
            padRenderBindings();
            padPopulateTemplateDropdown(page);
            padCacheColors(page, [], '#000000');
            padRenderGrid();
            return;
        }
        if (!resp.ok) throw new Error('HTTP ' + resp.status);

        const json = await resp.json();
        padState.rawJson = json;
        const maxCols = (deviceInfoCache && deviceInfoCache.max_grid_cols) || 8;
        const maxRows = (deviceInfoCache && deviceInfoCache.max_grid_rows) || 8;
        padState.cols = Math.min(json.cols || 3, maxCols);
        padState.rows = Math.min(json.rows || 2, maxRows);

        document.getElementById('pad-cols').value = padState.cols;
        document.getElementById('pad-rows').value = padState.rows;
        document.getElementById('pad-name').value = json.name || '';
        document.getElementById('pad-wake-screen').value = json.wake_screen || '';
        padInitBindableColor(document.getElementById('pad-page-bg-color-wrap'));
        padSetBindableColor('pad-edit-page-bg-color', json.bg_color, '#000000');

        // Load pad bindings
        padState.bindings = padBindingsFromJson(json.bindings);
        padRenderBindings();
        padLoadLevelActions(json.pad_actions);

        // Load template pad setting
        padState.templatePad = (json.template_pad !== undefined && json.template_pad !== null) ? json.template_pad : -1;
        padPopulateTemplateDropdown(page);
        await padLoadTemplateButtons();

        // Update dropdown label
        padUpdateDropdownLabel(page, json.name || '');

        // Index buttons by "col,row" for easy lookup
        padState.buttons = [];
        if (json.buttons && Array.isArray(json.buttons)) {
            json.buttons.forEach(b => {
                padState.buttons.push(Object.assign({}, b));
            });
        }

        // Cache colors for cross-pad swatches
        padCacheColors(page, padState.buttons, padColorToHex(json.bg_color, '#000000'));
        padRenderGrid();
    } catch (err) {
        console.error('padLoadPage error:', err);
        showMessage('Failed to load Pad ' + (page + 1), 'error');
        padRenderGrid();
    }
}

function padCloneJson(value) {
    return JSON.parse(JSON.stringify(value));
}

function padBuildSaveContext() {
    const snapshot = {
        page: padState.page,
        cols: padState.cols,
        rows: padState.rows,
        rawJson: padCloneJson(padState.rawJson || {}),
        buttons: padCloneJson(padState.buttons || []),
        bindings: padCloneJson(padState.bindings || []),
    };

    // Merge-on-save: start with rawJson as base, overlay our changes
    const payload = snapshot.rawJson;
    payload.layout = payload.layout || 'grid';
    payload.cols = snapshot.cols;
    payload.rows = snapshot.rows;
    const padName = document.getElementById('pad-name').value.trim();
    if (padName) payload.name = padName;
    else delete payload.name;
    const wakeScreen = document.getElementById('pad-wake-screen').value;
    if (wakeScreen) payload.wake_screen = wakeScreen;
    else delete payload.wake_screen;
    const pageBgC = padGetBindableColor('pad-edit-page-bg-color');
    if (pageBgC && pageBgC !== '#000000') {
        payload.bg_color = pageBgC.startsWith('#') ? pageBgC.slice(1) : pageBgC;
    } else {
        delete payload.bg_color;
    }
    delete payload.bg_color_default;

    // Strip legacy pad-level button_defaults (now device-level)
    delete payload.button_defaults;

    // Pad bindings → dict (skip entries with invalid names)
    if (snapshot.bindings.length > 0) {
        var badNames = snapshot.bindings.filter(function(b) { return b.name && !padIsValidBindingName(b.name); });
        if (badNames.length > 0) {
            throw new Error('Invalid binding name(s): ' + badNames.map(function(b) { return '"' + b.name + '"'; }).join(', ') + '\nNames must start with a letter and contain only letters, digits, or underscores.');
        }
        var bd = padBindingsToDict(snapshot.bindings);
        if (bd) payload.bindings = bd;
        else delete payload.bindings;
    } else {
        delete payload.bindings;
    }

    var padActions = padCloneJson(padBuildLevelActions());
    if (padActions.length > 0) payload.pad_actions = padActions;
    else delete payload.pad_actions;

    // Validate pad-level binding values before save
    if (typeof bindingValidatePadBindings === 'function') {
        var bvPad = bindingValidatePadBindings();
        if (!bvPad.valid) {
            throw new Error(bvPad.count + ' pad binding error' + (bvPad.count > 1 ? 's' : '') + ' — check highlighted fields');
        }
    }

    // Validate button defaults binding fields
    if (typeof bindingValidateDefaults === 'function') {
        var bvDef = bindingValidateDefaults();
        if (!bvDef.valid) {
            throw new Error(bvDef.count + ' button defaults error' + (bvDef.count > 1 ? 's' : '') + ' — check highlighted fields');
        }
    }

    // Template pad
    var tpl = parseInt(document.getElementById('pad-template-pad').value);
    if (tpl >= 0) {
        payload.template_pad = tpl;
    } else {
        delete payload.template_pad;
    }

    payload.buttons = snapshot.buttons;

    // Convert color ints to hex strings for JSON
    payload.buttons.forEach(b => padColorsToHex(b));

    // On boards with DISPLAY_BLANK_ON_SAVE, heavy PSRAM I/O during icon
    // upload causes DMA bus contention → cyan flashes on MIPI-DSI panels.
    // Blank the backlight for the entire save sequence and restore after.
    return {
        page: snapshot.page,
        cols: snapshot.cols,
        rows: snapshot.rows,
        buttons: snapshot.buttons,
        name: padName,
        body: JSON.stringify(payload),
        blankOnSave: Boolean(deviceInfoCache && deviceInfoCache.display_blank_on_save),
    };
}

async function padSetSaveBrightness(brightness) {
    const response = await fetch('/api/component/display/brightness', {
        method: 'PUT',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ brightness: brightness }),
    });
    if (!response.ok) throw new Error('Failed to set display brightness: HTTP ' + response.status);
}

async function padPersistPage(context) {
    let savedBrightness = 0;
    let brightnessBlanked = false;

    try {
        if (context.blankOnSave) {
            const cfgResp = await fetch('/api/config');
            if (!cfgResp.ok) throw new Error('Failed to read display brightness: HTTP ' + cfgResp.status);
            const cfg = await cfgResp.json();
            savedBrightness = cfg.backlight_brightness ?? 80;
            await padSetSaveBrightness(0);
            brightnessBlanked = true;
        }

        await padUploadPageIcons(context);

        const resp = await fetch('/api/pad?page=' + context.page, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: context.body,
        });
        if (!resp.ok) {
            const err = await resp.json().catch(() => ({}));
            throw new Error(err.error || 'HTTP ' + resp.status);
        }

        if (context.blankOnSave) {
            // Wait for LVGL to rebuild tiles and render into the framebuffer
            await new Promise(r => setTimeout(r, 500));
        }
    } finally {
        if (brightnessBlanked) {
            await padSetSaveBrightness(savedBrightness);
        }
    }
}

async function padSavePage(options) {
    const bulk = Boolean(options && options.bulk);
    try {
        const context = padBuildSaveContext();
        await padPersistPage(context);

        showMessage('Pad ' + (context.page + 1) + ' saved', 'success');
        padClearDirty();
        padUpdateDropdownLabel(context.page, context.name);

        if (!bulk) {
            // Refresh deviceInfoCache so target screen dropdowns pick up new pad names.
            await getDeviceInfo(true);
            // Reload to get canonical version from device.
            await padLoadPage(context.page);
        }
        return context;
    } catch (err) {
        console.error('padSavePage error:', err);
        if (bulk) throw err;
        showMessage('Save failed: ' + err.message, 'error');
        return null;
    }
}


function padUpdateDropdownLabel(page, name) {
    const sel = document.getElementById('pad-page-select');
    if (!sel) return;
    const opt = sel.options[page];
    if (opt) opt.textContent = name ? 'Pad ' + (page + 1) + ': ' + name : 'Pad ' + (page + 1);
}

// Populate pad-page-select labels from deviceInfoCache.available_screens
function padRefreshDropdownLabels() {
    if (!deviceInfoCache || !deviceInfoCache.available_screens) return;
    deviceInfoCache.available_screens.forEach(s => {
        const m = s.id.match(/^pad_(\d+)$/);
        if (m) {
            const idx = parseInt(m[1]);
            // Extract custom name portion after "Pad N: " if present
            const prefixRe = /^Pad \d+: (.+)$/;
            const match = s.name.match(prefixRe);
            padUpdateDropdownLabel(idx, match ? match[1] : '');
        }
    });
}

document.addEventListener('DOMContentLoaded', () => {
    // Shell-level initialization only.
    // Fragment-level init is handled by portal_nav.js + portal_fragment_init.js.

    // Load version info for shell header badges (also seeds deviceInfoCache
    // and sets portalMode from the ap_active flag).
    loadVersion();

    // Initialize health widget (badge in shell header)
    initHealthWidget();
});
