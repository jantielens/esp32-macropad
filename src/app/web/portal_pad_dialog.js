// portal_pad_dialog.js - Button edit dialog (open, save, clear, clipboard)
// Part of the ESP32 Macropad configuration portal.
// Bundled into portal_pad_editor.js during minification.

function padDialogOpen(col, row) {
    padState.editCol = col;
    padState.editRow = row;

    // Refresh target screen dropdowns so pad names are current
    padPopulateScreenDropdown();
    padPopulateSoundDropdown();

    // Sync device-level button defaults from the DOM into padState so placeholders are current
    padState.buttonDefaults = padCollectButtonDefaults();

    const btn = padFindButton(col, row) || {};

    document.getElementById('pad-edit-title').textContent =
        'Button [' + col + ', ' + row + ']';

    document.getElementById('pad-edit-label-top').value = padLabelToInput(btn.label_top);
    document.getElementById('pad-edit-label-center').value = padLabelToInput(btn.label_center);
    document.getElementById('pad-edit-label-bottom').value = padLabelToInput(btn.label_bottom);
    document.getElementById('pad-edit-label-top-style').value = btn.label_top_style || '';
    document.getElementById('pad-edit-label-center-style').value = btn.label_center_style || '';
    document.getElementById('pad-edit-label-bottom-style').value = btn.label_bottom_style || '';
    // Set label style placeholders from pad defaults
    document.getElementById('pad-edit-label-top-style').placeholder = padState.buttonDefaults.label_top_style || 'font:24;align:left;mode:dot';
    document.getElementById('pad-edit-label-center-style').placeholder = padState.buttonDefaults.label_center_style || 'font:24;align:left;mode:dot';
    document.getElementById('pad-edit-label-bottom-style').placeholder = padState.buttonDefaults.label_bottom_style || 'font:24;align:left;mode:dot';
    ['top', 'center', 'bottom'].forEach(syncLabelStyleVisibility);

    // Wire and init monospace toggle for mixed-binding label inputs
    ['pad-edit-label-top', 'pad-edit-label-center', 'pad-edit-label-bottom'].forEach(function(id) {
        var el = document.getElementById(id);
        el.oninput = function() { padUpdateMixedBindingFont(el); };
        padUpdateMixedBindingFont(el);
    });

    // Button state
    var btnStateEl = document.getElementById('pad-edit-btn-state');
    btnStateEl.value = btn.btn_state || '';
    document.getElementById('pad-edit-btn-state-section').open = !!btn.btn_state;

    // Initialize bindable color components and set values
    document.querySelectorAll('.pad-edit-modal .bindable-color').forEach(padInitBindableColor);

    // Use pad defaults as fallback when button has no explicit color
    var effBg = padGetEffectiveDefault('bg_color');
    var effFg = padGetEffectiveDefault('fg_color');
    var effBorder = padGetEffectiveDefault('border_color');
    padSetBindableColor('pad-edit-bg-color', btn.bg_color || effBg);
    padSetBindableColor('pad-edit-fg-color', btn.fg_color || effFg);
    padSetBindableColor('pad-edit-border-color', btn.border_color || effBorder);

    // Auto-open colors section if any color has a binding or custom override
    var hasColorOverride = btn.bg_color || btn.fg_color || btn.border_color ||
        (btn.border_width !== undefined) || (btn.corner_radius !== undefined);
    document.getElementById('pad-edit-colors-section').open = !!hasColorOverride;

    var effBw = padGetEffectiveDefault('border_width');
    var effCr = padGetEffectiveDefault('corner_radius');
    document.getElementById('pad-edit-border-width').value = (btn.border_width !== undefined) ? btn.border_width : effBw;
    document.getElementById('pad-edit-corner-radius').value = (btn.corner_radius !== undefined) ? btn.corner_radius : effCr;
    // Set placeholders to show what the pad default is
    document.getElementById('pad-edit-border-width').placeholder = effBw;
    document.getElementById('pad-edit-corner-radius').placeholder = effCr;
    document.getElementById('pad-edit-ui-offset').value = btn.ui_offset || '';

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
    for (var ai = 0; ai < MAX_ACTIONS; ai++) {
        actionEditorLoad('pad-edit-action-' + ai, tapActions[ai] || null);
        var wrap = document.getElementById('pad-edit-action-' + ai + '-wrap');
        if (wrap) wrap.style.display = (ai === 0 || (tapActions[ai] && tapActions[ai].type)) ? '' : 'none';
    }
    padUpdateAddLink('tap');

    // Long-press actions (array of up to 3)
    var lpActions = btn.lp_actions || [];
    // Legacy single-action fallback
    if (!lpActions.length && btn.lp_action && btn.lp_action.type) lpActions = [btn.lp_action];
    for (var ai = 0; ai < MAX_ACTIONS; ai++) {
        actionEditorLoad('pad-edit-lp-action-' + ai, lpActions[ai] || null);
        var wrap = document.getElementById('pad-edit-lp-action-' + ai + '-wrap');
        if (wrap) wrap.style.display = (ai === 0 || (lpActions[ai] && lpActions[ai].type)) ? '' : 'none';
    }
    padUpdateAddLink('lp');

    // Image background
    document.getElementById('pad-edit-bg-image-url').value = btn.bg_image_url || '';
    document.getElementById('pad-edit-bg-image-user').value = btn.bg_image_user || '';
    document.getElementById('pad-edit-bg-image-password').value = '';
    document.getElementById('pad-edit-bg-image-interval').value = (btn.bg_image_interval_ms !== undefined) ? btn.bg_image_interval_ms : 0;
    document.getElementById('pad-edit-bg-image-letterbox').checked = !!btn.bg_image_letterbox;
    document.getElementById('pad-edit-image-section').open = !!btn.bg_image_url;

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
    padWidgetTypeChanged();

    // Bar chart widget fields
    document.getElementById('pad-edit-widget-bar-min').value = (btn.widget_bar_min !== undefined) ? btn.widget_bar_min : '0';
    document.getElementById('pad-edit-widget-bar-max').value = (btn.widget_bar_max !== undefined) ? btn.widget_bar_max : '3';
    document.getElementById('pad-edit-widget-data-binding').value = btn.widget_data_binding || '';
    padSetBindableColor('pad-edit-widget-bar-color', btn.widget_bar_color, '#4CAF50');
    padSetBindableColor('pad-edit-widget-bar-bg-color', btn.widget_bar_bg_color, '#1A1A1A');
    document.getElementById('pad-edit-widget-bar-width-pct').value = (btn.widget_bar_width_pct !== undefined) ? btn.widget_bar_width_pct : 100;
    document.getElementById('pad-edit-widget-orientation').value = btn.widget_orientation || 'vertical';
    document.getElementById('pad-edit-widget-bar-anim-ms').value = (btn.widget_anim_ms !== undefined) ? btn.widget_anim_ms : 300;

    // Gauge widget fields
    document.getElementById('pad-edit-gauge-data-binding').value = btn.widget_data_binding || '';
    document.getElementById('pad-edit-gauge-data-binding-2').value = btn.widget_data_binding_2 || '';
    document.getElementById('pad-edit-gauge-data-binding-3').value = btn.widget_data_binding_3 || '';
    document.getElementById('pad-edit-gauge-data-binding-4').value = btn.widget_data_binding_4 || '';
    document.getElementById('pad-edit-gauge-start-label').value = padLabelToInput(btn.widget_gauge_start_label);
    document.getElementById('pad-edit-gauge-start-label-2').value = padLabelToInput(btn.widget_gauge_start_label_2);
    document.getElementById('pad-edit-gauge-start-label-3').value = padLabelToInput(btn.widget_gauge_start_label_3);
    document.getElementById('pad-edit-gauge-start-label-4').value = padLabelToInput(btn.widget_gauge_start_label_4);
    document.getElementById('pad-edit-gauge-min').value = (btn.widget_gauge_min !== undefined) ? btn.widget_gauge_min : '0';
    document.getElementById('pad-edit-gauge-max').value = (btn.widget_gauge_max !== undefined) ? btn.widget_gauge_max : '100';
    document.getElementById('pad-edit-gauge-degrees').value = (btn.widget_gauge_degrees !== undefined) ? btn.widget_gauge_degrees : 180;
    document.getElementById('pad-edit-gauge-start-angle').value = (btn.widget_gauge_start_angle !== undefined) ? btn.widget_gauge_start_angle : 180;
    document.getElementById('pad-edit-gauge-zero-centered').checked = (btn.widget_gauge_zero_centered !== undefined) ? btn.widget_gauge_zero_centered : false;
    document.getElementById('pad-edit-gauge-dual-binding-pair-1').checked = (btn.widget_gauge_dual_binding_pair_1 !== undefined) ? btn.widget_gauge_dual_binding_pair_1 : false;
    document.getElementById('pad-edit-gauge-dual-binding-pair-2').checked = (btn.widget_gauge_dual_binding_pair_2 !== undefined) ? btn.widget_gauge_dual_binding_pair_2 : false;
    document.getElementById('pad-edit-gauge-show-needle').checked = (btn.widget_gauge_show_needle !== undefined) ? btn.widget_gauge_show_needle : true;
    document.getElementById('pad-edit-gauge-anim-ms').value = (btn.widget_anim_ms !== undefined) ? btn.widget_anim_ms : 300;
    padSetBindableColor('pad-edit-gauge-arc-color', btn.widget_arc_color, '#4CAF50');
    padSetBindableColor('pad-edit-gauge-arc-color-2', btn.widget_arc_color_2, '#2196F3');
    padSetBindableColor('pad-edit-gauge-arc-color-3', btn.widget_arc_color_3, '#9C27B0');
    padSetBindableColor('pad-edit-gauge-arc-color-4', btn.widget_arc_color_4, '#FF9800');
    padSetBindableColor('pad-edit-gauge-track-color', btn.widget_gauge_track_color, '#1A1A1A');
    padSetBindableColor('pad-edit-gauge-needle-color', btn.widget_gauge_needle_color, '#FFFFFF');
    padSetBindableColor('pad-edit-gauge-tick-color', btn.widget_gauge_tick_color, '#808080');
    document.getElementById('pad-edit-gauge-arc-width-pct').value = (btn.widget_gauge_arc_width_pct !== undefined) ? btn.widget_gauge_arc_width_pct : 15;
    document.getElementById('pad-edit-gauge-ticks').value = (btn.widget_gauge_ticks !== undefined) ? btn.widget_gauge_ticks : 5;
    document.getElementById('pad-edit-gauge-needle-width').value = (btn.widget_gauge_needle_width !== undefined) ? btn.widget_gauge_needle_width : 2;
    document.getElementById('pad-edit-gauge-needle-cutoff').value = (btn.widget_gauge_needle_cutoff_pct !== undefined) ? btn.widget_gauge_needle_cutoff_pct : 0;
    document.getElementById('pad-edit-gauge-tick-width').value = (btn.widget_gauge_tick_width !== undefined) ? btn.widget_gauge_tick_width : 1;
    document.getElementById('pad-edit-gauge-marker-value').value = btn.widget_gauge_marker_value || '';
    document.getElementById('pad-edit-gauge-marker-zone-deg').value = (btn.widget_gauge_marker_zone_deg !== undefined) ? btn.widget_gauge_marker_zone_deg : 0;
    document.getElementById('pad-edit-gauge-marker-tick-width').value = (btn.widget_gauge_marker_tick_width !== undefined) ? btn.widget_gauge_marker_tick_width : 2;
    padSetBindableColor('pad-edit-gauge-marker-tick-color', btn.widget_gauge_marker_tick_color, '#FFFFFF');
    padSetBindableColor('pad-edit-gauge-marker-zone-color', btn.widget_gauge_marker_zone_color, '#FF5722');

    // Sparkline widget fields
    document.getElementById('pad-edit-sparkline-data-binding').value = btn.widget_data_binding || '';
    document.getElementById('pad-edit-sparkline-data-binding-2').value = btn.widget_data_binding_2 || '';
    document.getElementById('pad-edit-sparkline-data-binding-3').value = btn.widget_data_binding_3 || '';
    document.getElementById('pad-edit-sparkline-min').value = (btn.widget_sparkline_min !== undefined && btn.widget_sparkline_min !== null) ? btn.widget_sparkline_min : '';
    document.getElementById('pad-edit-sparkline-max').value = (btn.widget_sparkline_max !== undefined && btn.widget_sparkline_max !== null) ? btn.widget_sparkline_max : '';
    document.getElementById('pad-edit-sparkline-window').value = (btn.widget_sparkline_window !== undefined) ? btn.widget_sparkline_window : 300;
    document.getElementById('pad-edit-sparkline-slots').value = (btn.widget_sparkline_slots !== undefined) ? btn.widget_sparkline_slots : 60;
    padSetBindableColor('pad-edit-sparkline-line-color', btn.widget_sparkline_line_color, '#4CAF50');
    padSetBindableColor('pad-edit-sparkline-line-color-2', btn.widget_sparkline_line_color_2, '#2196F3');
    padSetBindableColor('pad-edit-sparkline-line-color-3', btn.widget_sparkline_line_color_3, '#9C27B0');
    document.getElementById('pad-edit-sparkline-line-width').value = (btn.widget_sparkline_line_width !== undefined) ? btn.widget_sparkline_line_width : 2;
    document.getElementById('pad-edit-sparkline-smooth').value = (btn.widget_sparkline_smooth !== undefined) ? btn.widget_sparkline_smooth : 0;
    document.getElementById('pad-edit-sparkline-unified-scale').checked = (btn.widget_sparkline_unified_scale !== undefined) ? btn.widget_sparkline_unified_scale : true;

    // Min/max markers
    document.getElementById('pad-edit-sparkline-marker-size-max').value = (btn.widget_sparkline_marker_size_max !== undefined) ? btn.widget_sparkline_marker_size_max : 0;
    document.getElementById('pad-edit-sparkline-max-fmt').value = btn.widget_sparkline_max_fmt || '';
    padSetBindableColor('pad-edit-sparkline-max-label-color', btn.widget_sparkline_max_label_color, '#FFFFFF');
    document.getElementById('pad-edit-sparkline-marker-size-min').value = (btn.widget_sparkline_marker_size_min !== undefined) ? btn.widget_sparkline_marker_size_min : 0;
    document.getElementById('pad-edit-sparkline-min-fmt').value = btn.widget_sparkline_min_fmt || '';
    padSetBindableColor('pad-edit-sparkline-min-label-color', btn.widget_sparkline_min_label_color, '#FFFFFF');

    // Current value dot
    document.getElementById('pad-edit-sparkline-current-dot').value = (btn.widget_sparkline_current_dot !== undefined) ? btn.widget_sparkline_current_dot : 0;

    // Current value labels
    document.getElementById('pad-edit-sparkline-label-width').value = (btn.widget_sparkline_label_width !== undefined) ? btn.widget_sparkline_label_width : 0;
    document.getElementById('pad-edit-sparkline-current-label').value = btn.widget_sparkline_current_label || '';
    document.getElementById('pad-edit-sparkline-current-label-2').value = btn.widget_sparkline_current_label_2 || '';
    document.getElementById('pad-edit-sparkline-current-label-3').value = btn.widget_sparkline_current_label_3 || '';

    // Reference lines
    for (let r = 1; r <= 3; r++) {
        document.getElementById('pad-edit-sparkline-ref-' + r + '-y').value = (btn['widget_sparkline_ref_' + r + '_y'] !== undefined) ? btn['widget_sparkline_ref_' + r + '_y'] : '';
        padSetBindableColor('pad-edit-sparkline-ref-' + r + '-color', btn['widget_sparkline_ref_' + r + '_color'], '#888888');
        document.getElementById('pad-edit-sparkline-ref-' + r + '-pattern').value = (btn['widget_sparkline_ref_' + r + '_pattern'] !== undefined) ? btn['widget_sparkline_ref_' + r + '_pattern'] : 0;
    }
    document.getElementById('pad-edit-sparkline-ref-in-view').checked = btn.widget_sparkline_ref_in_view || false;

    // Table widget fields
    document.getElementById('pad-edit-table-data-binding').value = btn.widget_data_binding || '';
    document.getElementById('pad-edit-table-style').value = btn.widget_table_style || '';
    document.getElementById('pad-edit-table-scrollable').value = (btn.widget_table_scrollable === false) ? 'false' : 'true';

    // Rocker widget fields
    document.getElementById('pad-edit-rocker-axis').value = btn.widget_rocker_axis || 'vertical';
    document.getElementById('pad-edit-rocker-color').value = btn.widget_rocker_color || '#FFFFFF';
    document.getElementById('pad-edit-rocker-opacity').value = (btn.widget_rocker_opacity !== undefined) ? btn.widget_rocker_opacity : 80;

    // Numeric Rocker widget fields
    document.getElementById('pad-edit-numericrocker-axis').value = btn.widget_numericrocker_axis || 'horizontal';
    document.getElementById('pad-edit-numericrocker-small-step').value = (btn.widget_numericrocker_small_step !== undefined) ? btn.widget_numericrocker_small_step : 1;
    document.getElementById('pad-edit-numericrocker-large-step').value = (btn.widget_numericrocker_large_step !== undefined) ? btn.widget_numericrocker_large_step : 10;
    document.getElementById('pad-edit-numericrocker-color').value = btn.widget_numericrocker_color || '#FFFFFF';
    document.getElementById('pad-edit-numericrocker-opacity').value = (btn.widget_numericrocker_opacity !== undefined) ? btn.widget_numericrocker_opacity : 80;

    // Numeric Rocker adjustment action
    actionEditorLoad('pad-edit-nr-adjust', btn.widget_numericrocker_action || null);

    document.getElementById('pad-edit-overlay').style.display = 'flex';
    document.body.style.overflow = 'hidden';
    document.documentElement.style.overflow = 'hidden';

    // Wire and init monospace toggle for bindable min/max inputs
    ['pad-edit-widget-bar-min', 'pad-edit-widget-bar-max',
     'pad-edit-gauge-min', 'pad-edit-gauge-max',
     'pad-edit-sparkline-min', 'pad-edit-sparkline-max'].forEach(function(id) {
        var el = document.getElementById(id);
        el.oninput = function() { padUpdateMixedBindingFont(el); };
        padUpdateMixedBindingFont(el);
    });

    // Enable paste button if clipboard has content
    document.getElementById('pad-edit-paste').disabled = !padState.btnClipboard;

    // Scroll dialog body to top
    const body = document.querySelector('.pad-edit-modal .pad-edit-body');
    if (body) body.scrollTop = 0;
}

function padDialogClose() {
    document.getElementById('pad-edit-overlay').style.display = 'none';
    document.body.style.overflow = '';
    document.documentElement.style.overflow = '';
}

function padDialogOk(keepOpen) {
    const col = padState.editCol;
    const row = padState.editRow;

    // Remove existing button at this position
    padState.buttons = padState.buttons.filter(b => !(b.col === col && b.row === row));

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
    // so that changing device-level button defaults propagates to existing buttons.
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
    const uiOffset = document.getElementById('pad-edit-ui-offset').value.trim();
    if (uiOffset) { btn.ui_offset = uiOffset; } else { delete btn.ui_offset; }

    // Tap actions (array)
    var tapArr = [];
    for (var ai = 0; ai < MAX_ACTIONS; ai++) {
        var a = actionEditorBuild('pad-edit-action-' + ai);
        if (a.type) tapArr.push(a);
    }
    if (tapArr.length) btn.actions = tapArr;

    // Long-press actions (array)
    var lpArr = [];
    for (var ai = 0; ai < MAX_ACTIONS; ai++) {
        var a = actionEditorBuild('pad-edit-lp-action-' + ai);
        if (a.type) lpArr.push(a);
    }
    if (lpArr.length) btn.lp_actions = lpArr;

    // Image background
    const imgUrl = document.getElementById('pad-edit-bg-image-url').value.trim();
    if (imgUrl) {
        btn.bg_image_url = imgUrl;
        const imgUser = document.getElementById('pad-edit-bg-image-user').value.trim();
        const imgPass = document.getElementById('pad-edit-bg-image-password').value;
        if (imgUser) btn.bg_image_user = imgUser;
        if (imgPass) btn.bg_image_password = imgPass;
        const imgInterval = parseInt(document.getElementById('pad-edit-bg-image-interval').value);
        if (!isNaN(imgInterval) && imgInterval >= 0) btn.bg_image_interval_ms = imgInterval;
        if (document.getElementById('pad-edit-bg-image-letterbox').checked) btn.bg_image_letterbox = true;
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
    const wtype = document.getElementById('pad-edit-widget-type').value;
    if (wtype) {
        btn.widget_type = wtype;
        if (wtype === 'bar_chart') {
            const wDataBinding = document.getElementById('pad-edit-widget-data-binding').value.trim();
            if (wDataBinding) btn.widget_data_binding = wDataBinding;
            btn.widget_bar_min = padGetBindableNumber('pad-edit-widget-bar-min', 0);
            btn.widget_bar_max = padGetBindableNumber('pad-edit-widget-bar-max', 3);
            btn.widget_bar_color = padGetBindableColor('pad-edit-widget-bar-color');
            btn.widget_bar_bg_color = padGetBindableColor('pad-edit-widget-bar-bg-color');
            const bwPct = parseInt(document.getElementById('pad-edit-widget-bar-width-pct').value);
            btn.widget_bar_width_pct = (isNaN(bwPct) || bwPct > 100) ? 100 : (bwPct < 1) ? 1 : bwPct;
            const orient = document.getElementById('pad-edit-widget-orientation').value;
            if (orient === 'horizontal') btn.widget_orientation = 'horizontal';
            const barAnimMs = parseInt(document.getElementById('pad-edit-widget-bar-anim-ms').value);
            btn.widget_anim_ms = (isNaN(barAnimMs) || barAnimMs < 0) ? 300 : (barAnimMs > 5000) ? 5000 : barAnimMs;
        }
        if (wtype === 'gauge') {
            const gDataBinding = document.getElementById('pad-edit-gauge-data-binding').value.trim();
            if (gDataBinding) btn.widget_data_binding = gDataBinding;
            btn.widget_data_binding_2 = document.getElementById('pad-edit-gauge-data-binding-2').value.trim();
            btn.widget_data_binding_3 = document.getElementById('pad-edit-gauge-data-binding-3').value.trim();
            btn.widget_data_binding_4 = document.getElementById('pad-edit-gauge-data-binding-4').value.trim();
            const gStartLabel = padLabelFromInput('pad-edit-gauge-start-label');
            const gStartLabel2 = padLabelFromInput('pad-edit-gauge-start-label-2');
            const gStartLabel3 = padLabelFromInput('pad-edit-gauge-start-label-3');
            const gStartLabel4 = padLabelFromInput('pad-edit-gauge-start-label-4');
            btn.widget_gauge_start_label = gStartLabel;
            btn.widget_gauge_start_label_2 = gStartLabel2;
            btn.widget_gauge_start_label_3 = gStartLabel3;
            btn.widget_gauge_start_label_4 = gStartLabel4;
            btn.widget_gauge_min = padGetBindableNumber('pad-edit-gauge-min', 0);
            btn.widget_gauge_max = padGetBindableNumber('pad-edit-gauge-max', 100);
            const gDeg = parseInt(document.getElementById('pad-edit-gauge-degrees').value);
            btn.widget_gauge_degrees = (isNaN(gDeg) || gDeg < 10) ? 180 : (gDeg > 360) ? 360 : gDeg;
            const gSa = parseInt(document.getElementById('pad-edit-gauge-start-angle').value);
            btn.widget_gauge_start_angle = (isNaN(gSa)) ? 180 : gSa % 360;
            btn.widget_gauge_zero_centered = document.getElementById('pad-edit-gauge-zero-centered').checked;
            btn.widget_gauge_dual_binding_pair_1 = document.getElementById('pad-edit-gauge-dual-binding-pair-1').checked;
            btn.widget_gauge_dual_binding_pair_2 = document.getElementById('pad-edit-gauge-dual-binding-pair-2').checked;
            btn.widget_gauge_show_needle = document.getElementById('pad-edit-gauge-show-needle').checked;
            btn.widget_arc_color = padGetBindableColor('pad-edit-gauge-arc-color');
            btn.widget_arc_color_2 = padGetBindableColor('pad-edit-gauge-arc-color-2');
            btn.widget_arc_color_3 = padGetBindableColor('pad-edit-gauge-arc-color-3');
            btn.widget_arc_color_4 = padGetBindableColor('pad-edit-gauge-arc-color-4');
            btn.widget_gauge_track_color = padGetBindableColor('pad-edit-gauge-track-color');
            btn.widget_gauge_needle_color = padGetBindableColor('pad-edit-gauge-needle-color');
            btn.widget_gauge_tick_color = padGetBindableColor('pad-edit-gauge-tick-color');
            const awPct = parseInt(document.getElementById('pad-edit-gauge-arc-width-pct').value);
            btn.widget_gauge_arc_width_pct = (isNaN(awPct) || awPct > 50) ? 15 : (awPct < 5) ? 5 : awPct;
            const gTicks = parseInt(document.getElementById('pad-edit-gauge-ticks').value);
            btn.widget_gauge_ticks = (isNaN(gTicks) || gTicks < 0) ? 5 : (gTicks > 20) ? 20 : gTicks;
            const gNeedleW = parseInt(document.getElementById('pad-edit-gauge-needle-width').value);
            btn.widget_gauge_needle_width = (isNaN(gNeedleW) || gNeedleW < 0) ? 2 : (gNeedleW > 10) ? 10 : gNeedleW;
            const gNeedleCut = parseInt(document.getElementById('pad-edit-gauge-needle-cutoff').value);
            btn.widget_gauge_needle_cutoff_pct = (isNaN(gNeedleCut) || gNeedleCut < 0) ? 0 : (gNeedleCut > 99) ? 99 : gNeedleCut;
            const gTickW = parseInt(document.getElementById('pad-edit-gauge-tick-width').value);
            btn.widget_gauge_tick_width = (isNaN(gTickW) || gTickW < 1) ? 1 : (gTickW > 5) ? 5 : gTickW;
            btn.widget_gauge_marker_value = document.getElementById('pad-edit-gauge-marker-value').value.trim();
            const gMZoneDeg = parseInt(document.getElementById('pad-edit-gauge-marker-zone-deg').value);
            btn.widget_gauge_marker_zone_deg = (isNaN(gMZoneDeg) || gMZoneDeg < 0) ? 0 : (gMZoneDeg > 90) ? 90 : gMZoneDeg;
            const gMTickW = parseInt(document.getElementById('pad-edit-gauge-marker-tick-width').value);
            btn.widget_gauge_marker_tick_width = (isNaN(gMTickW) || gMTickW < 0) ? 2 : (gMTickW > 5) ? 5 : gMTickW;
            btn.widget_gauge_marker_tick_color = padGetBindableColor('pad-edit-gauge-marker-tick-color');
            btn.widget_gauge_marker_zone_color = padGetBindableColor('pad-edit-gauge-marker-zone-color');
            const gaugeAnimMs = parseInt(document.getElementById('pad-edit-gauge-anim-ms').value);
            btn.widget_anim_ms = (isNaN(gaugeAnimMs) || gaugeAnimMs < 0) ? 300 : (gaugeAnimMs > 5000) ? 5000 : gaugeAnimMs;
        }
        if (wtype === 'sparkline') {
            const sDataBinding = document.getElementById('pad-edit-sparkline-data-binding').value.trim();
            if (sDataBinding) btn.widget_data_binding = sDataBinding;
            btn.widget_data_binding_2 = document.getElementById('pad-edit-sparkline-data-binding-2').value.trim();
            btn.widget_data_binding_3 = document.getElementById('pad-edit-sparkline-data-binding-3').value.trim();
            const sMin = padGetBindableNumber('pad-edit-sparkline-min', undefined);
            const sMax = padGetBindableNumber('pad-edit-sparkline-max', undefined);
            if (sMin !== undefined) btn.widget_sparkline_min = sMin;
            if (sMax !== undefined) btn.widget_sparkline_max = sMax;
            const sWindow = parseInt(document.getElementById('pad-edit-sparkline-window').value);
            btn.widget_sparkline_window = (isNaN(sWindow) || sWindow < 10) ? 300 : sWindow;
            const sSlots = parseInt(document.getElementById('pad-edit-sparkline-slots').value);
            btn.widget_sparkline_slots = (isNaN(sSlots) || sSlots < 2) ? 60 : (sSlots > 255) ? 255 : sSlots;
            btn.widget_sparkline_line_color = padGetBindableColor('pad-edit-sparkline-line-color');
            btn.widget_sparkline_line_color_2 = padGetBindableColor('pad-edit-sparkline-line-color-2');
            btn.widget_sparkline_line_color_3 = padGetBindableColor('pad-edit-sparkline-line-color-3');
            const sLw = parseInt(document.getElementById('pad-edit-sparkline-line-width').value);
            btn.widget_sparkline_line_width = (isNaN(sLw) || sLw < 1) ? 2 : (sLw > 10) ? 10 : sLw;
            const sSmooth = parseInt(document.getElementById('pad-edit-sparkline-smooth').value);
            btn.widget_sparkline_smooth = (isNaN(sSmooth) || sSmooth < 0) ? 0 : (sSmooth > 8) ? 8 : sSmooth;
            btn.widget_sparkline_unified_scale = document.getElementById('pad-edit-sparkline-unified-scale').checked;

            // Min/max markers
            const maxSz = parseInt(document.getElementById('pad-edit-sparkline-marker-size-max').value);
            btn.widget_sparkline_marker_size_max = (isNaN(maxSz) || maxSz < 0) ? 0 : (maxSz > 20) ? 20 : maxSz;
            const maxFmt = document.getElementById('pad-edit-sparkline-max-fmt').value.trim();
            if (maxFmt) btn.widget_sparkline_max_fmt = maxFmt;
            const maxLblClr = padGetBindableColor('pad-edit-sparkline-max-label-color');
            if (maxLblClr && maxLblClr.toLowerCase() !== '#ffffff') btn.widget_sparkline_max_label_color = maxLblClr;

            const minSz = parseInt(document.getElementById('pad-edit-sparkline-marker-size-min').value);
            btn.widget_sparkline_marker_size_min = (isNaN(minSz) || minSz < 0) ? 0 : (minSz > 20) ? 20 : minSz;
            const minFmt = document.getElementById('pad-edit-sparkline-min-fmt').value.trim();
            if (minFmt) btn.widget_sparkline_min_fmt = minFmt;
            const minLblClr = padGetBindableColor('pad-edit-sparkline-min-label-color');
            if (minLblClr && minLblClr.toLowerCase() !== '#ffffff') btn.widget_sparkline_min_label_color = minLblClr;

            // Current value dot
            const cdSz = parseInt(document.getElementById('pad-edit-sparkline-current-dot').value);
            btn.widget_sparkline_current_dot = (isNaN(cdSz) || cdSz < 0) ? 0 : (cdSz > 20) ? 20 : cdSz;

            // Current value labels
            const lblW = parseInt(document.getElementById('pad-edit-sparkline-label-width').value);
            if (!isNaN(lblW) && lblW > 0) btn.widget_sparkline_label_width = (lblW > 200) ? 200 : lblW;
            const cl1 = document.getElementById('pad-edit-sparkline-current-label').value.trim();
            if (cl1) btn.widget_sparkline_current_label = cl1;
            const cl2 = document.getElementById('pad-edit-sparkline-current-label-2').value.trim();
            if (cl2) btn.widget_sparkline_current_label_2 = cl2;
            const cl3 = document.getElementById('pad-edit-sparkline-current-label-3').value.trim();
            if (cl3) btn.widget_sparkline_current_label_3 = cl3;

            // Reference lines
            for (let r = 1; r <= 3; r++) {
                const ry = parseFloat(document.getElementById('pad-edit-sparkline-ref-' + r + '-y').value);
                if (!isNaN(ry)) {
                    btn['widget_sparkline_ref_' + r + '_y'] = ry;
                    btn['widget_sparkline_ref_' + r + '_color'] = padGetBindableColor('pad-edit-sparkline-ref-' + r + '-color');
                    btn['widget_sparkline_ref_' + r + '_pattern'] = parseInt(document.getElementById('pad-edit-sparkline-ref-' + r + '-pattern').value) || 0;
                }
            }
            if (document.getElementById('pad-edit-sparkline-ref-in-view').checked) btn.widget_sparkline_ref_in_view = true;
        }
        if (wtype === 'table') {
            const tDataBinding = document.getElementById('pad-edit-table-data-binding').value.trim();
            if (tDataBinding) btn.widget_data_binding = tDataBinding;
            const tStyle = document.getElementById('pad-edit-table-style').value.trim();
            if (tStyle) btn.widget_table_style = tStyle;
            btn.widget_table_scrollable = document.getElementById('pad-edit-table-scrollable').value === 'true';
        }
        if (wtype === 'rocker') {
            const rAxis = document.getElementById('pad-edit-rocker-axis').value;
            if (rAxis === 'horizontal') btn.widget_rocker_axis = 'horizontal';
            const rColor = document.getElementById('pad-edit-rocker-color').value.trim();
            if (rColor && rColor !== '#FFFFFF') btn.widget_rocker_color = rColor;
            const rOpa = parseInt(document.getElementById('pad-edit-rocker-opacity').value);
            if (!isNaN(rOpa) && rOpa !== 80) btn.widget_rocker_opacity = Math.max(0, Math.min(255, rOpa));
        }
        if (wtype === 'numericrocker') {
            const nrAxis = document.getElementById('pad-edit-numericrocker-axis').value;
            if (nrAxis === 'vertical') btn.widget_numericrocker_axis = 'vertical';
            const nrSmall = parseFloat(document.getElementById('pad-edit-numericrocker-small-step').value);
            if (!isNaN(nrSmall) && nrSmall !== 1) btn.widget_numericrocker_small_step = Math.max(0, nrSmall);
            const nrLarge = parseFloat(document.getElementById('pad-edit-numericrocker-large-step').value);
            if (!isNaN(nrLarge) && nrLarge !== 10) btn.widget_numericrocker_large_step = Math.max(0, nrLarge);
            const nrColor = document.getElementById('pad-edit-numericrocker-color').value.trim();
            if (nrColor && nrColor !== '#FFFFFF') btn.widget_numericrocker_color = nrColor;
            const nrOpa = parseInt(document.getElementById('pad-edit-numericrocker-opacity').value);
            if (!isNaN(nrOpa) && nrOpa !== 80) btn.widget_numericrocker_opacity = Math.max(0, Math.min(255, nrOpa));
            // Adjustment action (stored as nested object)
            var adjAction = actionEditorBuild('pad-edit-nr-adjust');
            if (adjAction.type) btn.widget_numericrocker_action = adjAction;
        }
    }

    // Button state
    const btnState = document.getElementById('pad-edit-btn-state').value.trim();
    if (btnState) btn.btn_state = btnState;

    // Validate all binding fields before accepting
    if (typeof bindingValidateDialog === 'function') {
        var bvResult = bindingValidateDialog();
        if (!bvResult.valid) {
            showMessage(bvResult.count + ' binding error' + (bvResult.count > 1 ? 's' : '') + ' — check highlighted fields', 'error');
            return;
        }
    }

    padState.buttons.push(btn);
    padMarkDirty();
    if (!keepOpen) padDialogClose();
    padRenderGrid();
}

function padDialogClear() {
    const col = padState.editCol;
    const row = padState.editRow;
    padState.buttons = padState.buttons.filter(b => !(b.col === col && b.row === row));
    padMarkDirty();
    padDialogClose();
    padRenderGrid();
}
