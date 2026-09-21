// portal_recipes.js - Recipe catalog loading and provisioning.
// Recipes are browser-supplied declarative pad layouts. The device remains
// the authority for component and pad validation when each change is saved.

const RECIPE_CATALOG_SCHEMA = 1;
const RECIPE_CATALOG_STORAGE_KEY = 'esp32-macropad.recipe-catalog';
const RECIPE_CATALOG_MAX_BYTES = 64 * 1024;

let recipeState = {
    catalog: null,
    selected: null,
    pad: null,
    placement: null,
    buttonSizes: null,
    installedPadPage: null,
    emptyPlacement: null,
    resizeSuggestion: null,
    installationComplete: false,
};

const RECIPE_SHAPES = {
    square: 1,
    wide: 1.75,
    tall: 1 / 1.75,
    very_wide: 3,
    very_tall: 1 / 3,
};

const RECIPE_SIZE_MIN_SPANS = { S: 1, M: 2, L: 3 };

function recipeError(message) { throw new Error(message); }

function recipeIsObject(value) {
    return value !== null && typeof value === 'object' && !Array.isArray(value);
}

function recipeBounds(buttons) {
    let cols = 0;
    let rows = 0;
    buttons.forEach(function (button) {
        cols = Math.max(cols, button.col_offset + (button.col_span || 1));
        rows = Math.max(rows, button.row_offset + (button.row_span || 1));
    });
    return { cols: cols, rows: rows };
}

function recipeIsFlowLayout(layout) {
    return layout.flow !== undefined;
}

function recipeValidateCatalog(catalog) {
    if (!recipeIsObject(catalog) || catalog.schema !== RECIPE_CATALOG_SCHEMA ||
        typeof catalog.catalog_version !== 'string' || !Array.isArray(catalog.recipes)) {
        recipeError('Catalog must contain schema, catalog_version, and recipes.');
    }
    const seen = new Set();
    catalog.recipes.forEach(function (recipe) {
        if (!recipeIsObject(recipe) || !/^[a-z0-9][a-z0-9-]*$/.test(recipe.id || '') ||
            typeof recipe.name !== 'string' || !recipe.name ||
            typeof recipe.description !== 'string' || !recipe.description ||
            !recipeIsObject(recipe.layout) || !Array.isArray(recipe.layout.buttons) ||
            recipe.layout.buttons.length === 0) {
            recipeError('Every recipe needs id, name, description, and layout.buttons.');
        }
        if (seen.has(recipe.id)) recipeError('Recipe ids must be unique.');
        seen.add(recipe.id);
        if (recipe.requires !== undefined &&
            (!Array.isArray(recipe.requires) || recipe.requires.some(function (item) { return !['display', 'mqtt', 'audio'].includes(item); }))) {
            recipeError('Recipe requirements must be display, mqtt, or audio.');
        }
        if (recipe.parameters !== undefined && !Array.isArray(recipe.parameters)) {
            recipeError('Recipe parameters must be an array.');
        }
        const parameterIds = new Set();
        (recipe.parameters || []).forEach(function (parameter) {
            if (!recipeIsObject(parameter) || !/^[a-z][a-z0-9_]*$/.test(parameter.id || '') ||
                typeof parameter.label !== 'string' || !parameter.label ||
                !['string', 'number'].includes(parameter.type) ||
                parameter.default === undefined || parameterIds.has(parameter.id) ||
                (parameter.description !== undefined &&
                    (typeof parameter.description !== 'string' || !parameter.description))) {
                recipeError('Each parameter needs a unique id, label, type, and default.');
            }
            parameterIds.add(parameter.id);
        });
        if (recipeIsFlowLayout(recipe.layout)) {
            if (!['row', 'column'].includes(recipe.layout.flow)) {
                recipeError('Recipe flow must be row or column.');
            }
            recipe.layout.buttons.forEach(function (button) {
                if (!recipeIsObject(button) || !Object.prototype.hasOwnProperty.call(RECIPE_SHAPES, button.shape) ||
                    !Object.prototype.hasOwnProperty.call(RECIPE_SIZE_MIN_SPANS, button.size) ||
                    button.col_offset !== undefined || button.row_offset !== undefined ||
                    button.col_span !== undefined || button.row_span !== undefined) {
                    recipeError('Flow recipe buttons need size and shape, without fixed positions or spans.');
                }
            });
        } else {
            const cells = new Set();
            recipe.layout.buttons.forEach(function (button) {
                if (!recipeIsObject(button) || !Number.isInteger(button.col_offset) ||
                    !Number.isInteger(button.row_offset) || button.col_offset < 0 || button.row_offset < 0) {
                    recipeError('Recipe buttons need non-negative col_offset and row_offset.');
                }
                const colSpan = button.col_span || 1;
                const rowSpan = button.row_span || 1;
                if (!Number.isInteger(colSpan) || !Number.isInteger(rowSpan) || colSpan < 1 || rowSpan < 1) {
                    recipeError('Recipe button spans must be positive whole numbers.');
                }
                for (let row = button.row_offset; row < button.row_offset + rowSpan; row++) {
                    for (let col = button.col_offset; col < button.col_offset + colSpan; col++) {
                        const key = col + ',' + row;
                        if (cells.has(key)) recipeError('Recipe buttons must not overlap.');
                        cells.add(key);
                    }
                }
            });
            const bounds = recipeBounds(recipe.layout.buttons);
            if (bounds.cols > 8 || bounds.rows > 8) recipeError('Recipe layout must fit within an 8x8 pad.');
        }
        if (recipe.provision !== undefined) {
            if (!recipeIsObject(recipe.provision) ||
                (recipe.provision.pad !== undefined && !recipeIsObject(recipe.provision.pad)) ||
                (recipe.provision.components !== undefined && !recipeIsObject(recipe.provision.components))) {
                recipeError('Recipe provision must contain pad and components objects.');
            }
            if (recipe.provision.pad !== undefined) {
                const padProvision = recipe.provision.pad;
                if (Object.keys(padProvision).some(function (key) { return key !== 'bindings'; }) ||
                    (padProvision.bindings !== undefined && !recipeIsObject(padProvision.bindings))) {
                    recipeError('Recipe pad provision supports only named bindings.');
                }
                if (padProvision.bindings !== undefined) {
                    Object.keys(padProvision.bindings).forEach(function (name) {
                        if (!/^[a-z][a-z0-9_]*$/.test(name) || typeof padProvision.bindings[name] !== 'string') {
                            recipeError('Recipe bindings need valid names and string values.');
                        }
                    });
                }
            }
        }
        if (recipe.components !== undefined && !recipeIsObject(recipe.components)) {
            recipeError('Recipe components must be an object.');
        }
        if (recipe.provision !== undefined && recipe.components !== undefined) {
            recipeError('Use provision.components instead of components.');
        }
    });
    return catalog;
}

function recipeLoadCatalogJson(text) {
    if (typeof text !== 'string' || new Blob([text]).size > RECIPE_CATALOG_MAX_BYTES) {
        recipeError('Catalog exceeds the 64 KiB limit.');
    }
    let catalog;
    try { catalog = JSON.parse(text); } catch (error) { recipeError('Catalog is not valid JSON.'); }
    return recipeValidateCatalog(catalog);
}

function recipeActiveCatalog() {
    const text = sessionStorage.getItem(RECIPE_CATALOG_STORAGE_KEY);
    if (!text) return { catalog: { schema: RECIPE_CATALOG_SCHEMA, catalog_version: 'empty', recipes: [] }, source: 'Empty catalog' };
    return { catalog: recipeLoadCatalogJson(text), source: 'Pasted catalog override' };
}

function recipeSupports(recipe) {
    return recipeMissingRequirements(recipe).length === 0;
}

function recipeMissingRequirements(recipe) {
    const info = deviceInfoCache || {};
    return (recipe.requires || []).filter(function (requirement) {
        return requirement !== 'display' && info['has_' + requirement] !== true;
    });
}

function recipeImpactSummary(recipe) {
    const buttonCount = recipe.layout.buttons.length;
    const parts = ['Adds ' + buttonCount + ' button' + (buttonCount === 1 ? '' : 's')];
    if (recipeIsFlowLayout(recipe.layout)) {
        parts.push('adaptive ' + recipe.layout.flow + ' layout');
    } else {
        const bounds = recipeBounds(recipe.layout.buttons);
        parts.push('needs a ' + bounds.cols + 'x' + bounds.rows + ' free area');
    }
    const provision = recipeProvision(recipe);
    const bindingCount = Object.keys((provision.pad && provision.pad.bindings) || {}).length;
    const componentCount = Object.keys(provision.components || {}).length;
    if (bindingCount) parts.push('adds ' + bindingCount + ' named binding' + (bindingCount === 1 ? '' : 's'));
    if (componentCount) parts.push('configures ' + componentCount + ' component' + (componentCount === 1 ? '' : 's'));
    return parts.join(' | ');
}

function recipeRenderCatalog() {
    const source = document.getElementById('recipes-source');
    const list = document.getElementById('recipes-list');
    const empty = document.getElementById('recipes-empty');
    if (!source || !list || !empty) return;
    const active = recipeActiveCatalog();
    recipeState.catalog = active.catalog;
    if (recipeState.selected) {
        recipeState.selected = active.catalog.recipes.find(function (recipe) {
            return recipe.id === recipeState.selected.id;
        }) || null;
    }
    source.textContent = active.source + ' | Version ' + active.catalog.catalog_version;
    list.innerHTML = '';
    empty.hidden = active.catalog.recipes.length !== 0;
    active.catalog.recipes.forEach(function (recipe) {
        const button = document.createElement('button');
        button.type = 'button';
        button.className = 'recipes-list-item';
        button.disabled = !recipeSupports(recipe);
        const selected = recipeState.selected && recipeState.selected.id === recipe.id;
        button.classList.toggle('selected', selected);
        button.setAttribute('aria-pressed', selected ? 'true' : 'false');
        const title = document.createElement('strong');
        title.textContent = recipe.name;
        const description = document.createElement('span');
        const missing = recipeMissingRequirements(recipe);
        description.textContent = missing.length
            ? 'Requires ' + missing.join(' and ') + '.' : recipe.description;
        button.append(title, description);
        button.addEventListener('click', function () { recipeSelect(recipe); });
        list.appendChild(button);
    });
}

function recipeSelect(recipe) {
    recipeState.selected = recipe;
    recipeState.pad = null;
    recipeState.placement = null;
    recipeState.installedPadPage = null;
    recipeState.installationComplete = false;
    document.getElementById('recipe-post-install-actions').hidden = true;
    recipeRenderCatalog();
    document.getElementById('recipe-detail').hidden = false;
    document.getElementById('recipe-detail-name').textContent = recipe.name;
    document.getElementById('recipe-detail-description').textContent = recipe.description;
    document.getElementById('recipe-impact-summary').textContent = recipeImpactSummary(recipe);
    const parameters = document.getElementById('recipe-parameters');
    parameters.innerHTML = '';
    (recipe.parameters || []).forEach(function (parameter) {
        const group = document.createElement('div');
        group.className = 'form-group mb-3';
        const label = document.createElement('label');
        label.htmlFor = 'recipe-parameter-' + parameter.id;
        label.textContent = parameter.label;
        const input = document.createElement('input');
        input.id = label.htmlFor;
        input.className = 'form-control';
        input.type = parameter.type === 'number' ? 'number' : 'text';
        input.value = String(parameter.default);
        group.append(label, input);
        if (parameter.description) {
            const description = document.createElement('div');
            description.className = 'form-text';
            description.textContent = parameter.description;
            group.appendChild(description);
        }
        parameters.appendChild(group);
    });
    recipePopulatePads();
}

function recipePopulatePads() {
    const select = document.getElementById('recipe-pad-select');
    if (!select) return;
    select.innerHTML = '';
    const maxPads = (deviceInfoCache && deviceInfoCache.max_pads) || 8;
    for (let index = 0; index < maxPads; index++) {
        const option = document.createElement('option');
        option.value = String(index);
        option.textContent = 'Pad ' + (index + 1);
        select.appendChild(option);
    }
    select.onchange = recipeLoadPad;
    recipeLoadPad();
}

async function recipeLoadPad() {
    const select = document.getElementById('recipe-pad-select');
    if (!select || !recipeState.selected) return;
    recipeState.installedPadPage = null;
    recipeState.installationComplete = false;
    document.getElementById('recipe-post-install-actions').hidden = true;
    const page = Number(select.value);
    const isFlow = recipeIsFlowLayout(recipeState.selected.layout);
    const bounds = isFlow ? null : recipeBounds(recipeState.selected.layout.buttons);
    const status = document.getElementById('recipe-placement-status');
    try {
        const response = await fetch('/api/pad?page=' + page);
        if (response.status === 404) {
            if (isFlow) throw new Error('Create this pad and choose its grid dimensions before installing an adaptive recipe.');
            recipeState.pad = { layout: 'grid', cols: bounds.cols, rows: bounds.rows, buttons: [], page: page };
        } else if (!response.ok) {
            throw new Error('HTTP ' + response.status);
        } else {
            recipeState.pad = await response.json();
            recipeState.pad.page = page;
        }
        recipeState.placement = null;
        recipeState.resizeSuggestion = null;
        recipeState.buttonSizes = await padGetButtonSizes(recipeState.pad.cols, recipeState.pad.rows);
        if (recipeState.pad.layout && recipeState.pad.layout !== 'grid') throw new Error('This pad does not use a grid layout.');
        const emptyPad = Object.assign({}, recipeState.pad, { buttons: [] });
        recipeState.emptyPlacement = recipeFindPlacement(recipeState.selected.layout, emptyPad, recipeState.buttonSizes);
        if (!recipeState.emptyPlacement) {
            recipeState.resizeSuggestion = await recipeFindMinimumResize(recipeState.selected.layout, recipeState.pad);
        }
        if (!isFlow && (recipeState.pad.cols < bounds.cols || recipeState.pad.rows < bounds.rows)) {
            status.textContent = 'This recipe needs a ' + bounds.cols + 'x' + bounds.rows + ' footprint.';
        } else {
            status.textContent = 'Select a free position for the recipe.';
        }
        recipeRenderPlacement();
    } catch (error) {
        recipeState.pad = null;
        status.textContent = 'Unable to load pad: ' + error.message;
        document.getElementById('recipe-placement-grid').innerHTML = '';
    }
}

function recipeRenderRecoveryActions(validCount) {
    const actions = document.getElementById('recipe-recovery-actions');
    const resize = document.getElementById('recipe-resize-grid-btn');
    const clear = document.getElementById('recipe-clear-buttons-btn');
    if (!actions || !resize || !clear) return;
    actions.hidden = true;
    resize.hidden = true;
    clear.hidden = true;
    if (validCount !== 0) return;
    if (recipeState.resizeSuggestion) {
        const suggestion = recipeState.resizeSuggestion;
        resize.textContent = 'Increase grid to ' + suggestion.cols + ' x ' + suggestion.rows;
        resize.hidden = false;
        actions.hidden = false;
        return;
    }
    if (recipeState.emptyPlacement && recipeState.pad && (recipeState.pad.buttons || []).length) {
        clear.hidden = false;
        actions.hidden = false;
    }
}

function recipeOccupiedCells(pad) {
    const occupied = new Set();
    (pad.buttons || []).forEach(function (button) {
        const colSpan = button.col_span || 1;
        const rowSpan = button.row_span || 1;
        for (let row = button.row; row < button.row + rowSpan; row++) {
            for (let col = button.col; col < button.col + colSpan; col++) occupied.add(col + ',' + row);
        }
    });
    return occupied;
}

function recipeRectangleFits(pad, occupied, col, row, colSpan, rowSpan) {
    if (col < 0 || row < 0 || col + colSpan > pad.cols || row + rowSpan > pad.rows) return false;
    for (let buttonRow = row; buttonRow < row + rowSpan; buttonRow++) {
        for (let buttonCol = col; buttonCol < col + colSpan; buttonCol++) {
            if (occupied.has(buttonCol + ',' + buttonRow)) return false;
        }
    }
    return true;
}

function recipeMarkRectangle(occupied, button) {
    for (let row = button.row; row < button.row + button.row_span; row++) {
        for (let col = button.col; col < button.col + button.col_span; col++) {
            occupied.add(col + ',' + row);
        }
    }
}

function recipeMinimumArea(button) {
    const minimum = RECIPE_SIZE_MIN_SPANS[button.size];
    if (button.shape === 'square') return minimum * minimum;
    if (button.shape === 'wide') return minimum;
    if (button.shape === 'tall') return minimum;
    if (button.shape === 'very_wide') return minimum + 1;
    return minimum + 2; // very_tall
}

function recipeMeetsSize(button, colSpan, rowSpan) {
    const minimum = RECIPE_SIZE_MIN_SPANS[button.size];
    if (button.shape === 'square') return colSpan >= minimum && rowSpan >= minimum;
    if (button.shape === 'wide' || button.shape === 'tall') return true;
    if (button.shape === 'very_wide') return colSpan >= minimum + 1;
    return rowSpan >= minimum + 2; // very_tall
}

function recipeResolveFixedLayout(layout, pad, originCol, originRow) {
    const occupied = recipeOccupiedCells(pad);
    const buttons = [];
    for (const button of layout.buttons) {
        const resolved = Object.assign({}, button, {
            col: originCol + button.col_offset,
            row: originRow + button.row_offset,
            col_span: button.col_span || 1,
            row_span: button.row_span || 1,
        });
        if (!recipeRectangleFits(pad, occupied, resolved.col, resolved.row, resolved.col_span, resolved.row_span)) return null;
        recipeMarkRectangle(occupied, resolved);
        buttons.push(resolved);
    }
    return buttons;
}

function recipeResolveFlowLayout(layout, pad, originCol, originRow, buttonSizes) {
    if (!buttonSizes) return null;
    const occupied = recipeOccupiedCells(pad);
    const buttons = [];
    const horizontal = layout.flow === 'row';
    let cursor = horizontal ? originRow * pad.cols + originCol : originCol * pad.rows + originRow;

    for (let buttonIndex = 0; buttonIndex < layout.buttons.length; buttonIndex++) {
        const button = layout.buttons[buttonIndex];
        let best = null;
        for (let row = 0; row < pad.rows; row++) {
            for (let col = 0; col < pad.cols; col++) {
                const order = horizontal ? row * pad.cols + col : col * pad.rows + row;
                for (let colSpan = 1; colSpan <= pad.cols - col; colSpan++) {
                    for (let rowSpan = 1; rowSpan <= pad.rows - row; rowSpan++) {
                        if ((button.shape === 'wide' || button.shape === 'very_wide') && rowSpan !== 1) continue;
                        if ((button.shape === 'tall' || button.shape === 'very_tall') && colSpan !== 1) continue;
                        if ((buttonIndex === 0 && (col !== originCol || row !== originRow)) ||
                            (buttonIndex > 0 && order < cursor)) continue;
                        if (!recipeMeetsSize(button, colSpan, rowSpan) ||
                            !recipeRectangleFits(pad, occupied, col, row, colSpan, rowSpan)) continue;
                        const width = buttonSizes.button_w * colSpan + buttonSizes.gap * (colSpan - 1);
                        const height = buttonSizes.button_h * rowSpan + buttonSizes.gap * (rowSpan - 1);
                        const shapeError = Math.abs(Math.log((width / height) / RECIPE_SHAPES[button.shape]));
                        const areaError = (colSpan * rowSpan) - recipeMinimumArea(button);
                        const score = shapeError + areaError * 0.15;
                        const candidate = { col: col, row: row, col_span: colSpan, row_span: rowSpan, score: score, shapeError: shapeError, areaError: areaError, order: order };
                        if (!best || candidate.score < best.score ||
                            (candidate.score === best.score && candidate.order < best.order)) best = candidate;
                    }
                }
            }
        }
        if (!best) return null;
        const resolved = Object.assign({}, button, best);
        recipeMarkRectangle(occupied, resolved);
        buttons.push(resolved);
        cursor = best.order + 1;
    }
    return buttons;
}

function recipeResolveLayout(layout, pad, originCol, originRow, buttonSizes) {
    return recipeIsFlowLayout(layout)
        ? recipeResolveFlowLayout(layout, pad, originCol, originRow, buttonSizes)
        : recipeResolveFixedLayout(layout, pad, originCol, originRow);
}

function recipeFindPlacement(layout, pad, buttonSizes) {
    for (let row = 0; row < pad.rows; row++) {
        for (let col = 0; col < pad.cols; col++) {
            const buttons = recipeResolveLayout(layout, pad, col, row, buttonSizes);
            if (buttons) return { col: col, row: row };
        }
    }
    return null;
}

function recipeGridLimits() {
    return {
        cols: (deviceInfoCache && deviceInfoCache.max_grid_cols) || 8,
        rows: (deviceInfoCache && deviceInfoCache.max_grid_rows) || 8,
    };
}

async function recipeFindMinimumResize(layout, pad) {
    const limits = recipeGridLimits();
    const candidates = [];
    for (let cols = pad.cols; cols <= limits.cols; cols++) {
        for (let rows = pad.rows; rows <= limits.rows; rows++) {
            if (cols === pad.cols && rows === pad.rows) continue;
            candidates.push({ cols: cols, rows: rows, increase: cols - pad.cols + rows - pad.rows });
        }
    }
    candidates.sort(function (left, right) {
        return left.increase - right.increase ||
            (left.rows - pad.rows) - (right.rows - pad.rows) ||
            (left.cols - pad.cols) - (right.cols - pad.cols);
    });
    for (const candidate of candidates) {
        const buttonSizes = await padGetButtonSizes(candidate.cols, candidate.rows);
        const emptyPad = Object.assign({}, pad, { cols: candidate.cols, rows: candidate.rows, buttons: [] });
        const placement = recipeFindPlacement(layout, emptyPad, buttonSizes);
        if (placement) return Object.assign(candidate, { buttonSizes: buttonSizes });
    }
    return null;
}

function recipeCanPlace(col, row) {
    if (!recipeState.pad || !recipeState.selected) return null;
    return recipeResolveLayout(recipeState.selected.layout, recipeState.pad, col, row, recipeState.buttonSizes);
}

function recipePlacementSummary(buttons) {
    if (!buttons || !buttons.length) return '';
    let minCol = Infinity, minRow = Infinity, maxCol = 0, maxRow = 0;
    buttons.forEach(function (button) {
        minCol = Math.min(minCol, button.col);
        minRow = Math.min(minRow, button.row);
        maxCol = Math.max(maxCol, button.col + button.col_span - 1);
        maxRow = Math.max(maxRow, button.row + button.row_span - 1);
    });
    return 'Adds ' + buttons.length + ' button' + (buttons.length === 1 ? '' : 's') +
        ' in columns ' + (minCol + 1) + '-' + (maxCol + 1) +
        ', rows ' + (minRow + 1) + '-' + (maxRow + 1) + '.';
}

function recipeRenderPlacement() {
    const grid = document.getElementById('recipe-placement-grid');
    const install = document.getElementById('recipe-install-btn');
    if (!grid || !install) return;
    grid.innerHTML = '';
    install.disabled = !recipeState.placement;
    const pad = recipeState.pad;
    if (!pad) return;
    grid.style.gridTemplateColumns = 'repeat(' + pad.cols + ', 1fr)';
    grid.style.gridTemplateRows = 'repeat(' + pad.rows + ', 1fr)';
    if (deviceInfoCache && deviceInfoCache.display_coord_width && deviceInfoCache.display_coord_height) {
        grid.style.aspectRatio = deviceInfoCache.display_coord_width + ' / ' + deviceInfoCache.display_coord_height;
    } else if (recipeState.buttonSizes && recipeState.buttonSizes.display_w && recipeState.buttonSizes.display_h) {
        grid.style.aspectRatio = recipeState.buttonSizes.display_w + ' / ' + recipeState.buttonSizes.display_h;
    }
    const occupied = recipeOccupiedCells(pad);
    const selectedButtons = recipeState.placement
        ? recipeCanPlace(recipeState.placement.col, recipeState.placement.row) : null;
    const selectedCells = new Set();
    (selectedButtons || []).forEach(function (button) {
        for (let row = button.row; row < button.row + button.row_span; row++) {
            for (let col = button.col; col < button.col + button.col_span; col++) selectedCells.add(col + ',' + row);
        }
    });
    let validCount = 0;
    for (let row = 0; row < pad.rows; row++) {
        for (let col = 0; col < pad.cols; col++) {
            const cell = document.createElement('button');
            cell.type = 'button';
            cell.className = 'recipe-placement-cell';
            const resolved = recipeCanPlace(col, row);
            const valid = !!resolved;
            if (occupied.has(col + ',' + row)) cell.classList.add('occupied');
            if (valid) { cell.classList.add('available'); validCount++; }
            if (selectedCells.has(col + ',' + row)) cell.classList.add('selected');
            cell.disabled = !valid;
            cell.title = valid ? 'Place recipe over this cell' : 'Recipe does not fit over this cell';
            cell.addEventListener('click', function () {
                recipeState.placement = { col: col, row: row };
                document.getElementById('recipe-placement-status').textContent = recipePlacementSummary(resolved);
                recipeRenderPlacement();
            });
            grid.appendChild(cell);
        }
    }
    if (!recipeState.placement && validCount === 0) {
        if (recipeState.resizeSuggestion) {
            document.getElementById('recipe-placement-status').textContent = 'This recipe needs a larger grid.';
        } else if (recipeState.emptyPlacement && (pad.buttons || []).length) {
            document.getElementById('recipe-placement-status').textContent = 'Existing buttons occupy every position where this recipe fits.';
        } else {
            document.getElementById('recipe-placement-status').textContent = 'This recipe cannot fit on this pad.';
        }
    }
    recipeRenderRecoveryActions(validCount);
}

function recipeApplyResizeSuggestion() {
    const suggestion = recipeState.resizeSuggestion;
    if (!suggestion || !recipeState.pad) return;
    recipeState.pad.cols = suggestion.cols;
    recipeState.pad.rows = suggestion.rows;
    recipeState.buttonSizes = suggestion.buttonSizes;
    recipeState.placement = null;
    recipeState.resizeSuggestion = null;
    recipeState.emptyPlacement = recipeFindPlacement(
        recipeState.selected.layout,
        Object.assign({}, recipeState.pad, { buttons: [] }),
        recipeState.buttonSizes
    );
    document.getElementById('recipe-placement-status').textContent = 'Grid will increase when the recipe is installed. Select a free position.';
    recipeRenderPlacement();
}

async function recipeClearButtons() {
    if (!recipeState.pad || !recipeState.emptyPlacement || !recipeState.selected) return;
    const page = recipeState.pad.page + 1;
    if (!confirm('Clear all buttons on Pad ' + page + '?')) return;
    try {
        const clearedPad = Object.assign({}, recipeState.pad, { buttons: [] });
        delete clearedPad.page;
        const response = await fetch('/api/pad?page=' + recipeState.pad.page, {
            method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(clearedPad),
        });
        if (!response.ok) {
            const error = await response.json().catch(function () { return {}; });
            throw new Error(error.error || 'Failed to clear buttons: HTTP ' + response.status);
        }
        await recipeLoadPad();
        showMessage('Buttons cleared. Select a position for the recipe.', 'success');
    } catch (error) {
        showMessage('Failed to clear buttons: ' + error.message, 'error');
    }
}

function recipeParameters() {
    const values = {};
    (recipeState.selected.parameters || []).forEach(function (parameter) {
        const input = document.getElementById('recipe-parameter-' + parameter.id);
        const raw = input ? input.value.trim() : '';
        if (!raw) recipeError(parameter.label + ' is required.');
        values[parameter.id] = parameter.type === 'number' ? Number(raw) : raw;
        if (parameter.type === 'number' && !Number.isFinite(values[parameter.id])) recipeError(parameter.label + ' must be a number.');
    });
    return values;
}

function recipeExpand(value, parameters) {
    if (Array.isArray(value)) return value.map(function (item) { return recipeExpand(item, parameters); });
    if (recipeIsObject(value)) {
        const result = {};
        Object.keys(value).forEach(function (key) { result[key] = recipeExpand(value[key], parameters); });
        return result;
    }
    if (typeof value !== 'string') return value;
    const exact = value.match(/^\$\{([a-z][a-z0-9_]*)\}$/);
    if (exact) {
        if (!(exact[1] in parameters)) recipeError('Unknown parameter ' + exact[1] + '.');
        return parameters[exact[1]];
    }
    return value.replace(/\$\{([a-z][a-z0-9_]*)\}/g, function (_, id) {
        if (!(id in parameters)) recipeError('Unknown parameter ' + id + '.');
        return String(parameters[id]);
    });
}

function recipeDeepMerge(current, patch) {
    if (!recipeIsObject(current) || !recipeIsObject(patch)) return patch;
    const merged = Object.assign({}, current);
    Object.keys(patch).forEach(function (key) {
        merged[key] = recipeIsObject(current[key]) && recipeIsObject(patch[key])
            ? recipeDeepMerge(current[key], patch[key])
            : patch[key];
    });
    return merged;
}

function recipeMergeBindings(existing, additions) {
    const merged = Object.assign({}, existing || {});
    Object.keys(additions || {}).forEach(function (name) {
        if (Object.prototype.hasOwnProperty.call(merged, name) && merged[name] !== additions[name]) {
            recipeError('Named binding "' + name + '" already has a different value.');
        }
        merged[name] = additions[name];
    });
    return merged;
}

function recipeProvision(recipe) {
    return recipe.provision || { components: recipe.components || {} };
}

function recipeSetProgress(steps) {
    const section = document.getElementById('recipe-progress');
    const list = document.getElementById('recipe-progress-list');
    document.getElementById('recipe-post-install-actions').hidden = true;
    section.hidden = false;
    list.innerHTML = '';
    return steps.map(function (text) {
        const item = document.createElement('li');
        item.textContent = text;
        list.appendChild(item);
        return item;
    });
}

async function recipeShowInstalledPad() {
    if (!Number.isInteger(recipeState.installedPadPage)) return;
    const screenId = 'pad_' + recipeState.installedPadPage;
    try {
        const response = await fetch('/api/component/display/screen', {
            method: 'PUT',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ screen: screenId }),
        });
        if (!response.ok) throw new Error('HTTP ' + response.status);
        showMessage('Showing ' + screenId + ' on device', 'info');
    } catch (error) {
        showMessage('Failed to switch screen: ' + error.message, 'error');
    }
}

function recipeOpenInstalledPadEditor() {
    if (!recipeState.installationComplete || !Number.isInteger(recipeState.installedPadPage)) return;
    sessionStorage.setItem('esp32-macropad.recipe-pad-editor-page', String(recipeState.installedPadPage));
    window.location.hash = '#pad-editor';
}

async function recipeInstall() {
    if (!recipeState.selected || !recipeState.pad || !recipeState.placement) return;
    try {
        recipeState.installedPadPage = null;
        recipeState.installationComplete = false;
        const parameters = recipeParameters();
        const recipe = recipeExpand(recipeState.selected, parameters);
        const provision = recipeProvision(recipe);
        const target = recipeState.pad;
        const placement = recipeState.placement;
        const resolved = recipeResolveLayout(recipe.layout, target, placement.col, placement.row, recipeState.buttonSizes);
        if (!resolved) recipeError('The recipe no longer fits at the selected position.');
        const buttons = resolved.map(function (button) {
            const configured = Object.assign({}, button);
            delete configured.col_offset;
            delete configured.row_offset;
            delete configured.size;
            delete configured.shape;
            delete configured.score;
            delete configured.shapeError;
            delete configured.areaError;
            delete configured.order;
            return configured;
        });
        const componentIds = Object.keys(provision.components || {});
        const progress = recipeSetProgress(componentIds.map(function (id) { return 'Configuring ' + id + '...'; }).concat([
            'Adding ' + buttons.length + ' buttons...', 'Generating button icons...', 'Saving pad...'
        ]));
        let progressIndex = 0;
        for (const id of componentIds) {
            const currentResponse = await fetch('/api/component/' + encodeURIComponent(id) + '/config');
            if (!currentResponse.ok) throw new Error('Failed to read ' + id + ': HTTP ' + currentResponse.status);
            const currentConfig = await currentResponse.json();
            const componentConfig = recipeDeepMerge(currentConfig, provision.components[id]);
            const response = await fetch('/api/component/' + encodeURIComponent(id) + '/config', {
                method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(componentConfig),
            });
            if (!response.ok) throw new Error('Failed to configure ' + id + ': HTTP ' + response.status);
            progress[progressIndex++].classList.add('complete');
        }
        progress[progressIndex++].classList.add('complete');
        const savedPad = Object.assign({}, target, {
            buttons: (target.buttons || []).concat(buttons),
            bindings: recipeMergeBindings(target.bindings, provision.pad && provision.pad.bindings),
        });
        delete savedPad.page;
        await padUploadPageIcons({
            page: target.page,
            cols: savedPad.cols,
            rows: savedPad.rows,
            buttons: savedPad.buttons,
        });
        progress[progressIndex++].classList.add('complete');
        const response = await fetch('/api/pad?page=' + target.page, {
            method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(savedPad),
        });
        if (!response.ok) {
            const error = await response.json().catch(function () { return {}; });
            throw new Error(error.error || 'Failed to save pad: HTTP ' + response.status);
        }
        progress[progressIndex].classList.add('complete');
        document.getElementById('recipe-placement-status').textContent = recipe.name + ' installed successfully.';
        showMessage(recipe.name + ' installed', 'success');
        await recipeLoadPad();
        recipeState.installedPadPage = target.page;
        recipeState.installationComplete = true;
        document.getElementById('recipe-post-install-actions').hidden = false;
    } catch (error) {
        const list = document.getElementById('recipe-progress-list');
        if (list) {
            const item = document.createElement('li');
            item.className = 'failed';
            item.textContent = 'Failed: ' + error.message;
            list.appendChild(item);
        }
        showMessage('Recipe install failed: ' + error.message, 'error');
    }
}

window.init_recipes_fragment = async function () {
    await getDeviceInfo();
    recipeState.selected = null;
    recipeState.pad = null;
    recipeState.placement = null;
    document.getElementById('recipe-detail').hidden = true;
    document.getElementById('recipe-progress').hidden = true;
    const lab = document.getElementById('recipe-lab');
    const isDev = new URLSearchParams(window.location.search).get('dev') === '1';
    lab.hidden = !isDev;
    if (isDev) {
        document.getElementById('recipe-catalog-input').value = sessionStorage.getItem(RECIPE_CATALOG_STORAGE_KEY) || '';
        document.getElementById('recipe-catalog-apply').onclick = function () {
            try {
                const text = document.getElementById('recipe-catalog-input').value;
                recipeLoadCatalogJson(text);
                sessionStorage.setItem(RECIPE_CATALOG_STORAGE_KEY, text);
                recipeRenderCatalog();
                showMessage('Pasted catalog override enabled', 'success');
            } catch (error) { showMessage(error.message, 'error'); }
        };
        document.getElementById('recipe-catalog-clear').onclick = function () {
            sessionStorage.removeItem(RECIPE_CATALOG_STORAGE_KEY);
            document.getElementById('recipe-catalog-input').value = '';
            recipeRenderCatalog();
        };
    }
    document.getElementById('recipe-install-btn').onclick = recipeInstall;
    document.getElementById('recipe-show-pad-btn').onclick = recipeShowInstalledPad;
    document.getElementById('recipe-open-pad-editor-btn').onclick = recipeOpenInstalledPadEditor;
    document.getElementById('recipe-resize-grid-btn').onclick = recipeApplyResizeSuggestion;
    document.getElementById('recipe-clear-buttons-btn').onclick = recipeClearButtons;
    document.getElementById('recipe-cancel-btn').onclick = function () {
        recipeState.selected = null;
        recipeState.placement = null;
        document.getElementById('recipe-detail').hidden = true;
    };
    try { recipeRenderCatalog(); }
    catch (error) { showMessage('Recipe catalog override rejected: ' + error.message, 'error'); sessionStorage.removeItem(RECIPE_CATALOG_STORAGE_KEY); recipeRenderCatalog(); }
};