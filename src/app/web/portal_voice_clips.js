(function () {
  'use strict';
  var base = '/api/component/voice-clips/';
  var playbackError = 'Could not play this clip. Try again.';

  function status(text) {
    var node = document.getElementById('voice-clips-status');
    if (node) {
      node.textContent = text;
      node.hidden = !text;
    }
  }

  function json(response) {
    return response.json().then(function (body) {
      if (!response.ok) throw new Error(body.message || body.error || 'Voice clips request failed');
      return body;
    });
  }

  function releaseAudio(audio) {
    audio.pause();
    audio.removeAttribute('src');
    audio.load();
  }

  function clipDate(entry) {
    if (!Number.isFinite(entry.created_at) || entry.created_at <= 0) return null;
    var date = new Date(entry.created_at * 1000);
    return Number.isNaN(date.getTime()) ? null : date;
  }

  function duration(milliseconds) {
    var seconds = Math.max(0, Math.floor(Number(milliseconds) / 1000) || 0);
    return Math.floor(seconds / 60) + ':' + String(seconds % 60).padStart(2, '0');
  }

  function iconButton(tag, icon, text, label) {
    var button = document.createElement(tag);
    button.className = 'btn btn-outline-secondary text-body';
    if (tag === 'button') button.type = 'button';
    button.title = text;
    button.setAttribute('aria-label', text + ' voice clip: ' + label);
    var glyph = document.createElement('span');
    glyph.className = 'portal-icon';
    glyph.setAttribute('aria-hidden', 'true');
    glyph.textContent = icon;
    var fallback = document.createElement('span');
    fallback.className = 'portal-icon-fallback';
    fallback.textContent = text;
    button.appendChild(glyph);
    button.appendChild(fallback);
    return button;
  }

  function createPlayer(entry, clipLabel, list) {
    var player = document.createElement('div');
    player.className = 'row g-2 align-items-center';
    var audio = document.createElement('audio');
    audio.hidden = true;
    audio.preload = 'none';
    audio.src = base + 'file?id=' + encodeURIComponent(entry.id);
    var play = iconButton('button', 'play_arrow', 'Play', clipLabel);
    play.className = 'btn btn-primary';
    var playColumn = document.createElement('div');
    playColumn.className = 'col-auto';
    var progress = document.createElement('div');
    progress.className = 'col';
    var seek = document.createElement('input');
    seek.type = 'range';
    seek.className = 'form-range';
    seek.min = '0';
    seek.max = String(Math.max(0, Number(entry.duration_ms) / 1000 || 0));
    seek.step = '0.1';
    seek.value = '0';
    seek.disabled = Number(seek.max) <= 0;
    seek.setAttribute('aria-label', 'Seek voice clip: ' + clipLabel);
    var times = document.createElement('div');
    times.className = 'd-flex justify-content-between text-muted small';
    var elapsed = document.createElement('span');
    var total = document.createElement('span');
    var seeking = false;

    function showTime(seconds) {
      elapsed.textContent = duration(seconds * 1000);
      total.textContent = duration(Number(seek.max) * 1000);
      seek.setAttribute('aria-valuetext', elapsed.textContent + ' of ' + total.textContent);
    }

    function syncPlayback() {
      var playing = !audio.paused && !audio.ended;
      var text = playing ? 'Pause' : 'Play';
      play.title = text;
      play.setAttribute('aria-label', text + ' voice clip: ' + clipLabel);
      play.firstChild.textContent = playing ? 'pause' : 'play_arrow';
      play.lastChild.textContent = text;
    }

    showTime(0);
    play.addEventListener('click', function () {
      if (audio.paused) {
        audio.play().catch(function (error) {
          if (error.name !== 'AbortError') status(playbackError);
        });
      } else {
        audio.pause();
      }
    });
    audio.addEventListener('play', function () {
      list.querySelectorAll('audio').forEach(function (other) {
        if (other !== audio) other.pause();
      });
      var statusNode = document.getElementById('voice-clips-status');
      if (statusNode && statusNode.textContent === playbackError) status('');
      syncPlayback();
    });
    audio.addEventListener('pause', syncPlayback);
    audio.addEventListener('ended', syncPlayback);
    audio.addEventListener('loadedmetadata', function () {
      if (Number.isFinite(audio.duration) && audio.duration > 0) {
        seek.max = String(audio.duration);
        seek.disabled = false;
        showTime(audio.currentTime);
      }
    });
    audio.addEventListener('timeupdate', function () {
      if (!seeking) {
        seek.value = String(audio.currentTime);
        showTime(audio.currentTime);
      }
    });
    audio.addEventListener('error', function () {
      if (audio.getAttribute('src')) status(playbackError);
    });
    seek.addEventListener('input', function () {
      seeking = true;
      showTime(Number(seek.value));
    });
    seek.addEventListener('change', function () {
      audio.currentTime = Number(seek.value);
      seeking = false;
    });

    times.appendChild(elapsed);
    times.appendChild(total);
    progress.appendChild(seek);
    progress.appendChild(times);
    player.appendChild(audio);
    playColumn.appendChild(play);
    player.appendChild(playColumn);
    player.appendChild(progress);
    return player;
  }

  function load() {
    status('Loading voice clips...');
    return Promise.all([fetch(base + 'status').then(json), fetch(base + 'list').then(json)])
      .then(function (results) {
        var state = results[0], entries = results[1].entries || [];
        var list = document.getElementById('voice-clips-list');
        if (!list) return;
        list.querySelectorAll('audio').forEach(releaseAudio);
        list.textContent = '';
        status(state.error || (entries.length ? '' : 'No voice clips yet.'));
        entries.sort(function (a, b) {
          var left = clipDate(a), right = clipDate(b);
          return (right ? right.getTime() : 0) - (left ? left.getTime() : 0);
        });
        var currentDay = null, group, dayLabel;
        entries.forEach(function (entry) {
          var date = clipDate(entry);
          var day = date ? date.toDateString() : 'unknown';
          if (day !== currentDay) {
            currentDay = day;
            var heading = document.createElement('h3');
            heading.className = 'h6 mt-3 mb-2';
            dayLabel = date ? date.toLocaleDateString(undefined, {
              weekday: 'long', year: 'numeric', month: 'long', day: 'numeric'
            }) : 'Unknown date';
            heading.textContent = dayLabel;
            list.appendChild(heading);
            group = document.createElement('div');
            group.className = 'd-flex flex-column gap-2 mb-4';
            list.appendChild(group);
          }
          var row = document.createElement('div');
          row.className = 'card voice-clip-card';
          var body = document.createElement('div');
          body.className = 'card-body';
          var label = document.createElement('div');
          label.className = 'fw-semibold mb-2';
          var time = date ? date.toLocaleTimeString(undefined, { hour: 'numeric', minute: '2-digit' }) : 'Time unavailable';
          label.textContent = time;
          var clipLabel = dayLabel + ', ' + label.textContent;
          var player = createPlayer(entry, clipLabel, list);
          var audio = player.querySelector('audio');
          var actions = document.createElement('div');
          actions.className = 'col-auto d-flex gap-2';
          var download = iconButton('a', 'download', 'Download', clipLabel);
          download.href = base + 'file?id=' + encodeURIComponent(entry.id) + '&download=1';
          var remove = iconButton('button', 'delete', 'Delete', clipLabel);
          remove.addEventListener('click', function () {
            if (!window.confirm('Delete this voice clip?')) return;
            var source = audio.src;
            releaseAudio(audio);
            remove.disabled = true;
            fetch(base + 'delete?id=' + encodeURIComponent(entry.id), { method: 'DELETE' })
              .then(json).then(load).catch(function (error) {
                status(error.message);
                audio.src = source;
                remove.disabled = false;
              });
          });
          actions.appendChild(download);
          actions.appendChild(remove);
          player.appendChild(actions);
          body.appendChild(label);
          body.appendChild(player);
          row.appendChild(body);
          group.appendChild(row);
        });
      }).catch(function (error) { status(error.message); });
  }

  window.init_voice_clips_fragment = function () {
    var refresh = document.getElementById('voice-clips-refresh');
    if (refresh) refresh.addEventListener('click', load);
    return load();
  };
}());
