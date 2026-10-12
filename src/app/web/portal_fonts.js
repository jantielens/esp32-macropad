window.init_fonts_fragment = function () {
    var panel = document.getElementById('fonts-panel');
    if (!panel || panel.dataset.initialized) return;
    panel.dataset.initialized = '1';
    var form = panel.querySelector('#font-upload-form');
    var name = panel.querySelector('#font-name');
    var file = panel.querySelector('#font-file');
    var progress = panel.querySelector('#font-progress');
    var status = panel.querySelector('#font-status');
    var list = panel.querySelector('#font-list');
    var refresh = panel.querySelector('#font-refresh');
    var busy = false;
    function message(text, error) {
        status.textContent = text;
        status.style.color = error ? 'var(--danger-color, #b42318)' : '';
    }
    function lock(value) {
        busy = value;
        form.querySelectorAll('input, button').forEach(function (control) { control.disabled = value; });
        refresh.disabled = value;
        list.querySelectorAll('button').forEach(function (control) { control.disabled = value; });
    }
    async function request(url, options) {
        var response = await fetch(url, options);
        var data;
        try { data = await response.json(); } catch (_) { throw new Error('Invalid server response (' + response.status + ')'); }
        if (!response.ok) throw new Error(data.message || data.error || 'Request failed (' + response.status + ')');
        return data;
    }
    async function load() {
        if (busy) return;
        lock(true);
        list.textContent = 'Loading fonts...';
        try {
            var fonts = await request('/api/fonts/list');
            if (!Array.isArray(fonts)) throw new Error('Invalid font list');
            list.textContent = '';
            if (!fonts.length) list.textContent = 'No uploaded fonts';
            fonts.forEach(function (font) {
                var row = document.createElement('div');
                row.className = 'list-group-item';
                row.style.cssText = 'display:grid;grid-template-columns:minmax(0,1fr) auto auto;align-items:center;gap:12px;';
                var id = document.createElement('code');
                id.textContent = font.name;
                id.style.overflowWrap = 'anywhere';
                var size = document.createElement('span');
                size.textContent = (font.size_bytes / 1024).toFixed(1) + ' KiB';
                size.style.whiteSpace = 'nowrap';
                var remove = document.createElement('button');
                remove.className = 'btn btn-outline-danger btn-sm';
                remove.type = 'button';
                remove.title = 'Delete ' + font.name;
                remove.setAttribute('aria-label', remove.title);
                remove.innerHTML = '<span class="portal-icon" aria-hidden="true">delete</span><span class="portal-icon-fallback">Delete</span>';
                remove.onclick = async function () {
                    if (busy || !window.confirm('Delete font "' + font.name + '"?')) return;
                    lock(true);
                    try {
                        await request('/api/fonts?name=' + encodeURIComponent(font.name), {method: 'DELETE'});
                        message('Deleted ' + font.name, false);
                    } catch (error) { message(error.message, true); }
                    finally { lock(false); }
                    await load();
                };
                row.append(id, size, remove);
                list.appendChild(row);
            });
        } catch (error) {
            list.textContent = 'Font list unavailable';
            message(error.message, true);
        } finally { lock(false); }
    }
    file.onchange = function () {
        if (file.files.length && !name.value) name.value = file.files[0].name.replace(/\.ttf$/i, '').toLowerCase();
    };
    form.onsubmit = function (event) {
        event.preventDefault();
        if (busy || !form.reportValidity()) return;
        var source = file.files[0];
        if (!source || !/\.ttf$/i.test(source.name) || !/^[a-z0-9_-]{1,31}$/.test(name.value)) {
            message('Choose a .ttf file and a valid lowercase font ID', true);
            return;
        }
        lock(true);
        progress.hidden = false;
        progress.value = 0;
        message('Uploading ' + name.value + '...', false);
        var xhr = new XMLHttpRequest();
        xhr.open('POST', '/api/fonts/upload?name=' + encodeURIComponent(name.value));
        xhr.setRequestHeader('Content-Type', 'application/octet-stream');
        xhr.upload.onprogress = function (event) {
            if (event.lengthComputable) progress.value = Math.round(event.loaded * 100 / event.total);
        };
        xhr.onload = async function () {
            lock(false);
            progress.hidden = true;
            var data = {};
            try { data = JSON.parse(xhr.responseText); } catch (_) {}
            if (xhr.status < 200 || xhr.status >= 300) {
                message(data.message || data.error || 'Upload failed (' + xhr.status + ')', true);
                return;
            }
            message('Uploaded ' + name.value, false);
            form.reset();
            await load();
        };
        xhr.onerror = xhr.onabort = function () {
            lock(false);
            progress.hidden = true;
            message('Upload connection failed; retry the upload', true);
        };
        xhr.send(source);
    };
    refresh.onclick = load;
    load();
};