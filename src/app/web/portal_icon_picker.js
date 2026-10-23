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

function padIconPickerResultColor() {
    return document.documentElement.getAttribute('data-bs-theme') === 'dark' ? '#FFFFFF' : '#000000';
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
    const select = session.dialog.querySelector('[data-icon-category]');
    select.replaceChildren(new Option('All categories', 'all'));
    for (const family of collections) {
        for (const name of Object.keys(family.categories).sort()) {
            const option = document.createElement('option');
            option.value = JSON.stringify([family.prefix, name]);
            option.textContent = family.title + ' / ' + name;
            select.appendChild(option);
        }
    }
    session.dialog.querySelector('[data-icon-results-label]').textContent = 'Search at least 3 characters or choose a category.';
}

async function padIconPickerSelect(session, icon) {
    const generation = ++session.selectionGeneration;
    session.selected = null;
    const useButton = session.dialog.querySelector('[data-icon-use]');
    const preview = session.dialog.querySelector('[data-icon-preview]');
    const previewImage = session.dialog.querySelector('[data-icon-preview-image]');
    const previousUrl = previewImage.dataset.url;
    if (previousUrl) padIconPickerRevoke(session, previousUrl);
    delete previewImage.dataset.url;
    previewImage.removeAttribute('src');
    previewImage.hidden = true;
    session.dialog.querySelector('[data-icon-preview-name]').textContent = '';
    useButton.disabled = true;
    if (!icon) {
        padIconPickerSetStatus(session, '');
        return;
    }
    padIconPickerSetStatus(session, 'Preparing icon...');
    try {
        const record = (await padIconLoadArtwork([icon], session.selectionController.signal))[0];
        const blob = await padRenderIconPNG(record, 96, session.colors.foreground);
        if (padIconPickerSession !== session || generation !== session.selectionGeneration) return;
        const url = padIconPickerOwnUrl(session, blob);
        session.selected = { icon, record };
        preview.style.backgroundColor = session.colors.background;
        previewImage.src = url;
        previewImage.dataset.url = url;
        previewImage.hidden = false;
        session.dialog.querySelector('[data-icon-preview-name]').textContent = icon.name;
        useButton.disabled = false;
        padIconPickerSetStatus(session, '');
    } catch (error) {
        if (padIconPickerSession !== session || generation !== session.selectionGeneration || error.name === 'AbortError') return;
        padIconPickerSetStatus(session, error.message || 'Icon could not be prepared.');
        padIconPickerShowRetry(session, () => padIconPickerSelect(session, icon));
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
        choice.setAttribute('aria-pressed', String(!!session.selected && session.selected.icon.id === icon.id));
        const image = document.createElement('img');
        image.alt = '';
        image.width = 38;
        image.height = 38;
        const blobPromise = padRenderIconPNG(records[index], 40, padIconPickerResultColor());
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

function padIconPickerClose(session) {
    if (!session || padIconPickerSession !== session) return;
    padIconPickerSession = null;
    if (session.retryTimer) clearTimeout(session.retryTimer);
    clearTimeout(session.searchTimer);
    session.metadataController.abort();
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
        '<div class="icon-picker-tools"><input class="form-control" type="search" data-icon-search placeholder="Search icons" aria-label="Search icons">' +
        '<select class="form-control" data-icon-source aria-label="Icon source"><option value="all">All sources</option>' +
        '<option value="mono">Material Symbols</option><option value="color">Noto emoji</option></select>' +
        '<select class="form-control" data-icon-category aria-label="Icon category"><option value="all">Loading categories...</option></select></div>' +
        '<div class="icon-picker-status" data-icon-status role="status" aria-live="polite"></div>' +
        '<div class="icon-picker-results" data-icon-results><div class="icon-picker-grid" data-icon-grid></div>' +
        '<p class="icon-picker-empty" data-icon-empty hidden>No matching icons.</p></div>' +
        '<div class="icon-picker-result-footer"><span data-icon-results-label></span><span data-icon-count></span>' +
        '<button type="button" class="btn btn-secondary btn-sm" data-icon-more hidden>Show more</button></div>' +
        '<button type="button" class="btn btn-secondary btn-sm" data-icon-retry hidden>Retry</button>' +
        '<section class="icon-picker-preview"><div class="icon-picker-preview-surface" data-icon-preview>' +
        '<img data-icon-preview-image alt="" hidden></div><span data-icon-preview-name></span></section></div>' +
        '<footer><span>Selection applies to the button draft.</span><div>' +
        '<button type="button" class="btn btn-secondary" data-icon-cancel>Cancel</button>' +
        '<button type="button" class="btn btn-primary" data-icon-use disabled>Use icon</button></div></footer>';
    document.body.appendChild(dialog);
    const session = {
        dialog,
        opener: document.activeElement,
        urls: new Set(),
        collections: null,
        matching: [],
        visibleCount: 48,
        selected: null,
        searchGeneration: 0,
        selectionGeneration: 0,
        metadataController: new AbortController(),
        searchController: null,
        selectionController: null,
        colors: typeof padIconEditorColors === 'function' ? padIconEditorColors() : { background: '#ffffff', foreground: '#222222' },
    };
    padIconPickerSession = session;
    dialog.querySelector('[data-icon-close]').onclick = () => padIconPickerClose(session);
    dialog.querySelector('[data-icon-cancel]').onclick = () => padIconPickerClose(session);
    dialog.oncancel = event => { event.preventDefault(); padIconPickerClose(session); };
    dialog.querySelector('[data-icon-search]').addEventListener('input', () => {
        clearTimeout(session.searchTimer);
        session.searchTimer = setTimeout(() => padIconPickerRenderResults(session), 300);
    });
    dialog.querySelector('[data-icon-category]').addEventListener('change', () => padIconPickerRenderResults(session));
    dialog.querySelector('[data-icon-source]').addEventListener('change', () => padIconPickerRenderResults(session));
    dialog.querySelector('[data-icon-retry]').onclick = () => padIconPickerLoadCollections(session)
        .then(() => padIconPickerRenderResults(session))
        .catch(error => {
            padIconPickerSetStatus(session, error.message || 'Icon categories are unavailable.');
            padIconPickerShowRetry(session, () => padIconPickerLoadCollections(session));
        });
    dialog.querySelector('[data-icon-more]').onclick = async event => {
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
    };
    dialog.querySelector('[data-icon-use]').onclick = () => {
        if (!session.selected) return;
        const selected = session.selected;
        padRegisterDraftIconSource(selected.record);
        padIconEditorSetDraft({ catalog_id: selected.icon.catalog_id, kind: selected.icon.kind });
        if (typeof padWorkspaceEdit === 'function') padWorkspaceEdit();
        padIconPickerClose(session);
    };
    dialog.showModal();
    padIconPickerLoadCollections(session).catch(error => {
        if (padIconPickerSession !== session) return;
        dialog.querySelector('[data-icon-results-label]').textContent = 'Catalog unavailable';
        padIconPickerSetStatus(session, error.message || 'Icon categories are unavailable.');
        padIconPickerShowRetry(session, () => padIconPickerLoadCollections(session).catch(retryError => {
            padIconPickerSetStatus(session, retryError.message || 'Icon categories are unavailable.');
        }));
    });
}