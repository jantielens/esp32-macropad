// portal_pad_icons.js - Icon source preparation, cell previews, and icon upload
// Part of the ESP32 Macropad configuration portal.
// Bundled into portal_pad_editor.js during minification.

// --- Icon Support ---

let padButtonSizesCache = null;

let padIconPreviewGeneration = 0;
let padIconPreviewObjectUrl = null;

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

function padIconEditorReadDraft() {
    const input = document.getElementById('pad-edit-icon-json');
    if (!input || !input.value) return null;
    try { return JSON.parse(input.value); } catch (error) { return null; }
}

function padIconEditorSetDraft(icon) {
    const input = document.getElementById('pad-edit-icon-json');
    if (input) input.value = icon ? JSON.stringify(icon) : '';
    padIconEditorRefresh();
}

function padIconEditorColors() {
    const background = padColorToHex(padGetBindableColor('pad-edit-bg-color'), padGetEffectiveDefault('bg_color'));
    const foreground = padColorToHex(padGetBindableColor('pad-edit-fg-color'), padGetEffectiveDefault('fg_color'));
    return { background: background || '#ffffff', foreground: foreground || '#222222' };
}

function padIconEditorRefresh() {
    const image = document.getElementById('pad-edit-icon-image');
    const name = document.getElementById('pad-edit-icon-name');
    const remove = document.getElementById('pad-edit-icon-remove');
    const position = document.getElementById('pad-edit-icon-position-group');
    const icon = padIconEditorReadDraft();
    if (name) name.textContent = icon
        ? icon.catalog_id ? icon.catalog_id.split(':').pop().replace(/-/g, ' ') : 'Local image'
        : 'Choose icon';
    if (remove) remove.disabled = !icon;
    if (position) {
        const widgetType = document.getElementById('pad-edit-widget-type');
        const editor = widgetType && padWidgetEditor(widgetType.value);
        position.style.display = icon && !(editor && editor.icon && editor.icon.fixedPosition) ? '' : 'none';
    }
    const foreground = padIconEditorColors().foreground;
    const signature = JSON.stringify([icon, foreground]);
    if (image && image._padIconSignature === signature) return;
    if (image) image._padIconSignature = signature;
    const generation = ++padIconPreviewGeneration;
    if (padIconPreviewObjectUrl) URL.revokeObjectURL(padIconPreviewObjectUrl);
    padIconPreviewObjectUrl = null;
    if (image) { image.removeAttribute('src'); image.hidden = !icon; }
    if (!icon || !image) return;
    padResolveIconSource(icon, {}).then(record => padRenderIconPNG(record, 40, foreground))
        .then(blob => {
            if (generation !== padIconPreviewGeneration) return;
            padIconPreviewObjectUrl = URL.createObjectURL(blob);
            image.src = padIconPreviewObjectUrl;
            image.hidden = false;
        }).catch(() => {
            if (generation !== padIconPreviewGeneration) return;
            image._padIconSignature = null;
            if (name) name.textContent += ' (unavailable)';
        });
}

function padAppendIconPreview(container, icon, foreground) {
    const image = document.createElement('img');
    image.className = 'pad-cell-icon';
    image.alt = '';
    image.setAttribute('aria-hidden', 'true');
    container.appendChild(image);
    padResolveIconSource(icon, {}).then(record => padRenderIconPNG(record, 48, foreground)).then(blob => {
        if (image.isConnected === false) return;
        const url = URL.createObjectURL(blob);
        image.onload = image.onerror = () => URL.revokeObjectURL(url);
        image.src = url;
    }).catch(() => {});
    return image;
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
const PAD_CELL_PREVIEW_PROPERTIES = [
    'label_top', 'label_center', 'label_bottom', 'bg_color', 'fg_color',
    'border_color', 'border_width', 'corner_radius', 'content_pad',
    'icon', 'icon_position', 'bg_image_url', 'bg_image_path', 'widget_type'
];

function padCellPreviewSignature(button) {
    return JSON.stringify([PAD_CELL_PREVIEW_PROPERTIES.map(key => button[key]), padWidgetPreview(button)]);
}

function padPreviewLuminance(color) {
    const channels = [1, 3, 5].map(offset => {
        const channel = parseInt(color.slice(offset, offset + 2), 16) / 255;
        return channel <= 0.04045 ? channel / 12.92 : Math.pow((channel + 0.055) / 1.055, 2.4);
    });
    return 0.2126 * channels[0] + 0.7152 * channels[1] + 0.0722 * channels[2];
}

function padUpdateCellBoundaryGuide(cell) {
    const input = document.getElementById('pad-edit-page-bg-color');
    const background = input && /^#[0-9a-fA-F]{6}$/.test(input.value.trim())
        ? input.value.trim() : padGetEffectiveDefault('default_pad_bg_color');
    const padLuminance = padPreviewLuminance(background);
    const blendsIn = [cell.dataset.previewEdgeColor, cell.dataset.previewFillColor].every(color => {
        const luminance = padPreviewLuminance(color);
        return (Math.max(padLuminance, luminance) + 0.05) /
            (Math.min(padLuminance, luminance) + 0.05) < 1.3;
    });
    cell.classList.remove('pad-cell-boundary-guide');
    if (blendsIn) cell.classList.add('pad-cell-boundary-guide');
    cell.style.setProperty('--pad-boundary-guide-color', padLuminance > 0.5 ? '#00000055' : '#ffffff55');
}

function padRenderCellContent(cell, btn, existingIcon) {
    const accessibleLabels = [btn.label_top, btn.label_center, btn.label_bottom].filter(Boolean).join('\n');
    if (accessibleLabels) cell.setAttribute('aria-description', accessibleLabels);
    const bg = padColorToHex(btn.bg_color, padGetEffectiveDefault('bg_color'));
    const fg = padColorToHex(btn.fg_color, padGetEffectiveDefault('fg_color'));
    cell.style.background = bg;
    cell.style.color = fg;
    const luminance = padPreviewLuminance(bg);
    cell.style.setProperty('--pad-resize-grip-color', luminance > 0.5 ? '#17212b' : '#ffffff');
    const borderColor = padColorToHex(btn.border_color, padGetEffectiveDefault('border_color'));
    const borderWidth = (btn.border_width !== undefined) ? btn.border_width : padGetEffectiveDefault('border_width');
    const cornerRadius = (btn.corner_radius !== undefined) ? btn.corner_radius : padGetEffectiveDefault('corner_radius');
    cell.style.setProperty('--pad-cell-border-color', borderColor);
    cell.style.border = borderWidth + 'px solid ' + borderColor;
    cell.style.borderRadius = cornerRadius + 'px';
    cell.dataset.previewEdgeColor = Number(borderWidth) > 0 ? borderColor : bg;
    cell.dataset.previewFillColor = bg;
    padUpdateCellBoundaryGuide(cell);
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
    if (btn.icon && !widget) {
        const hasCenter = !!btn.label_center;
        const pos = btn.icon_position || 'above';
        if (hasCenter && pos === 'left') {
            const row = document.createElement('div');
            row.className = 'pad-cell-icon-row';
            if (existingIcon) row.appendChild(existingIcon);
            else padAppendIconPreview(row, btn.icon, fg);
            const lbl = document.createElement('span');
            lbl.className = 'pad-cell-label-center';
            lbl.style.fontSize = '12px';
            padAppendBindingPreview(lbl, btn.label_center);
            row.appendChild(lbl);
            cell.appendChild(row);
        } else {
            const iconSlot = document.createElement('div');
            iconSlot.className = 'pad-cell-icon-slot';
            if (existingIcon) iconSlot.appendChild(existingIcon);
            else padAppendIconPreview(iconSlot, btn.icon, fg);
            cell.appendChild(iconSlot);
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

function padGetTrackAvailableSize(axis, dimension, pixelShift, layout) {
    dimension = Number(dimension) || 0;
    const defaults = key => layout && Object.prototype.hasOwnProperty.call(layout, key)
        ? Number(layout[key]) || 0
        : typeof padGetEffectiveDefault === 'function' ? Number(padGetEffectiveDefault(key)) || 0 : 0;
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

const padIconSourceCache = new Map();
const padDraftIconSources = new Map();

function padRegisterDraftIconSource(record) {
    const normalized = padNormalizeIconSource(record, record);
    padDraftIconSources.set(normalized.catalog_id, normalized);
    return normalized;
}

function padCreateOpaqueIconId(prefix, exists) {
    let id;
    do {
        const bytes = new Uint8Array(12);
        if (window.crypto && window.crypto.getRandomValues) window.crypto.getRandomValues(bytes);
        else bytes.forEach((_, index) => { bytes[index] = Math.floor(Math.random() * 256); });
        id = prefix + Array.from(bytes, value => value.toString(16).padStart(2, '0')).join('');
    } while (exists(id));
    return id;
}

function padRegisterLocalDraftIconSource(record) {
    const normalized = padNormalizeIconSource(record, { kind: 'color' });
    if (!padState.draftIconSources) padState.draftIconSources = {};
    const sourceId = padCreateOpaqueIconId('d_', id => Object.prototype.hasOwnProperty.call(padState.draftIconSources, id));
    padState.draftIconSources[sourceId] = normalized;
    return { draft_source_id: sourceId, kind: 'color' };
}

function padDraftIconSourceReferences() {
    const references = new Set();
    const addButton = button => {
        if (button && button.icon && button.icon.draft_source_id)
            references.add(button.icon.draft_source_id);
    };
    for (const button of [...(padState.mainButtons || []), ...(padState.overlayButtons || []),
        ...(padState.templateButtons || [])]) addButton(button);
    addButton(padState.btnClipboard);
    if (padState.padClipboard) {
        for (const button of [...(padState.padClipboard.buttons || []),
            ...(padState.padClipboard.overlay_buttons || [])]) addButton(button);
    }
    return references;
}

function padPruneDraftIconSources() {
    if (!padState.draftIconSources) return;
    const references = padDraftIconSourceReferences();
    for (const sourceId of Object.keys(padState.draftIconSources)) {
        if (!references.has(sourceId)) delete padState.draftIconSources[sourceId];
    }
}

function padValidatePngSourceData(data) {
    const pngLimit = Number(deviceInfoCache && deviceInfoCache.icon_max_png_size) || 512 * 1024;
    if (typeof data !== 'string' || !data.length || data.length % 4 !== 0 ||
        !/^(?:[A-Za-z0-9+/]{4})*(?:[A-Za-z0-9+/]{2}==|[A-Za-z0-9+/]{3}=)?$/.test(data))
        throw new Error('Local icon source must contain canonical base64 PNG data.');
    const padding = data.endsWith('==') ? 2 : data.endsWith('=') ? 1 : 0;
    const byteLength = data.length / 4 * 3 - padding;
    if (byteLength < 33 || byteLength > pngLimit)
        throw new Error('Local icon source PNG must be between 33 bytes and 512 KiB.');

    const alphabet = 'ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/';
    const header = new Uint8Array(24);
    let output = 0;
    for (let offset = 0; offset < data.length; offset += 4) {
        const first = alphabet.indexOf(data[offset]);
        const second = alphabet.indexOf(data[offset + 1]);
        const third = data[offset + 2] === '=' ? 0 : alphabet.indexOf(data[offset + 2]);
        const fourth = data[offset + 3] === '=' ? 0 : alphabet.indexOf(data[offset + 3]);
        if (first < 0 || second < 0 || third < 0 || fourth < 0 ||
            (data[offset + 2] === '=' && (second & 15) !== 0) ||
            (data[offset + 3] === '=' && data[offset + 2] !== '=' && (third & 3) !== 0))
            throw new Error('Local icon source base64 is invalid.');
        for (const value of [(first << 2) | (second >> 4),
            ((second & 15) << 4) | (third >> 2), ((third & 3) << 6) | fourth]) {
            if (output < byteLength && output < header.length) header[output] = value;
            output++;
        }
    }
    if (header[0] !== 137 || header[1] !== 80 || header[2] !== 78 || header[3] !== 71 ||
        header[4] !== 13 || header[5] !== 10 || header[6] !== 26 || header[7] !== 10 ||
        header[8] !== 0 || header[9] !== 0 || header[10] !== 0 || header[11] !== 13 ||
        header[12] !== 73 || header[13] !== 72 || header[14] !== 68 || header[15] !== 82)
        throw new Error('Local icon source does not have a valid PNG header.');
    const width = ((header[16] * 0x1000000) + (header[17] << 16) + (header[18] << 8) + header[19]) >>> 0;
    const height = ((header[20] * 0x1000000) + (header[21] << 16) + (header[22] << 8) + header[23]) >>> 0;
    if (!width || !height || width > 720 || height > 720)
        throw new Error('Local icon source dimensions must be between 1 and 720 pixels.');
    return byteLength;
}

function padNormalizeIconSource(record, expected) {
    if (record && record.media_type === 'image/png') {
        if (!expected || expected.kind !== 'color' || expected.catalog_id !== undefined ||
            record.kind !== 'color' || Object.keys(record).sort().join(',') !== 'data,kind,media_type')
            throw new Error('Local PNG icon metadata is invalid or mismatched.');
        padValidatePngSourceData(record.data);
        const normalized = { kind: 'color', media_type: 'image/png', data: record.data };
        const sourceLimit = Number(deviceInfoCache && deviceInfoCache.icon_max_source_json_size) || 704 * 1024;
        if (new TextEncoder().encode(JSON.stringify(normalized)).length > sourceLimit)
            throw new Error('Local icon source exceeds the 704 KiB source limit.');
        return normalized;
    }
    if (!record || record.media_type !== 'image/svg+xml' || typeof record.data !== 'string')
        throw new Error('Icon source must contain SVG artwork.');
    const catalogId = expected.catalog_id;
    const kind = expected.kind;
    const match = typeof catalogId === 'string' && catalogId.match(/^(material-symbols|noto):([a-z0-9][a-z0-9-]*)$/);
    if (!match || catalogId.length > 128 ||
        kind !== (match[1] === 'material-symbols' ? 'mono' : 'color') ||
        record.catalog_id !== catalogId || record.kind !== kind)
        throw new Error('Icon catalog metadata is invalid or mismatched.');
    if (/<!DOCTYPE|<!ENTITY/i.test(record.data)) throw new Error('Icon SVG declarations are not allowed.');

    const parsed = new DOMParser().parseFromString(record.data, 'image/svg+xml');
    const root = parsed.documentElement;
    if (!root || root.localName !== 'svg' || parsed.querySelector('parsererror'))
        throw new Error('Icon artwork is not valid SVG.');

    const viewBox = (root.getAttribute('viewBox') || '').trim().split(/[\s,]+/).map(Number);
    let bounds;
    if (viewBox.length === 4 && viewBox.every(Number.isFinite)) bounds = viewBox;
    else {
        const width = Number(root.getAttribute('width'));
        const height = Number(root.getAttribute('height'));
        if (!Number.isFinite(width) || !Number.isFinite(height))
            throw new Error('Icon SVG needs a finite viewBox.');
        bounds = [0, 0, width, height];
    }
    if (bounds[2] <= 0 || bounds[3] <= 0 || bounds[2] > 4096 || bounds[3] > 4096)
        throw new Error('Icon SVG dimensions must be between 1 and 4096.');

    const allowedElements = new Set([
        'svg', 'g', 'path', 'rect', 'circle', 'ellipse', 'line', 'polyline', 'polygon',
        'defs', 'linearGradient', 'radialGradient', 'stop', 'clipPath', 'mask', 'use',
    ]);
    const allowedAttributes = new Set([
        'id', 'xmlns', 'viewBox', 'width', 'height', 'x', 'y', 'x1', 'x2', 'y1', 'y2',
        'cx', 'cy', 'r', 'rx', 'ry', 'd', 'points', 'fill', 'fill-rule', 'clip-rule',
        'stroke', 'stroke-width', 'stroke-linecap', 'stroke-linejoin', 'stroke-miterlimit',
        'opacity', 'fill-opacity', 'stroke-opacity', 'transform', 'offset', 'stop-color',
        'stop-opacity', 'gradientUnits', 'gradientTransform', 'spreadMethod', 'clip-path',
        'mask', 'href', 'xlink:href', 'preserveAspectRatio', 'color',
    ]);
    const nodes = [root, ...root.querySelectorAll('*')];
    for (const node of nodes) {
        if (!allowedElements.has(node.localName)) throw new Error('Icon SVG contains unsupported artwork.');
        if (node.namespaceURI && node.namespaceURI !== 'http://www.w3.org/2000/svg')
            throw new Error('Icon SVG contains a non-SVG namespace.');
        if (Array.from(node.childNodes).some(child => child.nodeType === 3 && child.textContent.trim()))
            throw new Error('Icon SVG text nodes are not allowed.');
        for (const attribute of Array.from(node.attributes)) {
            const name = attribute.name;
            const value = attribute.value.trim();
            if (/^on/i.test(name) || name === 'style') throw new Error('Icon SVG contains an active attribute.');
            if (name === 'href' || name === 'xlink:href') {
                if (!/^#[A-Za-z_][\w:.-]*$/.test(value)) throw new Error('Icon SVG references must be local.');
            }
            const urls = value.match(/url\([^)]*\)/gi) || [];
            if (urls.some(url => !/^url\(\s*['"]?#[A-Za-z_][\w:.-]*['"]?\s*\)$/i.test(url)))
                throw new Error('Icon SVG contains an external resource reference.');
            if (!allowedAttributes.has(name)) node.removeAttribute(name);
            else if (value === 'currentColor')
                node.setAttribute(name, kind === 'mono' ? '#ffffff' : '#000000');
        }
    }

    root.setAttribute('xmlns', 'http://www.w3.org/2000/svg');
    root.setAttribute('viewBox', bounds.join(' '));
    root.setAttribute('width', String(bounds[2]));
    root.setAttribute('height', String(bounds[3]));
    root.setAttribute('preserveAspectRatio', 'xMidYMid meet');
    if (kind === 'mono') {
        root.setAttribute('fill', '#ffffff');
        root.setAttribute('color', '#ffffff');
    }
    const normalized = {
        catalog_id: catalogId,
        kind,
        media_type: 'image/svg+xml',
        data: new XMLSerializer().serializeToString(root),
    };
    const sourceLimit = Math.min(64 * 1024,
        Number(deviceInfoCache && deviceInfoCache.icon_max_source_json_size) || 64 * 1024);
    if (new TextEncoder().encode(JSON.stringify(normalized)).length > sourceLimit)
        throw new Error('Icon artwork exceeds the 64 KiB source limit.');
    return normalized;
}

function padValidateImportedIconSources(pads, supplied) {
    if (supplied !== undefined && (!supplied || typeof supplied !== 'object' || Array.isArray(supplied)))
        throw new Error('Imported icon sources must be an object keyed by source ID.');
    const referenced = new Map();
    let buttonCount = 0;
    for (const pad of pads) {
        if (!pad) continue;
        for (const buttons of [pad.buttons || [], pad.overlay_buttons || []]) {
            if (!Array.isArray(buttons)) throw new Error('Imported pad button lists must be arrays.');
            for (const button of buttons) {
                buttonCount++;
                if (!button || typeof button !== 'object' || Array.isArray(button)) continue;
                if (Object.prototype.hasOwnProperty.call(button, 'icon_id'))
                    throw new Error('The previous icon format cannot be imported.');
                const icon = button.icon;
                if (!icon) continue;
                if (typeof icon !== 'object' || Array.isArray(icon)) throw new Error('Imported icon metadata is invalid.');
                if (icon.source_id !== undefined) {
                    if (!/^s_[0-9a-f]{24}$/.test(icon.source_id)) throw new Error('Imported icon source ID is invalid.');
                    const previous = referenced.get(icon.source_id);
                    if (previous && (previous.catalog_id !== icon.catalog_id || previous.kind !== icon.kind))
                        throw new Error('An imported source ID has conflicting icon metadata.');
                    referenced.set(icon.source_id, icon);
                }
            }
        }
    }
    if (referenced.size > buttonCount) throw new Error('Imported icon source count exceeds the button count.');
    const normalizedSources = {};
    const sourceJsonLimit = Number(deviceInfoCache && deviceInfoCache.icon_max_source_json_size) || 704 * 1024;
    for (const [sourceId, icon] of referenced) {
        if (!Object.prototype.hasOwnProperty.call(supplied || {}, sourceId))
            throw new Error('Imported icon source ' + sourceId + ' is missing.');
        const record = supplied[sourceId];
        const sourceLimit = record && record.media_type === 'image/png'
            ? sourceJsonLimit : Math.min(64 * 1024, sourceJsonLimit);
        if (new TextEncoder().encode(JSON.stringify(record)).length > sourceLimit)
            throw new Error(record && record.media_type === 'image/png'
                ? 'Imported local icon source exceeds the configured source JSON limit.'
                : 'Imported catalog icon source exceeds the 64 KiB limit.');
        normalizedSources[sourceId] = padNormalizeIconSource(record, icon);
    }
    return normalizedSources;
}

async function padCollectExportIconSources(pads, rootPages, maxPads, draftContext) {
    const loaded = new Map(pads.map(entry => [entry.page, entry.payload]));
    const pending = rootPages.slice();
    const visited = new Set();
    const references = new Map();
    const draftReferences = new Map();
    const draftSources = draftContext && draftContext.byDraftId || padState.draftIconSources || {};
    const suppliedSources = draftContext && draftContext.bySourceId || {};
    let buttonCount = 0;
    while (pending.length) {
        const page = pending.pop();
        if (!Number.isInteger(page) || page < 0 || page >= maxPads)
            throw new Error('A pad template reference is outside the device pad range.');
        if (visited.has(page)) continue;
        visited.add(page);
        let payload = loaded.get(page);
        if (!payload) {
            const response = await fetch('/api/pad?page=' + page);
            if (!response.ok) throw new Error('Failed to read template Pad ' + (page + 1) + ': HTTP ' + response.status);
            payload = await response.json();
            loaded.set(page, payload);
        }
        if (!payload || typeof payload !== 'object') throw new Error('Template Pad ' + (page + 1) + ' is unavailable.');
        for (const buttons of [payload.buttons || [], payload.overlay_buttons || []]) {
            if (!Array.isArray(buttons)) throw new Error('Pad export contains an invalid button list.');
            for (const button of buttons) {
                buttonCount++;
                if (Object.prototype.hasOwnProperty.call(button, 'icon_id'))
                    throw new Error('The previous icon format cannot be exported.');
                const icon = button.icon;
                if (!icon) continue;
                if (icon.draft_source_id) {
                    if (!/^d_[0-9a-f]{24}$/.test(icon.draft_source_id) || icon.source_id ||
                        icon.catalog_id !== undefined)
                        throw new Error('Pad export contains invalid local draft icon metadata.');
                    let draft = draftReferences.get(icon.draft_source_id);
                    const source = draftSources[icon.draft_source_id];
                    if (!source) throw new Error('Local icon source ' + icon.draft_source_id + ' is missing.');
                    const record = padNormalizeIconSource(source, icon);
                    if (draft && draft.record.data !== record.data)
                        throw new Error('A local draft icon source has conflicting artwork.');
                    if (!draft) {
                        draft = { record, icons: [] };
                        draftReferences.set(icon.draft_source_id, draft);
                    }
                    draft.icons.push(icon);
                    continue;
                }
                if (!/^s_[0-9a-f]{24}$/.test(icon.source_id || ''))
                    throw new Error('Pad export contains an icon without a retained source.');
                const existing = references.get(icon.source_id);
                if (existing && (existing.icon.catalog_id !== icon.catalog_id || existing.icon.kind !== icon.kind))
                    throw new Error('A retained source has conflicting icon metadata.');
                if (!existing) references.set(icon.source_id, {
                    icon,
                    record: suppliedSources[icon.source_id]
                        ? padNormalizeIconSource(suppliedSources[icon.source_id], icon) : null,
                });
            }
        }
        if (payload.template_pad !== undefined && payload.template_pad !== null && Number(payload.template_pad) >= 0)
            pending.push(Number(payload.template_pad));
    }
    if (references.size + draftReferences.size > buttonCount)
        throw new Error('Exported source count exceeds the referenced button count.');

    for (const draft of draftReferences.values()) {
        const sourceId = padCreateOpaqueIconId('s_', id => references.has(id));
        for (const icon of draft.icons) {
            icon.source_id = sourceId;
            delete icon.draft_source_id;
        }
        references.set(sourceId, { icon: { kind: 'color' }, record: draft.record });
    }

    const sources = {};
    const sourceLimit = Number(deviceInfoCache && deviceInfoCache.icon_max_source_json_size) || 704 * 1024;
    for (const [sourceId, reference] of references) {
        let record = reference.record;
        if (!record) {
            const response = await fetch('/api/icons/source?id=' + encodeURIComponent(sourceId));
            if (!response.ok) throw new Error('Failed to read icon source ' + sourceId + ': HTTP ' + response.status);
            record = await padReadIconSourceResponse(response);
        }
        record = padNormalizeIconSource(record, reference.icon);
        if (new TextEncoder().encode(JSON.stringify(record)).length > sourceLimit)
            throw new Error('Icon source ' + sourceId + ' exceeds its format-specific export limit.');
        sources[sourceId] = record;
    }
    return sources;
}

async function padReadIconSourceResponse(response) {
    if ((response.headers.get('Content-Type') || '').split(';')[0].trim() !== 'image/png')
        return response.json();
    const blob = await response.blob();
    if (!blob.size || blob.size > 512 * 1024) throw new Error('Retained PNG source is empty or too large.');
    const bytes = new Uint8Array(await blob.arrayBuffer());
    let binary = '';
    for (const byte of bytes) binary += String.fromCharCode(byte);
    return { kind: 'color', media_type: 'image/png', data: btoa(binary) };
}

async function padLoadRetainedIconSource(sourceId, expected) {
    if (!/^s_[0-9a-f]{24}$/.test(sourceId)) throw new Error('Invalid retained icon source ID.');
    let pending = padIconSourceCache.get(sourceId);
    if (!pending) {
        pending = (async function () {
            const response = await fetch('/api/icons/source?id=' + encodeURIComponent(sourceId));
            if (!response.ok) throw new Error('Retained icon source is unavailable: HTTP ' + response.status);
            const record = await padReadIconSourceResponse(response);
            return padNormalizeIconSource(record, record);
        })();
        padIconSourceCache.set(sourceId, pending);
        pending.catch(function () { padIconSourceCache.delete(sourceId); });
    }
    return padNormalizeIconSource(await pending, expected);
}

function padFindDraftIconSource(context, catalogId) {
    const sources = context.iconSources && context.iconSources.byCatalogId;
    return (sources && sources[catalogId]) || padDraftIconSources.get(catalogId);
}

async function padResolveIconSource(icon, context) {
    if (!icon || Object.prototype.hasOwnProperty.call(icon, 'icon_id'))
        throw new Error('The previous icon format is no longer supported. Choose an icon again.');
    if (icon.source_id) {
        const importedSources = context.iconSources && context.iconSources.bySourceId;
        const imported = importedSources && importedSources[icon.source_id];
        if (imported) return padNormalizeIconSource(imported, icon);
        return padLoadRetainedIconSource(icon.source_id, icon);
    }
    if (icon.draft_source_id) {
        if (!/^d_[0-9a-f]{24}$/.test(icon.draft_source_id))
            throw new Error('Local draft icon source ID is invalid.');
        const draftSources = context.iconSources && context.iconSources.byDraftId;
        const record = (draftSources && draftSources[icon.draft_source_id]) ||
            (padState.draftIconSources && padState.draftIconSources[icon.draft_source_id]);
        if (!record) throw new Error('Local draft icon source is unavailable.');
        return padNormalizeIconSource(record, icon);
    }
    let record = padFindDraftIconSource(context, icon.catalog_id);
    if (!record && typeof padFetchCatalogIconSource === 'function')
        record = await padFetchCatalogIconSource(icon.catalog_id, icon.kind);
    if (!record) throw new Error('Icon artwork is not available. Select the icon again while online.');
    return padNormalizeIconSource(record, icon);
}

function padDecodeBase64Bytes(data) {
    const binary = atob(data);
    const bytes = new Uint8Array(binary.length);
    for (let index = 0; index < binary.length; index++) bytes[index] = binary.charCodeAt(index);
    return bytes;
}

async function padRenderIconPNG(source, size, foreground) {
    let svg = source.data;
    if (source.kind === 'mono' && foreground) {
        const root = new DOMParser().parseFromString(svg, 'image/svg+xml').documentElement;
        for (const node of [root, ...root.querySelectorAll('*')]) {
            for (const attribute of Array.from(node.attributes)) {
                if (attribute.value.toLowerCase() === '#ffffff') node.setAttribute(attribute.name, foreground);
            }
        }
        svg = new XMLSerializer().serializeToString(root);
    }
    const imageBlob = source.media_type === 'image/png'
        ? new Blob([padDecodeBase64Bytes(source.data)], { type: 'image/png' })
        : new Blob([svg], { type: 'image/svg+xml' });
    const imageUrl = URL.createObjectURL(imageBlob);
    const image = new Image();
    let canvas = null;
    try {
        image.src = imageUrl;
        await image.decode();
        if (!image.naturalWidth || !image.naturalHeight) throw new Error('Icon artwork has no pixels.');
        canvas = document.createElement('canvas');
        canvas.width = size;
        canvas.height = size;
        const context = canvas.getContext('2d');
        context.clearRect(0, 0, size, size);
        const factor = Math.min(size / image.naturalWidth, size / image.naturalHeight);
        const width = image.naturalWidth * factor;
        const height = image.naturalHeight * factor;
        context.drawImage(image, (size - width) / 2, (size - height) / 2, width, height);
        const blob = await new Promise((resolve, reject) => canvas.toBlob(result =>
            result ? resolve(result) : reject(new Error('PNG rendering failed.')), 'image/png'));
        const pngLimit = Number(deviceInfoCache && deviceInfoCache.icon_max_png_size) || 512 * 1024;
        if (blob.size > pngLimit) throw new Error('Generated icon exceeds the PNG size limit.');
        return blob;
    } finally {
        if (canvas) { canvas.width = 0; canvas.height = 0; }
        image.removeAttribute('src');
        URL.revokeObjectURL(imageUrl);
    }
}

function padCaptureIconLayout() {
    const keys = ['pad_inset_left_px', 'pad_inset_right_px', 'pad_inset_top_px',
        'pad_inset_bottom_px', 'pixel_shift_distance_px'];
    const layout = {};
    keys.forEach(key => { layout[key] = typeof padGetEffectiveDefault === 'function'
        ? Number(padGetEffectiveDefault(key)) || 0 : 0; });
    return layout;
}

function padIconRenderKey(payload, button) {
    const cols = Number(payload.cols ?? 3);
    const rows = Number(payload.rows ?? 3);
    const values = [cols, rows, button.col ?? 0, button.row ?? 0,
        button.col_span ?? 1, button.row_span ?? 1, button.icon_scale_pct ?? 0,
        button.label_top ? 1 : 0, button.label_bottom ? 1 : 0,
        button.border_width ?? '', button.content_pad ?? '', button.widget_type ?? ''];
    for (let index = 0; index < Math.min(cols, 8); index++) values.push(payload.column_widths_px?.[index] ?? 0);
    for (let index = 0; index < Math.min(rows, 8); index++) values.push(payload.row_heights_px?.[index] ?? 0);
    let hash = 2166136261;
    for (const value of values) {
        for (const byte of new TextEncoder().encode(String(value))) hash = Math.imul(hash ^ byte, 16777619) >>> 0;
        hash = Math.imul(hash, 16777619) >>> 0;
    }
    return (hash || 1).toString(16).padStart(8, '0');
}

function padSelectTemplateButtons(payload, templateButtons) {
    const cols = Number(payload.cols);
    const rows = Number(payload.rows);
    const occupied = new Set();
    const mark = button => {
        const col = Number(button.col) || 0;
        const row = Number(button.row) || 0;
        const colSpan = Number(button.col_span) || 1;
        const rowSpan = Number(button.row_span) || 1;
        for (let x = col; x < col + colSpan; x++) {
            for (let y = row; y < row + rowSpan; y++) {
                if (x >= 0 && x < 8 && y >= 0 && y < 8) occupied.add(x + ',' + y);
            }
        }
    };
    (payload.buttons || []).forEach(mark);
    const selected = [];
    for (const button of templateButtons || []) {
        const col = Number(button.col) || 0;
        const row = Number(button.row) || 0;
        const colSpan = Number(button.col_span) || 1;
        const rowSpan = Number(button.row_span) || 1;
        if (col >= cols || row >= rows) continue;
        let conflict = false;
        for (let x = col; x < col + colSpan && !conflict; x++) {
            for (let y = row; y < row + rowSpan; y++) {
                if (x >= cols || y >= rows || occupied.has(x + ',' + y)) {
                    conflict = true;
                    break;
                }
            }
        }
        if (conflict) continue;
        selected.push(button);
        mark(button);
    }
    return selected;
}

async function padFetchConfiguredPads() {
    const response = await fetch('/api/info');
    if (!response.ok) throw new Error('Failed to list saved pads: HTTP ' + response.status);
    const info = await response.json();
    if (!Array.isArray(info.configured_pads)) throw new Error('Saved pad list is unavailable.');
    return info.configured_pads;
}

async function padPreparePageSave(context) {
    if (context.operation) {
        context.operation.stage('Preparing artwork');
        await context.operation.yield();
    }
    const payload = JSON.parse(JSON.stringify(context.payload || JSON.parse(context.body)));
    const iconSources = Object.assign({ byCatalogId: {}, bySourceId: {}, byDraftId: {}, sourceIdMap: {} }, context.iconSources || {});
    iconSources.sourceIdMap = Object.assign({}, iconSources.sourceIdMap);
    const sourceIdMap = iconSources.sourceIdMap;
    if (!Array.isArray(payload.buttons) ||
        (payload.overlay_buttons !== undefined && !Array.isArray(payload.overlay_buttons)))
        throw new Error('Pad payload must contain Main and Overlay button arrays.');
    for (const group of [payload.buttons, payload.overlay_buttons || []]) {
        for (const button of group) {
            if (Object.prototype.hasOwnProperty.call(button, 'icon_id'))
                throw new Error('Unsupported icon_id field; choose an icon using the picker.');
            if (button.icon && sourceIdMap[button.icon.source_id])
                button.icon.source_id = sourceIdMap[button.icon.source_id];
        }
    }
    const targetPage = Number(context.page);
    const buttons = payload.buttons.map(button => ({
        button, layer: 'main', targetPage, targetPayload: payload,
    })).concat((payload.overlay_buttons || []).map(button => ({
        button, layer: 'overlay', targetPage, targetPayload: payload,
    })));
    let inheritedButtons = [];
    const templatePage = Number(payload.template_pad);
    if (Number.isInteger(templatePage) && templatePage >= 0) {
        let templateButtons;
        if (context.templatePage === templatePage && Array.isArray(context.templateButtons)) {
            templateButtons = context.templateButtons;
        } else {
            const response = await fetch('/api/pad?page=' + templatePage);
            if (response.status === 404) templateButtons = [];
            else if (!response.ok) throw new Error('Failed to read template Pad ' + (templatePage + 1) + ': HTTP ' + response.status);
            else {
                const template = await response.json();
                templateButtons = Array.isArray(template.buttons) ? template.buttons : [];
            }
        }
        templateButtons = JSON.parse(JSON.stringify(templateButtons));
        for (const button of templateButtons) {
            if (Object.prototype.hasOwnProperty.call(button, 'icon_id'))
                throw new Error('Unsupported icon_id field in template pad; choose an icon using the picker.');
            if (button.icon && sourceIdMap[button.icon.source_id])
                button.icon.source_id = sourceIdMap[button.icon.source_id];
        }
        inheritedButtons = padSelectTemplateButtons(payload, templateButtons);
        for (const button of inheritedButtons)
            buttons.push({ button, layer: 'main', inherited: true, targetPage, targetPayload: payload });
    }
    const maxButtons = Number(deviceInfoCache && deviceInfoCache.max_pad_buttons) || 64;
    if (payload.buttons.length + (payload.overlay_buttons || []).length + inheritedButtons.length > maxButtons)
        throw new Error('Combined Main and Overlay button limit exceeded.');

    if (!context.skipTemplateDependents && Number.isInteger(targetPage) &&
        payload.buttons.some(button => button.icon)) {
        const response = await fetch('/api/pad/icon_context?page=' + targetPage);
        if (!response.ok)
            throw new Error('Failed to read icon dependency context: HTTP ' + response.status);
        const { dependents } = await response.json();
        if (!Array.isArray(dependents)) throw new Error('Icon dependency context is unavailable.');
        for (const dependentPayload of dependents) {
            const page = dependentPayload.page;
            const inherited = padSelectTemplateButtons(dependentPayload, payload.buttons);
            if (dependentPayload.button_count + inherited.length > maxButtons)
                throw new Error('Combined Main and Overlay button limit exceeded for dependent Pad ' + (page + 1) + '.');
            buttons.push(...inherited.filter(button => button.icon).map(button => ({
                button,
                layer: 'main',
                inherited: true,
                targetPage: page,
                targetPayload: dependentPayload,
            })));
        }
    }

    const sourceUploads = new Map();
    const sourceRecords = new Map();
    const iconButtons = [];
    for (const entry of buttons) {
        const icon = entry.button.icon;
        if (!icon) continue;
        const importedSources = iconSources.bySourceId;
        const imported = icon.source_id && importedSources[icon.source_id];
        const lookupKey = imported ? 'import:' + icon.source_id : icon.source_id ||
            (icon.draft_source_id ? 'draft:' + icon.draft_source_id : 'catalog:' + icon.catalog_id);
        let record = sourceRecords.get(lookupKey);
        if (!record) {
            record = await padResolveIconSource(icon, context);
            sourceRecords.set(lookupKey, record);
        }
        if (!icon.source_id || imported) sourceUploads.set(lookupKey, record);
        iconButtons.push({
            button: entry.button,
            layer: entry.layer,
            targetPage: entry.targetPage,
            targetPayload: entry.targetPayload,
            icon,
            record,
            lookupKey,
        });
    }

    const estimatedPayload = JSON.parse(JSON.stringify(payload));
    for (const group of [estimatedPayload.buttons, estimatedPayload.overlay_buttons || []]) {
        for (const button of group) {
            if (button.icon && !button.icon.source_id) {
                button.icon.source_id = 's_' + '0'.repeat(24);
                delete button.icon.draft_source_id;
            }
        }
    }
    const serializedSize = value => new TextEncoder().encode(JSON.stringify(value)).length;
    if (serializedSize(estimatedPayload) > 48 * 1024)
        throw new Error('Pad config exceeds the 48 KiB save limit.');
    const sourceBytes = Array.from(sourceRecords.values()).reduce((total, record) =>
        total + serializedSize(record), 0);
    if (serializedSize(estimatedPayload) + sourceBytes > 16 * 1024 * 1024)
        throw new Error('Pad and retained icon sources exceed the 16 MiB preparation limit.');

    const variants = [];
    if (iconButtons.length) {
        const pages = Array.from(new Set(iconButtons.map(entry => entry.targetPage)));
        const response = await fetch('/api/icons/installed?pages=' + pages.join(','));
        if (!response.ok) throw new Error('Failed to read icon metadata: HTTP ' + response.status);
        const { artifacts } = await response.json();
        if (!artifacts || typeof artifacts !== 'object' || Array.isArray(artifacts))
            throw new Error('Installed icon metadata is unavailable.');
        const layout = context.iconLayout || padCaptureIconLayout();
        const buttonSizeCache = new Map();
        const maxDimension = Number(deviceInfoCache && deviceInfoCache.icon_max_dimension) || 720;
        const rendered = new Map();
        let totalPngBytes = 0;
        let processed = 0;
        for (const entry of iconButtons) {
            if (context.operation) {
                context.operation.stage('Preparing artwork', processed++, iconButtons.length);
                await context.operation.yield();
            }
            const button = entry.button;
            const targetPayload = entry.targetPayload;
            const cols = Number(targetPayload.cols);
            const rows = Number(targetPayload.rows);
            if (!Number.isInteger(cols) || !Number.isInteger(rows) || cols < 1 || rows < 1)
                throw new Error('Invalid pad grid dimensions.');
            const dimensionsKey = cols + 'x' + rows;
            let buttonSizes = buttonSizeCache.get(dimensionsKey);
            if (!buttonSizes) {
                buttonSizes = await padGetButtonSizes(cols, rows);
                buttonSizeCache.set(dimensionsKey, buttonSizes);
            }
            const gap = Number(buttonSizes.gap) || 0;
            const displayWidth = Number(buttonSizes.display_w) ||
                (Number(buttonSizes.button_w) || 0) * cols + gap * (cols - 1);
            const displayHeight = Number(buttonSizes.display_h) ||
                (Number(buttonSizes.button_h) || 0) * rows + gap * (rows - 1);
            const columnSizing = padComputeAxisTrackSizes(cols,
                padGetTrackAvailableSize('columns', displayWidth, buttonSizes.pixel_shift_margin, layout),
                gap, targetPayload.column_widths_px);
            const rowSizing = padComputeAxisTrackSizes(rows,
                padGetTrackAvailableSize('rows', displayHeight, buttonSizes.pixel_shift_margin, layout),
                gap, targetPayload.row_heights_px);
            if (!columnSizing.valid || !rowSizing.valid) throw new Error('Pad track sizes do not fit the display.');
            const col = Number(button.col);
            const row = Number(button.row);
            const colSpan = Number(button.col_span) || 1;
            const rowSpan = Number(button.row_span) || 1;
            if (!Number.isInteger(col) || !Number.isInteger(row) || col < 0 || row < 0 ||
                colSpan < 1 || rowSpan < 1)
                throw new Error('Icon button position or span is outside the pad grid.');
            const key = 'pad_' + entry.targetPage + '_' + entry.layer + '_' + col + '_' + row;
            if (col + colSpan > cols || row + rowSpan > rows) continue;
            const borderWidth = button.border_width !== undefined ? Number(button.border_width) : 1;
            const padding = Number(buttonSizes.padding) || 0;
            const fullWidth = padTrackSpanSize(columnSizing.sizes, col, colSpan, gap) - padding * 2 - borderWidth * 2;
            const fullHeight = padTrackSpanSize(rowSizing.sizes, row, rowSpan, gap) - padding * 2 - borderWidth * 2;
            const labelHeight = Number(buttonSizes.font_small_h) || 0;
            let iconWidth = fullWidth;
            let iconHeight = fullHeight - (button.label_top ? labelHeight : 0) - (button.label_bottom ? labelHeight : 0);
            const widget = padWidgetEditor(button.widget_type);
            if (widget && widget.icon) {
                iconWidth = Math.floor(iconWidth * widget.icon.width);
                iconHeight = Math.floor(iconHeight * widget.icon.height);
            }
            if (button.icon_scale_pct && Number(button.icon_scale_pct) > 0) {
                iconWidth = Math.round(iconWidth * Number(button.icon_scale_pct) / 100);
                iconHeight = Math.round(iconHeight * Number(button.icon_scale_pct) / 100);
            }
            const size = Math.max(1, Math.floor(Math.min(iconWidth, iconHeight, maxDimension)));
            const renderKey = padIconRenderKey(targetPayload, button);
            const installed = artifacts[key];
            if (installed && installed.source_id === entry.lookupKey &&
                installed.kind === (entry.icon.kind === 'mono' ? 1 : 0) &&
                installed.render_key === renderKey && installed.width === size && installed.height === size)
                continue;
            const cacheKey = entry.lookupKey + ':' + size;
            let blob = rendered.get(cacheKey);
            if (!blob) {
                blob = await padRenderIconPNG(entry.record, size);
                rendered.set(cacheKey, blob);
                totalPngBytes += blob.size;
            }
            variants.push({ key, blob, sourceKey: entry.lookupKey, kind: entry.icon.kind, renderKey });
        }
        const combinedPngLimit = iconButtons.length *
            (Number(deviceInfoCache && deviceInfoCache.icon_max_png_size) || 512 * 1024);
        if (totalPngBytes > combinedPngLimit) throw new Error('Prepared icon variants exceed the combined PNG limit.');
        if (context.operation) context.operation.stage('Preparing artwork', iconButtons.length, iconButtons.length);
    }

    return Object.assign({}, context, {
        iconSources,
        payload,
        sourceUploads: Array.from(sourceUploads.entries()),
        variants,
    });
}

async function padRegenerateAllSavedIcons() {
    return padQueuePersistenceOperation(async () => {
        let savedBrightness = 0;
        let brightnessBlanked = false;
        let regenerated = 0;
        try {
            const pages = await padFetchConfiguredPads();
            for (const page of pages) {
                const response = await fetch('/api/pad?page=' + page);
                if (response.status === 404) continue;
                if (!response.ok)
                    throw new Error('Failed to read Pad ' + (page + 1) + ': HTTP ' + response.status);
                const payload = await response.json();
                const prepared = await padPreparePageSave({
                    page,
                    payload,
                    skipTemplateDependents: true,
                    blankOnSave: false,
                });
                if (!prepared.variants.length) continue;

                if (!brightnessBlanked && deviceInfoCache && deviceInfoCache.display_blank_on_save) {
                    const configResponse = await fetch('/api/config');
                    if (!configResponse.ok)
                        throw new Error('Failed to read display brightness: HTTP ' + configResponse.status);
                    const config = await configResponse.json();
                    savedBrightness = config.backlight_brightness ?? 80;
                    await padSetSaveBrightness(0);
                    brightnessBlanked = true;
                }

                await padPersistPage(prepared);
                regenerated++;
            }
            if (brightnessBlanked) await new Promise(resolve => setTimeout(resolve, 500));
            return regenerated;
        } finally {
            if (brightnessBlanked) await padSetSaveBrightness(savedBrightness);
        }
    });
}

async function padUploadIconSources(prepared) {
    const sourceIds = new Map();
    let uploaded = 0;
    if (prepared.operation && prepared.sourceUploads.length)
        prepared.operation.stage('Uploading image sources', 0, prepared.sourceUploads.length);
    for (const [sourceKey, record] of prepared.sourceUploads) {
        const response = await fetch('/api/icons/source', {
            method: 'POST',
            headers: { 'Content-Type': record.media_type === 'image/png' ? 'image/png' : 'application/json' },
            body: record.media_type === 'image/png' ? padDecodeBase64Bytes(record.data) : JSON.stringify(record),
        });
        if (!response.ok) {
            const error = await response.json().catch(() => ({}));
            throw new Error(error.message || error.error || 'Icon source upload failed: HTTP ' + response.status);
        }
        const result = await response.json();
        if (!result || !/^s_[0-9a-f]{24}$/.test(result.id || ''))
            throw new Error('Icon source upload returned an invalid ID.');
        sourceIds.set(sourceKey, result.id);
        if (prepared.operation)
            prepared.operation.stage('Uploading image sources', ++uploaded, prepared.sourceUploads.length);
        if (sourceKey.startsWith('import:'))
            prepared.iconSources.sourceIdMap[sourceKey.slice(7)] = result.id;
    }
    for (const group of [prepared.payload.buttons, prepared.payload.overlay_buttons || []]) {
        for (const button of group) {
            if (!button.icon) continue;
            const imported = prepared.iconSources.bySourceId[button.icon.source_id];
            const sourceKey = imported ? 'import:' + button.icon.source_id
                : button.icon.source_id ? null : button.icon.draft_source_id
                    ? 'draft:' + button.icon.draft_source_id : 'catalog:' + button.icon.catalog_id;
            if (!sourceKey) continue;
            const sourceId = sourceIds.get(sourceKey) || prepared.iconSources.sourceIdMap[button.icon.source_id];
            if (!sourceId) throw new Error('Prepared icon source ID is missing.');
            button.icon.source_id = sourceId;
            delete button.icon.draft_source_id;
        }
    }
    for (const variant of prepared.variants) {
        variant.sourceId = sourceIds.get(variant.sourceKey) || variant.sourceKey;
        if (!variant.sourceId) throw new Error('Prepared icon source ID is missing.');
    }
}

async function padInstallPreparedIcons(prepared) {
    let uploaded = 0;
    if (prepared.operation && prepared.variants.length)
        prepared.operation.stage('Uploading button images', 0, prepared.variants.length);
    for (const variant of prepared.variants) {
        const response = await fetch('/api/icons/install?id=' + encodeURIComponent(variant.key) +
            '&source_id=' + encodeURIComponent(variant.sourceId) + '&kind=' + (variant.kind === 'mono' ? 1 : 0) +
            '&render_key=' + variant.renderKey, {
            method: 'POST',
            headers: { 'Content-Type': 'image/png' },
            body: variant.blob,
        });
        if (!response.ok) throw new Error('Icon upload failed for ' + variant.key + ': HTTP ' + response.status);
        if (prepared.operation)
            prepared.operation.stage('Uploading button images', ++uploaded, prepared.variants.length);
    }
}
