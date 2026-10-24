let padWorkspace = null;

function padWorkspaceInit() {
    const root = document.getElementById('pad-editor-root');
    if (!root) return;
    if (padWorkspace) padWorkspaceDispose();
    const controller = new AbortController();
    padWorkspace = { root, controller, selected: false, scope: 'pad', tab: 'Layout', forms: new Map(), errors: new Map(), revision: 0, loading: false };
    const element = id => root.querySelector('#' + id);
    element('pad-workspace-commands').appendChild(element('pad-floating-footer'));
    const canvas = element('pad-workspace-canvas');
    ['pad-block-banner', 'pad-grid'].forEach(id => canvas.appendChild(element(id)));
    const fields = element('pad-workspace-pad-fields');
    const controls = root.querySelector('.pad-controls');
    controls.dataset.padTab = 'Layout';
    fields.appendChild(controls);
    root.querySelectorAll('[data-pad-tab]').forEach(node => fields.appendChild(node));
    element('pad-workspace-button-fields').appendChild(element('pad-edit-overlay'));
    const body = root.querySelector('.pad-edit-body');
    Array.from(body.children).forEach(node => {
        const actions = node.querySelector('#pad-action-editors');
        const appearance = ['pad-edit-colors-section', 'pad-edit-btn-state-section'].includes(node.id);
        node.dataset.buttonTab = actions ? 'Actions' : appearance ? 'Appearance' : 'Content';
    });
    const on = (node, event, callback) => node.addEventListener(event, callback, { signal: controller.signal });
    on(canvas, 'click', event => {
        if (event.target !== canvas || !padWorkspace.selected) return;
        padWorkspaceSetScope('pad');
        padWorkspaceReturn();
    });
    root.addEventListener('wheel', padWorkspaceWheel, { passive: false, signal: controller.signal });
    on(element('pad-workspace-pad-scope'), 'click', () => padWorkspaceSetScope('pad'));
    on(element('pad-workspace-button-scope'), 'click', () => padWorkspaceSetScope('button'));
    on(element('pad-workspace-back'), 'click', padWorkspaceReturn);
    on(element('pad-workspace-mobile-save'), 'click', () => element('pad-save-btn').click());
    root.querySelectorAll('[data-pad-layer]').forEach(button => {
        on(button, 'click', () => padWorkspaceSetLayer(button.dataset.padLayer));
    });
    ['input', 'change'].forEach(event => {
        on(element('pad-edit-overlay'), event, padWorkspaceEdit);
        on(fields, event, () => {
            if (!padWorkspace.loading) { padMarkDirty(); padRenderGrid(); padWorkspaceSummaries(); padWorkspaceRefresh(); }
        });
    });
    on(element('pad-edit-overlay'), 'click', event => {
        if (event.target.closest('.reset-hint')) padWorkspaceEdit();
    });
    on(root, 'click', event => {
        const choice = event.target.closest('[data-pad-choice]');
        if (choice) padWorkspaceSwitch(Number(choice.dataset.padChoice));
    });
    on(document, 'click', event => {
        const menu = element('pad-more-menu');
        if (menu && !event.target.closest('#pad-more-btn')) menu.style.display = 'none';
    });
    on(document, 'keydown', event => {
        if (event.key === 'Escape' && padState.placingBlock) padExitPlacementMode();
    });
    on(window, 'beforeunload', event => {
        if (padDirty && root.isConnected) { event.preventDefault(); event.returnValue = ''; }
    });
    padWorkspaceSetScope('pad');
    root.classList.remove('pad-workspace-inspecting');
    if (typeof ResizeObserver !== 'undefined') {
        padWorkspace.resizeObserver = new ResizeObserver(padWorkspaceFit);
        padWorkspace.resizeObserver.observe(canvas);
        padWorkspace.resizeObserver.observe(document.getElementById('content-pane'));
    }
}

function padWorkspaceWheel(event) {
    if (event.ctrlKey || event.target.type !== 'number') return;
    event.preventDefault();
    let container = event.target.parentElement;
    while (container) {
        const style = getComputedStyle(container);
        const unit = event.deltaMode === 1 ? 16 : event.deltaMode === 2 ? container.clientHeight : 1;
        const top = event.deltaY * unit;
        const left = event.deltaX * unit;
        const vertical = /^(auto|scroll)$/.test(style.overflowY) &&
            (top < 0 ? container.scrollTop > 0 : top > 0 && container.scrollTop + container.clientHeight < container.scrollHeight);
        const horizontal = /^(auto|scroll)$/.test(style.overflowX) &&
            (left < 0 ? container.scrollLeft > 0 : left > 0 && container.scrollLeft + container.clientWidth < container.scrollWidth);
        if (vertical || horizontal || container === document.scrollingElement) {
            container.scrollBy({ top, left, behavior: 'instant' });
            return;
        }
        container = container.parentElement;
    }
}

function padWorkspaceFit() {
    if (!padWorkspace) return;
    const home = document.querySelector('.nav-welcome-link');
    if (home && window.innerWidth > 980) {
        const top = padWorkspace.root.getBoundingClientRect().top + document.getElementById('content-pane').scrollTop;
        padWorkspace.root.style.setProperty('--pad-workspace-toolbar-height', Math.max(0, home.getBoundingClientRect().bottom - 1 - top) + 'px');
    }
    const content = document.getElementById('content-pane');
    const workspace = padWorkspace.root.querySelector('.pad-workspace');
    const toolbar = padWorkspace.root.querySelector('.pad-workspace-toolbar');
    const inspector = document.getElementById('pad-workspace-inspector');
    padWorkspace.root.style.setProperty('--pad-inspector-left', (inspector.getBoundingClientRect().left - toolbar.getBoundingClientRect().left) + 'px');
    const offset = workspace.getBoundingClientRect().top - content.getBoundingClientRect().top + content.scrollTop;
    const height = content.clientHeight - offset - parseFloat(getComputedStyle(content).paddingBottom);
    padWorkspace.root.style.setProperty('--pad-workspace-height', Math.max(0, height) + 'px');
    const canvas = document.getElementById('pad-workspace-canvas');
    const ratio = deviceInfoCache && deviceInfoCache.display_coord_width && deviceInfoCache.display_coord_height ?
        deviceInfoCache.display_coord_width / deviceInfoCache.display_coord_height : 1;
    const width = Math.min(canvas.clientWidth - 32, (canvas.clientHeight - 32) * ratio);
    const grid = document.getElementById('pad-grid');
    grid.style.width = Math.max(0, width) + 'px';
    grid.style.height = Math.max(0, width / ratio) + 'px';
    const scale = Math.max(0, width) / ((deviceInfoCache && deviceInfoCache.display_coord_width) || width || 1);
    const reserve = padGetEffectiveDefault('pixel_shift_distance_px');
    grid.style.gap = padGetEffectiveDefault('button_spacing_px') * scale + 'px';
    grid.style.padding = ['top', 'right', 'bottom', 'left'].map(edge => (reserve + padGetEffectiveDefault('pad_inset_' + edge + '_px')) * scale + 'px').join(' ');
    if (typeof padRenderGridTracks === 'function') padRenderGridTracks();
}

function padWorkspaceMoveSelection(col, row, button) {
    if (!padWorkspace || !padWorkspace.selected || padState.editCol !== col || padState.editRow !== row) return;
    const oldKey = padWorkspaceButtonKey(padState.selectedLayer, col, row);
    const saved = padWorkspace.forms.get(oldKey);
    if (saved) {
        saved.values.forEach(field => {
            if (field.id === 'pad-edit-col-span') field.value = String(button.col_span || 1);
            if (field.id === 'pad-edit-row-span') field.value = String(button.row_span || 1);
        });
    }
    const newKey = padWorkspaceButtonKey(padState.selectedLayer, button.col, button.row);
    padWorkspace.forms.delete(oldKey);
    if (saved) padWorkspace.forms.set(newKey, saved);
    const error = padWorkspace.errors.get(oldKey);
    padWorkspace.errors.delete(oldKey);
    if (error) padWorkspace.errors.set(newKey, error);
    padWorkspace.selected = false;
    padDialogOpen(button.col, button.row);
}

function padWorkspaceDispose() {
    if (!padWorkspace) return;
    if (typeof padButtonBuilderClose === 'function') padButtonBuilderClose();
    padWorkspace.controller.abort();
    if (padWorkspace.resizeObserver) padWorkspace.resizeObserver.disconnect();
    if (padWorkspace.confirmResolve) padWorkspace.confirmResolve(false);
    if (typeof padPvHide === 'function') padPvHide();
    if (typeof padWidgetEditorsDispose === 'function') padWidgetEditorsDispose();
    padWorkspace = null;
    padDirty = false;
}

function padWorkspaceButtonKey(layer, col, row) { return layer + ':' + col + ',' + row; }
function padWorkspaceKey() { return padWorkspaceButtonKey(padState.selectedLayer, padState.editCol, padState.editRow); }

function padWorkspaceSetLayer(layer) {
    if (!padWorkspace || (layer !== 'main' && layer !== 'overlay') || layer === padState.selectedLayer) return;
    if (padWorkspace.selected) {
        padWorkspaceCapture();
        padState.layerSelections[padState.selectedLayer] = { col: padState.editCol, row: padState.editRow };
    }
    padWorkspace.selected = false;
    padPvHide();
    padState.selectedLayer = layer;
    padWorkspaceSetScope('pad');
    padWorkspaceRenderLayer();
    padRenderGrid();
    const selection = padState.layerSelections[layer];
    if (selection) padDialogOpen(selection.col, selection.row);
    else padWorkspaceReturn();
}

function padWorkspaceRenderLayer() {
    if (!padWorkspace) return;
    const overlay = padState.selectedLayer === 'overlay';
    padWorkspace.root.classList.toggle('pad-overlay-editing', overlay);
    padWorkspace.root.querySelectorAll('[data-pad-layer]').forEach(button => {
        button.setAttribute('aria-pressed', String(button.dataset.padLayer === padState.selectedLayer));
    });
    const capacity = document.getElementById('pad-layer-capacity');
    if (capacity) capacity.textContent = (padState.mainButtons.length + padState.overlayButtons.length) + ' / ' +
        padMaxButtons() + ' button slots used';
    const fields = document.getElementById('pad-workspace-pad-fields');
    if (!fields) return;
    fields.querySelectorAll('input, select, textarea, button').forEach(control => {
        if (overlay) {
            if (!control.hasAttribute('data-main-disabled')) {
                control.dataset.mainDisabled = String(Boolean(control.disabled));
            }
            control.disabled = true;
        } else if (control.hasAttribute('data-main-disabled')) {
            control.disabled = control.dataset.mainDisabled === 'true';
            delete control.dataset.mainDisabled;
        }
    });
}

function padWorkspaceSelect(col, row) {
    if (padWorkspace && padWorkspace.selected && padState.editCol === col && padState.editRow === row) {
        padWorkspaceSetScope('pad');
        padWorkspaceReturn();
        return;
    }
    padDialogOpen(col, row);
    if (typeof padButtonBuilderAfterSelect === 'function') padButtonBuilderAfterSelect();
}

function padWorkspaceCapture() {
    if (!padWorkspace || !padWorkspace.selected) return;
    const values = [];
    padWorkspace.root.querySelectorAll('#pad-edit-overlay input, #pad-edit-overlay select, #pad-edit-overlay textarea').forEach(input => {
        if (input.id) values.push({ id: input.id, value: input.value, checked: input.checked });
    });
    const details = [];
    padWorkspace.root.querySelectorAll('#pad-edit-overlay details').forEach((node, index) => details.push({ index, open: node.open }));
    padWorkspace.forms.set(padWorkspaceKey(), { values, details, scroll: padWorkspace.root.querySelector('.pad-edit-body').scrollTop });
}

function padWorkspaceBeforeSelect() {
    if (!padWorkspace) return;
    padWorkspaceCapture();
    padWorkspace.loading = true;
}

function padWorkspaceAfterSelect() {
    if (!padWorkspace) return;
    const saved = padWorkspace.forms.get(padWorkspaceKey());
    if (saved) {
        saved.values.forEach(field => {
            const input = document.getElementById(field.id);
            if (input) { input.value = field.value; input.checked = field.checked; }
        });
        padActionPrefixes('tap').concat(padActionPrefixes('lp'), padWidgetActionPrefixes()).forEach(actionEditorTypeChanged);
        padWidgetTypeChanged();
        padIconEditorRefresh();
        saved.details.forEach(field => {
            const node = padWorkspace.root.querySelectorAll('#pad-edit-overlay details')[field.index];
            if (node) node.open = field.open;
        });
    } else {
        padWorkspace.root.querySelectorAll('#pad-edit-overlay .action-list-slot').forEach(node => { node.open = false; });
    }
    padWorkspace.selected = true;
    padState.layerSelections[padState.selectedLayer] = { col: padState.editCol, row: padState.editRow };
    padWorkspace.loading = false;
    padWorkspaceSetScope('button');
    const error = padWorkspace.errors.get(padWorkspaceKey());
    if (error) padDialogShowValidationError(error); else padDialogClearValidationError();
    padWorkspace.root.querySelector('.pad-edit-body').scrollTop = saved ? saved.scroll : 0;
    padWorkspaceCapture();
    padWorkspaceSummaries();
    padRenderGrid();
    if (error) padWorkspaceEdit(true);
}

function padWorkspaceWidgetSettled(type) {
    if (padWorkspace && padWorkspace.selected &&
        document.getElementById('pad-edit-widget-type').value === type && padWorkspace.errors.has(padWorkspaceKey())) {
        padWorkspaceEdit(true);
    }
}

function padWorkspaceEdit(revalidate) {
    if (!padWorkspace || padWorkspace.loading || !padWorkspace.selected) return;
    const before = padWorkspace.forms.get(padWorkspaceKey());
    padWorkspaceCapture();
    const after = padWorkspace.forms.get(padWorkspaceKey());
    if (revalidate !== true && before && JSON.stringify(before.values) === JSON.stringify(after.values)) return;
    if (revalidate !== true) padMarkDirty();
    try {
        const button = padDialogBuildButton();
        padDialogValidateButton(button);
        padState.buttons = padState.buttons.filter(item => item.col !== button.col || item.row !== button.row);
        padState.buttons.push(button);
        if (typeof padPruneDraftIconSources === 'function') padPruneDraftIconSources();
        padWorkspace.errors.delete(padWorkspaceKey());
        padDialogClearValidationError();
        padRenderGrid();
    } catch (error) {
        padWorkspace.errors.set(padWorkspaceKey(), error);
        padDialogShowValidationError(error);
    }
    padWorkspaceSummaries();
    padWorkspaceRefresh();
}

function padWorkspaceSummaries() {
    if (!padWorkspace) return;
    padActionPrefixes('tap').concat(padActionPrefixes('lp'), padLevelActionPrefixes()).forEach(prefix => {
        const group = document.getElementById(prefix + '-group');
        const type = document.getElementById(prefix + '-type');
        if (!group || !type || !type.value) return;
        const summary = group.querySelector('summary');
        const option = Array.from(type.options).find(option => option.value === type.value);
        const detail = ['target', 'topic', 'entity', 'text', 'url', 'keys'].map(suffix => document.getElementById(prefix + '-' + suffix)).find(input => {
            if (!input || !input.value) return false;
            for (let node = input; node && node !== group; node = node.parentElement) {
                if (node.style && node.style.display === 'none') return false;
            }
            return true;
        });
        const detailText = detail && detail.tagName === 'SELECT' ? detail.options[detail.selectedIndex].textContent : detail && detail.value;
        if (summary) summary.textContent = group.dataset.slotLabel + ': ' + (option ? option.textContent : type.value) + (detailText ? ' · ' + detailText : '');
    });
}

function padWorkspaceValidate() {
    if (!padWorkspace) return;
    while (padWorkspace.errors.size) {
        const [key] = padWorkspace.errors.entries().next().value;
        const separator = key.indexOf(':');
        const layer = key.slice(0, separator);
        const position = key.slice(separator + 1).split(',').map(Number);
        if (layer !== padState.selectedLayer) padWorkspaceSetLayer(layer);
        padDialogOpen(position[0], position[1]);
        const message = padWorkspace.errors.get(key);
        if (!message) continue;
        padDialogFocusValidationError(message);
        throw Object.assign(new Error('Button [' + layer + ' ' + position.join(',') + ']: ' + (message.message || message)), { fieldIds: message.fieldIds || [] });
    }
    for (const [layer, buttons] of [['main', padState.mainButtons], ['overlay', padState.overlayButtons]]) {
        const occupied = new Set();
        buttons.forEach(button => {
            const cols = button.col_span || 1;
            const rows = button.row_span || 1;
            if (button.col < 0 || button.row < 0 || button.col + cols > padState.cols || button.row + rows > padState.rows) throw new Error('A button is outside the pad. Adjust its position or grid size.');
            for (let row = button.row; row < button.row + rows; row++) {
                for (let col = button.col; col < button.col + cols; col++) {
                    const key = col + ',' + row;
                    if (occupied.has(key)) throw new Error(layer + ' buttons overlap at [' + key + ']');
                    occupied.add(key);
                }
            }
        });
    }
    const max = padMaxButtons();
    if (padState.mainButtons.length + padState.overlayButtons.length > max)
        throw new Error('Combined Main and Overlay button limit exceeded (' + max + ')');
}

function padWorkspaceClearButton(col, row) {
    if (!padWorkspace) return;
    const key = padWorkspaceButtonKey(padState.selectedLayer, col, row);
    padWorkspace.forms.delete(key);
    padWorkspace.errors.delete(key);
    padWorkspace.selected = false;
    padWorkspaceSetScope('pad');
}

function padWorkspaceReset(layer) {
    if (!padWorkspace) return;
    if (layer) {
        for (const entries of [padWorkspace.forms, padWorkspace.errors]) {
            for (const key of entries.keys()) {
                if (key.startsWith(layer + ':')) entries.delete(key);
            }
        }
        padState.layerSelections[layer] = null;
    } else {
        padWorkspace.forms.clear();
        padWorkspace.errors.clear();
        padState.layerSelections = { main: null, overlay: null };
    }
    padWorkspace.selected = false;
    padWorkspaceSetScope('pad');
    padWorkspace.root.classList.remove('pad-workspace-inspecting');
}

function padWorkspaceReturn() {
    if (!padWorkspace) return;
    padWorkspaceCapture();
    padWorkspace.root.classList.remove('pad-workspace-inspecting');
}

function padWorkspaceSetScope(scope) {
    if (!padWorkspace) return;
    if (scope === 'button' && !padWorkspace.selected) return;
    if (scope === 'pad') {
        padWorkspaceCapture();
        padWorkspace.selected = false;
    }
    const sameScope = padWorkspace.scope === scope;
    padWorkspace.scope = scope;
    if (!sameScope) padWorkspace.tab = scope === 'button' ? 'Content' : 'Layout';
    padWorkspace.root.classList.add('pad-workspace-inspecting');
    const tabs = scope === 'button' ? ['Content', 'Actions', 'Appearance'] : ['Layout', 'Appearance', 'Bindings', 'Actions'];
    const container = document.getElementById('pad-workspace-tabs');
    container.replaceChildren();
    tabs.forEach(tab => {
        const button = document.createElement('button');
        button.type = 'button';
        button.textContent = tab;
        button.setAttribute('role', 'tab');
        button.addEventListener('click', () => { padWorkspace.tab = tab; padWorkspaceRenderTabs(); });
        container.appendChild(button);
    });
    padWorkspaceRenderTabs();
    padWorkspaceRefresh();
}

function padWorkspaceRenderTabs() {
    if (!padWorkspace) return;
    const root = padWorkspace.root;
    const buttonScope = padWorkspace.scope === 'button';
    document.getElementById('pad-workspace-pad-fields').hidden = buttonScope;
    document.getElementById('pad-workspace-button-fields').hidden = !buttonScope;
    root.querySelectorAll('[data-button-tab]').forEach(node => { node.hidden = node.dataset.buttonTab !== padWorkspace.tab; });
    root.querySelectorAll('[data-pad-tab]').forEach(node => { node.hidden = node.dataset.padTab !== padWorkspace.tab; });
    root.querySelectorAll('#pad-workspace-tabs button').forEach(button => button.setAttribute('aria-selected', String(button.textContent === padWorkspace.tab)));
    ['button', 'pad'].forEach(scope => document.getElementById('pad-workspace-' + scope + '-scope').setAttribute('aria-pressed', String(scope === padWorkspace.scope)));
    document.getElementById('pad-workspace-button-scope').disabled = !padWorkspace.selected;
}

function padWorkspaceRefresh() {
    if (!padWorkspace || !padWorkspace.root.isConnected) return;
    if (typeof padButtonBuilderUpdateEntry === 'function') padButtonBuilderUpdateEntry();
    padWorkspaceFit();
    const name = document.getElementById('pad-name').value;
    document.getElementById('pad-workspace-status').textContent = padSaveInProgress ? 'Saving...' : padDirty ? 'Unsaved edits' : '';
    const save = document.getElementById('pad-save-btn');
    save.disabled = padSaveInProgress || padWorkspace.loading;
    document.getElementById('pad-workspace-mobile-save').disabled = save.disabled;
    padWorkspaceRenderLayer();
    document.querySelectorAll('#pad-grid .pad-cell').forEach(cell => {
        const selected = padWorkspace.selected && cell.dataset.layer === padState.selectedLayer && Number(cell.dataset.col) === padState.editCol && Number(cell.dataset.row) === padState.editRow;
        cell.classList.toggle('pad-cell-selected', selected);
        cell.setAttribute('role', 'button');
        cell.setAttribute('aria-label', cell.dataset.layer + ' button [' + cell.dataset.col + ', ' + cell.dataset.row + ']');
        cell.setAttribute('aria-pressed', String(selected));
        cell.tabIndex = 0;
        cell.onkeydown = event => {
            if (event.target === cell && (event.key === 'Enter' || event.key === ' ')) { event.preventDefault(); cell.click(); }
        };
    });
    const list = document.getElementById('pad-workspace-pads');
    const choices = Array.from({ length: (deviceInfoCache && deviceInfoCache.max_pads) || 8 }, (_, page) => {
        const label = 'Pad ' + (page + 1);
        const screen = ((deviceInfoCache && deviceInfoCache.available_screens) || []).find(screen => screen.id === 'pad_' + page);
        const padName = page === padState.page ? name : screen ? screen.name.replace(/^Pad \d+: /, '').replace(/^Pad \d+$/, '') : '';
        return { value: page, name: label + (padName ? ': ' + padName : '') };
    });
    const signature = JSON.stringify([padState.page, choices]);
    if (signature === padWorkspace.railSignature) return;
    padWorkspace.railSignature = signature;
    list.replaceChildren();
    choices.forEach(option => {
        const button = document.createElement('button');
        button.type = 'button';
        button.dataset.padChoice = option.value;
        button.textContent = option.name;
        button.setAttribute('aria-current', String(Number(option.value) === padState.page));
        list.appendChild(button);
    });
}

function padWorkspaceConfirm(message) {
    if (!padWorkspace) return Promise.resolve(true);
    if (padWorkspace.confirmResolve) return Promise.resolve(false);
    const dialog = document.getElementById('pad-workspace-discard');
    document.getElementById('pad-workspace-discard-message').textContent = message;
    dialog.showModal();
    return new Promise(resolve => {
        const workspace = padWorkspace;
        const finish = discard => {
            dialog.close();
            workspace.confirmResolve = null;
            resolve(discard);
        };
        workspace.confirmResolve = finish;
        document.getElementById('pad-workspace-keep').onclick = () => finish(false);
        document.getElementById('pad-workspace-discard-confirm').onclick = () => finish(true);
        dialog.oncancel = event => { event.preventDefault(); finish(false); };
    });
}

async function padWorkspaceSwitch(page) {
    if (!padWorkspace || page === padState.page) return;
    const workspace = padWorkspace;
    if (padDirty && !await padWorkspaceConfirm('Discard edits and switch pads?')) return;
    if (workspace !== padWorkspace) return;
    await padLoadPage(page);
    if (workspace !== padWorkspace) return;
    padWorkspaceRefresh();
}