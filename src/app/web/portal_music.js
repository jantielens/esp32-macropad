// portal_music.js — Music catalog storage management view.
(function () {
  'use strict';

  var catalog = { files: [], count: 0, limit: 0 };
  var busy = false;

  function setStatus(message) {
    var status = document.getElementById('music-catalog-status');
    if (status) status.textContent = message;
  }

  function setBusy(value) {
    busy = value;
    var file = document.getElementById('music-upload-file');
    var upload = document.getElementById('music-upload-btn');
    if (file) file.disabled = value;
    if (upload) upload.disabled = value || catalog.count >= catalog.limit;
    document.querySelectorAll('#music-track-list button').forEach(function (button) {
      button.disabled = value;
    });
  }

  function errorMessage(response) {
    return response.json().then(function (body) {
      return body.message || 'Music storage request failed';
    }).catch(function () {
      return 'Music storage request failed';
    });
  }

  function renderCatalog(catalog) {
    var list = document.getElementById('music-track-list');
    if (!list) return;

    list.innerHTML = '';
    var files = Array.isArray(catalog.files) ? catalog.files : [];
    setStatus(files.length ? catalog.count + ' of ' + catalog.limit + ' music files.' :
      'No music files found. ' + catalog.count + ' of ' + catalog.limit + ' used.');
    files.forEach(function (path) {
      var row = document.createElement('div');
      row.className = 'list-group-item d-flex justify-content-between align-items-center';
      var name = document.createElement('span');
      name.textContent = path;
      var remove = document.createElement('button');
      remove.type = 'button';
      remove.className = 'btn btn-outline-danger btn-sm';
      remove.textContent = 'Delete';
      remove.disabled = busy;
      remove.addEventListener('click', function () {
        deleteMusic(path);
      });
      row.appendChild(name);
      row.appendChild(remove);
      list.appendChild(row);
    });
    setBusy(busy);
  }

  function loadCatalog() {
    return fetch('/api/music')
      .then(function (response) {
        if (!response.ok) return errorMessage(response).then(function (message) { throw new Error(message); });
        return response.json();
      })
      .then(function (response) {
        catalog = response;
        renderCatalog(catalog);
      });
  }

  function deleteMusic(path) {
    if (busy) return;
    setBusy(true);
    setStatus('Deleting ' + path + '...');
    fetch('/api/music?path=' + encodeURIComponent(path), { method: 'DELETE' })
      .then(function (response) {
        if (!response.ok) return errorMessage(response).then(function (message) { throw new Error(message); });
        return loadCatalog();
      })
      .catch(function (error) {
        setStatus(error.message);
      })
      .finally(function () {
        setBusy(false);
      });
  }

  function uploadMusic() {
    var input = document.getElementById('music-upload-file');
    if (!input || !input.files || !input.files.length || busy) {
      setStatus('Select an MP3 file first.');
      return;
    }
    var file = input.files[0];
    var path = '/media/' + file.name;
    setBusy(true);
    setStatus('Uploading ' + file.name + '...');
    fetch('/api/music?path=' + encodeURIComponent(path), {
      method: 'POST',
      headers: { 'Content-Type': 'application/octet-stream' },
      body: file
    })
      .then(function (response) {
        if (!response.ok) return errorMessage(response).then(function (message) { throw new Error(message); });
        input.value = '';
        return loadCatalog();
      })
      .catch(function (error) {
        setStatus(error.message);
      })
      .finally(function () {
        setBusy(false);
      });
  }

  window.init_music_fragment = function () {
    var upload = document.getElementById('music-upload-btn');
    if (upload) upload.addEventListener('click', uploadMusic);
    loadCatalog()
      .catch(function (error) {
        setStatus(error.message);
      });
  };
}());