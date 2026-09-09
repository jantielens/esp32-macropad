// portal_image_library.js - Local image library browser and slideshow settings.
(function () {
  'use strict';
  var directory = '/images';
  var busy = false;
  var slideshowConfig = null;
  function status(message) { var element = document.getElementById('image-library-status'); if (element) element.textContent = message; }
  function uploadError(message) { status(message); showMessage(message, 'error'); }
  function errorMessage(response) { return response.json().then(function (body) { return body.message || 'Image library request failed'; }).catch(function () { return 'Image library request failed'; }); }
  function setBusy(value) {
    busy = value;
    ['image-library-file', 'image-library-upload', 'image-library-save'].forEach(function (id) { var element = document.getElementById(id); if (element) element.disabled = value; });
    document.querySelectorAll('#image-library-files button').forEach(function (button) { button.disabled = value; });
  }
  function render(catalog) {
    var list = document.getElementById('image-library-files'); if (!list) return;
    list.replaceChildren();
    var files = Array.isArray(catalog.files) ? catalog.files : [];
    status(files.length ? files.length + ' image' + (files.length === 1 ? '' : 's') + ' in ' + directory + '.' : 'No images in ' + directory + '.');
    if (catalog.overflow) status('Showing the first ' + files.length + ' of ' + catalog.total_found + ' images in ' + directory + '.');
    files.forEach(function (path) {
      var row = document.createElement('div'); row.className = 'list-group-item d-flex justify-content-between align-items-center';
      var name = document.createElement('span'); name.textContent = path;
      var remove = document.createElement('button'); remove.type = 'button'; remove.className = 'btn btn-outline-danger btn-sm'; remove.textContent = 'Delete'; remove.addEventListener('click', function () { deleteImage(path); });
      row.append(name, remove); list.appendChild(row);
    });
    setBusy(busy);
  }
  function loadCatalog() { return fetch('/api/images?directory=' + encodeURIComponent(directory)).then(function (response) { if (!response.ok) return errorMessage(response).then(function (message) { throw new Error(message); }); return response.json(); }).then(render); }
  function deleteImage(path) {
    if (busy || !confirm('Delete ' + path + '?')) return;
    setBusy(true); status('Deleting ' + path + '...');
    fetch('/api/images?path=' + encodeURIComponent(path), { method: 'DELETE' }).then(function (response) { if (!response.ok) return errorMessage(response).then(function (message) { throw new Error(message); }); return loadCatalog(); }).catch(function (error) { status(error.message); }).finally(function () { setBusy(false); });
  }
  function uploadImage() {
    var input = document.getElementById('image-library-file');
    if (!input || !input.files || !input.files.length || busy) { status('Select a JPEG or PNG file first.'); return; }
    var file = input.files[0]; if (!/\.(jpe?g|png)$/i.test(file.name)) { uploadError('Only JPEG and PNG files are supported.'); return; }
    if (file.size > 4 * 1024 * 1024) { uploadError('Image upload exceeds the 4 MiB limit.'); return; }
    var path = directory + '/' + file.name; setBusy(true); status('Uploading ' + file.name + '...');
    fetch('/api/images?path=' + encodeURIComponent(path), { method: 'POST', headers: { 'Content-Type': 'application/octet-stream' }, body: file }).then(function (response) { if (!response.ok) return errorMessage(response).then(function (message) { throw new Error(message); }); input.value = ''; return loadCatalog(); }).catch(function (error) { uploadError(error.message); }).finally(function () { setBusy(false); });
  }
  function saveConfig() {
    var interval = parseInt(document.getElementById('image-library-interval').value, 10);
    if (isNaN(interval) || interval < 0 || interval > 3600) { showMessage('Interval must be between 0 and 3600 seconds', 'error'); return; }
    fetch('/api/component/image-library/config', { method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify({ directory: directory, interval_seconds: interval }) }).then(function (response) { if (!response.ok) return errorMessage(response).then(function (message) { throw new Error(message); }); showMessage('Slideshow interval saved', 'success'); }).catch(function (error) { showMessage(error.message, 'error'); });
  }
  window.init_image_library_fragment = function () {
    document.getElementById('image-library-upload').addEventListener('click', uploadImage);
    document.getElementById('image-library-save').addEventListener('click', saveConfig);
    fetch('/api/component/image-library/config').then(function (response) { return response.ok ? response.json() : null; }).then(function (config) { slideshowConfig = config; var interval = document.getElementById('image-library-interval'); if (interval) interval.value = config ? config.interval_seconds : 10; });
    loadCatalog().catch(function (error) { status(error.message); });
  };
}());