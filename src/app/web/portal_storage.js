function storageFormatBytes(value) {
    if (typeof value !== 'number') return '-';
    var units = ['B', 'KB', 'MB', 'GB'];
    var index = 0;
    while (value >= 1024 && index < units.length - 1) {
        value /= 1024;
        index++;
    }
    return (index === 0 ? value : value.toFixed(1)) + ' ' + units[index];
}

function storageEscapeHtml(value) {
    return String(value).replace(/[&<>"']/g, function (character) {
        return {
            '&': '&amp;',
            '<': '&lt;',
            '>': '&gt;',
            '"': '&quot;',
            "'": '&#39;'
        }[character];
    });
}

function storageShowError(message) {
    var element = document.getElementById('storage-error');
    if (!element) return;
    element.textContent = message;
    element.classList.toggle('d-none', !message);
}

window.init_storage_fragment = function () {
    var entries = document.getElementById('storage-entries');
    var expanded = new Set(['/']);

    function renderRows(rows) {
        entries.innerHTML = rows.length ? rows.join('') :
            '<tr><td colspan="4" class="text-muted p-3">This folder is empty.</td></tr>';
    }

    async function loadDirectory(path, indent) {
        var response = await fetch('/api/component/storage/list?path=' + encodeURIComponent(path));
        if (!response.ok) throw new Error('Could not read ' + path);
        var result = await response.json();
        var items = result.entries.sort(function (left, right) {
            if (left.type !== right.type) return left.type === 'directory' ? -1 : 1;
            return left.name.localeCompare(right.name, undefined, { sensitivity: 'base' });
        });
        var rows = [];
        for (var index = 0; index < items.length; index++) {
            var item = items[index];
            var isDirectory = item.type === 'directory';
            var prefix = '&nbsp;'.repeat(indent * 4);
            var toggle = isDirectory ? '<button class="btn btn-sm btn-link p-0 me-1 storage-toggle" data-path="' +
                storageEscapeHtml(item.path) + '" aria-label="Toggle folder">&#9656;</button>' : '<span class="me-1">&#128196;</span>';
            var modified = item.modified_at ? new Date(item.modified_at * 1000).toLocaleString() : '-';
            var open = isDirectory ? '' : '<a class="btn btn-sm btn-outline-secondary storage-open" href="/api/component/storage/file?path=' +
                encodeURIComponent(item.path) + '" target="_blank" rel="noopener" aria-label="Download or open file" title="Download or open file">&#8595;</a>';
            rows.push('<tr data-path="' + storageEscapeHtml(item.path) + '" data-indent="' + indent + '"><td>' + prefix + toggle +
                storageEscapeHtml(item.name) + '</td><td class="text-end">' + (isDirectory ? '-' : storageFormatBytes(item.size)) +
                '</td><td class="text-end text-muted">' + modified + '</td><td class="text-end">' + open + '</td></tr>');
        }
        if (result.truncated) rows.push('<tr><td colspan="4" class="text-muted p-3">More entries are not shown.</td></tr>');
        return rows;
    }

    async function refresh() {
        storageShowError('');
        try {
            var response = await fetch('/api/component/storage/status');
            if (!response.ok) throw new Error('Could not read storage status');
            var status = await response.json();
            var backend = status.backend === 'sdmmc' ? 'SD card (SDMMC)' : 'Internal flash (LittleFS)';
            document.getElementById('storage-backend').textContent = backend;
            document.getElementById('storage-status').textContent = status.mounted ? 'Mounted' : 'Unavailable';
            document.getElementById('storage-capacity').textContent = storageFormatBytes(status.total_bytes);
            var percent = status.total_bytes ? Math.min(100, status.used_bytes * 100 / status.total_bytes) : 0;
            document.getElementById('storage-usage-bar').style.width = percent + '%';
            document.getElementById('storage-usage-bar').textContent = Math.round(percent) + '%';
            document.getElementById('storage-usage-text').textContent = storageFormatBytes(status.used_bytes) + ' used, ' +
                storageFormatBytes(status.free_bytes) + ' free';
            if (!status.mounted) {
                renderRows([]);
                storageShowError('Storage is unavailable.');
                return;
            }
            renderRows(await loadDirectory('/', 0));
        } catch (error) {
            renderRows([]);
            storageShowError(error.message);
        }
    }

    entries.addEventListener('click', async function (event) {
        var toggle = event.target.closest('.storage-toggle');
        if (!toggle) return;
        var row = toggle.closest('tr');
        var path = toggle.dataset.path;
        if (expanded.has(path)) {
            var descendants = entries.querySelectorAll('tr[data-path^="' + CSS.escape(path + '/') + '"]');
            for (var index = 0; index < descendants.length; index++) descendants[index].remove();
            expanded.delete(path);
            toggle.innerHTML = '&#9656;';
            return;
        }
        expanded.add(path);
        toggle.innerHTML = '&#9662;';
        try {
            var children = await loadDirectory(path, Number(row.dataset.indent) + 1);
            row.insertAdjacentHTML('afterend', children.join(''));
        } catch (error) {
            storageShowError(error.message);
        }
    });
    document.getElementById('storage-refresh-btn').addEventListener('click', refresh);
    refresh();
};