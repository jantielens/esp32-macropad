// portal_pad_icons.js - Icon rendering, canvas, Material Symbols, cell content, and icon upload
// Part of the ESP32 Macropad configuration portal.
// Bundled into portal_pad_editor.js during minification.

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

// Shared cell content renderer for normal and ghost buttons.
// Sets colors, border, labels, icon, and bg-image placeholder on the cell div.
function padRenderCellContent(cell, btn) {
    const bg = padColorToHex(btn.bg_color, padGetEffectiveDefault('bg_color'));
    const fg = padColorToHex(btn.fg_color, padGetEffectiveDefault('fg_color'));
    cell.style.background = bg;
    cell.style.color = fg;
    const borderColor = padColorToHex(btn.border_color, padGetEffectiveDefault('border_color'));
    const borderWidth = (btn.border_width !== undefined) ? btn.border_width : padGetEffectiveDefault('border_width');
    const cornerRadius = (btn.corner_radius !== undefined) ? btn.corner_radius : padGetEffectiveDefault('corner_radius');
    cell.style.border = borderWidth + 'px solid ' + borderColor;
    cell.style.borderRadius = cornerRadius + 'px';
    // Content padding — mirror the device inset so the preview stays WYSIWYG
    var contentPad = (btn.content_pad !== undefined) ? btn.content_pad : padGetEffectiveDefault('content_pad');
    var cpNum = parseInt(contentPad, 10);
    if (!isNaN(cpNum)) {
        if (cpNum < 0) cpNum = 0; else if (cpNum > 50) cpNum = 50;
        cell.style.padding = cpNum + 'px';
    }
    const hasTop = !!btn.label_top;
    const hasBottom = !!btn.label_bottom;
    if (hasTop || hasBottom) cell.style.justifyContent = 'space-between';
    if (hasTop) {
        const el = document.createElement('div');
        el.className = 'pad-cell-label-top';
        el.textContent = padSimplifyBindings(btn.label_top);
        cell.appendChild(el);
    } else if (hasBottom) {
        cell.appendChild(document.createElement('div'));
    }
    if (btn.bg_image_url) {
        const img = document.createElement('div');
        img.className = 'pad-cell-image-placeholder';
        img.textContent = '\u{1F5BC}';
        cell.appendChild(img);
    }
    if (btn.icon_id) {
        const iconParsed = padIconIdToType(btn.icon_id);
        const hasCenter = !!btn.label_center;
        const pos = btn.icon_position || 'above';
        if (hasCenter && pos === 'left') {
            // Horizontal row: icon left, label right
            const row = document.createElement('div');
            row.className = 'pad-cell-icon-row';
            if (iconParsed.type === 'emoji') {
                const ico = document.createElement('span');
                ico.className = 'pad-cell-icon';
                ico.textContent = iconParsed.value;
                row.appendChild(ico);
            } else if (iconParsed.type === 'mi') {
                padEnsureMaterialSymbols();
                const ico = document.createElement('span');
                ico.className = 'material-symbols-outlined pad-cell-icon';
                ico.textContent = iconParsed.value;
                ico.style.color = fg;
                row.appendChild(ico);
            }
            const lbl = document.createElement('span');
            lbl.className = 'pad-cell-label-center';
            lbl.style.fontSize = '12px';
            lbl.textContent = padSimplifyBindings(btn.label_center);
            row.appendChild(lbl);
            cell.appendChild(row);
        } else {
            // Above (default) or Center: show icon, then label below if present
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
            if (hasCenter) {
                const elc = document.createElement('div');
                elc.className = 'pad-cell-label-center';
                elc.style.fontSize = '11px';
                elc.textContent = padSimplifyBindings(btn.label_center);
                cell.appendChild(elc);
            }
        }
    } else if (!btn.bg_image_url) {
        const centerText = btn.label_center || '\u2022';
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
        cell.appendChild(document.createElement('div'));
    }
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
    var posGroup = document.getElementById('pad-edit-icon-position-group');
    if (posGroup) {
        var wt = document.getElementById('pad-edit-widget-type');
        var widgetOverrides = wt && (wt.value === 'gauge' || wt.value === 'bar_chart' || wt.value === 'sparkline');
        posGroup.style.display = (type && !widgetOverrides) ? '' : 'none';
    }
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

async function padGetButtonSizes(cols, rows) {
    if (padButtonSizesCache &&
        padButtonSizesCache.cols === cols &&
        padButtonSizesCache.rows === rows) {
        return padButtonSizesCache;
    }
    const resp = await fetch('/api/pad/button_sizes?cols=' + cols + '&rows=' + rows);
    if (!resp.ok) throw new Error('Failed to get button sizes');
    const data = await resp.json();
    data.cols = cols;
    data.rows = rows;
    padButtonSizesCache = data;
    return data;
}

async function padUploadPageIcons(context) {
    // Always delete old page icons (cleans up removed icons)
    const deleteResponse = await fetch('/api/icons/page?page=' + context.page, { method: 'DELETE' });
    if (!deleteResponse.ok) throw new Error('Failed to delete existing icons: HTTP ' + deleteResponse.status);

    const iconButtons = context.buttons.filter(b => b.icon_id);
    if (iconButtons.length === 0) return;

    const btnSizes = await padGetButtonSizes(context.cols, context.rows);
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
        const key = 'pad_' + context.page + '_' + btn.col + '_' + btn.row;

        const resp = await fetch('/api/icons/install?id=' + encodeURIComponent(key) + '&kind=' + kind, {
            method: 'POST',
            headers: { 'Content-Type': 'image/png' },
            body: pngBlob,
        });
        if (!resp.ok) {
            throw new Error('Icon upload failed for ' + key + ': HTTP ' + resp.status);
        }
    }
}
