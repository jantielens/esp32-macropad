(() => {
  const input = document.getElementById('boardFilter');
  const container = document.getElementById('boards');
  const count = document.getElementById('boardCount');

  if (!input || !container || !count) return;

  const cards = Array.from(container.querySelectorAll('[data-board]'));

  function update() {
    const q = (input.value || '').trim().toLowerCase();
    let visible = 0;

    for (const el of cards) {
      const name = (el.getAttribute('data-board') || '').toLowerCase();
      const chip = (el.getAttribute('data-chip') || '').toLowerCase();
      const match = !q || name.includes(q) || chip.includes(q);
      el.style.display = match ? '' : 'none';
      if (match) visible++;
    }

    count.textContent = `${visible} / ${cards.length} boards`;
  }

  input.addEventListener('input', update);
  update();
})();

(() => {
  const pre = document.getElementById('releaseNotes');
  if (!pre) return;

  fetch('./release-notes.md', { cache: 'no-store' })
    .then((r) => {
      if (!r.ok) throw new Error(`HTTP ${r.status}`);
      return r.text();
    })
    .then((text) => {
      const trimmed = (text || '').trim();
      if (!trimmed.length) {
        pre.textContent = 'No release notes provided.';
        return;
      }

      // If a markdown renderer is available, render to HTML.
      // Otherwise, fall back to plain text.
      const hasMarked = typeof window.marked !== 'undefined' && typeof window.marked.parse === 'function';
      const hasPurify = typeof window.DOMPurify !== 'undefined' && typeof window.DOMPurify.sanitize === 'function';

      if (hasMarked && hasPurify) {
        const html = window.marked.parse(trimmed, {
          mangle: false,
          headerIds: false,
        });
        pre.innerHTML = window.DOMPurify.sanitize(html, { USE_PROFILES: { html: true } });
      } else {
        pre.textContent = trimmed;
      }
    })
    .catch(() => {
      pre.textContent = 'Release notes are not available here. Use the “View release” link above.';
    });
})();

(() => {
  const deviceInput = document.getElementById('deviceBase');
  const authUserInput = document.getElementById('authUsername');
  const authPassInput = document.getElementById('authPassword');
  const statusEl = document.getElementById('ota-status');
  const updateBtn = document.getElementById('ota-update-btn');

  if (!deviceInput || !statusEl || !updateBtn) return;

  const params = new URLSearchParams(window.location.search);
  const deviceParam = params.get('device') || '';

  if (deviceParam) {
    deviceInput.value = deviceParam;
  }

  function setStatus(message, type = 'info') {
    statusEl.textContent = message;
    statusEl.classList.remove('info', 'success', 'error');
    statusEl.classList.add(type);
  }

  function normalizeDeviceBase(raw) {
    const trimmed = (raw || '').trim();
    if (!trimmed) return '';
    if (trimmed.startsWith('http://') || trimmed.startsWith('https://')) {
      return trimmed.replace(/\/$/, '');
    }
    return `http://${trimmed.replace(/\/$/, '')}`;
  }

  function buildAuthHeader() {
    const user = (authUserInput ? authUserInput.value : '').trim();
    const pass = authPassInput ? authPassInput.value : '';
    if (!user && !pass) return null;
    if (!user || !pass) return null;
    const token = btoa(`${user}:${pass}`);
    return `Basic ${token}`;
  }

  async function fetchDeviceInfo(deviceBase, headers) {
    const resp = await fetch(`${deviceBase}/api/info`, { headers, cache: 'no-store', mode: 'cors' });
    if (!resp.ok) {
      throw new Error(`Failed to fetch device info (HTTP ${resp.status})`);
    }
    return resp.json();
  }

  async function fetchOtaManifest(board) {
    const resp = await fetch(`./ota/${board}.json`, { cache: 'no-store' });
    if (!resp.ok) {
      throw new Error(`Failed to load OTA manifest (HTTP ${resp.status})`);
    }
    return resp.json();
  }


  async function pollUpdateStatus(deviceBase, headers) {
    const statusUrl = `${deviceBase}/api/firmware/update/status`;
    let attempts = 0;
    const maxAttempts = 120;
    let consecutiveFailures = 0;
    const maxFailures = 5;

    const poll = async () => {
      attempts++;
      try {
        const resp = await fetch(statusUrl, { headers, cache: 'no-store' });
        if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
        const data = await resp.json();
        consecutiveFailures = 0;

        if (data.state === 'error') {
          setStatus(`Update failed: ${data.error || 'Unknown error'}`, 'error');
          return;
        }

        const total = data.total || 0;
        const progress = data.progress || 0;
        if (total > 0) {
          const percent = Math.min(100, Math.round((progress / total) * 100));
          setStatus(`Updating… ${percent}% (${data.state || 'in progress'})`, 'info');
        } else {
          setStatus(`Updating… (${data.state || 'in progress'})`, 'info');
        }

        if (data.state === 'rebooting') {
          setStatus('Update complete. Device rebooting…', 'success');
          return;
        }
      } catch (err) {
        consecutiveFailures += 1;
        if (consecutiveFailures >= maxFailures) {
          setStatus('Device is rebooting…', 'info');
          return;
        }
        setStatus(`Update in progress. Status unavailable (${err.message}). Retrying…`, 'info');
      }

      if (attempts < maxAttempts) {
        setTimeout(poll, 1000);
      } else {
        setStatus('Update started. Device should reboot shortly.', 'success');
      }
    };

    poll();
  }

  async function startOta(button) {
    const deviceBase = normalizeDeviceBase(deviceInput.value);
    if (!deviceBase) {
      setStatus('Enter a device URL before updating.', 'error');
      return;
    }

    const authHeader = buildAuthHeader();
    const headers = {
      'Content-Type': 'application/json'
    };
    if (authHeader) headers.Authorization = authHeader;

    try {
      if (button) button.disabled = true;
      setStatus('Detecting device board…', 'info');

      const info = await fetchDeviceInfo(deviceBase, headers);
      const board = (info && info.board_name) ? info.board_name : '';
      if (!board) {
        throw new Error('Device did not report a board name');
      }

      setStatus(`Loading OTA manifest for ${board}…`, 'info');

      const manifest = await fetchOtaManifest(board);
      if (!manifest || !manifest.url) {
        throw new Error('OTA manifest missing URL');
      }

      const payload = {
        url: manifest.url,
        version: manifest.version || '',
        sha256: manifest.sha256 || '',
        size: manifest.size || 0
      };

      setStatus(`Starting update on ${deviceBase} (${board})…`, 'info');

      const resp = await fetch(`${deviceBase}/api/firmware/update`, {
        method: 'POST',
        headers,
        body: JSON.stringify(payload),
        mode: 'cors'
      });

      const data = await resp.json().catch(() => ({}));
      if (!resp.ok || !data.success) {
        throw new Error(data.message || `HTTP ${resp.status}`);
      }

      setStatus('Update started. Tracking progress…', 'info');
      pollUpdateStatus(deviceBase, headers);
    } catch (err) {
      setStatus(`Update failed: ${err.message}`, 'error');
    } finally {
      if (button) button.disabled = false;
    }
  }

  updateBtn.addEventListener('click', () => startOta(updateBtn));

  setStatus('Ready.', 'info');
})();
