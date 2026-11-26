window.init_ebook_library_fragment = function () {
    var panel = document.getElementById('ebook-library-panel');
    if (!panel || panel.dataset.initialized) return;
    panel.dataset.initialized = '1';
    var form = panel.querySelector('#ebook-upload-form');
    var file = panel.querySelector('#ebook-file');
    var list = panel.querySelector('#ebook-list');
    var status = panel.querySelector('#ebook-status');
    var progress = panel.querySelector('#ebook-progress');
    var busy = false;
    function message(text, error) {
        status.textContent = text;
        status.style.color = error ? 'var(--danger-color, #b42318)' : '';
    }
    function lock(value) {
        busy = value;
        panel.querySelectorAll('input, button').forEach(function (control) { control.disabled = value; });
    }
    async function request(url, options) {
        var response = await fetch(url, options);
        var data;
        try { data = await response.json(); } catch (_) { throw new Error('Invalid server response (' + response.status + ')'); }
        if (!response.ok) throw new Error(data.message || data.error || 'Request failed (' + response.status + ')');
        return data;
    }
    async function load() {
        lock(true);
        list.textContent = 'Loading E-books...';
        try {
            var data = await request('/api/component/ebook-library/list');
            if (!Array.isArray(data.books)) throw new Error('Invalid E-book list');
            list.textContent = '';
            if (!data.books.length) list.textContent = 'No E-books';
            data.books.forEach(function (book) {
                var row = document.createElement('div');
                row.className = 'list-group-item';
                row.style.cssText = 'display:grid;grid-template-columns:minmax(0,1fr) auto auto;align-items:center;gap:12px;';
                var name = document.createElement('span');
                name.textContent = book.title;
                name.style.overflowWrap = 'anywhere';
                var size = document.createElement('span');
                size.textContent = (book.size_bytes / 1024).toFixed(1) + ' KiB';
                var remove = document.createElement('button');
                remove.type = 'button';
                remove.className = 'btn btn-outline-danger btn-sm';
                remove.textContent = 'Delete';
                remove.setAttribute('aria-label', 'Delete ' + book.id);
                remove.onclick = async function () {
                    if (busy || !window.confirm('Delete E-book "' + book.id + '"?')) return;
                    lock(true);
                    try {
                        await request('/api/component/ebook-library/delete?book=' + encodeURIComponent(book.id), {method: 'DELETE'});
                        message('Deleted ' + book.id, false);
                    } catch (error) { message(error.message, true); }
                    await load();
                };
                row.append(name, size, remove);
                list.appendChild(row);
            });
        } catch (error) {
            list.textContent = 'E-book library unavailable';
            message(error.message, true);
        } finally { lock(false); }
    }
    form.onsubmit = function (event) {
        event.preventDefault();
        if (busy || !form.reportValidity()) return;
        var book = file.files[0];
        if (!book || !/\.epub$/i.test(book.name)) { message('Choose an EPUB file', true); return; }
        lock(true);
        progress.hidden = false;
        progress.value = 0;
        message('Uploading ' + book.name + '...', false);
        var xhr = new XMLHttpRequest();
        xhr.open('POST', '/api/component/ebook-library/upload?book=' + encodeURIComponent(book.name));
        xhr.setRequestHeader('Content-Type', 'application/octet-stream');
        xhr.upload.onprogress = function (event) {
            if (event.lengthComputable) progress.value = Math.round(event.loaded * 100 / event.total);
        };
        xhr.onload = async function () {
            progress.hidden = true;
            var data;
            try { data = JSON.parse(xhr.responseText); }
            catch (_) { message('Invalid server response (' + xhr.status + ')', true); lock(false); return; }
            if (xhr.status < 200 || xhr.status >= 300) {
                message(data.message || data.error || 'Upload failed (' + xhr.status + ')', true);
                lock(false);
                return;
            }
            message('Stored ' + book.name + '. Compatibility is checked when opened.', false);
            form.reset();
            await load();
        };
        xhr.onerror = xhr.onabort = function () {
            lock(false);
            progress.hidden = true;
            message('Upload connection failed', true);
        };
        xhr.send(book);
    };
    load();
};
