// portal_pad_editor.js - Pad editor, grid rendering, button dialog, and page init
// Part of the ESP32 Macropad configuration portal.

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
    colorCache: {},      // page → hex[] — colors from visited pads
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

// --- Icon Support ---

let padButtonSizesCache = null;

// Lazy-load Material Symbols font
let _materialSymbolsLoaded = false;
function padEnsureMaterialSymbols() {
    if (_materialSymbolsLoaded) return Promise.resolve();
    return new Promise((resolve) => {
        _materialSymbolsLoaded = true;
        const link = document.createElement('link');
        link.rel = 'stylesheet';
        link.href = 'https://fonts.googleapis.com/css2?family=Material+Symbols+Outlined:opsz,wght,FILL,GRAD@48,400,1,0';
        link.onload = () => document.fonts.ready.then(resolve);
        link.onerror = resolve;
        document.head.appendChild(link);
    });
}

// Simplify binding tokens for grid preview: [mqtt:topic;path;fmt] → [mqtt:topic]
function padSimplifyBindings(text) {
    if (!text) return text;
    return text.replace(/\[(\w+):([^\];]*)[^\]]*\]/g, '[$1:$2]');
}

// Read a bindable-number field: keep binding strings as-is, parse plain numbers, or return default.
function padGetBindableNumber(id, def) {
    const raw = document.getElementById(id).value.trim();
    if (raw.includes('[')) return raw;
    const n = parseFloat(raw);
    return isNaN(n) ? def : n;
}

// Convert stored label value (real \n) to display string (\n escape) for <input>
function padLabelToInput(val) { return (val || '').replace(/\n/g, '\\n'); }
// Convert user-typed string (\n escape) back to stored value (real \n)
function padLabelFromInput(id) { return document.getElementById(id).value.trim().replace(/\\n/g, '\n'); }

function padIconIdToType(iconId) {
    if (!iconId) return { type: '', value: '' };
    if (iconId.startsWith('emoji_')) return { type: 'emoji', value: iconId.substring(6) };
    if (iconId.startsWith('mi_')) return { type: 'mi', value: iconId.substring(3) };
    return { type: '', value: '' };
}

function padBuildIconId() {
    const type = document.getElementById('pad-edit-icon-type').value;
    if (type === 'emoji') {
        const val = document.getElementById('pad-edit-icon-emoji').value.trim();
        return val ? 'emoji_' + val : '';
    }
    if (type === 'mi') {
        const val = document.getElementById('pad-edit-icon-mi').value.trim();
        return val ? 'mi_' + val : '';
    }
    return '';
}

function padIconTypeChanged() {
    const type = document.getElementById('pad-edit-icon-type').value;
    document.getElementById('pad-edit-icon-emoji-group').style.display = (type === 'emoji') ? '' : 'none';
    document.getElementById('pad-edit-icon-mi-group').style.display = (type === 'mi') ? '' : 'none';
    if (type === 'mi') padEnsureMaterialSymbols();
    padUpdateIconPreview();
}

function padUpdateIconPreview() {
    const box = document.getElementById('pad-edit-icon-preview-box');
    const canvas = document.getElementById('pad-edit-icon-canvas');
    if (!box || !canvas) return;
    const type = document.getElementById('pad-edit-icon-type').value;

    if (!type) { box.style.display = 'none'; return; }

    const size = 64;
    canvas.width = size;
    canvas.height = size;
    const ctx = canvas.getContext('2d');
    ctx.clearRect(0, 0, size, size);

    if (type === 'emoji') {
        const emoji = document.getElementById('pad-edit-icon-emoji').value.trim();
        if (!emoji) { box.style.display = 'none'; return; }
        ctx.font = (size * 0.75) + 'px serif';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText(emoji, size / 2, size / 2);
        padCenterCanvasContent(ctx, size, size);
    } else if (type === 'mi') {
        const name = document.getElementById('pad-edit-icon-mi').value.trim();
        if (!name) { box.style.display = 'none'; return; }
        ctx.font = size + 'px "Material Symbols Outlined"';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillStyle = '#ffffff';
        ctx.fillText(name, size / 2, size / 2);
        padCenterCanvasContent(ctx, size, size);
    }

    box.style.display = '';
}

function padCenterCanvasContent(ctx, w, h) {
    const imgData = ctx.getImageData(0, 0, w, h);
    const px = imgData.data;
    let minY = h, maxY = 0;
    for (let y = 0; y < h; y++) {
        for (let x = 0; x < w; x++) {
            if (px[(y * w + x) * 4 + 3] > 0) {
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
                break;
            }
        }
    }
    if (minY >= maxY) return;
    const contentMid = (minY + maxY) / 2;
    const canvasMid = h / 2;
    const shift = Math.round(canvasMid - contentMid);
    if (shift === 0) return;
    ctx.clearRect(0, 0, w, h);
    ctx.putImageData(imgData, 0, shift);
}

function padRenderIconOnCanvas(canvas, iconId, width, height) {
    canvas.width = width;
    canvas.height = height;
    const ctx = canvas.getContext('2d');
    ctx.clearRect(0, 0, width, height);

    const parsed = padIconIdToType(iconId);
    if (parsed.type === 'emoji') {
        const fontSize = Math.floor(Math.min(width, height) * 0.75);
        ctx.font = fontSize + 'px serif';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText(parsed.value, width / 2, height / 2);
        padCenterCanvasContent(ctx, width, height);
    } else if (parsed.type === 'mi') {
        const fontSize = Math.min(width, height);
        ctx.font = fontSize + 'px "Material Symbols Outlined"';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillStyle = '#ffffff';
        ctx.fillText(parsed.value, width / 2, height / 2);
        padCenterCanvasContent(ctx, width, height);
    }
}

function padCanvasToPNG(canvas) {
    return new Promise(function(resolve, reject) {
        canvas.toBlob(function(blob) {
            if (blob) resolve(blob);
            else reject(new Error('Canvas toBlob failed'));
        }, 'image/png');
    });
}

async function padGetButtonSizes() {
    if (padButtonSizesCache &&
        padButtonSizesCache.cols === padState.cols &&
        padButtonSizesCache.rows === padState.rows) {
        return padButtonSizesCache;
    }
    const resp = await fetch('/api/pad/button_sizes?cols=' + padState.cols + '&rows=' + padState.rows);
    if (!resp.ok) throw new Error('Failed to get button sizes');
    const data = await resp.json();
    data.cols = padState.cols;
    data.rows = padState.rows;
    padButtonSizesCache = data;
    return data;
}

async function padUploadPageIcons() {
    // Always delete old page icons (cleans up removed icons)
    await fetch('/api/icons/page?page=' + padState.page, { method: 'DELETE' });

    const iconButtons = padState.buttons.filter(b => b.icon_id);
    if (iconButtons.length === 0) return;

    const btnSizes = await padGetButtonSizes();
    const baseW = btnSizes.button_w - btnSizes.padding * 2;
    const baseH = btnSizes.button_h - btnSizes.padding * 2;

    const needsMi = iconButtons.some(b => b.icon_id.startsWith('mi_'));
    if (needsMi) await padEnsureMaterialSymbols();

    const canvas = document.createElement('canvas');

    for (const btn of iconButtons) {
        const cs = btn.col_span || 1;
        const rs = btn.row_span || 1;
        // LVGL content area = button - 2*padding - 2*border_width
        const bw = (btn.border_width !== undefined) ? btn.border_width : 1;
        const fullW = baseW * cs + btnSizes.gap * (cs - 1) - bw * 2;
        const fullH = baseH * rs + btnSizes.gap * (rs - 1) - bw * 2;
        const kind = btn.icon_id.startsWith('mi_') ? 1 : 0;

        // Reserve space for top/bottom labels (font_small_h each)
        const labelH = btnSizes.font_small_h || 0;
        const topReserve = btn.label_top ? labelH : 0;
        const bottomReserve = btn.label_bottom ? labelH : 0;
        var iconW = fullW;
        var iconH = fullH - topReserve - bottomReserve;

        // For bar chart widgets, icon only occupies the top half
        if (btn.widget_type === 'bar_chart') {
            iconH = Math.floor((fullH - topReserve - bottomReserve) / 2);
        }

        // For gauge widgets, icon sits inside the arc — scale down
        if (btn.widget_type === 'gauge') {
            iconW = Math.floor(iconW * 0.4);
            iconH = Math.floor(iconH * 0.4);
        }

        // Apply explicit icon_scale_pct if set (1-250%)
        if (btn.icon_scale_pct && btn.icon_scale_pct > 0) {
            iconW = Math.max(1, Math.round(iconW * btn.icon_scale_pct / 100));
            iconH = Math.max(1, Math.round(iconH * btn.icon_scale_pct / 100));
        }

        // Make icon square — glyph is sized to min dimension anyway,
        // avoids transparent padding in the taller axis
        var iconSize = Math.min(iconW, iconH);
        iconW = iconSize;
        iconH = iconSize;

        padRenderIconOnCanvas(canvas, btn.icon_id, iconW, iconH);
        const pngBlob = await padCanvasToPNG(canvas);
        const key = 'pad_' + padState.page + '_' + btn.col + '_' + btn.row;

        const resp = await fetch('/api/icons/install?id=' + encodeURIComponent(key) + '&kind=' + kind, {
            method: 'POST',
            headers: { 'Content-Type': 'image/png' },
            body: pngBlob,
        });
        if (!resp.ok) {
            console.error('Icon upload failed for ' + key);
        }
    }
}

function padInit() {
    const section = document.getElementById('pad-config-section');
    if (!section) return;

    // Generate action editor HTML from shared module
    const aeContainer = document.getElementById('pad-action-editors');
    if (aeContainer) {
        aeContainer.innerHTML =
            actionEditorHTML('pad-edit-action', 'Tap Action', { showBleHint: true, showKeyHelp: true }) +
            '<hr style="border:none; border-top:1px solid #e5e5ea; margin:12px 0;">' +
            actionEditorHTML('pad-edit-lp-action', 'Long-Press Action', { showBleHint: true, showKeyHelp: true });
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

    document.getElementById('pad-save-btn').addEventListener('click', padSavePage);
    document.getElementById('pad-delete-btn').addEventListener('click', padDeletePage);
    document.getElementById('pad-show-btn').addEventListener('click', padShowOnDevice);
    document.getElementById('pad-binding-add').addEventListener('click', padAddBinding);

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
                padPopulatePadDropdown();
                padPopulateScreenDropdown();
                padLoadPage(0);
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

function padPopulateScreenDropdown() {
    actionEditorPopulateScreens(
        ['pad-edit-action', 'pad-edit-lp-action'],
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

function padActionTypeChanged(prefix) {
    const pfx = 'pad-edit-' + ((prefix === 'lp') ? 'lp-action' : 'action');
    actionEditorTypeChanged(pfx);
}

const WIDGET_SECTIONS = ['bar_chart', 'gauge', 'sparkline'];

function padWidgetTypeChanged() {
    const wtype = document.getElementById('pad-edit-widget-type').value;
    WIDGET_SECTIONS.forEach(s => {
        const el = document.getElementById('pad-edit-' + s.replace('_', '-') + '-section');
        if (el) { el.style.display = (wtype === s) ? '' : 'none'; if (wtype === s) el.open = true; }
    });
}

async function padLoadPage(page) {
    padState.page = page;
    padState.rawJson = null;
    padState.buttons = [];
    padState.bindings = [];
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
            padRenderBindings();
            padCacheColors(page, [], '#000000');
            padRenderGrid();
            return;
        }
        if (!resp.ok) throw new Error('HTTP ' + resp.status);

        const json = await resp.json();
        padState.rawJson = json;
        padState.cols = json.cols || 3;
        padState.rows = json.rows || 2;

        document.getElementById('pad-cols').value = padState.cols;
        document.getElementById('pad-rows').value = padState.rows;
        document.getElementById('pad-name').value = json.name || '';
        document.getElementById('pad-wake-screen').value = json.wake_screen || '';
        padInitBindableColor(document.getElementById('pad-page-bg-color-wrap'));
        padSetBindableColor('pad-edit-page-bg-color', json.bg_color, '#000000');

        // Load pad bindings
        padState.bindings = padBindingsFromJson(json.bindings);
        padRenderBindings();

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

function padFindButton(col, row) {
    return padState.buttons.find(b => b.col === col && b.row === row);
}

function padIsCellOccupied(col, row) {
    // Check if any button occupies this cell (via its span)
    for (const b of padState.buttons) {
        const bc = b.col, br = b.row;
        const cs = b.col_span || 1, rs = b.row_span || 1;
        if (col >= bc && col < bc + cs && row >= br && row < br + rs) {
            return b;
        }
    }
    return null;
}

function padRenderGrid() {
    const grid = document.getElementById('pad-grid');
    const emptyState = document.getElementById('pad-empty-state');
    if (!grid) return;

    const cols = padState.cols;
    const rows = padState.rows;

    grid.style.gridTemplateColumns = 'repeat(' + cols + ', 1fr)';
    grid.style.gridTemplateRows = 'repeat(' + rows + ', 1fr)';
    // Apply device aspect ratio so the editor mimics the real screen
    if (deviceInfoCache && deviceInfoCache.display_coord_width && deviceInfoCache.display_coord_height) {
        grid.style.aspectRatio = deviceInfoCache.display_coord_width + ' / ' + deviceInfoCache.display_coord_height;
    }
    const pageBgInput = document.getElementById('pad-edit-page-bg-color');
    grid.style.background = (pageBgInput && /^#[0-9a-fA-F]{6}$/.test(pageBgInput.value.trim())) ? pageBgInput.value.trim() : '#000000';
    grid.innerHTML = '';

    if (emptyState) emptyState.style.display = 'none';

    // Track which cells are "covered" by a spanning button
    const covered = new Set();

    // First pass: place buttons with spans
    for (const b of padState.buttons) {
        const bc = b.col, br = b.row;
        const cs = Math.min(b.col_span || 1, cols - bc);
        const rs = Math.min(b.row_span || 1, rows - br);
        if (bc >= cols || br >= rows) continue; // Out of current grid

        for (let dc = 0; dc < cs; dc++) {
            for (let dr = 0; dr < rs; dr++) {
                if (dc === 0 && dr === 0) continue;
                covered.add((bc + dc) + ',' + (br + dr));
            }
        }
    }

    // Second pass: render cells
    for (let r = 0; r < rows; r++) {
        for (let c = 0; c < cols; c++) {
            const key = c + ',' + r;
            if (covered.has(key)) continue; // Skip cells covered by a span

            const btn = padFindButton(c, r);
            const cell = document.createElement('div');
            cell.classList.add('pad-cell');

            if (btn) {
                cell.classList.add('pad-cell-btn');
                const cs = Math.min(btn.col_span || 1, cols - c);
                const rs = Math.min(btn.row_span || 1, rows - r);
                if (cs > 1) cell.style.gridColumn = 'span ' + cs;
                if (rs > 1) cell.style.gridRow = 'span ' + rs;

                const bg = padColorToHex(btn.bg_color, '#333333');
                const fg = padColorToHex(btn.fg_color, '#ffffff');
                cell.style.background = bg;
                cell.style.color = fg;



                const borderColor = padColorToHex(btn.border_color, '#000000');
                const borderWidth = (btn.border_width !== undefined) ? btn.border_width : 1;
                const cornerRadius = (btn.corner_radius !== undefined) ? btn.corner_radius : 8;
                cell.style.border = borderWidth + 'px solid ' + borderColor;
                cell.style.borderRadius = cornerRadius + 'px';

                // Spread labels to edges when top or bottom labels are present
                const hasTop = !!btn.label_top;
                const hasBottom = !!btn.label_bottom;
                if (hasTop || hasBottom) {
                    cell.style.justifyContent = 'space-between';
                }

                if (hasTop) {
                    const el = document.createElement('div');
                    el.className = 'pad-cell-label-top';
                    el.textContent = padSimplifyBindings(btn.label_top);
                    cell.appendChild(el);
                } else if (hasBottom) {
                    // Spacer so center content stays centered with space-between
                    cell.appendChild(document.createElement('div'));
                }
                // Background image placeholder (absolute-positioned behind content)
                if (btn.bg_image_url) {
                    const img = document.createElement('div');
                    img.className = 'pad-cell-image-placeholder';
                    img.textContent = '\u{1F5BC}';
                    cell.appendChild(img);
                }
                if (btn.icon_id) {
                    const iconParsed = padIconIdToType(btn.icon_id);
                    if (iconParsed.type === 'emoji') {
                        const el = document.createElement('div');
                        el.className = 'pad-cell-icon';
                        el.textContent = iconParsed.value;
                        cell.appendChild(el);
                    } else if (iconParsed.type === 'mi') {
                        padEnsureMaterialSymbols();
                        const el = document.createElement('span');
                        el.className = 'material-symbols-outlined pad-cell-icon';
                        el.textContent = iconParsed.value;
                        el.style.color = fg;
                        cell.appendChild(el);
                    }
                } else if (!btn.bg_image_url) {
                    const centerText = btn.label_center || '•';
                    const elc = document.createElement('div');
                    elc.className = 'pad-cell-label-center';
                    elc.textContent = padSimplifyBindings(centerText);
                    cell.appendChild(elc);
                } else if (btn.label_center) {
                    const elc = document.createElement('div');
                    elc.className = 'pad-cell-label-center';
                    elc.textContent = padSimplifyBindings(btn.label_center);
                    cell.appendChild(elc);
                }
                if (hasBottom) {
                    const el = document.createElement('div');
                    el.className = 'pad-cell-label-bottom';
                    el.textContent = padSimplifyBindings(btn.label_bottom);
                    cell.appendChild(el);
                } else if (hasTop) {
                    // Spacer so center content stays centered with space-between
                    cell.appendChild(document.createElement('div'));
                }

                // Widget indicator
                if (btn.widget_type === 'bar_chart') {
                    const bar = document.createElement('div');
                    bar.className = 'pad-cell-widget-bar';
                    bar.title = 'Bar Chart Widget';
                    cell.appendChild(bar);
                }
                if (btn.widget_type === 'gauge') {
                    const arc = document.createElement('div');
                    arc.className = 'pad-cell-widget-gauge';
                    arc.title = 'Gauge Widget';
                    cell.appendChild(arc);
                }
                if (btn.widget_type === 'sparkline') {
                    const spark = document.createElement('div');
                    spark.className = 'pad-cell-widget-sparkline';
                    spark.title = 'Sparkline Widget';
                    cell.appendChild(spark);
                }

                cell.addEventListener('click', () => padDialogOpen(c, r));
            } else {
                cell.classList.add('pad-cell-empty');
                cell.textContent = '+';
                cell.addEventListener('click', () => padDialogOpen(c, r));
            }

            grid.appendChild(cell);
        }
    }
}


/** Show the binding help overlay. */
function showBindingHelp(section) {
    const overlay = document.getElementById('binding-help-overlay');
    if (!overlay) return;

    overlay.style.display = 'flex';

    const body = overlay.querySelector('.binding-docs-body');
    const target = section ? overlay.querySelector(`[data-binding-section="${section}"]`) : null;
    overlay.querySelectorAll('.binding-docs-section').forEach(sectionEl => {
        sectionEl.classList.toggle('is-active', sectionEl === target);
    });

    requestAnimationFrame(() => {
        if (target) {
            target.scrollIntoView({ behavior: 'smooth', block: 'start' });
        } else if (body) {
            body.scrollTop = 0;
        }
    });
}

function closeBindingHelp() {
    const overlay = document.getElementById('binding-help-overlay');
    if (!overlay) return;

    overlay.style.display = 'none';
    overlay.querySelectorAll('.binding-docs-section').forEach(sectionEl => {
        sectionEl.classList.remove('is-active');
    });
}

function showStyleHelp() {
    document.getElementById('style-help-overlay').style.display = 'flex';
}

/** Toggle label style input visibility for a label slot (top/center/bottom). */
function toggleLabelStyle(slot) {
    var wrap = document.getElementById('pad-edit-label-' + slot + '-style-wrap');
    var btn = wrap.previousElementSibling.querySelector('.style-toggle-btn');
    var visible = wrap.style.display !== 'none';
    wrap.style.display = visible ? 'none' : 'flex';
    btn.classList.toggle('active', !visible);
    if (!visible) wrap.querySelector('input').focus();
}

/** Show or hide a label style wrap based on whether it has a value. */
function syncLabelStyleVisibility(slot) {
    var input = document.getElementById('pad-edit-label-' + slot + '-style');
    var wrap = document.getElementById('pad-edit-label-' + slot + '-style-wrap');
    var btn = wrap.previousElementSibling.querySelector('.style-toggle-btn');
    var hasValue = input.value.trim().length > 0;
    wrap.style.display = hasValue ? 'flex' : 'none';
    btn.classList.toggle('active', hasValue);
}

// --- Pad Bindings helpers ---

function padRenderBindings() {
    const list = document.getElementById('pad-bindings-list');
    if (!list) return;
    list.innerHTML = '';
    if (!padState.bindings || padState.bindings.length === 0) return;
    padState.bindings.forEach(function(b, idx) {
        const row = document.createElement('div');
        row.className = 'pad-binding-row';
        row.innerHTML =
            '<input type="text" class="pad-binding-name" value="' + escAttr(b.name) + '" placeholder="name" maxlength="31" spellcheck="false">' +
            '<span style="color:#86868b; flex-shrink:0;">→</span>' +
            '<input type="text" class="pad-binding-value" value="' + escAttr(b.value) + '" placeholder="[mqtt:topic;path]" maxlength="191" spellcheck="false">' +
            '<button type="button" class="btn btn-small pad-binding-del" style="padding:2px 8px; font-size:12px; color:#ff3b30;">✕</button>';
        row.querySelector('.pad-binding-name').addEventListener('input', function() {
            var v = this.value.trim();
            padState.bindings[idx].name = v;
            var ok = v === '' || padIsValidBindingName(v);
            this.style.borderColor = ok ? '' : '#ff3b30';
            this.title = ok ? '' : 'Must start with a letter; only letters, digits, and underscores allowed';
            padMarkDirty();
        });
        row.querySelector('.pad-binding-value').addEventListener('input', function() {
            padState.bindings[idx].value = this.value;
            padMarkDirty();
        });
        row.querySelector('.pad-binding-del').addEventListener('click', function() {
            padState.bindings.splice(idx, 1);
            padRenderBindings();
            padMarkDirty();
        });
        list.appendChild(row);
    });
}

function padIsValidBindingName(name) {
    return /^[a-zA-Z][a-zA-Z0-9_]*$/.test(name) && name.length < 32;
}

function padAddBinding() {
    if (!padState.bindings) padState.bindings = [];
    padState.bindings.push({ name: '', value: '' });
    padRenderBindings();
    // Focus the new name input
    const names = document.querySelectorAll('.pad-binding-name');
    if (names.length) names[names.length - 1].focus();
    padMarkDirty();
}

function padBindingsFromJson(obj) {
    var arr = [];
    if (obj && typeof obj === 'object') {
        Object.keys(obj).forEach(function(k) { arr.push({ name: k, value: obj[k] }); });
    }
    return arr;
}

function padBindingsToDict(bindings) {
    var bd = {};
    if (bindings) bindings.forEach(function(b) { if (b.name) bd[b.name] = b.value || ''; });
    return Object.keys(bd).length > 0 ? bd : null;
}

function padCacheColors(page, buttons, pageBgColor) {
    const seen = new Set();
    const colors = [];
    // Include page background color in cache
    if (pageBgColor && pageBgColor !== '#000000') {
        seen.add(pageBgColor); colors.push(pageBgColor);
    }
    for (const b of buttons) {
        [b.bg_color, b.fg_color, b.border_color].forEach(val => {
            const hex = padColorToHex(val, null);
            if (hex && !seen.has(hex)) { seen.add(hex); colors.push(hex); }
        });
    }
    padState.colorCache[page] = colors;
}



function padCollectUsedColors(editCol, editRow) {
    const seen = new Set();
    const colors = [];
    function add(val) {
        const hex = padColorToHex(val, null);
        if (!hex || seen.has(hex)) return;
        seen.add(hex);
        colors.push(hex);
    }
    // 1) Current button first (highest priority)
    const cur = padFindButton(editCol, editRow);
    if (cur) { add(cur.bg_color); add(cur.fg_color); add(cur.border_color); }
    // 2) Other buttons on same pad
    for (const b of padState.buttons) {
        if (b.col === editCol && b.row === editRow) continue;
        add(b.bg_color); add(b.fg_color); add(b.border_color);
    }
    // 3) Colors from other visited pads
    for (const [pg, hexArr] of Object.entries(padState.colorCache)) {
        if (parseInt(pg) === padState.page) continue;
        hexArr.forEach(hex => add(hex));
    }
    return colors.slice(0, 9);
}

function padDialogOpen(col, row) {
    padState.editCol = col;
    padState.editRow = row;

    // Refresh target screen dropdowns so pad names are current
    padPopulateScreenDropdown();

    const btn = padFindButton(col, row) || {};

    document.getElementById('pad-edit-title').textContent =
        'Button [' + col + ', ' + row + ']';

    document.getElementById('pad-edit-label-top').value = padLabelToInput(btn.label_top);
    document.getElementById('pad-edit-label-center').value = padLabelToInput(btn.label_center);
    document.getElementById('pad-edit-label-bottom').value = padLabelToInput(btn.label_bottom);
    document.getElementById('pad-edit-label-top-style').value = btn.label_top_style || '';
    document.getElementById('pad-edit-label-center-style').value = btn.label_center_style || '';
    document.getElementById('pad-edit-label-bottom-style').value = btn.label_bottom_style || '';
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

    padSetBindableColor('pad-edit-bg-color', btn.bg_color || '#333333');
    padSetBindableColor('pad-edit-fg-color', btn.fg_color || '#ffffff');
    padSetBindableColor('pad-edit-border-color', btn.border_color || '#000000');

    // Auto-open colors section if any color has a binding
    document.getElementById('pad-edit-colors-section').open =
        padIsBinding(btn.bg_color) || padIsBinding(btn.fg_color) || padIsBinding(btn.border_color);

    document.getElementById('pad-edit-border-width').value = (btn.border_width !== undefined) ? btn.border_width : 0;
    document.getElementById('pad-edit-corner-radius').value = (btn.corner_radius !== undefined) ? btn.corner_radius : 8;
    document.getElementById('pad-edit-ui-offset').value = btn.ui_offset || '';

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

    // Tap action
    actionEditorLoad('pad-edit-action', btn.action);

    // Long-press action
    actionEditorLoad('pad-edit-lp-action', btn.lp_action);

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
    document.getElementById('pad-edit-gauge-tick-width').value = (btn.widget_gauge_tick_width !== undefined) ? btn.widget_gauge_tick_width : 1;

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

    btn.bg_color = padGetBindableColor('pad-edit-bg-color') || '#333333';
    btn.fg_color = padGetBindableColor('pad-edit-fg-color') || '#ffffff';
    btn.border_color = padGetBindableColor('pad-edit-border-color') || '#000000';

    const bw = document.getElementById('pad-edit-border-width').value.trim();
    btn.border_width = bw || '0';
    const cr = document.getElementById('pad-edit-corner-radius').value.trim();
    btn.corner_radius = cr || '8';
    const uiOffset = document.getElementById('pad-edit-ui-offset').value.trim();
    if (uiOffset) { btn.ui_offset = uiOffset; } else { delete btn.ui_offset; }

    // Tap action
    const tapAct = actionEditorBuild('pad-edit-action');
    if (tapAct.type) btn.action = tapAct;

    // Long-press action
    const lpAct = actionEditorBuild('pad-edit-lp-action');
    if (lpAct.type) btn.lp_action = lpAct;

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

    // Widget type
    const wtype = document.getElementById('pad-edit-widget-type').value;
    if (wtype) {
        btn.widget_type = wtype;
        // Data binding template for widget value
        const wDataBinding = document.getElementById('pad-edit-widget-data-binding').value.trim();
        if (wDataBinding) btn.widget_data_binding = wDataBinding;
        if (wtype === 'bar_chart') {
            btn.widget_bar_min = padGetBindableNumber('pad-edit-widget-bar-min', 0);
            btn.widget_bar_max = padGetBindableNumber('pad-edit-widget-bar-max', 3);
            btn.widget_bar_color = padGetBindableColor('pad-edit-widget-bar-color');
            btn.widget_bar_bg_color = padGetBindableColor('pad-edit-widget-bar-bg-color');
            const bwPct = parseInt(document.getElementById('pad-edit-widget-bar-width-pct').value);
            btn.widget_bar_width_pct = (isNaN(bwPct) || bwPct > 100) ? 100 : (bwPct < 1) ? 1 : bwPct;
            const orient = document.getElementById('pad-edit-widget-orientation').value;
            if (orient === 'horizontal') btn.widget_orientation = 'horizontal';
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
            const gTickW = parseInt(document.getElementById('pad-edit-gauge-tick-width').value);
            btn.widget_gauge_tick_width = (isNaN(gTickW) || gTickW < 1) ? 1 : (gTickW > 5) ? 5 : gTickW;
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
    }

    // Button state
    const btnState = document.getElementById('pad-edit-btn-state').value.trim();
    if (btnState) btn.btn_state = btnState;

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
            await fetch('/api/display/brightness', {
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
        try {
            const infoResp = await fetch(API_INFO);
            if (infoResp.ok) deviceInfoCache = await infoResp.json();
        } catch (_) {}

        // Reload to get canonical version from device
        padLoadPage(padState.page);
    } catch (err) {
        console.error('padSavePage error:', err);
        showMessage('Save failed: ' + err.message, 'error');
    } finally {
        if (blankOnSave && savedBrightness > 0) {
            fetch('/api/display/brightness', {
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
    // Initialize navigation highlighting
    initNavigation();
    
    // Attach event handlers (check if elements exist for multi-page support)
    const configForm = document.getElementById('config-form');
    if (configForm) {
        configForm.addEventListener('submit', saveConfig);
    }
    
    const saveOnlyBtn = document.getElementById('save-only-btn');
    if (saveOnlyBtn) {
        saveOnlyBtn.addEventListener('click', saveOnly);
    }
    
    const rebootBtn = document.getElementById('reboot-btn');
    if (rebootBtn) {
        rebootBtn.addEventListener('click', rebootDevice);
    }
    
    const resetBtn = document.getElementById('reset-btn');
    if (resetBtn) {
        resetBtn.addEventListener('click', resetConfig);
    }
    
    const firmwareFile = document.getElementById('firmware-file');
    if (firmwareFile) {
        firmwareFile.addEventListener('change', handleFileSelect);
    }
    
    const uploadBtn = document.getElementById('upload-btn');
    if (uploadBtn) {
        uploadBtn.addEventListener('click', uploadFirmware);
    }

    // Firmware page: GitHub Pages link is populated in updateOnlineUpdateSection()
    
    const deviceName = document.getElementById('device_name');
    if (deviceName) {
        deviceName.addEventListener('input', updateSanitizedName);
    }
    
    // Add focus handlers for all inputs to prevent keyboard from covering them
    const inputs = document.querySelectorAll('input[type="text"], input[type="password"], textarea');
    inputs.forEach(input => {
        input.addEventListener('focus', handleInputFocus);
    });
    
    // Add brightness slider event handler
    const brightnessSlider = document.getElementById('backlight_brightness');
    if (brightnessSlider) {
        brightnessSlider.addEventListener('input', handleBrightnessChange);
    }
    
    // Add screen selection dropdown event handler
    const screenSelect = document.getElementById('screen_selection');
    if (screenSelect) {
        screenSelect.addEventListener('change', handleScreenChange);
    }
    
    // Load initial data
    loadMode();
    
    // Only load config if config form exists (home and network pages)
    if (configForm) {
        loadConfig();
    } else {
        // Hide loading overlay on pages without config form (firmware page)
        const overlay = document.getElementById('form-loading-overlay');
        if (overlay) overlay.style.display = 'none';
    }
    
    loadVersion();
    
    // Initialize health widget
    initHealthWidget();

    // Initialize pad configuration UI
    padInit();
});