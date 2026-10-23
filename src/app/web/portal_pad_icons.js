// portal_pad_icons.js - Icon rendering, canvas, Material Symbols, cell content, and icon upload
// Part of the ESP32 Macropad configuration portal.
// Bundled into portal_pad_editor.js during minification.

// --- Icon Support ---

let padButtonSizesCache = null;

// Lazy-load Material Symbols font
function padEnsureMaterialSymbols() {
    return portalEnsureMaterialSymbols();
}

const PAD_BINDING_PREVIEW_MAX_CHARS = 28;

function padBindingPreviewAbbreviation(token) {
    if (token.scheme === 'expr') return '[expr:...]';
    const split = bindingSplitFallback(token.params);
    const params = bindingSplitParams(split.params);
    const source = (params[0] || '').replace(/[\r\n]+/g, ' ');
    const prefix = '[' + token.scheme + ':';
    const closing = token.unclosed ? '' : ']';
    const full = prefix + source + closing;
    if (full.length <= PAD_BINDING_PREVIEW_MAX_CHARS) return full;

    const suffixLength = Math.max(0, PAD_BINDING_PREVIEW_MAX_CHARS - prefix.length - closing.length - 3);
    return prefix + '...' + source.slice(-suffixLength) + closing;
}

function padAppendBindingPreview(container, text) {
    if (!text || typeof bindingTokenize !== 'function' || typeof bindingSplitFallback !== 'function' ||
        typeof bindingSplitParams !== 'function') {
        container.textContent = text || '';
        return;
    }
    const tokens = bindingTokenize(text);
    if (!tokens.length) {
        container.textContent = text;
        return;
    }

    let cursor = 0;
    const appendText = value => {
        if (value) container.appendChild(document.createTextNode(value));
    };
    tokens.forEach(token => {
        appendText(text.substring(cursor, token.start));
        const abbreviation = document.createElement('span');
        abbreviation.className = 'pad-cell-binding';
        abbreviation.textContent = padBindingPreviewAbbreviation(token);
        abbreviation.title = token.raw;
        abbreviation.setAttribute('aria-hidden', 'true');
        container.appendChild(abbreviation);
        cursor = token.end;
    });
    appendText(text.substring(cursor));
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

function padWidgetPreview(btn) {
    if (!btn.widget_type) return null;
    const catalog = deviceInfoCache && deviceInfoCache.widget_catalog;
    const metadata = Array.isArray(catalog) && catalog.find(entry => entry.type === btn.widget_type);
    const preview = { ...(metadata || { name: 'Unknown Widget', icon: 'extension' }) };
    if (preview.axis_field) {
        const axis = btn[preview.axis_field] || preview.default_axis;
        preview.icon = (axis === 'horizontal' ? preview.horizontal_icon : preview.vertical_icon) || preview.icon;
        preview.vertical = !!preview.second_icon && axis === 'vertical';
    }
    return preview;
}

function padWidgetPreviewSymbol(preview) {
    const symbol = document.createElement('span');
    symbol.className = 'pad-widget-symbol' + (preview.vertical ? ' pad-widget-symbol-vertical' : '');
    symbol.setAttribute('aria-hidden', 'true');
    for (const icon of [preview.icon, preview.second_icon].filter(Boolean)) {
        const glyph = document.createElement('span');
        glyph.className = 'pad-widget-glyph';
        glyph.textContent = icon;
        symbol.appendChild(glyph);
    }
    return symbol;
}

// Shared cell content renderer for normal and ghost buttons.
// Sets colors, border, labels, icon, and bg-image placeholder on the cell div.
function padRenderCellContent(cell, btn) {
    const accessibleLabels = [btn.label_top, btn.label_center, btn.label_bottom].filter(Boolean).join('\n');
    if (accessibleLabels) cell.setAttribute('aria-description', accessibleLabels);
    const bg = padColorToHex(btn.bg_color, padGetEffectiveDefault('bg_color'));
    const fg = padColorToHex(btn.fg_color, padGetEffectiveDefault('fg_color'));
    cell.style.background = bg;
    cell.style.color = fg;
    const channels = [1, 3, 5].map(offset => {
        const channel = parseInt(bg.slice(offset, offset + 2), 16) / 255;
        return channel <= 0.04045 ? channel / 12.92 : Math.pow((channel + 0.055) / 1.055, 2.4);
    });
    const luminance = 0.2126 * channels[0] + 0.7152 * channels[1] + 0.0722 * channels[2];
    cell.style.setProperty('--pad-resize-grip-color', luminance > 0.5 ? '#17212b' : '#ffffff');
    const borderColor = padColorToHex(btn.border_color, padGetEffectiveDefault('border_color'));
    const borderWidth = (btn.border_width !== undefined) ? btn.border_width : padGetEffectiveDefault('border_width');
    const cornerRadius = (btn.corner_radius !== undefined) ? btn.corner_radius : padGetEffectiveDefault('corner_radius');
    cell.style.setProperty('--pad-cell-border-color', borderColor);
    cell.style.border = borderWidth + 'px solid ' + borderColor;
    cell.style.borderRadius = cornerRadius + 'px';
    // Content padding — mirror the device inset so the preview stays WYSIWYG
    var contentPad = (btn.content_pad !== undefined) ? btn.content_pad : padGetEffectiveDefault('content_pad');
    var cpNum = parseInt(contentPad, 10);
    if (!isNaN(cpNum)) {
        if (cpNum < 0) cpNum = 0; else if (cpNum > 50) cpNum = 50;
        cell.style.padding = cpNum + 'px';
    }
    const widget = padWidgetPreview(btn);
    if (widget) {
        cell.classList.add('pad-cell-has-widget');
        const marker = document.createElement('span');
        marker.className = 'pad-widget-marker';
        marker.title = widget.name + ' Widget';
        marker.setAttribute('aria-label', marker.title);
        marker.appendChild(padWidgetPreviewSymbol(widget));
        cell.appendChild(marker);
    }
    const hasTop = !!btn.label_top;
    const hasBottom = !!btn.label_bottom;
    if (hasTop || hasBottom) cell.style.justifyContent = 'space-between';
    if (hasTop) {
        const el = document.createElement('div');
        el.className = 'pad-cell-label-top';
        padAppendBindingPreview(el, btn.label_top);
        cell.appendChild(el);
    } else if (hasBottom) {
        cell.appendChild(document.createElement('div'));
    }
    if (!widget && (btn.bg_image_url || btn.bg_image_path)) {
        const img = document.createElement('div');
        img.className = 'pad-cell-image-placeholder';
        img.textContent = '\u{1F5BC}';
        cell.appendChild(img);
    }
    if (btn.icon_id && !widget) {
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
            padAppendBindingPreview(lbl, btn.label_center);
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
                padAppendBindingPreview(elc, btn.label_center);
                cell.appendChild(elc);
            }
        }
    } else if (!btn.bg_image_url && !btn.bg_image_path) {
        const centerText = btn.label_center || (widget ? '' : '\u2022');
        if (centerText) {
            const elc = document.createElement('div');
            elc.className = 'pad-cell-label-center';
            padAppendBindingPreview(elc, centerText);
            cell.appendChild(elc);
        }
    } else if (btn.label_center) {
        const elc = document.createElement('div');
        elc.className = 'pad-cell-label-center';
        padAppendBindingPreview(elc, btn.label_center);
        cell.appendChild(elc);
    }
    if (hasBottom) {
        const el = document.createElement('div');
        el.className = 'pad-cell-label-bottom';
        padAppendBindingPreview(el, btn.label_bottom);
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
        const editor = wt && padWidgetEditor(wt.value);
        var widgetOverrides = wt && editor && editor.icon && editor.icon.fixedPosition;
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

function padComputeAxisTrackSizes(count, safeSize, gap, overrides) {
    safeSize = Number(safeSize) || 0;
    gap = Number(gap) || 0;
    const totalGap = Math.max(0, count - 1) * gap;
    const available = Math.max(0, safeSize - totalGap);
    const sizes = Array(count).fill(0);
    const fixed = Array.from({ length: count }, (_, index) => Number(overrides && overrides[index]) || 0);
    const autoCount = fixed.filter(size => size === 0).length;
    const fixedTotal = fixed.reduce((total, size) => total + size, 0);
    const hasOverrides = fixedTotal > 0;

    if (hasOverrides && (!autoCount || fixedTotal + autoCount > available)) {
        sizes.fill(count ? Math.floor(available / count) : 0);
        return { sizes, valid: false };
    }

    if (!hasOverrides) {
        sizes.fill(count ? Math.floor(available / count) : 0);
        return { sizes, valid: true };
    }

    let remainder = (available - fixedTotal) % autoCount;
    const autoSize = Math.floor((available - fixedTotal) / autoCount);
    for (let index = 0; index < count; index++) {
        if (fixed[index]) sizes[index] = fixed[index];
        else {
            sizes[index] = autoSize + (remainder ? 1 : 0);
            if (remainder) remainder--;
        }
    }
    return { sizes, valid: true };
}

function padGetTrackAvailableSize(axis, dimension, pixelShift) {
    dimension = Number(dimension) || 0;
    const defaults = key => typeof padGetEffectiveDefault === 'function'
        ? Number(padGetEffectiveDefault(key)) || 0 : 0;
    const horizontal = axis === 'columns';
    const leading = defaults(horizontal ? 'pad_inset_left_px' : 'pad_inset_top_px');
    const trailing = defaults(horizontal ? 'pad_inset_right_px' : 'pad_inset_bottom_px');
    const shift = Number(pixelShift !== undefined ? pixelShift : defaults('pixel_shift_distance_px')) || 0;
    return Math.max(0, dimension - 2 * shift - leading - trailing);
}

function padTrackSpanSize(sizes, start, span, gap) {
    let total = 0;
    for (let offset = 0; offset < span; offset++) {
        const index = start + offset;
        total += sizes[index] !== undefined ? sizes[index] : (sizes[sizes.length - 1] || 0);
        if (offset + 1 < span) total += gap;
    }
    return total;
}

async function padUploadPageIcons(context) {
    // Always delete old page icons (cleans up removed icons)
    const deleteResponse = await fetch('/api/icons/page?page=' + context.page, { method: 'DELETE' });
    if (!deleteResponse.ok) throw new Error('Failed to delete existing icons: HTTP ' + deleteResponse.status);

    const iconButtons = (context.icons || context.buttons.map(button => Object.assign({}, button, { layer: 'main' })))
        .filter(button => button.icon_id);
    if (iconButtons.length === 0) return;

    const btnSizes = await padGetButtonSizes(context.cols, context.rows);
    const gap = Number(btnSizes.gap) || 0;
    const displayW = Number(btnSizes.display_w) ||
        (Number(btnSizes.button_w) || 0) * context.cols + gap * (context.cols - 1);
    const displayH = Number(btnSizes.display_h) ||
        (Number(btnSizes.button_h) || 0) * context.rows + gap * (context.rows - 1);
    const columnSizing = padComputeAxisTrackSizes(context.cols,
        padGetTrackAvailableSize('columns', displayW, btnSizes.pixel_shift_margin),
        gap, context.column_widths_px);
    const rowSizing = padComputeAxisTrackSizes(context.rows,
        padGetTrackAvailableSize('rows', displayH, btnSizes.pixel_shift_margin),
        gap, context.row_heights_px);
    if (!columnSizing.valid || !rowSizing.valid) throw new Error('Pad track sizes do not fit the display');
    const columnTracks = columnSizing.sizes;
    const rowTracks = rowSizing.sizes;
    const maxIconDimension = Number(deviceInfoCache && deviceInfoCache.icon_max_dimension) || 720;

    const needsMi = iconButtons.some(b => b.icon_id.startsWith('mi_'));
    if (needsMi) await padEnsureMaterialSymbols();

    const canvas = document.createElement('canvas');

    for (const btn of iconButtons) {
        const cs = btn.col_span || 1;
        const rs = btn.row_span || 1;
        // LVGL content area = button - 2*padding - 2*border_width
        const bw = (btn.border_width !== undefined) ? btn.border_width : 1;
        const fullW = padTrackSpanSize(columnTracks, btn.col, cs, gap) - btnSizes.padding * 2 - bw * 2;
        const fullH = padTrackSpanSize(rowTracks, btn.row, rs, gap) - btnSizes.padding * 2 - bw * 2;
        const kind = btn.icon_id.startsWith('mi_') ? 1 : 0;

        // Reserve space for top/bottom labels (font_small_h each)
        const labelH = btnSizes.font_small_h || 0;
        const topReserve = btn.label_top ? labelH : 0;
        const bottomReserve = btn.label_bottom ? labelH : 0;
        var iconW = fullW;
        var iconH = fullH - topReserve - bottomReserve;

        const editor = padWidgetEditor(btn.widget_type);
        if (editor && editor.icon) {
            iconW = Math.floor(iconW * editor.icon.width);
            iconH = Math.floor(iconH * editor.icon.height);
        }

        // Apply explicit icon_scale_pct if set (1-250%)
        if (btn.icon_scale_pct && btn.icon_scale_pct > 0) {
            iconW = Math.max(1, Math.round(iconW * btn.icon_scale_pct / 100));
            iconH = Math.max(1, Math.round(iconH * btn.icon_scale_pct / 100));
        }

        // Make icon square — glyph is sized to min dimension anyway,
        // avoids transparent padding in the taller axis
        const iconSize = Math.max(1, Math.floor(Math.min(iconW, iconH, maxIconDimension)));
        iconW = iconSize;
        iconH = iconW;

        padRenderIconOnCanvas(canvas, btn.icon_id, iconW, iconH);
        const pngBlob = await padCanvasToPNG(canvas);
        const layer = btn.layer === 'overlay' ? 'overlay' : 'main';
        const key = 'pad_' + context.page + '_' + layer + '_' + btn.col + '_' + btn.row;

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
