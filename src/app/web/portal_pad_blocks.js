// portal_pad_blocks.js - Building block catalog, placement, and occupancy helpers
// Part of the ESP32 Macropad configuration portal.
// Bundled into portal_pad_editor.js during minification.

// ===== BUILDING BLOCKS =====

let padBlockCatalog = [];

async function padLoadBlockCatalog() {
    try {
        const resp = await fetch('/api/pad/blocks');
        if (resp.ok) padBlockCatalog = await resp.json();
        else padBlockCatalog = [];
    } catch (e) { padBlockCatalog = []; }
    padRenderBlockMenuItems();
}

function padBuildOccupancySet() {
    const occupied = new Set();
    for (const b of padState.buttons) {
        const cs = b.col_span || 1, rs = b.row_span || 1;
        for (let dc = 0; dc < cs; dc++)
            for (let dr = 0; dr < rs; dr++)
                occupied.add((b.col + dc) + ',' + (b.row + dr));
    }
    return occupied;
}

function padCountFreeCells() {
    const occupied = padBuildOccupancySet();
    let free = 0;
    for (let r = 0; r < padState.rows; r++)
        for (let c = 0; c < padState.cols; c++)
            if (!occupied.has(c + ',' + r)) free++;
    return free;
}

function padCanPlaceBlock(block, anchorCol, anchorRow, occupied) {
    if (!occupied) occupied = padBuildOccupancySet();
    for (const btn of block.buttons) {
        const c = anchorCol + btn.col_offset;
        const r = anchorRow + btn.row_offset;
        const cs = btn.col_span || 1, rs = btn.row_span || 1;
        for (let dc = 0; dc < cs; dc++) {
            for (let dr = 0; dr < rs; dr++) {
                if (c + dc >= padState.cols || r + dr >= padState.rows) return false;
                if (occupied.has((c + dc) + ',' + (r + dr))) return false;
            }
        }
    }
    return true;
}

function padBlockFootprintCells(block, anchorCol, anchorRow) {
    const cells = new Set();
    for (const btn of block.buttons) {
        const c = anchorCol + btn.col_offset;
        const r = anchorRow + btn.row_offset;
        const cs = btn.col_span || 1, rs = btn.row_span || 1;
        for (let dc = 0; dc < cs; dc++)
            for (let dr = 0; dr < rs; dr++)
                cells.add((c + dc) + ',' + (r + dr));
    }
    return cells;
}

function padRenderBlockMenuItems() {
    const container = document.getElementById('pad-block-items');
    const separator = document.getElementById('pad-block-separator');
    if (!container) return;
    container.innerHTML = '';
    if (padBlockCatalog.length === 0) {
        if (separator) separator.style.display = 'none';
        return;
    }
    if (separator) separator.style.display = '';
    for (const block of padBlockCatalog) {
        const btn = document.createElement('button');
        btn.type = 'button';
        btn.textContent = block.icon + ' ' + block.name;
        btn.addEventListener('click', () => padTryInsertBlock(block));
        container.appendChild(btn);
    }
}

function padTryInsertBlock(block) {
    document.getElementById('pad-more-menu').style.display = 'none';

    if (padState.cols < block.min_cols || padState.rows < block.min_rows) {
        showMessage("Can't insert " + block.name + " — needs " +
            block.min_cols + "×" + block.min_rows + " grid, pad is " +
            padState.cols + "×" + padState.rows, 'error');
        return;
    }
    const free = padCountFreeCells();
    if (free < block.min_free) {
        showMessage("Can't insert " + block.name + " — needs " +
            block.min_free + " free cells, pad has " + free, 'error');
        return;
    }
    if (padState.buttons.length + block.buttons.length > 64) {
        showMessage("Can't insert " + block.name + " — would exceed 64 button limit", 'error');
        return;
    }
    padEnterPlacementMode(block);
}

function padEnterPlacementMode(block) {
    padState.placingBlock = block;
    const banner = document.getElementById('pad-block-banner');
    const bannerText = document.getElementById('pad-block-banner-text');
    if (banner) banner.style.display = 'flex';
    if (bannerText) bannerText.textContent = 'Click a cell to place ' + block.icon + ' ' + block.name + ' — Esc to cancel';
    padRenderGrid();
}

function padExitPlacementMode() {
    padState.placingBlock = null;
    const banner = document.getElementById('pad-block-banner');
    if (banner) banner.style.display = 'none';
    padRenderGrid();
}

function padInsertBlock(block, anchorCol, anchorRow) {
    for (const btn of block.buttons) {
        const newBtn = JSON.parse(JSON.stringify(btn));
        newBtn.col = anchorCol + btn.col_offset;
        newBtn.row = anchorRow + btn.row_offset;
        delete newBtn.col_offset;
        delete newBtn.row_offset;
        padState.buttons.push(newBtn);
    }
    if (block.bindings) {
        for (const [name, value] of Object.entries(block.bindings)) {
            if (!padState.bindings.find(b => b.name === name)) {
                padState.bindings.push({ name, value });
            }
        }
        padRenderBindings();
    }
    padExitPlacementMode();
    padMarkDirty();
    padRenderGrid();
    showMessage('Building block "' + block.name + '" inserted (unsaved)', 'success');
}
