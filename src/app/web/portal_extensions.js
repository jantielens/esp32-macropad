window.extensionCatalog = [];

async function extensionFetchSlots() {
    const response = await fetch('/api/extensions');
    if (!response.ok) throw new Error('HTTP ' + response.status);
    const result = await response.json();
    window.extensionCatalog = result.slots || [];
    return window.extensionCatalog;
}

function extensionSlotName(slot) {
    return slot.capacity > 65536 ? 'Large slot' : 'Small slot';
}

async function extensionUpload(slot, input) {
    const file = input.files && input.files[0];
    if (!file) return;
    try {
        const response = await fetch('/api/extensions/upload?slot=' + slot.slot + '&filename=' + encodeURIComponent(file.name), {
            method: 'POST', headers: {'Content-Type': 'application/octet-stream'}, body: file
        });
        const result = await response.json().catch(function () { return {}; });
        if (!response.ok) throw new Error(result.message || result.error || 'HTTP ' + response.status);
        if (typeof setPendingReboot === 'function') setPendingReboot();
        await extensionRenderSlots();
    } catch (error) {
        showMessage('Extension upload failed: ' + error.message, 'error');
    } finally {
        input.value = '';
    }
}

async function extensionDelete(slot) {
    if (!confirm('Delete this extension slot?')) return;
    const response = await fetch('/api/extensions?slot=' + slot.slot, {method: 'DELETE'});
    if (!response.ok) { showMessage('Could not delete extension', 'error'); return; }
    showMessage('Extension deleted; reboot to unload', 'success');
    if (typeof setPendingReboot === 'function') setPendingReboot();
    await extensionRenderSlots();
}

async function extensionSetEnabled(slot) {
    const response = await fetch('/api/extensions/enabled?slot=' + slot.slot + '&enabled=' + (!slot.enabled), {method: 'POST'});
    if (!response.ok) { showMessage('Could not update extension', 'error'); return; }
    showMessage('Extension updated; reboot to apply', 'success');
    if (typeof setPendingReboot === 'function') setPendingReboot();
    await extensionRenderSlots();
}

async function extensionRenderSlots() {
    const root = document.getElementById('extensions-slots');
    if (!root) return;
    try {
        const slots = await extensionFetchSlots();
        root.replaceChildren();
        slots.forEach(function (slot) {
            const card = document.createElement('div');
            card.className = 'card extensions-slot-card';
            const header = document.createElement('div');
            header.className = 'card-header';
            const title = (slot.installed || slot.staged || slot.pending_delete) ? (slot.title || slot.id) + ' @ ' + slot.version : 'Empty';
            const state = slot.pending_delete ? ' - delete pending, reboot required' : slot.staged ? ' - uploaded, reboot pending' : slot.incompatible_abi ? ' - ABI ' + slot.abi_version + ' incompatible, rebuild and re-upload' : slot.loaded ? ' - loaded' : '';
            const heading = document.createElement('h5');
            heading.className = 'mb-0';
            heading.textContent = extensionSlotName(slot) + ' ' + (slot.slot + 1);
            header.appendChild(heading);
            const body = document.createElement('div');
            body.className = 'card-body';
            const name = document.createElement('div');
            name.className = 'extensions-slot-name';
            name.textContent = title;
            const details = document.createElement('small');
            details.textContent = Math.floor((slot.staged ? slot.staged_size : slot.size) / 1024) +
                ' / ' + Math.floor(slot.capacity / 1024) + ' KiB | ABI ' + slot.abi_version +
                ' | ' + (slot.target_abi || 'unknown target') + state;
            body.append(name, details);
            if (slot.runtime_detail) {
                const runtime = document.createElement('small');
                runtime.className = 'extensions-slot-runtime';
                runtime.textContent = 'Runtime: ' + slot.runtime_detail;
                body.appendChild(runtime);
            }
            const input = document.createElement('input'); input.type = 'file'; input.accept = '.ext,application/octet-stream'; input.style.display = 'none';
            const upload = document.createElement('button'); upload.type = 'button'; upload.className = 'btn btn-outline-primary btn-sm'; upload.textContent = slot.installed ? 'Replace' : 'Upload'; upload.onclick = function () { input.click(); };
            input.onchange = function () { extensionUpload(slot, input); };
            const actions = document.createElement('div');
            actions.className = 'extensions-slot-actions';
            actions.append(input, upload);
            if (slot.installed) { const del = document.createElement('button'); del.type = 'button'; del.className = 'btn btn-outline-danger btn-sm'; del.textContent = 'Delete'; del.onclick = function () { extensionDelete(slot); }; actions.appendChild(del); }
            if (slot.installed) { const toggle = document.createElement('button'); toggle.type = 'button'; toggle.className = 'btn btn-outline-secondary btn-sm'; toggle.textContent = slot.enabled ? 'Disable' : 'Enable'; toggle.onclick = function () { extensionSetEnabled(slot); }; actions.appendChild(toggle); }
            body.appendChild(actions);
            card.append(header, body);
            root.appendChild(card);
        });
    } catch (error) { root.textContent = 'Extension status unavailable'; }
}

window.init_extensions_fragment = extensionRenderSlots;