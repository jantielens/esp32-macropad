(function () {
  'use strict';
  var base = '/api/component/voice-notes/';
  var playbackError = 'Could not play this recording. Try again.';
  var generation = 0, timer = null, selected = null, latest = null, rows = [], lastReady = '', lastStatusError = '';
  var reconcileNeeded = false, lastAttempt = '', detailRefresh = null, selectionVersion = 0;
  if (typeof window.registerConfigFields === 'function')
    window.registerConfigFields(['voice_notes_base_url', 'voice_notes_model', 'voice_notes_auto_transcribe']);

  function status(text) {
    var node = document.getElementById('voice-notes-status');
    if (node) {
      node.textContent = text;
      node.hidden = !text;
    }
  }

  function json(response) {
    return response.json().then(function (body) {
      if (!response.ok) throw new Error(body.message || body.error || 'Voice notes request failed');
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
    button.setAttribute('aria-label', text + ' voice note: ' + label);
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
    seek.setAttribute('aria-label', 'Seek voice note: ' + clipLabel);
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
      play.setAttribute('aria-label', text + ' voice note: ' + clipLabel);
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
      var statusNode = document.getElementById('voice-notes-status');
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

  function updateStatus() {
    if (!latest) return;
    var setup = document.getElementById('voice-notes-setup');
    var coordinator = latest.processing || {};
    if (setup) setup.hidden = latest.transcription_configured !== false && coordinator.reason !== 'configuration';
    var guidance = document.getElementById('voice-notes-processing-status');
    if (guidance) {
      var reasons = {
        offline: 'Waiting for Wi-Fi',
        recording: 'Waiting for recording to finish',
        update: 'Waiting for device update or storage availability',
        retry: 'Retrying later',
        configuration: 'Check transcription settings',
        none: 'Waiting to transcribe'
      };
      guidance.textContent = (coordinator.state === 'waiting' || coordinator.state === 'blocked')
        ? (reasons[coordinator.reason] || '') : '';
      guidance.hidden = !guidance.textContent;
    }
    var processing = latest.transcription || {};
    var busy = ['preparing', 'transcribing', 'saving'].indexOf(processing.state) !== -1;
    rows.forEach(function (row) {
      var matches = processing.note_id === row.entry.id;
      if (matches && processing.state === 'ready') row.entry.has_transcript = true;
      if (row.transcriptIndicator) row.transcriptIndicator.hidden = !row.entry.has_transcript;
      if (row.durableStatus) row.durableStatus.textContent = row.entry.has_transcript ? 'Transcript ready' : 'Not transcribed';
      row.transcribe.disabled = busy || coordinator.state === 'running' || latest.transcription_configured === false;
      row.transcribe.textContent = matches && processing.state === 'error' ? 'Retry transcription' :
        (row.entry.has_transcript ? 'Transcribe again' : 'Transcribe');
      if (matches && busy) {
        row.state.textContent = { preparing: 'Preparing...', transcribing: 'Transcribing...', saving: 'Saving transcript...' }[processing.state];
      } else if (matches && processing.state === 'error') {
        row.state.textContent = 'Transcription failed: ' + (processing.error || 'Please try again.') +
          (row.entry.has_transcript ? ' Previous transcript saved.' : '');
      } else {
        row.state.textContent = '';
      }
      row.state.hidden = !(matches && (busy || processing.state === 'error'));
    });
    if (selected && rows.some(function (row) { return row.detail && row.entry.id === selected; }) &&
        processing.note_id === selected && processing.state === 'ready' && lastReady !== selected) {
      lastReady = selected;
      refreshDetail(selected);
    }
    if (processing.state !== 'ready') lastReady = '';
  }

  function refreshDetail(id) {
    if (detailRefresh && detailRefresh.id === id) return detailRefresh.request;
    var request = openDetail(id, false);
    detailRefresh = { id: id, request: request };
    request.then(function () {
      if (detailRefresh && detailRefresh.request === request) detailRefresh = null;
    });
    return request;
  }

  function reconcile(token, signal) {
    return fetch(base + 'list', { signal: signal }).then(json).then(function (result) {
      if (token !== generation) return;
      var entries = new Map((result.entries || []).map(function (entry) { return [entry.id, entry]; }));
      var refreshSelected = false;
      rows.forEach(function (row) {
        var entry = entries.get(row.entry.id);
        if (!entry) return;
        if (row.detail && entry.has_transcript && !row.entry.has_transcript) refreshSelected = true;
        row.entry.has_transcript = !!entry.has_transcript;
      });
      updateStatus();
      if (refreshSelected && selected) refreshDetail(selected);
      reconcileNeeded = latest && latest.processing && latest.processing.state === 'running';
    });
  }

  function poll(token) {
    var controller = new AbortController();
    var timeout = setTimeout(function () { controller.abort(); }, 8000);
    return fetch(base + 'status', { signal: controller.signal }).then(json).then(function (value) {
      if (token !== generation) return;
      latest = value;
      var statusNode = document.getElementById('voice-notes-status');
      if (lastStatusError && statusNode && statusNode.textContent === lastStatusError) status('');
      lastStatusError = '';
      updateStatus();
      if (value.processing && value.processing.state === 'running') reconcileNeeded = true;
      var attempt = value.transcription || {};
      var attemptKey = JSON.stringify([attempt.note_id, attempt.state, attempt.error]);
      if (['ready', 'error'].indexOf(attempt.state) !== -1 && attemptKey !== lastAttempt)
        reconcileNeeded = true;
      lastAttempt = attemptKey;
      if (reconcileNeeded) return reconcile(token, controller.signal);
    }).catch(function (error) {
      if (token === generation) {
        lastStatusError = error.name === 'AbortError' ? 'Voice Notes status request timed out.' : error.message;
        status(lastStatusError);
      }
    }).then(function () {
      clearTimeout(timeout);
      if (token === generation) timer = setTimeout(function () { poll(token); }, 2000);
    });
  }

  function startTranscription(entry) {
    return fetch(base + 'transcribe?id=' + encodeURIComponent(entry.id), { method: 'POST' })
      .then(json).then(function () {
        latest = latest || {};
        lastAttempt = '';
        latest.transcription = { state: 'preparing', note_id: entry.id, error: '' };
        updateStatus();
      }).catch(function (error) { status(error.message); });
  }

  function transcriptionControls(entry, detailsButton, transcriptIndicator, durableStatus) {
    var box = document.createElement('div');
    box.className = 'mt-3 d-flex align-items-center flex-wrap gap-2';
    var transcribe = document.createElement('button');
    transcribe.type = 'button';
    transcribe.className = 'btn btn-outline-primary';
    transcribe.textContent = 'Transcribe';
    transcribe.disabled = true;
    transcribe.addEventListener('click', function () { startTranscription(entry); });
    var state = document.createElement('span');
    state.className = 'text-muted small';
    state.setAttribute('role', 'status');
    if (detailsButton) {
      var details = document.createElement('button');
      details.type = 'button';
      details.className = 'btn btn-primary';
      details.textContent = 'View details';
      details.addEventListener('click', function () { openDetail(entry.id, true); });
      box.appendChild(details);
    }
    box.appendChild(transcribe);
    box.appendChild(state);
    rows.push({ entry: entry, transcribe: transcribe, state: state, transcriptIndicator: transcriptIndicator,
      durableStatus: durableStatus });
    updateStatus();
    return box;
  }

  function openDetail(id, switchPanel) {
    var token = generation;
    if (switchPanel) {
      ++selectionVersion;
      detailRefresh = null;
      selected = id;
      document.getElementById('voice-notes-list').querySelectorAll('audio').forEach(releaseAudio);
      document.getElementById('voice-notes-catalog').hidden = true;
      document.getElementById('voice-notes-detail').hidden = false;
      document.getElementById('voice-notes-back').hidden = false;
      document.getElementById('voice-notes-detail-title').textContent = 'Voice note';
      document.getElementById('voice-notes-detail-meta').textContent = '';
      document.getElementById('voice-notes-detail-transcript').textContent = 'Loading...';
      document.getElementById('voice-notes-detail-player').querySelectorAll('audio').forEach(releaseAudio);
      document.getElementById('voice-notes-detail-player').textContent = '';
      document.getElementById('voice-notes-detail-actions').textContent = '';
    }
    var version = selectionVersion;
    return fetch(base + 'detail?id=' + encodeURIComponent(id)).then(json).then(function (result) {
      if (token !== generation || version !== selectionVersion || selected !== id) return;
      var note = result.note, date = clipDate(note);
      document.getElementById('voice-notes-detail-title').textContent =
        'Voice note — ' + (date ? date.toLocaleString() : 'Time unavailable');
      document.getElementById('voice-notes-detail-meta').textContent =
        duration(note.duration_ms) + ' · ' + (Number(note.size_bytes) || 0).toLocaleString() + ' bytes';
      var transcript = document.getElementById('voice-notes-detail-transcript');
      transcript.textContent = result.transcript ? result.transcript.text : 'No transcript yet.';
      if (!switchPanel) {
        rows.forEach(function (row) {
          if (row.entry.id === id) row.entry.has_transcript = !!result.transcript;
        });
        updateStatus();
        return;
      }
      var player = document.getElementById('voice-notes-detail-player');
      player.querySelectorAll('audio').forEach(releaseAudio);
      player.textContent = '';
      player.appendChild(createPlayer(note, date ? date.toLocaleString() : 'Time unavailable', player));
      var actions = document.getElementById('voice-notes-detail-actions');
      actions.textContent = '';
      rows = rows.filter(function (row) { return !row.detail; });
      note.has_transcript = !!result.transcript;
      actions.appendChild(transcriptionControls(note, false));
      rows[rows.length - 1].detail = true;
      updateStatus();
    }).catch(function (error) {
      if (token === generation && version === selectionVersion && selected === id) status(error.message);
    });
  }

  function load(initialStatus) {
    var token = generation;
    var existingList = document.getElementById('voice-notes-list');
    if (!existingList || !existingList.children.length) status('Loading voice notes...');
    return Promise.all([fetch(base + 'list').then(json), initialStatus])
      .then(function (results) {
        if (token !== generation) return;
        var result = results[0];
        var entries = result.entries || [];
        var list = document.getElementById('voice-notes-list');
        if (!list) return;
        list.querySelectorAll('audio').forEach(releaseAudio);
        list.textContent = '';
        rows = rows.filter(function (row) { return row.detail; });
        status(lastStatusError || (entries.length ? '' : 'No voice notes yet. Use a configured Start recording action to make one.'));
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
          row.className = 'card voice-note-card';
          var body = document.createElement('div');
          body.className = 'card-body';
          var label = document.createElement('div');
          label.className = 'fw-semibold mb-2';
          var time = date ? date.toLocaleTimeString(undefined, { hour: 'numeric', minute: '2-digit' }) : 'Time unavailable';
          label.textContent = time + ' · ' + duration(entry.duration_ms) + ' · ' +
            (Number(entry.size_bytes) || 0).toLocaleString() + ' bytes';
          var clipLabel = dayLabel + ', ' + label.textContent;
          var transcriptIndicator = document.createElement('span');
          transcriptIndicator.className = 'ms-2 text-muted';
          transcriptIndicator.setAttribute('role', 'img');
          transcriptIndicator.setAttribute('aria-label', 'Transcript available');
          transcriptIndicator.title = 'Transcript available';
          transcriptIndicator.hidden = !entry.has_transcript;
          var transcriptIcon = document.createElement('span');
          transcriptIcon.className = 'portal-icon fs-6 align-middle';
          transcriptIcon.setAttribute('aria-hidden', 'true');
          transcriptIcon.textContent = 'description';
          var transcriptFallback = document.createElement('span');
          transcriptFallback.className = 'portal-icon-fallback small';
          transcriptFallback.setAttribute('aria-hidden', 'true');
          transcriptFallback.textContent = 'Transcript available';
          transcriptIndicator.appendChild(transcriptIcon);
          transcriptIndicator.appendChild(transcriptFallback);
          label.appendChild(transcriptIndicator);
          var durableStatus = document.createElement('span');
          durableStatus.className = 'ms-2 text-muted small';
          label.appendChild(durableStatus);
          var player = createPlayer(entry, clipLabel, list);
          var audio = player.querySelector('audio');
          var actions = document.createElement('div');
          actions.className = 'col-auto d-flex gap-2';
          var download = iconButton('a', 'download', 'Download', clipLabel);
          download.href = base + 'file?id=' + encodeURIComponent(entry.id) + '&download=1';
          var remove = iconButton('button', 'delete', 'Delete', clipLabel);
          remove.addEventListener('click', function () {
            if (!window.confirm('Delete this voice note?')) return;
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
          body.appendChild(transcriptionControls(entry, true, transcriptIndicator, durableStatus));
          row.appendChild(body);
          group.appendChild(row);
        });
        updateStatus();
      }).catch(function (error) { if (token === generation) status(error.message); });
  }

  window.init_voice_notes_fragment = function () {
    var token = ++generation;
    selected = null;
    rows = [];
    latest = null;
    lastReady = '';
    lastStatusError = '';
    reconcileNeeded = false;
    lastAttempt = '';
    detailRefresh = null;
    ++selectionVersion;
    var refresh = document.getElementById('voice-notes-refresh');
    if (refresh) refresh.addEventListener('click', function () {
      refresh.disabled = true;
      var request = selected ? openDetail(selected, false) : load();
      request.then(function () { if (token === generation) refresh.disabled = false; });
    });
    var back = document.getElementById('voice-notes-back');
    if (back) back.addEventListener('click', function () {
      selected = null;
      ++selectionVersion;
      detailRefresh = null;
      document.getElementById('voice-notes-detail-player').querySelectorAll('audio').forEach(releaseAudio);
      document.getElementById('voice-notes-detail').hidden = true;
      document.getElementById('voice-notes-catalog').hidden = false;
      back.hidden = true;
      rows = rows.filter(function (row) { return !row.detail; });
      load();
    });
    function dispose() {
      if (token !== generation) return;
      ++generation;
      if (timer !== null) clearTimeout(timer);
      timer = null;
      document.querySelectorAll('#voice-notes-list audio, #voice-notes-detail-player audio').forEach(releaseAudio);
      window.removeEventListener('portal-fragment-leave', dispose);
    }
    window.addEventListener('portal-fragment-leave', dispose);
    return load(poll(token));
  };

  window.init_voice_notes_settings_fragment = function () {
    loadConfig();
    var button = document.getElementById('voice-notes-settings-save');
    var key = document.getElementById('voice_notes_api_key');
    var indicator = document.getElementById('voice-notes-key-status');
    function showKeyStatus(config) {
      indicator.textContent = config.voice_notes_api_key_set
        ? 'An API key is saved. Leave blank to keep it.' : 'No API key is saved.';
    }
    fetch('/api/config').then(json).then(showKeyStatus).catch(function (error) {
      indicator.textContent = 'Could not read key status: ' + error.message;
    });
    button.addEventListener('click', function () {
      var config = {
        voice_notes_base_url: document.getElementById('voice_notes_base_url').value,
        voice_notes_model: document.getElementById('voice_notes_model').value,
        voice_notes_api_key: key.value,
        voice_notes_auto_transcribe: document.getElementById('voice_notes_auto_transcribe').checked
      };
      button.disabled = true;
      fetch('/api/config?no_reboot=1', {
        method: 'POST', headers: { 'Content-Type': 'application/json' }, body: JSON.stringify(config)
      }).then(json).then(function () {
        key.value = '';
        showMessage('Configuration saved', 'success');
        return fetch('/api/config').then(json).then(showKeyStatus);
      }).catch(function (error) { showMessage('Error saving: ' + error.message, 'error'); })
        .then(function () { button.disabled = false; });
    });
  };
}());
