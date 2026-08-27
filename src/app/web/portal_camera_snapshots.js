window.init_camera_snapshots_fragment = function () {
    var storageBase = '/api/component/storage';
    var latestCard = document.getElementById('camera-latest-card');
    var latestImage = document.getElementById('camera-latest-image');
    var latestLink = document.getElementById('camera-latest-link');
    var latestMetadata = document.getElementById('camera-latest-metadata');
    var latestEmpty = document.getElementById('camera-latest-empty');
    var latestDelete = document.getElementById('camera-latest-delete');
    var folders = document.getElementById('camera-roll-folders');
    var empty = document.getElementById('camera-roll-empty');
    var galleryPageSize = 48;

    function fileUrl(path) {
        return storageBase + '/file?path=' + encodeURIComponent(path);
    }

    function formatDate(value) {
        var year = value.slice(0, 4);
        var month = value.slice(4, 6);
        var day = value.slice(6, 8);
        return new Date(year + '-' + month + '-' + day + 'T00:00:00Z').toLocaleDateString();
    }

    function formatSize(value) {
        if (typeof value !== 'number') return '';
        if (value < 1024) return value + ' B';
        return (value / 1024).toFixed(1) + ' KB';
    }

    function metadataText(entry) {
        var parts = [];
        var size = formatSize(entry.size);
        if (size) parts.push(size);
        if (entry.modified_at) parts.push('Taken ' + new Date(entry.modified_at * 1000).toLocaleTimeString([], {
            hour: '2-digit', minute: '2-digit'
        }));
        return parts.join(' | ');
    }

    async function list(path) {
        var response = await fetch(storageBase + '/list?path=' + encodeURIComponent(path));
        if (response.status === 404) return [];
        if (!response.ok) throw new Error('Could not read saved snapshots');
        var result = await response.json();
        return result.entries || [];
    }

    async function remove(path, label) {
        if (!window.confirm('Delete ' + label + '?')) return false;
        var response = await fetch(storageBase + '/delete?path=' + encodeURIComponent(path), {
            method: 'DELETE'
        });
        if (!response.ok) throw new Error('Could not delete ' + label);
        return true;
    }

    function showLatest(entry) {
        var path = '/camera/latest.jpg';
        latestImage.src = fileUrl(path) + '&v=' + Date.now();
        latestLink.href = fileUrl(path);
        latestImage.hidden = false;
        latestLink.hidden = false;
        latestEmpty.hidden = true;
        latestDelete.hidden = false;
        latestMetadata.textContent = entry ? metadataText(entry) : '';
        latestMetadata.hidden = !latestMetadata.textContent;
    }

    function markLatestEmpty() {
        latestImage.hidden = true;
        latestLink.hidden = true;
        latestEmpty.hidden = false;
        latestDelete.hidden = true;
        latestMetadata.hidden = true;
    }

    function createImageCard(entry, reloadFolder) {
        var card = document.createElement('div');
        card.className = 'camera-roll-item';
        var imageLink = document.createElement('a');
        imageLink.href = fileUrl(entry.path);
        imageLink.target = '_blank';
        imageLink.rel = 'noopener';
        var image = document.createElement('img');
        image.className = 'camera-roll-image card-img-top';
        image.src = fileUrl(entry.path);
        image.loading = 'lazy';
        image.alt = 'Camera snapshot ' + entry.name;
        imageLink.appendChild(image);
        card.appendChild(imageLink);
        var body = document.createElement('div');
        body.className = 'camera-roll-item-controls d-flex align-items-center justify-content-between gap-2';
        var metadata = document.createElement('small');
        metadata.className = 'camera-snapshot-metadata text-muted';
        metadata.textContent = metadataText(entry) || entry.name;
        body.appendChild(metadata);
        var deleteButton = document.createElement('button');
        deleteButton.className = 'btn btn-outline-danger btn-sm';
        deleteButton.type = 'button';
        deleteButton.textContent = 'Delete';
        deleteButton.addEventListener('click', async function () {
            try {
                if (await remove(entry.path, 'snapshot ' + entry.name)) reloadFolder();
            } catch (error) {
                window.showMessage(error.message, 'error');
            }
        });
        body.appendChild(deleteButton);
        card.appendChild(body);
        return card;
    }

    function createFolder(folder) {
        var card = document.createElement('div');
        card.className = 'card mb-3';
        var body = document.createElement('div');
        body.className = 'card-body';
        var header = document.createElement('div');
        header.className = 'd-flex align-items-center justify-content-between gap-3';
        var details = document.createElement('details');
        details.className = 'flex-grow-1';
        var summary = document.createElement('summary');
        summary.textContent = formatDate(folder.name);
        details.appendChild(summary);
        var gallery = document.createElement('div');
        gallery.className = 'camera-roll-grid mt-3';
        details.appendChild(gallery);
        header.appendChild(details);
        var deleteButton = document.createElement('button');
        deleteButton.className = 'btn btn-outline-danger btn-sm';
        deleteButton.type = 'button';
        deleteButton.textContent = 'Delete day';
        deleteButton.addEventListener('click', async function () {
            try {
                if (await remove(folder.path, 'all snapshots from ' + formatDate(folder.name))) refresh();
            } catch (error) {
                window.showMessage(error.message, 'error');
            }
        });
        header.appendChild(deleteButton);
        body.appendChild(header);
        card.appendChild(body);

        var loaded = false;
        async function loadFolder() {
            gallery.replaceChildren();
            var files = (await list(folder.path)).filter(function (entry) {
                return entry.type === 'file' && /^\d{6}\.jpg$/i.test(entry.name);
            }).sort(function (left, right) {
                return right.name.localeCompare(left.name);
            });
            if (!files.length) {
                gallery.textContent = 'This day has no snapshots.';
                gallery.classList.add('text-muted');
                return;
            }
            gallery.classList.remove('text-muted');
            var displayed = 0;
            function appendPage() {
                var pageEnd = Math.min(displayed + galleryPageSize, files.length);
                while (displayed < pageEnd) {
                    gallery.appendChild(createImageCard(files[displayed++], loadFolder));
                }
                if (displayed >= files.length) return;
                var loadMore = document.createElement('button');
                loadMore.className = 'btn btn-outline-secondary btn-sm mt-3';
                loadMore.type = 'button';
                loadMore.textContent = 'Show more snapshots';
                loadMore.addEventListener('click', function () {
                    loadMore.remove();
                    appendPage();
                });
                gallery.appendChild(loadMore);
            }
            appendPage();
        }
        details.addEventListener('toggle', function () {
            if (!details.open || loaded) return;
            loaded = true;
            loadFolder().catch(function (error) {
                gallery.textContent = error.message;
                gallery.classList.add('text-danger');
            });
        });
        return card;
    }

    async function refresh() {
        folders.replaceChildren();
        try {
            var cameraEntries = await list('/camera');
            var latest = cameraEntries.find(function (entry) {
                return entry.type === 'file' && entry.name === 'latest.jpg';
            });
            if (latest) showLatest(latest);
            else markLatestEmpty();
            var days = cameraEntries.filter(function (entry) {
                return entry.type === 'directory' && /^\d{8}$/.test(entry.name);
            }).sort(function (left, right) {
                return right.name.localeCompare(left.name);
            });
            empty.hidden = days.length > 0;
            days.forEach(function (day) {
                folders.appendChild(createFolder(day));
            });
        } catch (error) {
            empty.textContent = error.message;
            empty.hidden = false;
        }
    }

    latestImage.addEventListener('error', markLatestEmpty);
    latestDelete.addEventListener('click', async function () {
        try {
            if (await remove('/camera/latest.jpg', 'the latest snapshot')) markLatestEmpty();
        } catch (error) {
            window.showMessage(error.message, 'error');
        }
    });
    document.getElementById('camera-snapshots-refresh').addEventListener('click', refresh);
    refresh();
};