(function () {
  'use strict';
  var base = '/api/component/voice-clips/';

  function status(text) {
    var node = document.getElementById('voice-clips-status');
    if (node) node.textContent = text;
  }

  function json(response) {
    return response.json().then(function (body) {
      if (!response.ok) throw new Error(body.message || body.error || 'Voice clips request failed');
      return body;
    });
  }

  function load() {
    return Promise.all([fetch(base + 'status').then(json), fetch(base + 'list').then(json)])
      .then(function (results) {
        var state = results[0], entries = results[1].entries || [];
        var list = document.getElementById('voice-clips-list');
        if (!list) return;
        list.textContent = '';
        status((entries.length ? entries.length + ' voice clip(s).' : 'No voice clips yet.') +
          ' Storage: ' + state.backend + ', approximately ' + Number(state.free_bytes).toLocaleString() +
          ' bytes free.' + (state.error ? ' ' + state.error : ''));
        entries.forEach(function (entry) {
          var row = document.createElement('div');
          row.className = 'list-group-item';
          var label = document.createElement('div');
          label.textContent = entry.id + ' — ' + Math.floor(entry.duration_ms / 1000) +
            ' s, ' + Number(entry.size_bytes).toLocaleString() + ' bytes' +
            (entry.created_at ? ' — ' + new Date(entry.created_at * 1000).toLocaleString() : '');
          var play = document.createElement('button');
          play.type = 'button';
          play.className = 'btn btn-outline-primary btn-sm me-2';
          play.textContent = 'Play';
          var download = document.createElement('a');
          download.className = 'btn btn-outline-secondary btn-sm me-2';
          download.textContent = 'Download';
          download.href = base + 'file?id=' + encodeURIComponent(entry.id) + '&download=1';
          var remove = document.createElement('button');
          remove.type = 'button';
          remove.className = 'btn btn-outline-danger btn-sm';
          remove.textContent = 'Delete';
          play.addEventListener('click', function () {
            var audio = row.querySelector('audio');
            if (!audio) {
              audio = document.createElement('audio');
              audio.controls = true;
              audio.preload = 'metadata';
              audio.src = base + 'file?id=' + encodeURIComponent(entry.id);
              row.appendChild(audio);
            }
            audio.play().catch(function () { status('Could not play this clip. Try again.'); });
          });
          remove.addEventListener('click', function () {
            var audio = row.querySelector('audio');
            if (audio) { audio.pause(); audio.removeAttribute('src'); audio.load(); audio.remove(); }
            remove.disabled = true;
            fetch(base + 'delete?id=' + encodeURIComponent(entry.id), { method: 'DELETE' })
              .then(json).then(load).catch(function (error) {
                status(error.message);
                remove.disabled = false;
              });
          });
          row.appendChild(label);
          row.appendChild(play);
          row.appendChild(download);
          row.appendChild(remove);
          list.appendChild(row);
        });
      }).catch(function (error) { status(error.message); });
  }

  window.init_voice_clips_fragment = function () {
    var refresh = document.getElementById('voice-clips-refresh');
    if (refresh) refresh.addEventListener('click', load);
    return load();
  };
}());
