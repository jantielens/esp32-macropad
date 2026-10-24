// portal_icon_picker.js - Browser-side foreground icon catalog and picker.

const PAD_ICONIFY_API = 'https://api.iconify.design';
const PAD_ICON_FAMILIES = [
    { prefix: 'material-symbols', title: 'Material Symbols', kind: 'mono' },
    { prefix: 'noto', title: 'Noto emoji', kind: 'color' },
];
const padIconArtworkCache = new Map();
let padIconApiQueue = Promise.resolve();
let padIconApiLastStart = 0;
let padIconApiCooldownUntil = 0;
let padIconPickerSession = null;

function padIconWait(milliseconds, signal) {
    return new Promise((resolve, reject) => {
        if (signal && signal.aborted) return reject(new DOMException('Aborted', 'AbortError'));
        const timer = setTimeout(() => {
            if (signal) signal.removeEventListener('abort', abort);
            resolve();
        }, milliseconds);
        function abort() {
            clearTimeout(timer);
            reject(new DOMException('Aborted', 'AbortError'));
        }
        if (signal) signal.addEventListener('abort', abort, { once: true });
    });
}

function padIconCooldownError() {
    const remaining = Math.max(1, Math.ceil((padIconApiCooldownUntil - Date.now()) / 1000));
    const error = new Error('The icon catalog is cooling down. Retry in about ' + remaining + ' seconds.');
    error.cooldown = true;
    return error;
}

function padIconRetryAfterMilliseconds(response) {
    const value = response.headers && response.headers.get('Retry-After');
    if (!value) return 30000;
    const seconds = Number(value);
    const parsed = Number.isFinite(seconds) ? seconds * 1000 : Date.parse(value) - Date.now();
    return Math.max(30000, Number.isFinite(parsed) ? parsed : 30000);
}

function padIconifyRequest(path, signal) {
    const task = padIconApiQueue.then(async function () {
        if (signal && signal.aborted) throw new DOMException('Aborted', 'AbortError');
        if (Date.now() < padIconApiCooldownUntil) throw padIconCooldownError();
        const wait = Math.max(0, padIconApiLastStart + 250 - Date.now());
        if (wait) await padIconWait(wait, signal);
        if (signal && signal.aborted) throw new DOMException('Aborted', 'AbortError');
        if (Date.now() < padIconApiCooldownUntil) throw padIconCooldownError();

        padIconApiLastStart = Date.now();
        const controller = new AbortController();
        let timedOut = false;
        const timeout = setTimeout(() => { timedOut = true; controller.abort(); }, 15000);
        const abort = () => controller.abort();
        if (signal) signal.addEventListener('abort', abort, { once: true });
        try {
            const response = await fetch(PAD_ICONIFY_API + path, {
                credentials: 'omit',
                signal: controller.signal,
            });
            if (response.status === 429) {
                padIconApiCooldownUntil = Math.max(padIconApiCooldownUntil,
                    Date.now() + padIconRetryAfterMilliseconds(response));
                throw padIconCooldownError();
            }
            if (!response.ok) throw new Error('Icon catalog request failed: HTTP ' + response.status);
            return await response.json();
        } catch (error) {
            if (timedOut) throw new Error('Icon catalog request timed out.');
            if (signal && signal.aborted) throw new DOMException('Aborted', 'AbortError');
            if (error instanceof TypeError) {
                padIconApiCooldownUntil = Math.max(padIconApiCooldownUntil, Date.now() + 30000);
                const networkError = new Error('The catalog request failed. A network or rate-limit response may be hidden; wait for cooldown, then retry.');
                networkError.cooldown = true;
                throw networkError;
            }
            throw error;
        } finally {
            clearTimeout(timeout);
            if (signal) signal.removeEventListener('abort', abort);
        }
    });
    padIconApiQueue = task.catch(() => {});
    return task;
}

function padIconCatalogEntry(id) {
    const separator = id.indexOf(':');
    if (separator < 1) return null;
    const prefix = id.slice(0, separator);
    const name = id.slice(separator + 1);
    const family = PAD_ICON_FAMILIES.find(item => item.prefix === prefix);
    if (!family || !/^[a-z0-9][a-z0-9-]*$/.test(name) || id.length > 128) return null;
    if (prefix === 'material-symbols' && /-(outline|rounded|sharp)$/.test(name)) return null;
    return { id, catalog_id: id, name: name.replace(/-/g, ' '), prefix, value: name, kind: family.kind };
}

function padIconSourceFromApi(icon, response) {
    const data = response && response.icons && response.icons[icon.value];
    if (!data || typeof data.body !== 'string') throw new Error('Icon artwork is unavailable for ' + icon.name + '.');
    const width = Number(data.width || response.width || 16);
    const height = Number(data.height || response.height || 16);
    const left = Number(data.left || 0);
    const top = Number(data.top || 0);
    if (![width, height, left, top].every(Number.isFinite) || width <= 0 || height <= 0 || width > 4096 || height > 4096)
        throw new Error('Icon artwork has invalid dimensions.');

    const rotate = Number(data.rotate || 0);
    if (!Number.isInteger(rotate)) throw new Error('Icon artwork has an invalid transform.');
    let body = data.body;
    if (rotate || data.hFlip || data.vFlip) {
        const centerX = left + width / 2;
        const centerY = top + height / 2;
        const scaleX = data.hFlip ? -1 : 1;
        const scaleY = data.vFlip ? -1 : 1;
        body = '<g transform="translate(' + centerX + ' ' + centerY + ') rotate(' + (rotate * 90) + ') scale(' +
            scaleX + ' ' + scaleY + ') translate(' + (-centerX) + ' ' + (-centerY) + ')">' + body + '</g>';
    }
    const source = {
        catalog_id: icon.catalog_id,
        kind: icon.kind,
        media_type: 'image/svg+xml',
        data: '<svg xmlns="http://www.w3.org/2000/svg" viewBox="' + [left, top, width, height].join(' ') + '">' + body + '</svg>',
    };
    return padNormalizeIconSource(source, icon);
}

async function padIconLoadArtwork(icons, signal) {
    const families = PAD_ICON_FAMILIES.filter(family => icons.some(icon => icon.prefix === family.prefix));
    for (const family of families) {
        const missing = [...new Set(icons.filter(icon => icon.prefix === family.prefix && !padIconArtworkCache.has(icon.id))
            .map(icon => icon.value))].sort();
        while (missing.length) {
            const names = [];
            const base = PAD_ICONIFY_API + '/' + family.prefix + '.json?icons=';
            while (missing.length) {
                const candidate = names.concat(missing[0]);
                if (names.length && (base + candidate.join(',')).length > 500) break;
                names.push(missing.shift());
            }
            if (!names.length) throw new Error('Icon artwork request exceeds the URL limit.');
            const batch = padIconifyRequest('/' + family.prefix + '.json?icons=' + names.join(','), signal);
            for (const name of names) {
                const icon = padIconCatalogEntry(family.prefix + ':' + name);
                const pending = batch.then(response => padIconSourceFromApi(icon, response));
                padIconArtworkCache.set(icon.id, pending);
                pending.catch(() => padIconArtworkCache.delete(icon.id));
            }
        }
    }
    return Promise.all(icons.map(icon => padIconArtworkCache.get(icon.id)));
}

async function padFetchCatalogIconSource(catalogId, kind) {
    const icon = padIconCatalogEntry(catalogId);
    if (!icon || icon.kind !== kind) throw new Error('Recipe icon catalog metadata is invalid.');
    return (await padIconLoadArtwork([icon]))[0];
}

function padIconInterleave(groups) {
    const results = [];
    for (let index = 0; index < Math.max(0, ...groups.map(group => group.length)); index++) {
        for (const group of groups) if (group[index]) results.push(group[index]);
    }
    return results;
}

function padIconPickerSetStatus(session, message) {
    if (padIconPickerSession !== session) return;
    session.dialog.querySelector('[data-icon-status]').textContent = message;
}

function padIconPickerRevoke(session, url) {
    if (!url) return;
    URL.revokeObjectURL(url);
    session.urls.delete(url);
}

function padIconPickerOwnUrl(session, blob) {
    const url = URL.createObjectURL(blob);
    session.urls.add(url);
    return url;
}

function padIconPickerShowRetry(session, retry) {
    const button = session.dialog.querySelector('[data-icon-retry]');
    button.hidden = false;
    button.onclick = retry;
    if (session.retryTimer) clearTimeout(session.retryTimer);
    const remaining = Math.max(0, padIconApiCooldownUntil - Date.now());
    button.disabled = remaining > 0;
    if (remaining > 0) {
        button.textContent = 'Retry after cooldown';
        session.retryTimer = setTimeout(() => {
            if (padIconPickerSession === session) button.disabled = false;
        }, remaining);
    } else {
        button.textContent = 'Retry';
    }
}

const PAD_ICON_INPUT_MAX_BYTES = 5 * 1024 * 1024;
const PAD_ICON_INPUT_MAX_PIXELS = 8000000;

function padIconImageDimensions(width, height) {
    if (!width || !height || width > 4096 || height > 4096 || width * height > PAD_ICON_INPUT_MAX_PIXELS)
        throw new Error('Image dimensions must be at most 4096 pixels per side and 8 million pixels total.');
    return { width, height };
}

function padIconInspectImageHeader(bytes) {
    const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const ascii = (offset, length) => String.fromCharCode(...bytes.subarray(offset, offset + length));
    const startsWith = signature => signature.every((value, index) => bytes[index] === value);

    if (startsWith([137, 80, 78, 71, 13, 10, 26, 10])) {
        let offset = 8;
        let width = 0;
        let height = 0;
        let sawHeader = false;
        let sawData = false;
        let sawEnd = false;
        while (offset + 12 <= bytes.length) {
            const size = view.getUint32(offset, false);
            if (size > bytes.length - offset - 12) throw new Error('PNG image data is truncated.');
            const type = ascii(offset + 4, 4);
            if (!sawHeader) {
                if (type !== 'IHDR' || size !== 13) throw new Error('PNG image header is invalid.');
                width = view.getUint32(offset + 8, false);
                height = view.getUint32(offset + 12, false);
                sawHeader = true;
            }
            if (type === 'acTL' || type === 'fcTL' || type === 'fdAT')
                throw new Error('Animated PNG images are not supported.');
            if (type === 'IDAT') sawData = true;
            offset += size + 12;
            if (type === 'IEND') {
                if (size !== 0 || offset !== bytes.length) throw new Error('PNG image ending is invalid.');
                sawEnd = true;
                break;
            }
        }
        if (!sawHeader || !sawData || !sawEnd) throw new Error('PNG image data is incomplete.');
        return Object.assign({ format: 'png' }, padIconImageDimensions(width, height));
    }

    if (startsWith([255, 216])) {
        const frameMarkers = new Set([0xc0, 0xc1, 0xc2, 0xc3, 0xc5, 0xc6, 0xc7,
            0xc9, 0xca, 0xcb, 0xcd, 0xce, 0xcf]);
        let offset = 2;
        while (offset < bytes.length) {
            if (bytes[offset++] !== 0xff) throw new Error('JPEG image header is invalid.');
            while (offset < bytes.length && bytes[offset] === 0xff) offset++;
            if (offset >= bytes.length) break;
            const marker = bytes[offset++];
            if (marker === 0xda || marker === 0xd9) break;
            if (marker === 0x01 || (marker >= 0xd0 && marker <= 0xd7)) continue;
            if (offset + 2 > bytes.length) throw new Error('JPEG image header is truncated.');
            const segmentLength = view.getUint16(offset, false);
            if (segmentLength < 2 || segmentLength > bytes.length - offset)
                throw new Error('JPEG image segment is invalid.');
            if (frameMarkers.has(marker)) {
                if (segmentLength < 7) throw new Error('JPEG image dimensions are invalid.');
                return Object.assign({ format: 'jpeg' }, padIconImageDimensions(
                    view.getUint16(offset + 5, false), view.getUint16(offset + 3, false)));
            }
            offset += segmentLength;
        }
        throw new Error('JPEG image dimensions could not be read.');
    }

    if (ascii(0, 4) === 'RIFF' && ascii(8, 4) === 'WEBP') {
        if (bytes.length < 20 || view.getUint32(4, true) + 8 !== bytes.length)
            throw new Error('WebP image header is invalid.');
        let offset = 12;
        let dimensions = null;
        while (offset + 8 <= bytes.length) {
            const type = ascii(offset, 4);
            const size = view.getUint32(offset + 4, true);
            const dataOffset = offset + 8;
            if (size > bytes.length - dataOffset) throw new Error('WebP image data is truncated.');
            if (type === 'ANIM' || type === 'ANMF') throw new Error('Animated WebP images are not supported.');
            if (type === 'VP8X') {
                if (size < 10) throw new Error('WebP image header is invalid.');
                if (bytes[dataOffset] & 2) throw new Error('Animated WebP images are not supported.');
                dimensions = {
                    width: 1 + bytes[dataOffset + 4] + (bytes[dataOffset + 5] << 8) + (bytes[dataOffset + 6] << 16),
                    height: 1 + bytes[dataOffset + 7] + (bytes[dataOffset + 8] << 8) + (bytes[dataOffset + 9] << 16),
                };
            } else if (type === 'VP8 ' && size >= 10 && bytes[dataOffset + 3] === 0x9d &&
                bytes[dataOffset + 4] === 0x01 && bytes[dataOffset + 5] === 0x2a) {
                dimensions = {
                    width: view.getUint16(dataOffset + 6, true) & 0x3fff,
                    height: view.getUint16(dataOffset + 8, true) & 0x3fff,
                };
            } else if (type === 'VP8L' && size >= 5 && bytes[dataOffset] === 0x2f) {
                dimensions = {
                    width: 1 + bytes[dataOffset + 1] + ((bytes[dataOffset + 2] & 0x3f) << 8),
                    height: 1 + (bytes[dataOffset + 2] >> 6) + (bytes[dataOffset + 3] << 2) +
                        ((bytes[dataOffset + 4] & 0x0f) << 10),
                };
            }
            offset = dataOffset + size + (size & 1);
        }
        if (offset !== bytes.length || !dimensions) throw new Error('WebP image dimensions could not be read.');
        return Object.assign({ format: 'webp' }, padIconImageDimensions(dimensions.width, dimensions.height));
    }

    throw new Error('Choose a static PNG, JPEG, or WebP image.');
}

function padIconBlobBase64(blob) {
    return blob.arrayBuffer().then(buffer => {
        const bytes = new Uint8Array(buffer);
        let binary = '';
        for (let offset = 0; offset < bytes.length; offset += 0x8000)
            binary += String.fromCharCode(...bytes.subarray(offset, Math.min(offset + 0x8000, bytes.length)));
        return btoa(binary);
    });
}

async function padIconNormalizeImageFile(file, maxResolution = 256) {
    if (![128, 256, 512, 720].includes(maxResolution))
        throw new Error('Choose a supported image resolution.');
    if (!file || !Number.isFinite(file.size) || file.size <= 0)
        throw new Error('Choose one non-empty image file.');
    if (file.size > PAD_ICON_INPUT_MAX_BYTES) throw new Error('Image files must be 5 MiB or smaller.');
    const bytes = new Uint8Array(await file.arrayBuffer());
    const dimensions = padIconInspectImageHeader(bytes);
    let bitmap = null;
    let image = null;
    let inputUrl = null;
    let canvas = null;
    try {
        if (typeof createImageBitmap === 'function') {
            bitmap = await createImageBitmap(file, { imageOrientation: 'from-image' });
        } else {
            inputUrl = URL.createObjectURL(file);
            image = new Image();
            image.src = inputUrl;
            await image.decode();
        }
        const width = bitmap ? bitmap.width : image.naturalWidth;
        const height = bitmap ? bitmap.height : image.naturalHeight;
        padIconImageDimensions(width, height);
        if (!((width === dimensions.width && height === dimensions.height) ||
            (width === dimensions.height && height === dimensions.width)))
            throw new Error('Decoded image dimensions do not match the image header.');
        const scale = Math.min(1, maxResolution / Math.max(width, height));
        const outputWidth = Math.max(1, Math.round(width * scale));
        const outputHeight = Math.max(1, Math.round(height * scale));
        canvas = document.createElement('canvas');
        canvas.width = outputWidth;
        canvas.height = outputHeight;
        const context = canvas.getContext('2d', { alpha: true });
        if (!context) throw new Error('Image normalization is unavailable in this browser.');
        context.clearRect(0, 0, outputWidth, outputHeight);
        context.imageSmoothingQuality = 'high';
        context.drawImage(bitmap || image, 0, 0, outputWidth, outputHeight);
        const png = await new Promise((resolve, reject) => canvas.toBlob(result =>
            result ? resolve(result) : reject(new Error('Normalized PNG could not be created.')), 'image/png'));
        const pngLimit = Number(deviceInfoCache && deviceInfoCache.icon_max_png_size) || 512 * 1024;
        if (png.size > pngLimit) throw new Error('Normalized PNG exceeds the 512 KiB retained image limit.');
        const record = { kind: 'color', media_type: 'image/png', data: await padIconBlobBase64(png) };
        return padNormalizeIconSource(record, { kind: 'color' });
    } catch (error) {
        if (error instanceof Error && /dimensions|5 MiB|PNG|Animated|Choose a static|Choose one/.test(error.message)) throw error;
        throw new Error('The image could not be decoded or normalized by this browser.');
    } finally {
        if (bitmap && typeof bitmap.close === 'function') bitmap.close();
        if (image) image.removeAttribute('src');
        if (inputUrl) URL.revokeObjectURL(inputUrl);
        if (canvas) { canvas.width = 0; canvas.height = 0; }
    }
}

async function padIconPickerLoadCollections(session) {
    const collections = await Promise.all(PAD_ICON_FAMILIES.map(async family => {
        const data = await padIconifyRequest('/collection?prefix=' + family.prefix, session.metadataController.signal);
        const categories = data.categories || {};
        const names = new Set(data.uncategorized || []);
        Object.values(categories).forEach(items => items.forEach(name => names.add(name)));
        const icons = [...names].map(name => padIconCatalogEntry(family.prefix + ':' + name)).filter(Boolean);
        return Object.assign({}, family, { categories, icons });
    }));
    if (padIconPickerSession !== session) return;
    session.collections = collections;
    padIconPickerUpdateCategories(session);
    session.dialog.querySelector('[data-icon-results-label]').textContent = 'Search at least 3 characters or choose a category.';
}

function padIconPickerUpdateCategories(session) {
    const select = session.dialog.querySelector('[data-icon-category]');
    const previous = select.value;
    const source = session.dialog.querySelector('[data-icon-source]').value;
    select.replaceChildren(new Option('All categories', 'all'));
    for (const family of session.collections || []) {
        if (source !== 'all' && family.kind !== source) continue;
        for (const name of Object.keys(family.categories).sort()) {
            const option = document.createElement('option');
            option.value = JSON.stringify([family.prefix, name]);
            option.textContent = source === 'all'
                ? (family.kind === 'mono' ? 'Symbols' : 'Emoji') + ' / ' + name : name;
            select.appendChild(option);
        }
    }
    if (Array.from(select.options).some(option => option.value === previous)) select.value = previous;
}

function padIconPickerSetProcessing(session, processing, message) {
    session.processing = processing;
    session.dialog.setAttribute('aria-busy', String(processing));
    session.dialog.querySelector('[data-icon-use]').disabled = processing || !session.selected;
    if (message !== undefined) padIconPickerSetStatus(session, message);
}

function padIconPickerSetPressed(session) {
    const selectedId = session.selected && session.selected.icon &&
        (session.selected.icon.id || session.selected.icon.catalog_id);
    session.dialog.querySelectorAll('.icon-picker-choice').forEach(choice => {
        choice.setAttribute('aria-pressed', String(choice.dataset.iconId === selectedId));
    });
}

function padIconPickerShowPreview(session, blob, name) {
    const image = session.dialog.querySelector('[data-icon-preview-image]');
    session.dialog.querySelector('.icon-picker-preview').hidden = false;
    const previous = session.previewUrl;
    const url = padIconPickerOwnUrl(session, blob);
    session.previewUrl = url;
    image.src = url;
    image.dataset.url = url;
    image.hidden = false;
    session.dialog.querySelector('[data-icon-preview-name]').textContent = name;
    if (previous) padIconPickerRevoke(session, previous);
}

function padIconPickerShowImageDetails(session, record) {
    const bytes = padDecodeBase64Bytes(record.data);
    const header = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    const width = header.getUint32(16, false);
    const height = header.getUint32(20, false);
    session.dialog.querySelector('[data-icon-preview-details]').textContent =
        width + ' \u00d7 ' + height + ' \u00b7 ' + (bytes.length / 1024).toFixed(1) + ' KB';
    return Math.max(width, height);
}

async function padIconPickerSelect(session, icon) {
    const generation = ++session.selectionGeneration;
    if (session.selectionController) session.selectionController.abort();
    session.selectionController = new AbortController();
    const signal = session.selectionController.signal;
    padIconPickerSetProcessing(session, true, 'Preparing icon...');
    try {
        const record = (await padIconLoadArtwork([icon], signal))[0];
        if (padIconPickerSession !== session || generation !== session.selectionGeneration) return;
        session.selected = { icon, record, existing: false, local: false };
        session.dialog.querySelector('.icon-picker-preview').hidden = true;
        padIconPickerSetPressed(session);
        padIconPickerSetProcessing(session, false, '');
    } catch (error) {
        if (padIconPickerSession !== session || generation !== session.selectionGeneration || error.name === 'AbortError') return;
        padIconPickerSetPressed(session);
        padIconPickerSetProcessing(session, false, error.message || 'Icon could not be prepared.');
        padIconPickerShowRetry(session, () => padIconPickerSelect(session, icon));
    }
}

async function padIconPickerSelectImage(session, file) {
    const generation = ++session.selectionGeneration;
    if (session.selectionController) session.selectionController.abort();
    session.selectionController = new AbortController();
    padIconPickerSetProcessing(session, true, 'Reading and normalizing image...');
    try {
        const resolution = Number(session.dialog.querySelector('[data-icon-resolution]').value);
        const record = await padIconNormalizeImageFile(file, resolution);
        if (padIconPickerSession !== session || generation !== session.selectionGeneration) return;
        const blob = await padRenderIconPNG(record, 96);
        if (padIconPickerSession !== session || generation !== session.selectionGeneration) return;
        session.selected = { icon: { kind: 'color' }, record, existing: false, local: true };
        session.originalFile = file;
        session.imageResolution = resolution;
        session.dialog.querySelector('[data-icon-resolution]').disabled = false;
        session.dialog.querySelector('[data-icon-resolution]').removeAttribute('title');
        padIconPickerShowPreview(session, blob, file.name || 'Local image');
        padIconPickerShowImageDetails(session, record);
        padIconPickerSetPressed(session);
        padIconPickerSetProcessing(session, false, '');
    } catch (error) {
        if (padIconPickerSession !== session || generation !== session.selectionGeneration) return;
        if (session.imageResolution)
            session.dialog.querySelector('[data-icon-resolution]').value = String(session.imageResolution);
        padIconPickerSetPressed(session);
        padIconPickerSetProcessing(session, false, error.message || 'Image could not be prepared.');
    }
}

async function padIconPickerDrawResults(session, generation) {
    const results = session.dialog.querySelector('[data-icon-grid]');
    const visible = session.matching.slice(results.children.length, session.visibleCount);
    const records = await padIconLoadArtwork(visible, session.searchController.signal);
    if (padIconPickerSession !== session || generation !== session.searchGeneration) return;
    visible.forEach((icon, index) => {
        const choice = document.createElement('button');
        choice.type = 'button';
        choice.className = 'icon-picker-choice';
        choice.title = icon.name;
        choice.setAttribute('aria-label', icon.name);
        const selectedIcon = session.selected && session.selected.icon;
        choice.setAttribute('aria-pressed', String(!!selectedIcon &&
            (selectedIcon.id || selectedIcon.catalog_id) === icon.id));
        choice.dataset.iconId = icon.id;
        const image = document.createElement('img');
        image.alt = '';
        image.width = 38;
        image.height = 38;
        image.style.backgroundColor = session.colors.background;
        const blobPromise = padRenderIconPNG(records[index], 40, session.colors.foreground);
        blobPromise.then(blob => {
            if (padIconPickerSession !== session || generation !== session.searchGeneration) return;
            const url = padIconPickerOwnUrl(session, blob);
            image.dataset.url = url;
            image.src = url;
        }).catch(error => padIconPickerSetStatus(session, error.message || 'Icon thumbnail failed.'));
        const label = document.createElement('span');
        label.textContent = icon.name;
        choice.append(image, label);
        choice.addEventListener('click', () => {
            session.dialog.querySelectorAll('.icon-picker-choice').forEach(item => item.setAttribute('aria-pressed', 'false'));
            choice.setAttribute('aria-pressed', 'true');
            session.selectionController = new AbortController();
            padIconPickerSelect(session, icon);
        });
        results.appendChild(choice);
    });
    session.dialog.querySelector('[data-icon-count]').textContent =
        Math.min(session.visibleCount, session.matching.length) + ' / ' + session.matching.length;
    session.dialog.querySelector('[data-icon-empty]').hidden = session.matching.length > 0;
    session.dialog.querySelector('[data-icon-more]').hidden = session.visibleCount >= session.matching.length;
}

async function padIconPickerRenderResults(session) {
    if (session.activeSource === 'image') return;
    clearTimeout(session.searchTimer);
    const generation = ++session.searchGeneration;
    if (session.searchController) session.searchController.abort();
    session.searchController = new AbortController();
    const query = session.dialog.querySelector('[data-icon-search]').value.trim();
    const categoryValue = session.dialog.querySelector('[data-icon-category]').value;
    const sourceFilter = session.dialog.querySelector('[data-icon-source]').value;
    const searchReady = Array.from(query).length >= 3;
    const category = categoryValue === 'all' ? null : JSON.parse(categoryValue);
    const label = session.dialog.querySelector('[data-icon-results-label]');
    const grid = session.dialog.querySelector('[data-icon-grid]');
    const retry = session.dialog.querySelector('[data-icon-retry]');
    if (!session.collections) {
        label.textContent = 'Loading categories...';
        return;
    }
    retry.hidden = true;
    session.dialog.querySelector('[data-icon-more]').hidden = true;
    grid.querySelectorAll('img[data-url]').forEach(image => padIconPickerRevoke(session, image.dataset.url));
    grid.replaceChildren();
    session.dialog.querySelector('[data-icon-empty]').hidden = true;
    session.dialog.querySelector('[data-icon-count]').textContent = '';
    if (!searchReady && !category) {
        label.textContent = 'Search at least 3 characters or choose a category.';
        return;
    }
    label.textContent = 'Loading...';
    session.dialog.setAttribute('aria-busy', 'true');
    try {
        const groups = await Promise.all(session.collections
            .filter(family => sourceFilter === 'all' || family.kind === sourceFilter)
            .map(async family => {
                let icons = family.icons;
                if (searchReady) {
                    const params = new URLSearchParams({ query, prefix: family.prefix, limit: '999' });
                    const response = await padIconifyRequest('/search?' + params.toString(), session.searchController.signal);
                    const canonical = new Set(family.icons.map(icon => icon.id));
                    icons = (response.icons || []).map(id => padIconCatalogEntry(id)).filter(icon => icon && canonical.has(icon.id));
                }
                if (category) {
                    if (category[0] !== family.prefix) return [];
                    const names = new Set(family.categories[category[1]] || []);
                    icons = icons.filter(icon => names.has(icon.value));
                }
                return icons;
            }));
        if (padIconPickerSession !== session || generation !== session.searchGeneration) return;
        session.matching = padIconInterleave(groups);
        session.visibleCount = 48;
        await padIconPickerDrawResults(session, generation);
        if (padIconPickerSession !== session || generation !== session.searchGeneration) return;
        label.textContent = searchReady ? 'Search results' : 'Browse icons';
    } catch (error) {
        if (padIconPickerSession !== session || generation !== session.searchGeneration || error.name === 'AbortError') return;
        label.textContent = 'Catalog unavailable';
        padIconPickerSetStatus(session, error.message || 'Icon catalog is unavailable.');
        padIconPickerShowRetry(session, () => padIconPickerRenderResults(session));
    } finally {
        if (generation === session.searchGeneration) session.dialog.setAttribute('aria-busy', 'false');
    }
}

function padIconPickerActivateSource(session, source) {
    if (padIconPickerSession !== session) return;
    session.activeSource = source;
    session.dialog.querySelector('[data-icon-source]').value = source;
    const catalogActive = source !== 'image';
    session.dialog.querySelector('[data-icon-catalog-panel]').hidden = !catalogActive;
    session.dialog.querySelector('[data-icon-image-panel]').hidden = catalogActive;
    if (!session.processing) padIconPickerSetStatus(session, '');
    if (!catalogActive) {
        clearTimeout(session.searchTimer);
        session.searchGeneration++;
        if (session.searchController) session.searchController.abort();
        session.dialog.setAttribute('aria-busy', String(session.processing));
        return;
    }
    if (session.collections) {
        padIconPickerUpdateCategories(session);
        padIconPickerRenderResults(session);
        return;
    }
    if (session.metadataPromise) return;

    session.metadataPromise = padIconPickerLoadCollections(session).then(() => {
        if (padIconPickerSession !== session) return;
        if (session.activeSource !== 'image') return padIconPickerRenderResults(session);
    }).catch(error => {
        if (padIconPickerSession !== session || session.activeSource === 'image') return;
        session.dialog.querySelector('[data-icon-results-label]').textContent = 'Catalog unavailable';
        padIconPickerSetStatus(session, error.message || 'Icon categories are unavailable.');
        padIconPickerShowRetry(session, () => {
            session.metadataPromise = null;
            padIconPickerActivateSource(session, session.activeSource);
        });
    }).finally(() => {
        if (session.metadataPromise) session.metadataPromise = null;
    });
}

function padIconPickerStartCurrentSelection(session, icon) {
    if (!icon) return;
    session.selected = { icon: JSON.parse(JSON.stringify(icon)), record: null, existing: true, local: false };
    session.dialog.querySelector('[data-icon-use]').disabled = false;
    if (icon.catalog_id) return;
    const preview = session.dialog.querySelector('[data-icon-preview]');
    session.dialog.querySelector('.icon-picker-preview').hidden = false;
    const previewImage = session.dialog.querySelector('[data-icon-preview-image]');
    const currentImage = document.getElementById('pad-edit-icon-image');
    const label = icon.catalog_id ? icon.catalog_id.split(':').pop().replace(/-/g, ' ') : 'Local image';
    preview.style.backgroundColor = session.colors.background;
    session.dialog.querySelector('[data-icon-preview-name]').textContent = label;
    if (currentImage && !currentImage.hidden && currentImage.src) {
        previewImage.src = currentImage.src;
        previewImage.hidden = false;
    }
    session.dialog.querySelector('[data-icon-use]').disabled = false;

    const generation = ++session.selectionGeneration;
    session.selectionController = new AbortController();
    padResolveIconSource(icon, {}).then(record => {
        if (padIconPickerSession !== session || generation !== session.selectionGeneration) return null;
        if (!icon.catalog_id) {
            const longestSide = padIconPickerShowImageDetails(session, record);
            const resolution = session.dialog.querySelector('[data-icon-resolution]');
            resolution.value = String([128, 256, 512, 720].find(limit => limit >= longestSide));
            resolution.disabled = true;
            resolution.title = 'Choose the original image to change resolution';
        }
        return padRenderIconPNG(record, 96, session.colors.foreground);
    })
        .then(blob => {
            if (padIconPickerSession !== session || generation !== session.selectionGeneration) return;
            session.selected.record = null;
            padIconPickerShowPreview(session, blob, label);
        }).catch(error => {
            if (padIconPickerSession !== session || generation !== session.selectionGeneration) return;
            padIconPickerSetStatus(session, 'The current icon remains selected; its retained preview could not be loaded. ' +
                (error.message || ''));
        });
}

function padIconPickerOwnerIsCurrent(session) {
    if (padState.page !== session.page || padState.selectedLayer !== session.layer ||
        padState.editCol !== session.col || padState.editRow !== session.row ||
        document.getElementById('pad-config-section') !== session.section) return false;
    if (session.workspace && (session.workspace !== padWorkspace || !session.workspace.selected ||
        typeof padWorkspaceKey === 'function' && padWorkspaceKey() !== session.workspaceKey)) return false;
    return true;
}

function padIconPickerClose(session) {
    if (!session || padIconPickerSession !== session) return;
    padIconPickerSession = null;
    if (session.retryTimer) clearTimeout(session.retryTimer);
    clearTimeout(session.searchTimer);
    session.metadataController.abort();
    session.eventController.abort();
    session.selectionGeneration++;
    if (session.searchController) session.searchController.abort();
    if (session.selectionController) session.selectionController.abort();
    for (const url of session.urls) URL.revokeObjectURL(url);
    session.urls.clear();
    session.dialog.close();
    session.dialog.remove();
    if (session.opener && session.opener.isConnected) session.opener.focus();
}

function padOpenIconPicker() {
    if (padIconPickerSession) return;
    const dialog = document.createElement('dialog');
    dialog.className = 'pad-button-builder portal-form icon-picker';
    dialog.setAttribute('aria-labelledby', 'icon-picker-title');
    dialog.innerHTML = '<header><div><h2 id="icon-picker-title">Choose an icon</h2></div>' +
        '<button type="button" class="btn btn-sm" data-icon-close aria-label="Close icon picker" title="Close icon picker">&#215;</button></header>' +
        '<div class="pad-button-builder-body icon-picker-body">' +
        '<label class="icon-picker-from" for="icon-picker-source">From' +
        '<select class="form-control" id="icon-picker-source" data-icon-source>' +
        '<option value="all">All icons</option><option value="mono">Symbols</option>' +
        '<option value="color">Emoji</option><option value="image">Image file</option></select></label>' +
        '<div class="icon-picker-status" data-icon-status role="status" aria-live="polite"></div>' +
        '<section data-icon-catalog-panel aria-label="Icon library">' +
        '<div class="icon-picker-tools"><input class="form-control" type="search" data-icon-search placeholder="Search icons" aria-label="Search icons">' +
        '<select class="form-control" data-icon-category aria-label="Category"><option value="all">All categories</option></select></div>' +
        '<div class="icon-picker-results" data-icon-results><div class="icon-picker-grid" data-icon-grid></div>' +
        '<p class="icon-picker-empty" data-icon-empty hidden>No matching icons.</p></div>' +
        '<div class="icon-picker-result-footer"><span data-icon-results-label>Search at least 3 characters or choose a category.</span><span data-icon-count></span>' +
        '<button type="button" class="btn btn-secondary btn-sm" data-icon-more hidden>Show more</button></div>' +
        '<button type="button" class="btn btn-secondary btn-sm" data-icon-retry hidden>Retry</button></section>' +
        '<section data-icon-image-panel aria-label="Image file" hidden>' +
        '<div class="icon-picker-drop" data-icon-drop role="region" tabindex="0" aria-label="Drop or paste an image here">' +
        '<div><strong>Drop or paste an image here</strong><span>Static PNG, JPEG, or WebP, up to 5 MiB</span></div>' +
        '<button type="button" class="btn btn-primary" data-icon-choose><span class="portal-icon" aria-hidden="true">upload</span><span>Upload image</span></button>' +
        '<input type="file" accept="image/png,image/jpeg,image/webp" data-icon-file hidden></div>' +
        '<label class="icon-picker-image-settings" for="icon-picker-resolution">Downscale to' +
        '<select class="form-control" id="icon-picker-resolution" data-icon-resolution>' +
        '<option value="128">Compact (128 px)</option><option value="256" selected>Standard (256 px)</option>' +
        '<option value="512">Large (512 px)</option><option value="720">Maximum (720 px)</option></select></label>' +
        '<section class="icon-picker-preview" hidden><div class="icon-picker-preview-surface" data-icon-preview>' +
        '<img data-icon-preview-image alt="" hidden></div><div class="icon-picker-preview-info">' +
        '<span data-icon-preview-name></span><span data-icon-preview-details></span></div></section></section></div>' +
        '<footer><div>' +
        '<button type="button" class="btn btn-secondary" data-icon-cancel>Cancel</button>' +
        '<button type="button" class="btn btn-primary" data-icon-use disabled>Apply</button></div></footer>';
    document.body.appendChild(dialog);
    const workspace = typeof padWorkspace !== 'undefined' ? padWorkspace : null;
    const session = {
        dialog,
        opener: document.activeElement,
        section: document.getElementById('pad-config-section'),
        page: padState.page,
        layer: padState.selectedLayer,
        col: padState.editCol,
        row: padState.editRow,
        workspace,
        workspaceKey: workspace && typeof padWorkspaceKey === 'function' ? padWorkspaceKey() : null,
        urls: new Set(),
        previewUrl: null,
        collections: null,
        matching: [],
        visibleCount: 48,
        selected: null,
        activeSource: 'all',
        originalFile: null,
        imageResolution: null,
        processing: false,
        searchGeneration: 0,
        selectionGeneration: 0,
        metadataController: new AbortController(),
        eventController: new AbortController(),
        searchController: null,
        selectionController: null,
        colors: typeof padIconEditorColors === 'function' ? padIconEditorColors() : { background: '#ffffff', foreground: '#222222' },
    };
    padIconPickerSession = session;
    dialog.querySelector('[data-icon-preview]').style.backgroundColor = session.colors.background;
    const signal = session.eventController.signal;
    const close = () => padIconPickerClose(session);
    dialog.querySelector('[data-icon-close]').addEventListener('click', close, { signal });
    dialog.querySelector('[data-icon-cancel]').addEventListener('click', close, { signal });
    dialog.oncancel = event => { event.preventDefault(); close(); };
    dialog.querySelector('[data-icon-search]').addEventListener('input', () => {
        clearTimeout(session.searchTimer);
        session.searchTimer = setTimeout(() => padIconPickerRenderResults(session), 300);
    }, { signal });
    dialog.querySelector('[data-icon-category]').addEventListener('change', () => padIconPickerRenderResults(session), { signal });
    dialog.querySelector('[data-icon-source]').addEventListener('change', event =>
        padIconPickerActivateSource(session, event.target.value), { signal });
    dialog.querySelector('[data-icon-resolution]').addEventListener('change', () => {
        if (session.originalFile) padIconPickerSelectImage(session, session.originalFile);
    }, { signal });
    dialog.querySelector('[data-icon-more]').addEventListener('click', async event => {
        const button = event.currentTarget;
        button.disabled = true;
        const generation = session.searchGeneration;
        session.visibleCount += 48;
        try { await padIconPickerDrawResults(session, generation); }
        catch (error) {
            if (padIconPickerSession === session && generation === session.searchGeneration) {
                session.visibleCount = dialog.querySelector('[data-icon-grid]').children.length;
                padIconPickerSetStatus(session, error.message || 'More icons are unavailable.');
                padIconPickerShowRetry(session, () => padIconPickerRenderResults(session));
            }
        } finally { button.disabled = false; }
    }, { signal });
    const fileInput = dialog.querySelector('[data-icon-file]');
    const chooseImage = () => fileInput.click();
    dialog.querySelector('[data-icon-choose]').addEventListener('click', chooseImage, { signal });
    fileInput.addEventListener('change', () => {
        const files = Array.from(fileInput.files || []);
        fileInput.value = '';
        if (!files.length) return;
        if (files.length !== 1) {
            padIconPickerSetStatus(session, 'Choose one image at a time.');
            return;
        }
        padIconPickerSelectImage(session, files[0]);
    }, { signal });
    fileInput.addEventListener('cancel', () => {}, { signal });
    const dropTarget = dialog.querySelector('[data-icon-drop]');
    dropTarget.addEventListener('keydown', event => {
        if (event.key !== 'Enter' && event.key !== ' ') return;
        event.preventDefault();
        chooseImage();
    }, { signal });
    dialog.addEventListener('dragover', event => {
        if (!Array.from(event.dataTransfer && event.dataTransfer.types || []).includes('Files')) return;
        event.preventDefault();
        if (session.activeSource === 'image' && dropTarget.contains(event.target)) dropTarget.classList.add('is-dragging');
    }, { signal });
    dialog.addEventListener('dragleave', event => {
        if (!dropTarget.contains(event.relatedTarget)) dropTarget.classList.remove('is-dragging');
    }, { signal });
    dialog.addEventListener('drop', event => {
        if (!Array.from(event.dataTransfer && event.dataTransfer.types || []).includes('Files')) return;
        event.preventDefault();
        dropTarget.classList.remove('is-dragging');
        if (session.activeSource !== 'image' || !dropTarget.contains(event.target)) return;
        const files = Array.from(event.dataTransfer.files || []);
        if (files.length !== 1) {
            padIconPickerSetStatus(session, 'Drop one image file at a time.');
            return;
        }
        padIconPickerSelectImage(session, files[0]);
    }, { signal });
    dialog.addEventListener('paste', event => {
        if (session.activeSource !== 'image') return;
        const target = event.target;
        if (target && target.closest && target.closest('input, textarea, select, [contenteditable="true"], [contenteditable=""]')) return;
        const files = Array.from(event.clipboardData && event.clipboardData.items || [])
            .filter(item => item.kind === 'file' && item.type.startsWith('image/'))
            .map(item => item.getAsFile()).filter(Boolean);
        if (!files.length) return;
        event.preventDefault();
        if (files.length !== 1) {
            padIconPickerSetStatus(session, 'Paste one image at a time.');
            return;
        }
        padIconPickerSelectImage(session, files[0]);
    }, { signal });
    dialog.querySelector('[data-icon-use]').addEventListener('click', () => {
        if (!session.selected || session.processing) return;
        if (!padIconPickerOwnerIsCurrent(session)) {
            padIconPickerClose(session);
            return;
        }
        const selected = session.selected;
        let icon;
        if (selected.existing) icon = JSON.parse(JSON.stringify(selected.icon));
        else if (selected.local) icon = padRegisterLocalDraftIconSource(selected.record);
        else {
            padRegisterDraftIconSource(selected.record);
            icon = { catalog_id: selected.icon.catalog_id, kind: selected.icon.kind };
        }
        padIconEditorSetDraft(icon);
        if (typeof padWorkspaceEdit === 'function') padWorkspaceEdit();
        if (typeof padPruneDraftIconSources === 'function') padPruneDraftIconSources();
        close();
    }, { signal });

    dialog.showModal();
    const currentIcon = padIconEditorReadDraft();
    const initialSource = currentIcon ? currentIcon.catalog_id ? currentIcon.kind : 'image' : 'all';
    padIconPickerActivateSource(session, initialSource);
    if (currentIcon) padIconPickerStartCurrentSelection(session, currentIcon);
    dialog.querySelector(initialSource === 'image' ? '[data-icon-choose]' : '[data-icon-search]').focus();
}