// portal_pad_grid.js - Grid rendering, resize handles, drag-and-drop, and template buttons
// Part of the ESP32 Macropad configuration portal.
// Bundled into portal_pad_editor.js during minification.

function padFindButton(col, row) {
    return padState.buttons.find(b => b.col === col && b.row === row);
}

// ===== BUTTON RESIZE (DRAG HANDLES) =====

function padGetGridGeometry() {
    const grid = document.getElementById('pad-grid');
    if (!grid) return null;
    const rect = grid.getBoundingClientRect();
    const style = getComputedStyle(grid);
    const gapX = parseFloat(style.columnGap || style.gap) || 0;
    const gapY = parseFloat(style.rowGap || style.gap) || 0;
    const parseTracks = (value, count, size, gap) => {
        const tracks = String(value || '').split(/\s+/).map(parseFloat).filter(Number.isFinite);
        if (tracks.length === count) return tracks;
        return Array(count).fill(Math.max(0, (size - gap * (count - 1)) / count));
    };
    const columnSizes = parseTracks(style.gridTemplateColumns, padState.cols, rect.width, gapX);
    const rowSizes = parseTracks(style.gridTemplateRows, padState.rows, rect.height, gapY);
    return { rect, gap: gapX, gapX, gapY, columnSizes, rowSizes };
}

function padResizeTrackDelta(raw, side, geometry, col, row, colSpan, rowSpan) {
    const horizontal = side === 'left' || side === 'right';
    const forward = side === 'right' || side === 'down';
    const movement = raw * (forward ? 1 : -1);
    const sizes = horizontal ? geometry.columnSizes : geometry.rowSizes;
    const gap = horizontal ? geometry.gapX : geometry.gapY;
    const origin = horizontal ? col : row;
    const span = horizontal ? colSpan : rowSpan;
    let track = movement >= 0
        ? (forward ? origin + span : origin - 1)
        : (forward ? origin + span - 1 : origin);
    const step = movement >= 0
        ? (forward ? 1 : -1)
        : (forward ? -1 : 1);
    let distance = Math.abs(movement);
    let consumed = 0;
    let delta = 0;
    while (track >= 0 && track < sizes.length) {
        const pitch = sizes[track] + gap;
        if (pitch <= 0) break;
        if (distance < consumed + pitch * 0.2) break;
        delta += movement >= 0 ? 1 : -1;
        consumed += pitch;
        track += step;
    }
    return delta;
}

function padRenderResizeHandles(cell, btn, col, row) {
    if (padState.placingBlock || padState.dragSource) return;
    const sides = ['right', 'left', 'down', 'up'];
    for (const side of sides) {
        const handle = document.createElement('div');
        handle.className = 'pad-resize-handle pad-resize-handle-' + side;
        handle.addEventListener('mousedown', ((s) => (e) => {
            e.stopPropagation();
            e.preventDefault();
            padResizeDragStart(col, row, s, e.clientX, e.clientY);
        })(side));
        handle.addEventListener('touchstart', ((s) => (e) => {
            e.stopPropagation();
            e.preventDefault();
            const t = e.touches[0];
            padResizeDragStart(col, row, s, t.clientX, t.clientY);
        })(side), { passive: false });
        handle.draggable = false;
        cell.appendChild(handle);
    }
}

function padResizeDragStart(col, row, side, startX, startY) {
    const btn = padFindButton(col, row);
    if (!btn) return;
    const geo = padGetGridGeometry();
    if (!geo) return;

    const origCol = btn.col, origRow = btn.row;
    const origCs = btn.col_span || 1, origRs = btn.row_span || 1;
    const occupied = padBuildOccupancySet();
    // Remove this button's own cells from the occupancy set
    for (let dc = 0; dc < origCs; dc++)
        for (let dr = 0; dr < origRs; dr++)
            occupied.delete((origCol + dc) + ',' + (origRow + dr));

    let lastDelta = 0;

    function onMove(clientX, clientY) {
        const raw = (side === 'left' || side === 'right') ? clientX - startX : clientY - startY;
        const delta = padResizeTrackDelta(raw, side, geo, origCol, origRow, origCs, origRs);
        if (delta === lastDelta) return;
        lastDelta = delta;

        // Compute new col, row, col_span, row_span
        let nc = origCol, nr = origRow, ncs = origCs, nrs = origRs;
        if (side === 'right')     ncs = origCs + delta;
        else if (side === 'left') { nc = origCol - delta; ncs = origCs + delta; }
        else if (side === 'down') nrs = origRs + delta;
        else if (side === 'up')   { nr = origRow - delta; nrs = origRs + delta; }

        // Clamp to valid range
        if (ncs < 1) { ncs = 1; nc = origCol + origCs - 1; }
        if (nrs < 1) { nrs = 1; nr = origRow + origRs - 1; }
        if (nc < 0) { ncs += nc; nc = 0; }
        if (nr < 0) { nrs += nr; nr = 0; }
        if (nc + ncs > padState.cols) ncs = padState.cols - nc;
        if (nr + nrs > padState.rows) nrs = padState.rows - nr;

        // Check occupancy for all new cells
        for (let dc = 0; dc < ncs; dc++) {
            for (let dr = 0; dr < nrs; dr++) {
                if (occupied.has((nc + dc) + ',' + (nr + dr))) return;
            }
        }

        // Apply preview
        btn.col = nc; btn.row = nr;
        btn.col_span = ncs; btn.row_span = nrs;
        if (btn.col_span === 1) delete btn.col_span;
        if (btn.row_span === 1) delete btn.row_span;
        padRenderGrid();
    }

    function onMouseMove(e) { onMove(e.clientX, e.clientY); }
    function onTouchMove(e) { if (e.touches.length) onMove(e.touches[0].clientX, e.touches[0].clientY); }

    function onEnd() {
        document.removeEventListener('mousemove', onMouseMove);
        document.removeEventListener('mouseup', onEnd);
        document.removeEventListener('touchmove', onTouchMove);
        document.removeEventListener('touchend', onEnd);
        document.removeEventListener('touchcancel', onEnd);
        const changed = btn.col !== origCol || btn.row !== origRow ||
                        (btn.col_span || 1) !== origCs || (btn.row_span || 1) !== origRs;
        if (changed) padMarkDirty();
        padRenderGrid();
        if (typeof padWorkspaceMoveSelection === 'function') padWorkspaceMoveSelection(origCol, origRow, btn);
    }

    document.addEventListener('mousemove', onMouseMove);
    document.addEventListener('mouseup', onEnd);
    document.addEventListener('touchmove', onTouchMove, { passive: false });
    document.addEventListener('touchend', onEnd);
    document.addEventListener('touchcancel', onEnd);
}

// Find a template button whose origin is at (col, row)
function padFindTemplateButton(col, row) {
    return padState.templateButtons.find(b => b.col === col && b.row === row) || null;
}

// Check whether a template button can be placed (mirrors backend merge_template_buttons occupancy check).
// Returns false when any span cell is out of bounds or occupied by the target pad's own buttons.
function padCanPlaceTemplateButton(tplBtn) {
    const cols = padState.cols, rows = padState.rows;
    const cs = tplBtn.col_span || 1, rs = tplBtn.row_span || 1;
    for (let dc = 0; dc < cs; dc++) {
        for (let dr = 0; dr < rs; dr++) {
            const c = tplBtn.col + dc, r = tplBtn.row + dr;
            if (c >= cols || r >= rows) return false;
            if (padState.buttons.some(b => {
                const bcs = b.col_span || 1, brs = b.row_span || 1;
                return c >= b.col && c < b.col + bcs && r >= b.row && r < b.row + brs;
            })) return false;
        }
    }
    return true;
}

function padRenderGridTracks() {
    const grid = document.getElementById('pad-grid');
    if (!grid) return;
    const cols = padState.cols;
    const rows = padState.rows;

    const displayW = Number(deviceInfoCache && deviceInfoCache.display_coord_width) || cols;
    const displayH = Number(deviceInfoCache && deviceInfoCache.display_coord_height) || rows;
    const gap = Number(padGetEffectiveDefault('button_spacing_px')) || 0;
    const columnSizing = padComputeAxisTrackSizes(cols,
        padGetTrackAvailableSize('columns', displayW), gap, padState.columnWidthsPx);
    const rowSizing = padComputeAxisTrackSizes(rows,
        padGetTrackAvailableSize('rows', displayH), gap, padState.rowHeightsPx);
    const rect = typeof grid.getBoundingClientRect === 'function' ? grid.getBoundingClientRect() : null;
    const gridWidth = parseFloat(grid.style.width) || (rect && rect.width) || displayW;
    const gridHeight = parseFloat(grid.style.height) || (rect && rect.height) || displayH;
    const scaleX = displayW ? gridWidth / displayW : 1;
    const scaleY = displayH ? gridHeight / displayH : scaleX;
    grid.style.gridTemplateColumns = columnSizing.sizes.map(size => size * scaleX + 'px').join(' ');
    grid.style.gridTemplateRows = rowSizing.sizes.map(size => size * scaleY + 'px').join(' ');
}

function padRenderGrid() {
    const grid = document.getElementById('pad-grid');
    if (!grid) return;

    const cols = padState.cols;
    const rows = padState.rows;
    padRenderGridTracks();
    // Apply device aspect ratio so the editor mimics the real screen
    if (deviceInfoCache && deviceInfoCache.display_coord_width && deviceInfoCache.display_coord_height) {
        grid.style.aspectRatio = deviceInfoCache.display_coord_width + ' / ' + deviceInfoCache.display_coord_height;
    }
    const pageBgInput = document.getElementById('pad-edit-page-bg-color');
    grid.style.background = (pageBgInput && /^#[0-9a-fA-F]{6}$/.test(pageBgInput.value.trim())) ? pageBgInput.value.trim() : padGetEffectiveDefault('default_pad_bg_color');
    grid.innerHTML = '';

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

    // Build full occupancy set for resize handle validation
    const occupied = padBuildOccupancySet();

    // Second pass: render cells
    for (let r = 0; r < rows; r++) {
        for (let c = 0; c < cols; c++) {
            const key = c + ',' + r;
            if (covered.has(key)) continue; // Skip cells covered by a span

            const btn = padFindButton(c, r);
            const cell = document.createElement('div');
            cell.classList.add('pad-cell');
            cell.dataset.col = c;
            cell.dataset.row = r;
            cell.style.gridColumn = (c + 1) + ' / span 1';
            cell.style.gridRow = (r + 1) + ' / span 1';

            if (btn) {
                cell.classList.add('pad-cell-btn');
                const cs = Math.min(btn.col_span || 1, cols - c);
                const rs = Math.min(btn.row_span || 1, rows - r);
                if (cs > 1) cell.style.gridColumn = (c + 1) + ' / span ' + cs;
                if (rs > 1) cell.style.gridRow = (r + 1) + ' / span ' + rs;

                padRenderCellContent(cell, btn);

                padRenderResizeHandles(cell, btn, c, r);

                if (!padState.placingBlock) {
                    cell.addEventListener('click', () => padWorkspaceSelect(c, r));
                    cell.draggable = true;
                    cell.addEventListener('dragstart', (e) => padDragStart(e, c, r));
                    cell.addEventListener('dragend', padDragEnd);
                }
            } else {
                // Check if a template button occupies this position
                const tplBtn = padFindTemplateButton(c, r);
                if (tplBtn && padCanPlaceTemplateButton(tplBtn)) {
                    cell.classList.add('pad-cell-btn', 'pad-cell-ghost');
                    const cs = Math.min(tplBtn.col_span || 1, cols - c);
                    const rs = Math.min(tplBtn.row_span || 1, rows - r);
                    if (cs > 1) cell.style.gridColumn = (c + 1) + ' / span ' + cs;
                    if (rs > 1) cell.style.gridRow = (r + 1) + ' / span ' + rs;
                    padRenderCellContent(cell, tplBtn);
                    // Mark cells covered by this template button's span
                    for (let dc = 0; dc < cs; dc++) {
                        for (let dr = 0; dr < rs; dr++) {
                            if (dc === 0 && dr === 0) continue;
                            covered.add((c + dc) + ',' + (r + dr));
                        }
                    }
                    cell.title = 'Inherited from template pad';
                } else {
                    cell.classList.add('pad-cell-empty');
                    cell.textContent = '+';
                    if (!padState.placingBlock) {
                        cell.addEventListener('dragover', (e) => padDragOver(e, c, r));
                        cell.addEventListener('dragleave', padDragLeave);
                        cell.addEventListener('drop', (e) => padDrop(e, c, r));
                    }
                }
                if (padState.placingBlock) {
                    cell.addEventListener('click', () => padPlacementClick(c, r));
                } else {
                    cell.addEventListener('click', () => padWorkspaceSelect(c, r));
                }
            }

            grid.appendChild(cell);
        }
    }

    // Placement mode: add hover overlay behavior
    if (padState.placingBlock) {
        grid.onmouseover = padPlacementHover;
        grid.onmouseleave = () => padPlacementClearGhosts(grid);
    } else {
        grid.onmouseover = null;
        grid.onmouseleave = null;
    }
    if (typeof padWorkspaceRefresh === 'function') padWorkspaceRefresh();
}

function padPlacementHover(e) {
    const cell = e.target.closest('.pad-cell');
    if (!cell || !cell.dataset.col) return;
    const grid = document.getElementById('pad-grid');
    padPlacementClearGhosts(grid);
    const ac = parseInt(cell.dataset.col);
    const ar = parseInt(cell.dataset.row);
    const block = padState.placingBlock;
    if (!block) return;
    const occupied = padBuildOccupancySet();
    const valid = padCanPlaceBlock(block, ac, ar, occupied);
    const footprint = padBlockFootprintCells(block, ac, ar);
    const allCells = grid.querySelectorAll('.pad-cell');
    for (const c of allCells) {
        const key = c.dataset.col + ',' + c.dataset.row;
        if (footprint.has(key)) {
            c.classList.add('pad-cell-ghost-block');
            if (!valid) c.classList.add('invalid');
        }
    }
}

function padPlacementClearGhosts(grid) {
    const cells = grid.querySelectorAll('.pad-cell-ghost-block');
    for (const c of cells) {
        c.classList.remove('pad-cell-ghost-block', 'invalid');
    }
}

// ===== DRAG-AND-DROP MOVE =====

function padDragStart(e, col, row) {
    const btn = padFindButton(col, row);
    if (!btn) { e.preventDefault(); return; }
    padState.dragSource = { col: col, row: row };
    e.dataTransfer.effectAllowed = 'move';
    e.dataTransfer.setData('text/plain', col + ',' + row);
    // rAF so the browser captures the original cell as the drag image before dimming
    requestAnimationFrame(() => {
        const cell = e.target.closest('.pad-cell');
        if (cell) cell.style.opacity = '0.3';
    });
}

function padDragEnd(e) {
    const cell = e.target.closest('.pad-cell');
    if (cell) cell.style.opacity = '';
    padState.dragSource = null;
    const grid = document.getElementById('pad-grid');
    if (grid) padPlacementClearGhosts(grid);
}

function padDragOver(e, col, row) {
    if (!padState.dragSource) return;
    e.preventDefault();
    e.dataTransfer.dropEffect = 'move';

    const grid = document.getElementById('pad-grid');
    padPlacementClearGhosts(grid);

    const srcBtn = padFindButton(padState.dragSource.col, padState.dragSource.row);
    if (!srcBtn) return;

    const cs = srcBtn.col_span || 1;
    const rs = srcBtn.row_span || 1;
    const buttonsWithoutSrc = padState.buttons.filter(b =>
        !(b.col === padState.dragSource.col && b.row === padState.dragSource.row)
    );
    const spanFits = (cs <= 1 && rs <= 1) || padCanSpanFit(col, row, cs, rs, buttonsWithoutSrc);
    const effCs = spanFits ? cs : 1;
    const effRs = spanFits ? rs : 1;

    for (let dc = 0; dc < effCs; dc++) {
        for (let dr = 0; dr < effRs; dr++) {
            const target = grid.querySelector(
                '.pad-cell[data-col="' + (col + dc) + '"][data-row="' + (row + dr) + '"]'
            );
            if (target) target.classList.add('pad-cell-ghost-block');
        }
    }
}

function padDragLeave(e) {
    // Only clear when leaving the cell entirely (not entering a child element)
    if (e.currentTarget.contains(e.relatedTarget)) return;
    const grid = document.getElementById('pad-grid');
    if (grid) padPlacementClearGhosts(grid);
}

function padDrop(e, col, row) {
    e.preventDefault();
    const grid = document.getElementById('pad-grid');
    if (grid) padPlacementClearGhosts(grid);

    if (!padState.dragSource) return;
    const srcCol = padState.dragSource.col;
    const srcRow = padState.dragSource.row;
    padState.dragSource = null;

    if (col === srcCol && row === srcRow) return;

    const srcIdx = padState.buttons.findIndex(b => b.col === srcCol && b.row === srcRow);
    if (srcIdx === -1) return;
    const btn = padState.buttons.splice(srcIdx, 1)[0];

    btn.col = col;
    btn.row = row;

    const cs = btn.col_span || 1;
    const rs = btn.row_span || 1;
    if (cs > 1 || rs > 1) {
        if (!padCanSpanFit(col, row, cs, rs, padState.buttons)) {
            delete btn.col_span;
            delete btn.row_span;
        }
    }

    padState.buttons.push(btn);
    padMarkDirty();
    padRenderGrid();
    if (typeof padWorkspaceMoveSelection === 'function') padWorkspaceMoveSelection(srcCol, srcRow, btn);
}

function padPlacementClick(col, row) {
    const block = padState.placingBlock;
    if (!block) return;
    if (padCanPlaceBlock(block, col, row)) {
        padInsertBlock(block, col, row);
    }
}
