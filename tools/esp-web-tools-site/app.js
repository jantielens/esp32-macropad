(() => {
  const input = document.getElementById('boardFilter');
  const container = document.getElementById('boards');
  const count = document.getElementById('boardCount');

  if (!input || !container || !count) return;

  const CATEGORY_ORDER = [
    {
      key: 'macropad',
      title: 'Macropad',
      description: 'Interactive pads on compatible touch displays, including e-paper.',
    },
    {
      key: 'epaper_frame',
      title: 'E-Paper Frame',
      description: 'Sleep-first image frames with scheduled refreshes.',
    },
    {
      key: 'headless',
      title: 'Headless',
      description: 'Sensor and automation nodes without a display.',
    },
    {
      key: 'shutter_tester',
      title: 'Shutter Tester',
      description: 'Film-plane shutter timing measurement.',
    },
    {
      key: 'coffee_scale',
      title: 'Coffee Scale',
      description: 'Guided pours, weight targets, and brew logs.',
    },
    {
      key: 'darkroom_timer',
      title: 'Darkroom Timer',
      description: 'Light metering and print timing with relay control.',
    },
    {
      key: 'voice_assistant',
      title: 'Voice Assistant',
      description: 'Cloud transcription and configurable voice actions.',
    },
  ];

  const initialBoards = Array.from(container.querySelectorAll(':scope > .board'));
  if (initialBoards.length) {
    const grouped = new Map(CATEGORY_ORDER.map((c) => [c.key, []]));
    const other = [];
    for (const board of initialBoards) {
      const cls = board.dataset.class || 'macropad';
      if (grouped.has(cls)) grouped.get(cls).push(board);
      else other.push(board);
    }
    container.innerHTML = '';
    const buildSection = (key, title, description, boards) => {
      const section = document.createElement('section');
      section.className = 'board-category';
      if (key) section.dataset.class = key;
      const heading = document.createElement('h2');
      heading.className = 'category-title';
      heading.textContent = title;
      section.appendChild(heading);
      if (description) {
        const desc = document.createElement('p');
        desc.className = 'category-desc';
        desc.textContent = description;
        section.appendChild(desc);
      }
      const grid = document.createElement('div');
      grid.className = 'boards';
      for (const b of boards) grid.appendChild(b);
      section.appendChild(grid);
      container.appendChild(section);
    };
    for (const { key, title, description } of CATEGORY_ORDER) {
      const boards = grouped.get(key);
      if (!boards.length) continue;
      buildSection(key, title, description, boards);
    }
    if (other.length) buildSection('', 'Other', '', other);
  }

  const cards = Array.from(container.querySelectorAll('[data-board]'));
  const selectedBoard = new URLSearchParams(window.location.search).get('board');
  const selectedCard = cards.find((card) => card.dataset.board === selectedBoard);
  let showSelected = Boolean(selectedCard);
  if (selectedCard) {
    input.value = selectedBoard;
    selectedCard.classList.add('is-selected');
  }

  function update() {
    const q = (input.value || '').trim().toLowerCase();
    let visible = 0;

    for (const el of cards) {
      const name = (el.getAttribute('data-board') || '').toLowerCase();
      const chip = (el.getAttribute('data-chip') || '').toLowerCase();
      const details = (el.textContent || '').toLowerCase();
      const match = showSelected ? el === selectedCard : !q || name.includes(q) || chip.includes(q) || details.includes(q);
      el.style.display = match ? '' : 'none';
      if (match) visible++;
    }

    // Hide category sections that have no visible boards.
    for (const section of container.querySelectorAll('.board-category')) {
      const anyVisible = Array.from(section.querySelectorAll('.board'))
        .some((b) => b.style.display !== 'none');
      section.style.display = anyVisible ? '' : 'none';
    }

    count.textContent = `${visible} / ${cards.length} boards`;
    const empty = document.getElementById('boardEmpty');
    if (empty) empty.hidden = visible !== 0;
  }

  input.addEventListener('input', () => {
    showSelected = false;
    if (selectedCard) selectedCard.classList.remove('is-selected');
    update();
  });
  update();
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
