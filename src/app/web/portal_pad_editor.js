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

const MAX_ACTIONS = 3; // Must match MAX_BUTTON_ACTIONS in pad_config.h

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

function padInit() {
    const section = document.getElementById('pad-config-section');
    if (!section) return;

    // Generate action editor HTML from shared module — up to 3 sequential actions per gesture.
    // Slots 2 and 3 start hidden (progressive disclosure via "+ Add" link).
    const aeContainer = document.getElementById('pad-action-editors');
    if (aeContainer) {
        var tapHtml = '';
        var lpHtml = '';
        var aeOpts = { showBleHint: true, showKeyHelp: true };
        for (var ai = 0; ai < MAX_ACTIONS; ai++) {
            var tapPfx = 'pad-edit-action-' + ai;
            var lpPfx = 'pad-edit-lp-action-' + ai;
            var tapLabel = ai === 0 ? 'Tap Action' : 'Tap Action ' + (ai + 1);
            var lpLabel = ai === 0 ? 'Long-Press Action' : 'Long-Press Action ' + (ai + 1);
            var hidden = ai > 0 ? ' style="display:none"' : '';
            tapHtml += '<div id="' + tapPfx + '-wrap"' + hidden + '>';
            if (ai > 0) tapHtml += '<div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:4px;"><span></span><a class="action-remove-link" onclick="padRemoveAction(\'tap\',' + ai + ')">&times; Remove</a></div>';
            tapHtml += actionEditorHTML(tapPfx, tapLabel, aeOpts);
            tapHtml += '</div>';
            lpHtml += '<div id="' + lpPfx + '-wrap"' + hidden + '>';
            if (ai > 0) lpHtml += '<div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:4px;"><span></span><a class="action-remove-link" onclick="padRemoveAction(\'lp\',' + ai + ')">&times; Remove</a></div>';
            lpHtml += actionEditorHTML(lpPfx, lpLabel, aeOpts);
            lpHtml += '</div>';
        }
        tapHtml += '<a id="pad-add-tap-action" class="action-add-link" onclick="padAddAction(\'tap\')">+ Add tap action</a>';
        lpHtml += '<a id="pad-add-lp-action" class="action-add-link" onclick="padAddAction(\'lp\')">+ Add long-press action</a>';
        aeContainer.innerHTML = tapHtml +
            '<hr style="border:none; border-top:1px solid #e5e5ea; margin:12px 0;">' +
            lpHtml;
    }

    const padActionContainer = document.getElementById('pad-level-action-editors');
    if (padActionContainer) {
        var padActionHtml = '';
        for (var pai = 0; pai < MAX_ACTIONS; pai++) {
            var padActionPfx = 'pad-level-action-' + pai;
            var padActionLabel = pai === 0 ? 'Tap Action' : 'Tap Action ' + (pai + 1);
            var padActionHidden = pai > 0 ? ' style="display:none"' : '';
            padActionHtml += '<div id="' + padActionPfx + '-wrap"' + padActionHidden + '>';
            if (pai > 0) padActionHtml += '<div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:4px;"><span></span><a class="action-remove-link" onclick="padRemoveLevelAction(' + pai + ')">&times; Remove</a></div>';
            padActionHtml += actionEditorHTML(padActionPfx, padActionLabel, { showBleHint: true, showKeyHelp: true });
            padActionHtml += '</div>';
        }
        padActionHtml += '<a id="pad-add-level-action" class="action-add-link" onclick="padAddLevelAction()">+ Add tap action</a>';
        padActionContainer.innerHTML = padActionHtml;
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

    // Wait for deviceInfoCache to be ready, then show section + load
    const waitForInfo = () => {
        if (deviceInfoCache) {
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
        } else {
            setTimeout(waitForInfo, 200);
        }
    };
    waitForInfo();
}

function padUpdateLevelActionAddLink() {
    var visibleCount = 0;
    for (var i = 0; i < MAX_ACTIONS; i++) {
        var wrap = document.getElementById('pad-level-action-' + i + '-wrap');
        if (wrap && wrap.style.display !== 'none') visibleCount++;
    }
    var link = document.getElementById('pad-add-level-action');
    if (link) link.style.display = visibleCount >= MAX_ACTIONS ? 'none' : '';
}

function padAddLevelAction() {
    for (var i = 1; i < MAX_ACTIONS; i++) {
        var wrap = document.getElementById('pad-level-action-' + i + '-wrap');
        if (wrap && wrap.style.display === 'none') {
            wrap.style.display = '';
            padMarkDirty();
            break;
        }
    }
    padUpdateLevelActionAddLink();
}

function padRemoveLevelAction(index) {
    var wrap = document.getElementById('pad-level-action-' + index + '-wrap');
    if (wrap) {
        wrap.style.display = 'none';
        actionEditorLoad('pad-level-action-' + index, null);
    }
    padMarkDirty();
    padUpdateLevelActionAddLink();
}

function padLoadLevelActions(actions) {
    padState.padActions = (actions || []).filter(function(action) {
        return action && typeof action === 'object' && action.type && action.type !== 'none';
    }).slice(0, MAX_ACTIONS);
    for (var i = 0; i < MAX_ACTIONS; i++) {
        var wrap = document.getElementById('pad-level-action-' + i + '-wrap');
        if (wrap) wrap.style.display = i === 0 || i < padState.padActions.length ? '' : 'none';
        actionEditorLoad('pad-level-action-' + i, padState.padActions[i] || null);
    }
    padUpdateLevelActionAddLink();
}

function padBuildLevelActions() {
    var actions = [];
    for (var i = 0; i < MAX_ACTIONS; i++) {
        var wrap = document.getElementById('pad-level-action-' + i + '-wrap');
        if (!wrap || wrap.style.display === 'none') continue;
        var action = actionEditorBuild('pad-level-action-' + i);
        if (action && action.type && action.type !== 'none') actions.push(action);
    }
    padState.padActions = actions;
    return actions;
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
    var prefixes = [];
    for (var i = 0; i < MAX_ACTIONS; i++) {
        prefixes.push('pad-edit-action-' + i);
        prefixes.push('pad-edit-lp-action-' + i);
        prefixes.push('pad-level-action-' + i);
    }
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
    var prefixes = [];
    for (var i = 0; i < MAX_ACTIONS; i++) {
        prefixes.push('pad-edit-action-' + i);
        prefixes.push('pad-edit-lp-action-' + i);
        prefixes.push('pad-level-action-' + i);
    }
    prefixes.push('pad-edit-nr-adjust');
    prefixes.push('pad-edit-list-select');
    actionEditorPopulateSounds(prefixes, padSoundListCache);
}

// Show the next hidden action slot for tap or lp
function padAddAction(gesture) {
    var pfx = (gesture === 'lp') ? 'pad-edit-lp-action-' : 'pad-edit-action-';
    for (var i = 1; i < MAX_ACTIONS; i++) {
        var wrap = document.getElementById(pfx + i + '-wrap');
        if (wrap && wrap.style.display === 'none') {
            wrap.style.display = '';
            padMarkDirty();
            break;
        }
    }
    padUpdateAddLink(gesture);
}

// Remove (hide + clear) an action slot
function padRemoveAction(gesture, index) {
    var pfx = (gesture === 'lp') ? 'pad-edit-lp-action-' : 'pad-edit-action-';
    var wrap = document.getElementById(pfx + index + '-wrap');
    if (wrap) {
        wrap.style.display = 'none';
        actionEditorLoad(pfx + index, null);
    }
    padMarkDirty();
    padUpdateAddLink(gesture);
}

// Show/hide the "+ Add" link based on how many slots are visible
function padUpdateAddLink(gesture) {
    var pfx = (gesture === 'lp') ? 'pad-edit-lp-action-' : 'pad-edit-action-';
    var linkId = (gesture === 'lp') ? 'pad-add-lp-action' : 'pad-add-tap-action';
    var visibleCount = 0;
    for (var i = 0; i < MAX_ACTIONS; i++) {
        var wrap = document.getElementById(pfx + i + '-wrap');
        if (wrap && wrap.style.display !== 'none') visibleCount++;
    }
    var link = document.getElementById(linkId);
    if (link) link.style.display = (visibleCount >= 3) ? 'none' : '';
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

    // Rocker widget: relabel Tap/LP action groups contextually
    var isRocker = (wtype === 'rocker');
    var isNumericRocker = (wtype === 'numericrocker');
    var axis = 'vertical';
    if (isRocker) {
        var axSel = document.getElementById('pad-edit-rocker-axis');
        if (axSel) axis = axSel.value;
    }
    var isList = (wtype === 'list');
    for (var ai = 0; ai < MAX_ACTIONS; ai++) {
        var tapLbl = document.querySelector('label[for="pad-edit-action-' + ai + '-type"]');
        var lpLbl = document.querySelector('label[for="pad-edit-lp-action-' + ai + '-type"]');
        if (tapLbl) {
            if (isRocker) {
                var zoneA = (axis === 'horizontal') ? 'Left' : 'Up';
                tapLbl.textContent = ai === 0 ? zoneA + ' Action' : zoneA + ' Action ' + (ai + 1);
            } else if (isList) {
                tapLbl.textContent = ai === 0 ? 'Select Action' : 'Select Action ' + (ai + 1);
            } else {
                tapLbl.textContent = ai === 0 ? 'Tap Action' : 'Tap Action ' + (ai + 1);
            }
        }
        if (lpLbl) {
            if (isRocker) {
                var zoneB = (axis === 'horizontal') ? 'Right' : 'Down';
                lpLbl.textContent = ai === 0 ? zoneB + ' Action' : zoneB + ' Action ' + (ai + 1);
            } else {
                lpLbl.textContent = ai === 0 ? 'Long-Press Action' : 'Long-Press Action ' + (ai + 1);
            }
        }
        var lpWrap = document.getElementById('pad-edit-lp-action-' + ai + '-wrap');
        if (isNumericRocker) {
            // Numeric rocker: hide all LP action slots (uses adjustment action instead)
            if (lpWrap) lpWrap.style.display = 'none';
        } else {
            // Non-numericrocker: ensure slot 0 is visible (restore after numericrocker switch)
            // but don't force-show slots 1/2 — preserve dialog's progressive disclosure
            if (lpWrap && ai === 0) lpWrap.style.display = '';
        }
    }
    var lpAddLink = document.getElementById('pad-add-lp-action');
    if (isNumericRocker) {
        if (lpAddLink) lpAddLink.style.display = 'none';
    } else {
        padUpdateAddLink('lp');
    }
    // Ensure tap add-action link visibility is correct
    padUpdateAddLink('tap');
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

async function padSavePage() {
    // Merge-on-save: start with rawJson as base, overlay our changes
    const payload = padState.rawJson ? Object.assign({}, padState.rawJson) : {};
    payload.layout = payload.layout || 'grid';
    payload.cols = padState.cols;
    payload.rows = padState.rows;
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
    if (padState.bindings && padState.bindings.length > 0) {
        var badNames = padState.bindings.filter(function(b) { return b.name && !padIsValidBindingName(b.name); });
        if (badNames.length > 0) {
            alert('Invalid binding name(s): ' + badNames.map(function(b) { return '"' + b.name + '"'; }).join(', ') + '\nNames must start with a letter and contain only letters, digits, or underscores.');
            return;
        }
        var bd = padBindingsToDict(padState.bindings);
        if (bd) payload.bindings = bd;
        else delete payload.bindings;
    } else {
        delete payload.bindings;
    }

    var padActions = padBuildLevelActions();
    if (padActions.length > 0) payload.pad_actions = padActions;
    else delete payload.pad_actions;

    // Validate pad-level binding values before save
    if (typeof bindingValidatePadBindings === 'function') {
        var bvPad = bindingValidatePadBindings();
        if (!bvPad.valid) {
            showMessage(bvPad.count + ' pad binding error' + (bvPad.count > 1 ? 's' : '') + ' — check highlighted fields', 'error');
            return;
        }
    }

    // Validate button defaults binding fields
    if (typeof bindingValidateDefaults === 'function') {
        var bvDef = bindingValidateDefaults();
        if (!bvDef.valid) {
            showMessage(bvDef.count + ' button defaults error' + (bvDef.count > 1 ? 's' : '') + ' — check highlighted fields', 'error');
            return;
        }
    }

    // Template pad
    var tpl = parseInt(document.getElementById('pad-template-pad').value);
    if (tpl >= 0) {
        payload.template_pad = tpl;
    } else {
        delete payload.template_pad;
    }

    payload.buttons = padState.buttons.map(b => Object.assign({}, b));

    // Convert color ints to hex strings for JSON
    payload.buttons.forEach(b => padColorsToHex(b));

    // On boards with DISPLAY_BLANK_ON_SAVE, heavy PSRAM I/O during icon
    // upload causes DMA bus contention → cyan flashes on MIPI-DSI panels.
    // Blank the backlight for the entire save sequence and restore after.
    const blankOnSave = deviceInfoCache && deviceInfoCache.display_blank_on_save;
    let savedBrightness = 0;

    try {
        if (blankOnSave) {
            const cfgResp = await fetch('/api/config');
            if (cfgResp.ok) {
                const cfg = await cfgResp.json();
                savedBrightness = cfg.backlight_brightness ?? 80;
            }
            await fetch('/api/component/display/brightness', {
                method: 'PUT',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ brightness: 0 }),
            });
        }

        await padUploadPageIcons();

        const resp = await fetch('/api/pad?page=' + padState.page, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload),
        });
        if (!resp.ok) {
            const err = await resp.json().catch(() => ({}));
            throw new Error(err.error || 'HTTP ' + resp.status);
        }

        if (blankOnSave) {
            // Wait for LVGL to rebuild tiles and render into the framebuffer
            await new Promise(r => setTimeout(r, 500));
        }

        showMessage('Pad ' + (padState.page + 1) + ' saved', 'success');
        padClearDirty();
        padUpdateDropdownLabel(padState.page, document.getElementById('pad-name').value.trim());

        // Refresh deviceInfoCache so target screen dropdowns pick up new pad names
        await getDeviceInfo(true);

        // Reload to get canonical version from device
        padLoadPage(padState.page);
    } catch (err) {
        console.error('padSavePage error:', err);
        showMessage('Save failed: ' + err.message, 'error');
    } finally {
        if (blankOnSave && savedBrightness > 0) {
            fetch('/api/component/display/brightness', {
                method: 'PUT',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ brightness: savedBrightness }),
            }).catch(() => {});
        }
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
