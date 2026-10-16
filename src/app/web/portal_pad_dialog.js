// portal_pad_dialog.js - Button edit dialog (open, save, clear, clipboard)
// Part of the ESP32 Macropad configuration portal.
// Bundled into portal_pad_editor.js during minification.

var padImageLibraryFiles = [];
let padDialogSelectionGeneration = 0;

function padPopulateLocalImageOptions(selected) {
    var select = document.getElementById('pad-edit-bg-image-path');
    if (!select) return;
    select.replaceChildren();
    var empty = document.createElement('option');
    empty.value = ''; empty.textContent = '(none)'; select.appendChild(empty);
    padImageLibraryFiles.forEach(function (path) {
        var option = document.createElement('option');
        option.value = path; option.textContent = path; select.appendChild(option);
    });
    if (selected && !padImageLibraryFiles.includes(selected)) {
        const option = document.createElement('option');
        option.value = selected; option.textContent = selected; select.appendChild(option);
    }
    select.value = selected || '';
}

function padLoadLocalImageOptions(selected) {
    if (!deviceInfoCache || deviceInfoCache.has_image_library !== true) return Promise.resolve();
    const section = document.getElementById('pad-config-section');
    const page = padState.page;
    const col = padState.editCol;
    const row = padState.editRow;
    const generation = padDialogSelectionGeneration;
    const input = document.getElementById('pad-edit-bg-image-path');
    const before = input ? input.value : '';
    return fetch('/api/images?directory=' + encodeURIComponent('/images'))
        .then(function (response) {
            if (!response.ok) throw new Error('Image library unavailable');
            return response.json();
        })
        .then(function (catalog) {
            if (generation !== padDialogSelectionGeneration || document.getElementById('pad-config-section') !== section || padState.page !== page || padState.editCol !== col || padState.editRow !== row) return;
            padImageLibraryFiles = Array.isArray(catalog.files) ? catalog.files : [];
            padPopulateLocalImageOptions(input.value !== before ? input.value : selected);
        })
        .catch(function () {
            if (generation !== padDialogSelectionGeneration || document.getElementById('pad-config-section') !== section || padState.page !== page || padState.editCol !== col || padState.editRow !== row) return;
            var group = document.getElementById('pad-edit-local-image-group');
            if (group) group.style.display = 'none';
        });
}

function padDialogOpen(col, row) {
    padDialogSelectionGeneration++;
    padPvHide();
    if (typeof padWorkspaceBeforeSelect === 'function') padWorkspaceBeforeSelect(col, row);
    padState.editCol = col;
    padState.editRow = row;

    // Refresh target screen dropdowns so pad names are current
    padPopulateScreenDropdown();
    padPopulateSoundDropdown();

    // Sync device-level pad and button defaults from the DOM into padState so placeholders
    // are current. The Pad and Button Defaults form may live on a separate page; only
    // re-collect when it is actually mounted, otherwise keep the defaults already
    // loaded from the device API (padLoadButtonDefaultsFromDevice) so inherited
    // values/placeholders stay correct instead of falling back to firmware.
    if (document.getElementById('pad-def-border-width')) {
        padState.buttonDefaults = padCollectButtonDefaults();
    }
    if (!padState.buttonDefaults) padState.buttonDefaults = {};

    const inherited = typeof padFindTemplateButton === 'function' ? padFindTemplateButton(col, row) : null;
    const btn = padFindButton(col, row) || inherited || {};

    document.getElementById('pad-edit-label-top').value = padLabelToInput(btn.label_top);
    document.getElementById('pad-edit-label-center').value = padLabelToInput(btn.label_center);
    document.getElementById('pad-edit-label-bottom').value = padLabelToInput(btn.label_bottom);
    document.getElementById('pad-edit-label-top-style').value = btn.label_top_style || '';
    document.getElementById('pad-edit-label-center-style').value = btn.label_center_style || '';
    document.getElementById('pad-edit-label-bottom-style').value = btn.label_bottom_style || '';
    // Set label style placeholders from pad defaults
    document.getElementById('pad-edit-label-top-style').placeholder = padState.buttonDefaults.label_top_style || 'font_size:24;align:left;mode:dot';
    document.getElementById('pad-edit-label-center-style').placeholder = padState.buttonDefaults.label_center_style || 'font_size:24;align:left;mode:dot';
    document.getElementById('pad-edit-label-bottom-style').placeholder = padState.buttonDefaults.label_bottom_style || 'font_size:24;align:left;mode:dot';
    ['top', 'center', 'bottom'].forEach(syncLabelStyleVisibility);

    // Button state
    var btnStateEl = document.getElementById('pad-edit-btn-state');
    btnStateEl.value = btn.btn_state || '';

    // Initialize bindable color components and set values
    document.querySelectorAll('.pad-edit-modal .bindable-color').forEach(padInitBindableColor);

    // Use pad defaults as fallback when button has no explicit color
    var effBg = padGetEffectiveDefault('bg_color');
    var effFg = padGetEffectiveDefault('fg_color');
    var effBorder = padGetEffectiveDefault('border_color');
    padSetBindableColor('pad-edit-bg-color', btn.bg_color || effBg);
    padSetBindableColor('pad-edit-fg-color', btn.fg_color || effFg);
    padSetBindableColor('pad-edit-border-color', btn.border_color || effBorder);

    var effBw = padGetEffectiveDefault('border_width');
    var effCr = padGetEffectiveDefault('corner_radius');
    var effCp = padGetEffectiveDefault('content_pad');
    document.getElementById('pad-edit-border-width').value = (btn.border_width !== undefined) ? btn.border_width : effBw;
    document.getElementById('pad-edit-corner-radius').value = (btn.corner_radius !== undefined) ? btn.corner_radius : effCr;
    document.getElementById('pad-edit-content-pad').value = (btn.content_pad !== undefined) ? btn.content_pad : effCp;
    // Set placeholders to show what the pad default is
    document.getElementById('pad-edit-border-width').placeholder = effBw;
    document.getElementById('pad-edit-corner-radius').placeholder = effCr;
    document.getElementById('pad-edit-content-pad').placeholder = effCp;
    document.getElementById('pad-edit-ui-offset').value = btn.ui_offset || '';
    document.getElementById('pad-edit-button-shadow').value = btn.button_shadow || 'inherit';

    // Update reset-hint visibility for appearance fields
    padUpdateResetHints();

    // Populate col_span / row_span dropdowns based on available space
    const maxCs = padState.cols - col;
    const maxRs = padState.rows - row;
    const csSel = document.getElementById('pad-edit-col-span');
    const rsSel = document.getElementById('pad-edit-row-span');
    csSel.innerHTML = '';
    rsSel.innerHTML = '';
    for (let i = 1; i <= maxCs; i++) {
        const o = document.createElement('option');
        o.value = i; o.textContent = i;
        if (i === (btn.col_span || 1)) o.selected = true;
        csSel.appendChild(o);
    }
    for (let i = 1; i <= maxRs; i++) {
        const o = document.createElement('option');
        o.value = i; o.textContent = i;
        if (i === (btn.row_span || 1)) o.selected = true;
        rsSel.appendChild(o);
    }

    // Tap actions (array of up to 3)
    var tapActions = btn.actions || [];
    // Legacy single-action fallback
    if (!tapActions.length && btn.action && btn.action.type) tapActions = [btn.action];
    actionEditorListLoad(padActionPrefixes('tap'), tapActions);

    // Long-press actions (array of up to 3)
    var lpActions = btn.lp_actions || [];
    // Legacy single-action fallback
    if (!lpActions.length && btn.lp_action && btn.lp_action.type) lpActions = [btn.lp_action];
    actionEditorListLoad(padActionPrefixes('lp'), lpActions);

    // Image background
    var localImageGroup = document.getElementById('pad-edit-local-image-group');
    if (localImageGroup) localImageGroup.style.display = '';
    padLoadLocalImageOptions(btn.bg_image_path || '');
    document.getElementById('pad-edit-bg-image-binding').value =
        btn.bg_image_path && btn.bg_image_path.indexOf('[') !== -1 ? btn.bg_image_path : '';
    document.getElementById('pad-edit-bg-image-url').value = btn.bg_image_url || '';
    document.getElementById('pad-edit-bg-image-user').value = btn.bg_image_user || '';
    updateWriteOnlySecretField('pad-edit-bg-image-password',
        'pad-edit-bg-image-password-status', btn.bg_image_password_set === true,
        'Not configured.', 'password');
    document.getElementById('pad-edit-bg-image-password').value = btn.bg_image_password || '';
    document.getElementById('pad-edit-bg-image-interval').value = (btn.bg_image_interval_ms !== undefined) ? btn.bg_image_interval_ms : 0;
    document.getElementById('pad-edit-bg-image-scale').value = btn.bg_image_letterbox ? 'letterbox' : 'cover';
    document.getElementById('pad-edit-image-section').open = !!btn.bg_image_path;
    document.getElementById('pad-edit-camera-feed-section').open = !!btn.bg_image_url;
    document.getElementById('pad-edit-background-image-section').open =
        !!(btn.bg_image_path || btn.bg_image_url || btn.bg_image_letterbox);

    // Icon
    const iconParsed = padIconIdToType(btn.icon_id || '');
    document.getElementById('pad-edit-icon-type').value = iconParsed.type;
    document.getElementById('pad-edit-icon-emoji').value = (iconParsed.type === 'emoji') ? iconParsed.value : '';
    document.getElementById('pad-edit-icon-mi').value = (iconParsed.type === 'mi') ? iconParsed.value : '';
    document.getElementById('pad-edit-icon-section').open = !!btn.icon_id;
    document.getElementById('pad-edit-icon-scale').value = (btn.icon_scale_pct !== undefined) ? btn.icon_scale_pct : 0;
    document.getElementById('pad-edit-icon-position').value = btn.icon_position || 'above';
    padIconTypeChanged();

    // Widget type
    document.getElementById('pad-edit-widget-type').value = btn.widget_type || '';
    padFillLoad(btn);
    padWidgetTypeChanged();

    padWidgetEditorsLoad(btn);

    document.getElementById('pad-edit-disable-pad-swipes').checked = btn.widget_disable_pad_swipes === true;

    padRefreshWidgetEditor();
    document.getElementById('pad-edit-overlay').style.display = 'flex';
    if (typeof padWorkspaceAfterSelect === 'function') padWorkspaceAfterSelect();

    // Enable paste button if clipboard has content
    document.getElementById('pad-edit-paste').disabled = !padState.btnClipboard;

    // Refresh binding length warnings for the loaded values
    if (typeof padScanMaxlenHints === 'function') padScanMaxlenHints();

    // Mount + prefetch the in-context live-value preview affordances.
    if (typeof padPvOnOpen === 'function') padPvOnOpen();
}

function padDialogRefreshFieldErrors(error) {
    const root = document.getElementById('pad-editor-root');
    const overlay = root && root.querySelectorAll ? root : document.getElementById('pad-edit-overlay');
    if (!overlay || !overlay.querySelectorAll) return;
    const fieldIds = error && error.fieldIds || [];
    overlay.querySelectorAll('input, select, textarea').forEach(input => {
        let relevant = !input.disabled;
        for (let parent = input; parent && parent !== overlay; parent = parent.parentElement) {
            if (parent.style && parent.style.display === 'none' && !(parent.dataset && (parent.dataset.buttonTab || parent.dataset.padTab))) relevant = false;
        }
        const invalid = relevant && (fieldIds.includes(input.id) || input.validity && !input.validity.valid);
        input.classList.toggle('pad-field-error', Boolean(invalid));
        if (invalid || input.classList.contains('binding-error')) input.setAttribute('aria-invalid', 'true');
        else input.removeAttribute('aria-invalid');
    });
}

function padDialogShowValidationError(message) {
    const alert = document.getElementById('pad-edit-validation-error');
    if (!alert) return;
    padDialogRefreshFieldErrors(message);
    document.getElementById('pad-edit-validation-message').textContent = message.message || message;
    alert.style.display = '';
    ['pad-action-controls', 'pad-workspace-mobile-save', 'pad-workspace-back'].forEach(function(id) {
        const control = document.getElementById(id);
        if (control) control.inert = true;
    });
}

function padDialogFocusValidationError(error) {
    const input = document.getElementById((error.fieldIds || [])[0]);
    if (!input) return;
    for (let parent = input.parentElement; parent; parent = parent.parentElement) {
        if (parent.tagName === 'DETAILS') parent.open = true;
        if (parent.dataset && parent.dataset.buttonTab && typeof padWorkspace !== 'undefined' && padWorkspace) {
            padWorkspace.tab = parent.dataset.buttonTab;
            padWorkspaceRenderTabs();
        }
    }
    if (input.focus) input.focus();
}

function padDialogDismissValidationError() {
    const alert = document.getElementById('pad-edit-validation-error');
    if (alert) alert.style.display = 'none';
    ['pad-action-controls', 'pad-workspace-mobile-save', 'pad-workspace-back'].forEach(function(id) {
        const control = document.getElementById(id);
        if (control) control.inert = false;
    });
}

function padDialogClearValidationError() {
    const message = document.getElementById('pad-edit-validation-message');
    if (message) message.textContent = '';
    padDialogRefreshFieldErrors();
    padDialogDismissValidationError();
}

function padDialogBuildButton() {
    const col = padState.editCol;
    const row = padState.editRow;

    // Build new button object — only include fields with values
    const btn = { col: col, row: row };

    const cs = parseInt(document.getElementById('pad-edit-col-span').value);
    const rs = parseInt(document.getElementById('pad-edit-row-span').value);
    if (cs > 1) btn.col_span = cs;
    if (rs > 1) btn.row_span = rs;

    const lt = padLabelFromInput('pad-edit-label-top');
    const lc = padLabelFromInput('pad-edit-label-center');
    const lb = padLabelFromInput('pad-edit-label-bottom');
    if (lt) btn.label_top = lt;
    if (lc) btn.label_center = lc;
    if (lb) btn.label_bottom = lb;

    const lts = document.getElementById('pad-edit-label-top-style').value.trim();
    const lcs = document.getElementById('pad-edit-label-center-style').value.trim();
    const lbs = document.getElementById('pad-edit-label-bottom-style').value.trim();
    if (lts) btn.label_top_style = lts;
    if (lcs) btn.label_center_style = lcs;
    if (lbs) btn.label_bottom_style = lbs;

    // Only store appearance values that differ from the effective device default,
    // so that changing device-level pad and button defaults propagates to existing buttons.
    var _bg = padGetBindableColor('pad-edit-bg-color');
    if (_bg && _bg !== padGetEffectiveDefault('bg_color')) btn.bg_color = _bg;
    var _fg = padGetBindableColor('pad-edit-fg-color');
    if (_fg && _fg !== padGetEffectiveDefault('fg_color')) btn.fg_color = _fg;
    var _bc = padGetBindableColor('pad-edit-border-color');
    if (_bc && _bc !== padGetEffectiveDefault('border_color')) btn.border_color = _bc;

    const bw = document.getElementById('pad-edit-border-width').value.trim();
    var effBw = padGetEffectiveDefault('border_width');
    if (bw && bw !== effBw) btn.border_width = bw;
    const cr = document.getElementById('pad-edit-corner-radius').value.trim();
    var effCr = padGetEffectiveDefault('corner_radius');
    if (cr && cr !== effCr) btn.corner_radius = cr;
    const cp = document.getElementById('pad-edit-content-pad').value.trim();
    var effCp = padGetEffectiveDefault('content_pad');
    if (cp && cp !== effCp) btn.content_pad = cp; else delete btn.content_pad;
    const uiOffset = document.getElementById('pad-edit-ui-offset').value.trim();
    if (uiOffset) { btn.ui_offset = uiOffset; } else { delete btn.ui_offset; }
    const buttonShadow = document.getElementById('pad-edit-button-shadow').value || 'inherit';
    if (buttonShadow !== 'inherit') btn.button_shadow = buttonShadow;

    // Tap actions (array)
    const selectedType = document.getElementById('pad-edit-widget-type').value;
    const widgetEditor = padWidgetEditor(selectedType);
    const wtype = widgetEditor && widgetEditor.supported === false ? '' : selectedType;
    padFillBuild(btn, wtype);
    const actionPolicy = widgetEditor ? widgetEditor.actions : { tap: true, longPress: true };
    var tapArr = wtype && actionPolicy.buildTap ? actionPolicy.buildTap()
        : actionPolicy.tap ? actionEditorListBuild(padActionPrefixes('tap')) : [];
    if (tapArr.length) btn.actions = tapArr;

    // Long-press actions (array)
    var lpArr = actionPolicy.longPress ? actionEditorListBuild(padActionPrefixes('lp')) : [];
    if (lpArr.length) btn.lp_actions = lpArr;

    // Image background
    const imgUrl = document.getElementById('pad-edit-bg-image-url').value.trim();
    const localImageBinding = document.getElementById('pad-edit-bg-image-binding').value.trim();
    const localImagePath = document.getElementById('pad-edit-bg-image-path').value;
    if (localImageBinding) btn.bg_image_path = localImageBinding;
    else if (localImagePath) btn.bg_image_path = localImagePath;
    if (document.getElementById('pad-edit-bg-image-scale').value === 'letterbox') btn.bg_image_letterbox = true;
    if (imgUrl) {
        btn.bg_image_url = imgUrl;
        const imgUser = document.getElementById('pad-edit-bg-image-user').value.trim();
        const imgPass = document.getElementById('pad-edit-bg-image-password').value;
        if (imgUser) btn.bg_image_user = imgUser;
        if (imgPass) btn.bg_image_password = imgPass;
        else {
            const previous = padFindButton(col, row);
            if (previous && previous.bg_image_password_set) btn.bg_image_password_set = true;
        }
        const imgInterval = parseInt(document.getElementById('pad-edit-bg-image-interval').value);
        if (!isNaN(imgInterval) && imgInterval >= 0) btn.bg_image_interval_ms = imgInterval;
    }

    // Icon
    const iconId = padBuildIconId();
    if (iconId) btn.icon_id = iconId;

    // Icon scale
    const iconScale = parseInt(document.getElementById('pad-edit-icon-scale').value);
    if (!isNaN(iconScale) && iconScale > 0 && iconScale <= 250) btn.icon_scale_pct = iconScale;

    // Icon position (only relevant when icon is set)
    if (iconId) {
        const iconPos = document.getElementById('pad-edit-icon-position').value;
        if (iconPos && iconPos !== 'above') btn.icon_position = iconPos;
    }

    // Widget type
    if (wtype) {
        btn.widget_type = wtype;
        const editor = padWidgetEditor(wtype);
        if (editor) padWidgetEditorBuild(btn, editor);
        if (editor && editor.inputNavigation) {
            btn.widget_disable_pad_swipes = document.getElementById('pad-edit-disable-pad-swipes').checked;
        }
    }

    // Button state
    const btnState = document.getElementById('pad-edit-btn-state').value.trim();
    if (btnState) btn.btn_state = btnState;

    return btn;
}

function padDialogValidateButton(btn) {
    const overlay = document.getElementById('pad-edit-overlay');
    const activeType = document.getElementById('pad-edit-widget-type').value;
    const sectionOwners = new Map();
    padWidgetEditors.forEach(editor => editor.sections.forEach(section => { sectionOwners.set(section.id, editor); }));
    if (overlay && overlay.querySelectorAll) overlay.querySelectorAll('input[type="number"]').forEach(input => {
        let parent = input;
        let relevant = !input.disabled;
        while (parent && parent !== overlay) {
            const owner = sectionOwners.get(parent.id);
            if (owner && (owner.type !== activeType || owner.supported === false)) relevant = false;
            if (parent.style && parent.style.display === 'none' && !(parent.dataset && parent.dataset.buttonTab)) relevant = false;
            parent = parent.parentElement;
        }
        if (relevant && input.validity && !input.validity.valid) {
            const label = overlay.querySelector('label[for="' + input.id + '"]');
            throw Object.assign(new Error((label ? label.textContent.trim() : input.id) + ': ' + input.validationMessage), { fieldIds: [input.id] });
        }
    });
    const others = padState.buttons.filter(function(button) { return button.col !== btn.col || button.row !== btn.row; });
    if (typeof padCanSpanFit === 'function' && !padCanSpanFit(btn.col, btn.row, btn.col_span || 1, btn.row_span || 1, others)) {
        throw Object.assign(new Error('Button span exceeds the pad or overlaps another button'), { fieldIds: ['pad-edit-col-span', 'pad-edit-row-span'] });
    }

    // Validate all binding fields before accepting
    if (typeof bindingValidateDialog === 'function') {
        var bvResult = bindingValidateDialog();
        if (!bvResult.valid) {
            throw Object.assign(new Error(bvResult.count + ' binding error' +
                (bvResult.count > 1 ? 's' : '') + ' - check highlighted fields'), { fieldIds: bvResult.fieldIds || [] });
        }
    }
}

function padDialogClear() {
    const col = padState.editCol;
    const row = padState.editRow;
    padState.buttons = padState.buttons.filter(b => !(b.col === col && b.row === row));
    if (typeof padWorkspaceClearButton === 'function') padWorkspaceClearButton(col, row);
    padMarkDirty();
    padWorkspaceReturn();
    padRenderGrid();
}

// --- Live binding preview (in-context) -------------------------------------
// Each bindable field (labels, colors, state, primary data binding) that holds
// a binding shows its live resolved value INLINE, right after the field's "fx"
// hint — no popover, so nothing can be hidden behind the modal. Values resolve
// via POST /api/pad/resolve on dialog open and refresh (debounced) as bindings
// are edited; click a value to force a fresh reading. Color values also show a
// swatch. Resolves VALUES only (not a pixel render); reads fields from the form.

var PAD_PV_FIELDS = [
    { id: 'pad-edit-label-top',           kind: 'text',  field: 'label_top' },
    { id: 'pad-edit-label-center',        kind: 'text',  field: 'label_center' },
    { id: 'pad-edit-label-bottom',        kind: 'text',  field: 'label_bottom' },
    { id: 'pad-edit-bg-color',            kind: 'color', field: 'bg_color' },
    { id: 'pad-edit-fg-color',            kind: 'color', field: 'fg_color' },
    { id: 'pad-edit-border-color',        kind: 'color', field: 'border_color' },
    { id: 'pad-edit-border-width',        kind: 'text',  field: 'border_width' },
    { id: 'pad-edit-corner-radius',       kind: 'text',  field: 'corner_radius' },
    { id: 'pad-edit-btn-state',           kind: 'text',  field: 'btn_state' }
];
var padPvTimer = null;  // debounce for edit-triggered refresh
let padPvGeneration = 0;

function padPvRequestGuard() {
    const generation = padPvGeneration;
    const overlay = document.getElementById('pad-edit-overlay');
    const page = padState.page;
    const col = padState.editCol;
    const row = padState.editRow;
    return () => generation === padPvGeneration && document.getElementById('pad-edit-overlay') === overlay &&
        page === padState.page && col === padState.editCol && row === padState.editRow;
}

function padPreviewEsc(s) {
    return String(s == null ? '' : s)
        .replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

function padPvFieldDef(id) {
    return padPvFields().find(field => field.id === id) || null;
}

function padPvFields() {
    const editor = padWidgetEditor(document.getElementById('pad-edit-widget-type').value);
    return PAD_PV_FIELDS.concat(editor ? editor.fields.filter(field => field.binding).map(field => ({
        id: field.id, field: field.key, kind: field.kind === 'color' ? 'color' : 'text', preview: field.preview
    })).concat(editor.previewFields || [], editor.track && editor.track.outline ? [{
        id: 'pad-edit-track-outline-color', field: 'widget_track_outline_color', kind: 'color'
    }] : []) : []);
}

function padPvFieldValue(id) {
    var f = padPvFieldDef(id);
    if (f && f.kind === 'color') return padGetBindableColor(id);
    var el = document.getElementById(id);
    return el ? el.value.trim() : '';
}

function padPvIsBinding(v) { return !!v && v.indexOf('[') !== -1; }

function padPvHex(v) {
    var s = String(v == null ? '' : v).trim();
    return /^#[0-9a-fA-F]{6}$/.test(s) ? s : null;
}

// A value counts as "not resolved yet" when it is empty or a placeholder. A
// just-subscribed MQTT topic resolves to EMPTY (not '---') until its (retained)
// message arrives, so both must trigger the delayed retry.
function padPvUnresolved(v) {
    var s = String(v == null ? '' : v).trim();
    return s === '' || s.indexOf('---') !== -1;
}

// Anchor the inline value chip next to each field's visible "fx" hint (uniform
// across text inputs and the display:none color inputs).
function padPvAnchor(id) {
    var el = document.getElementById(id);
    if (!el) return null;
    var node = el;
    while (node && node !== document.body) {
        var fx = node.querySelector ? node.querySelector('.fx-hint') : null;
        if (fx) return fx;
        node = node.parentElement;
    }
    return null;
}

// Create the inline value chip for each field once (idempotent), and hook a
// debounced refresh so values track edits.
function padPvMount() {
    padPvFields().forEach(function (f) {
        var anchor = padPvAnchor(f.id);
        if (!anchor || anchor.parentNode.querySelector('.pad-pv-val[data-pv="' + f.id + '"]')) return;
        var chip = document.createElement('span');
        chip.className = 'pad-pv-val';
        chip.setAttribute('data-pv', f.id);
        chip.style.display = 'none';
        chip.addEventListener('click', function (ev) { ev.preventDefault(); ev.stopPropagation(); padPvRefreshField(f.id); });
        anchor.insertAdjacentElement('afterend', chip);
    });
    var overlay = document.getElementById('pad-edit-overlay');
    if (overlay && !overlay._pvHooked) {
        overlay._pvHooked = true;
        var refresh = function (event) {
            const field = padPvFieldDef(event.target.id);
            if (field && field.preview) field.preview();
            if (padPvTimer) clearTimeout(padPvTimer);
            padPvTimer = setTimeout(padPvRefreshAll, 350);
        };
        overlay.addEventListener('input', refresh);
        overlay.addEventListener('change', refresh);
    }
}

// Render one chip. state 'loading' shows a spinner-ish ellipsis; hidden when the
// field holds no binding.
function padPvSetChip(id, value, state) {
    const field = padPvFieldDef(id);
    if (field && field.preview) field.preview(state === 'loading' ? undefined : value);
    var chip = document.querySelector('.pad-pv-val[data-pv="' + id + '"]');
    if (!chip) return;
    if (!padPvIsBinding(padPvFieldValue(id))) { chip.style.display = 'none'; chip.title = ''; return; }
    chip.style.display = 'inline-flex';
    if (state === 'loading') {
        chip.innerHTML = '<span class="pad-pv-arrow">\u2192</span><span class="pad-pv-text pad-pv-muted">\u2026</span>';
        chip.title = 'Resolving\u2026';
        return;
    }
    var shown = (value === '' || value == null) ? '(empty)' : String(value);
    var muted = shown.indexOf('---') !== -1 ? ' pad-pv-muted' : '';
    var hex = padPvHex(value);
    var inner = '<span class="pad-pv-arrow">\u2192</span>';
    if (hex) inner += '<span class="pad-pv-swatch" style="background:' + padPreviewEsc(hex) + ';"></span>';
    inner += '<span class="pad-pv-text' + muted + '">' + padPreviewEsc(shown) + '</span>';
    chip.innerHTML = inner;
    chip.title = shown + ' \u2014 click to refresh';
}

// Resolve one field (used on click for a fresh reading). A single delayed retry
// covers a just-subscribed MQTT topic whose (retained) message arrives shortly
// after the first resolve.
function padPvRefreshField(id, retriesLeft) {
    const current = padPvRequestGuard();
    var val = padPvFieldValue(id);
    if (!padPvIsBinding(val)) { padPvSetChip(id); return; }
    var rl = (retriesLeft === undefined) ? 2 : retriesLeft;
    padPvSetChip(id, null, 'loading');
    fetch('/api/pad/resolve', {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ screen: 'pad_' + padState.page, bindings: [val] })
    }).then(function (r) { return r.ok ? r.json() : null; })
      .then(function (j) {
          if (!current() || padPvFieldValue(id) !== val) return;
          var v = (j && j.resolved && j.resolved[0]) ? j.resolved[0].value : '---';
          padPvSetChip(id, v);
          if (padPvUnresolved(v) && rl > 0)
              setTimeout(function () { if (current() && padPvFieldValue(id) === val) padPvRefreshField(id, rl - 1); }, 1000);
      })
    .catch(function () { if (current() && padPvFieldValue(id) === val) padPvSetChip(id, '---'); });
}

// Batch-resolve all bound fields in one request (dialog open + debounced edits).
function padPvRefreshAll(retriesLeft) {
    const current = padPvRequestGuard();
    padPvMount();
    var rl = (retriesLeft === undefined) ? 2 : retriesLeft;
    var btn = {}, any = false;
    padPvFields().forEach(function (f) {
        var v = padPvFieldValue(f.id);
        if (padPvIsBinding(v)) { btn[f.field] = v; any = true; padPvSetChip(f.id, null, 'loading'); }
        else padPvSetChip(f.id);
    });
    if (!any) return;
    fetch('/api/pad/resolve', {
        method: 'POST', headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ screen: 'pad_' + padState.page, button: btn })
    }).then(function (r) { return r.ok ? r.json() : null; })
      .then(function (j) {
          if (!current()) return;
          var fields = (j && j.button) || {};
          var anyUnresolved = false;
          padPvFields().forEach(function (f) {
              if (padPvIsBinding(padPvFieldValue(f.id)) && padPvFieldValue(f.id) === btn[f.field]) {
                  var v = fields[f.field] !== undefined ? fields[f.field] : '---';
                  padPvSetChip(f.id, v);
                  if (padPvUnresolved(v)) anyUnresolved = true;
              }
          });
          if (anyUnresolved && rl > 0) setTimeout(function () { if (current()) padPvRefreshAll(rl - 1); }, 1000);
      })
      .catch(function () {
          if (!current()) return;
          padPvFields().forEach(function (f) {
              if (padPvIsBinding(padPvFieldValue(f.id)) && padPvFieldValue(f.id) === btn[f.field]) padPvSetChip(f.id, '---');
          });
      });
}

function padPvOnOpen() { padPvMount(); padPvRefreshAll(); }
function padPvHide() { padPvGeneration++; clearTimeout(padPvTimer); }
