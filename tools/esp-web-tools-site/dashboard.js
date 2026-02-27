const API_INFO = '/api/info';
const API_HEALTH = '/api/health';
const API_HEALTH_HISTORY = '/api/health/history';

const STATUS_IDLE = 'Idle.';

const DEFAULT_POLL_MS = 5000;
const DEFAULT_HISTORY_SECONDS = 300;

const seriesDefs = [
  {
    key: 'cpu_usage',
    label: 'CPU Usage',
    unit: '%',
    yaxis: 'y',
    color: '#667eea',
    defaultVisible: true,
  },
  {
    key: 'cpu_temperature',
    label: 'CPU Temperature',
    unit: '°C',
    yaxis: 'y4',
    color: '#ff2d55',
    defaultVisible: false,
  },
  {
    key: 'heap_internal_free',
    label: 'Internal Free Heap',
    unit: 'B',
    yaxis: 'y2',
    color: '#34c759',
    defaultVisible: true,
  },
  {
    key: 'heap_internal_largest',
    label: 'Internal Largest Block',
    unit: 'B',
    yaxis: 'y2',
    color: '#af52de',
    defaultVisible: false,
  },
  {
    key: 'psram_free',
    label: 'PSRAM Free',
    unit: 'B',
    yaxis: 'y2',
    color: '#0a84ff',
    defaultVisible: false,
  },
  {
    key: 'wifi_rssi',
    label: 'WiFi RSSI',
    unit: 'dBm',
    yaxis: 'y3',
    color: '#ff9f0a',
    defaultVisible: false,
  },
];

const bandDefs = [
  {
    key: 'heap_internal_free',
    minKey: 'heap_internal_free_min_window',
    maxKey: 'heap_internal_free_max_window',
    yaxis: 'y2',
    fill: 'rgba(52, 199, 89, 0.18)',
  },
  {
    key: 'psram_free',
    minKey: 'psram_free_min_window',
    maxKey: 'psram_free_max_window',
    yaxis: 'y2',
    fill: 'rgba(10, 132, 255, 0.16)',
  },
  {
    key: 'heap_internal_largest',
    minKey: 'heap_internal_largest_min_window',
    maxKey: 'heap_internal_largest_max_window',
    yaxis: 'y2',
    fill: 'rgba(175, 82, 222, 0.16)',
  },
];

const historyFieldMap = {
  cpu_usage: 'cpu_usage',
  cpu_temperature: null,
  heap_internal_free: 'heap_internal_free',
  heap_internal_largest: 'heap_internal_largest',
  psram_free: 'psram_free',
  wifi_rssi: null,
};

let pollTimer = null;
let pollInFlight = false;
let pollIntervalMs = DEFAULT_POLL_MS;
let maxSamples = 300;
let deviceBase = '';
let deviceInfo = null;

let traces = [];
let traceIndexByKey = new Map();
let bandTraceIndexByKey = new Map();
let chartReady = false;
let chartLayout = null;

const ui = {
  deviceInput: document.getElementById('deviceBase'),
  authUserInput: document.getElementById('authUsername'),
  authPassInput: document.getElementById('authPassword'),
  connectBtn: document.getElementById('connect-btn'),
  disconnectBtn: document.getElementById('disconnect-btn'),
  clearBtn: document.getElementById('clear-btn'),
  status: document.getElementById('dashboard-status'),
  deviceMeta: document.getElementById('device-meta'),
  latestValues: document.getElementById('latest-values'),
  healthPanel: document.getElementById('health-panel'),
};

function setHealthPanelVisible(visible) {
  if (!ui.healthPanel) return;
  ui.healthPanel.classList.toggle('is-hidden', !visible);
}

function resizeChartSoon() {
  if (!chartReady) return;
  requestAnimationFrame(() => {
    Plotly.Plots.resize('health-chart');
  });
}

function setStatus(message, type = 'info') {
  if (!ui.status) return;
  ui.status.textContent = message;
  ui.status.classList.remove('info', 'success', 'error');
  ui.status.classList.add(type);
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
  const user = (ui.authUserInput ? ui.authUserInput.value : '').trim();
  const pass = ui.authPassInput ? ui.authPassInput.value : '';
  if (!user && !pass) return null;
  if (!user || !pass) return null;
  return `Basic ${btoa(`${user}:${pass}`)}`;
}

function buildHeaders() {
  const headers = { 'Content-Type': 'application/json' };
  const auth = buildAuthHeader();
  if (auth) headers.Authorization = auth;
  return headers;
}

async function fetchJson(path) {
  const url = `${deviceBase}${path}`;
  const resp = await fetch(url, { headers: buildHeaders(), cache: 'no-store', mode: 'cors' });
  if (!resp.ok) {
    throw new Error(`HTTP ${resp.status}`);
  }
  return resp.json();
}

function formatBytes(bytes) {
  const b = Number(bytes);
  if (!Number.isFinite(b)) return '--';
  if (b >= 1024 * 1024) return `${(b / (1024 * 1024)).toFixed(2)} MB`;
  if (b >= 1024) return `${(b / 1024).toFixed(1)} KB`;
  return `${Math.round(b)} B`;
}

function formatValue(def, value) {
  if (typeof value !== 'number' || !isFinite(value)) return '--';
  if (def.unit === 'B') return formatBytes(value);
  if (def.unit === 'dBm') return `${Math.round(value)} ${def.unit}`;
  if (def.unit) return `${value.toFixed(1)} ${def.unit}`;
  return `${value.toFixed(1)}`;
}

function updateLatestValues(health) {
  if (!ui.latestValues) return;
  if (!health) {
    ui.latestValues.innerHTML = '<div>Not connected.</div>';
    return;
  }

  const rows = seriesDefs.map((def) => {
    const value = def.key in health ? health[def.key] : null;
    const formatted = formatValue(def, value);
    return `<div>${def.label}: <strong>${formatted}</strong></div>`;
  });

  ui.latestValues.innerHTML = rows.join('');
}

function updateDeviceMeta(info) {
  if (!ui.deviceMeta) return;
  if (!info) {
    ui.deviceMeta.innerHTML = '<div>Not connected.</div>';
    return;
  }

  const items = [
    `Firmware v${info.version || '?'}`,
    `${info.chip_model || 'ESP'} rev ${info.chip_revision ?? '?'}`,
    `${info.chip_cores ?? '?'} core${info.chip_cores === 1 ? '' : 's'} @ ${info.cpu_freq ?? '?'} MHz`,
    `Flash ${formatBytes(info.flash_chip_size)}`,
    info.psram_size > 0 ? `PSRAM ${formatBytes(info.psram_size)}` : 'No PSRAM',
    info.wifi_hostname ? `Hostname ${info.wifi_hostname}` : 'Hostname —',
  ];

  ui.deviceMeta.innerHTML = items.map((line) => `<div>${line}</div>`).join('');
}

function configureFromDeviceInfo(info) {
  const pollMs = (info && typeof info.health_poll_interval_ms === 'number') ? info.health_poll_interval_ms : DEFAULT_POLL_MS;
  const windowSeconds = (info && typeof info.health_history_seconds === 'number') ? info.health_history_seconds : DEFAULT_HISTORY_SECONDS;

  pollIntervalMs = Math.max(1000, Math.min(60000, Math.trunc(pollMs)));
  const seconds = Math.max(30, Math.min(3600, Math.trunc(windowSeconds)));
  maxSamples = Math.max(60, Math.min(1200, Math.floor((seconds * 1000) / pollIntervalMs)));
}

function buildTraces() {
  traces = [];
  traceIndexByKey = new Map();
  bandTraceIndexByKey = new Map();

  seriesDefs.forEach((def) => {
    const idx = traces.length;
    let hoverValue = '%{y:.2f}';
    if (def.unit === 'B') {
      hoverValue = '%{y:.0f} B';
    } else if (def.unit === 'dBm') {
      hoverValue = '%{y:.0f} dBm';
    } else if (def.unit) {
      hoverValue = `%{y:.1f} ${def.unit}`;
    }
    traces.push({
      name: def.label,
      x: [],
      y: [],
      mode: 'lines+markers',
      line: { color: def.color, width: 2 },
      marker: { color: def.color, size: 4, opacity: 0.85 },
      hovertemplate: `${hoverValue}<extra></extra>`,
      yaxis: def.yaxis,
      visible: def.defaultVisible ? true : 'legendonly',
    });
    traceIndexByKey.set(def.key, idx);
  });

  bandDefs.forEach((band) => {
    const maxIdx = traces.length;
    traces.push({
      name: `${band.key} max`,
      x: [],
      y: [],
      mode: 'lines',
      line: { width: 0 },
      hoverinfo: 'skip',
      showlegend: false,
      yaxis: band.yaxis,
      visible: true,
    });

    const minIdx = traces.length;
    traces.push({
      name: `${band.key} min`,
      x: [],
      y: [],
      mode: 'lines',
      line: { width: 0 },
      fill: 'tonexty',
      fillcolor: band.fill,
      hoverinfo: 'skip',
      showlegend: false,
      yaxis: band.yaxis,
      visible: true,
    });

    bandTraceIndexByKey.set(band.key, [maxIdx, minIdx]);
  });
}

function initChart() {
  if (typeof Plotly === 'undefined') {
    setStatus('Plotly failed to load.', 'error');
    return;
  }

  buildTraces();

  const layout = {
    margin: { t: 20, r: 90, b: 40, l: 50 },
    showlegend: true,
    legend: {
      orientation: 'h',
      x: 0,
      y: 1.08,
      xanchor: 'left',
      yanchor: 'bottom',
    },
    xaxis: { title: 'Time', type: 'date', showgrid: true },
    yaxis: { title: 'Percent', rangemode: 'tozero', zeroline: true, range: [0, 100] },
    yaxis2: {
      title: 'Bytes',
      overlaying: 'y',
      side: 'right',
      showgrid: false,
      rangemode: 'tozero',
      zeroline: true,
    },
    yaxis3: {
      title: 'dBm',
      overlaying: 'y',
      side: 'right',
      position: 0.95,
      showgrid: false,
      zeroline: false,
    },
    yaxis4: {
      title: '°C',
      overlaying: 'y',
      side: 'right',
      position: 0.9,
      showgrid: false,
      rangemode: 'tozero',
      zeroline: true,
    },
    uirevision: 'health-dashboard',
  };

  const config = { responsive: true, displaylogo: false };

  chartLayout = layout;

  Plotly.newPlot('health-chart', traces, layout, config).then(() => {
    chartReady = true;
  });
}

function clearChart() {
  if (!chartReady) return;
  buildTraces();
  Plotly.react('health-chart', traces, chartLayout || undefined);
  updateLatestValues(null);
}

function pushSample(ts, health) {
  if (!chartReady) return;

  const xUpdates = [];
  const yUpdates = [];
  const indices = [];

  seriesDefs.forEach((def) => {
    const idx = traceIndexByKey.get(def.key);
    if (idx === undefined) return;
    const value = (health && def.key in health) ? health[def.key] : null;
    const numeric = (typeof value === 'number' && isFinite(value)) ? value : null;
    xUpdates.push([ts]);
    yUpdates.push([numeric]);
    indices.push(idx);
  });

  bandDefs.forEach((band) => {
    const indicesForBand = bandTraceIndexByKey.get(band.key);
    if (!indicesForBand || indicesForBand.length !== 2) return;
    const maxValue = (health && band.maxKey in health) ? health[band.maxKey] : null;
    const minValue = (health && band.minKey in health) ? health[band.minKey] : null;
    const maxNumeric = (typeof maxValue === 'number' && isFinite(maxValue)) ? maxValue : null;
    const minNumeric = (typeof minValue === 'number' && isFinite(minValue)) ? minValue : null;

    xUpdates.push([ts]);
    yUpdates.push([maxNumeric]);
    indices.push(indicesForBand[0]);

    xUpdates.push([ts]);
    yUpdates.push([minNumeric]);
    indices.push(indicesForBand[1]);
  });

  Plotly.extendTraces('health-chart', { x: xUpdates, y: yUpdates }, indices, maxSamples);
}

function loadHistoryFromDevice(hist) {
  if (!chartReady) return;
  if (!hist || hist.available !== true) return;

  const uptime = Array.isArray(hist.uptime_ms) ? hist.uptime_ms : [];
  const periodMs = (typeof hist.period_ms === 'number' && isFinite(hist.period_ms) && hist.period_ms > 0)
    ? Math.trunc(hist.period_ms)
    : pollIntervalMs;

  let timestamps = [];
  if (uptime.length > 0) {
    const lastUptime = uptime[uptime.length - 1];
    const now = Date.now();
    timestamps = uptime.map((u) => now - Math.max(0, lastUptime - u));
  } else {
    const now = Date.now();
    timestamps = Array.from({ length: hist.count || 0 }, (_, i) => now - ((hist.count - 1 - i) * periodMs));
  }

  const updatedTraces = traces.map((t) => ({ ...t, x: [], y: [] }));

  seriesDefs.forEach((def) => {
    const idx = traceIndexByKey.get(def.key);
    const field = historyFieldMap[def.key];
    if (idx === undefined || !field || !Array.isArray(hist[field])) return;
    const values = hist[field].map((v) => (typeof v === 'number' && isFinite(v)) ? v : null);
    updatedTraces[idx].x = timestamps.slice(0, values.length);
    updatedTraces[idx].y = values;
  });

  bandDefs.forEach((band) => {
    const indicesForBand = bandTraceIndexByKey.get(band.key);
    if (!indicesForBand || indicesForBand.length !== 2) return;
    const maxArr = Array.isArray(hist[band.maxKey]) ? hist[band.maxKey] : [];
    const minArr = Array.isArray(hist[band.minKey]) ? hist[band.minKey] : [];
    const maxValues = maxArr.map((v) => (typeof v === 'number' && isFinite(v)) ? v : null);
    const minValues = minArr.map((v) => (typeof v === 'number' && isFinite(v)) ? v : null);
    const maxIdx = indicesForBand[0];
    const minIdx = indicesForBand[1];
    updatedTraces[maxIdx].x = timestamps.slice(0, maxValues.length);
    updatedTraces[maxIdx].y = maxValues;
    updatedTraces[minIdx].x = timestamps.slice(0, minValues.length);
    updatedTraces[minIdx].y = minValues;
  });

  Plotly.react('health-chart', updatedTraces, chartLayout || undefined);

  const lastIndex = timestamps.length - 1;
  if (lastIndex >= 0) {
    const latest = {};
    seriesDefs.forEach((def) => {
      const field = historyFieldMap[def.key];
      if (!field || !Array.isArray(hist[field])) return;
      const arr = hist[field];
      latest[def.key] = arr[arr.length - 1];
    });
    updateLatestValues(latest);
  }
}

async function fetchInfo() {
  const info = await fetchJson(API_INFO);
  deviceInfo = info;
  updateDeviceMeta(info);
  configureFromDeviceInfo(info);
  return info;
}

async function fetchHistory() {
  if (!deviceInfo || deviceInfo.health_history_available !== true) return;
  try {
    const hist = await fetchJson(API_HEALTH_HISTORY);
    loadHistoryFromDevice(hist);
  } catch (err) {
    console.warn('Failed to load history', err);
  }
}

async function pollHealth() {
  if (!deviceBase || pollInFlight) return;
  pollInFlight = true;
  try {
    const health = await fetchJson(API_HEALTH);
    pushSample(Date.now(), health);
    updateLatestValues(health);
    setStatus(`Last update ${new Date().toLocaleTimeString()}`, 'success');
  } catch (err) {
    setStatus(`Health poll failed: ${err.message}`, 'error');
  } finally {
    pollInFlight = false;
  }
}

function startPolling() {
  if (pollTimer) return;
  pollTimer = setInterval(pollHealth, pollIntervalMs);
}

function stopPolling() {
  if (!pollTimer) return;
  clearInterval(pollTimer);
  pollTimer = null;
}

async function connect() {
  if (!chartReady) {
    setStatus('Chart is not ready.', 'error');
    return;
  }

  deviceBase = normalizeDeviceBase(ui.deviceInput ? ui.deviceInput.value : '');
  if (!deviceBase) {
    setStatus('Enter a device URL first.', 'error');
    return;
  }

  setStatus('Connecting...', 'info');

  try {
    await fetchInfo();
    await fetchHistory();
    await pollHealth();
    startPolling();

    setHealthPanelVisible(true);
    resizeChartSoon();

    if (ui.disconnectBtn) ui.disconnectBtn.disabled = false;
    if (ui.connectBtn) ui.connectBtn.disabled = true;
    setStatus(`Connected to ${deviceBase}`, 'success');
  } catch (err) {
    setStatus(`Connection failed: ${err.message}`, 'error');
  }
}

function disconnect() {
  stopPolling();
  deviceBase = '';
  deviceInfo = null;
  if (ui.connectBtn) ui.connectBtn.disabled = false;
  if (ui.disconnectBtn) ui.disconnectBtn.disabled = true;
  updateDeviceMeta(null);
  updateLatestValues(null);
  setHealthPanelVisible(false);
  setStatus(STATUS_IDLE, 'info');
}

function initFromQuery() {
  const params = new URLSearchParams(window.location.search);
  const deviceParam = params.get('device') || '';
  if (deviceParam && ui.deviceInput) {
    ui.deviceInput.value = deviceParam;
  }
}

document.addEventListener('DOMContentLoaded', () => {
  initFromQuery();
  initChart();

  if (ui.connectBtn) ui.connectBtn.addEventListener('click', connect);
  if (ui.disconnectBtn) ui.disconnectBtn.addEventListener('click', disconnect);
  if (ui.clearBtn) ui.clearBtn.addEventListener('click', clearChart);

  setHealthPanelVisible(false);
  setStatus(STATUS_IDLE, 'info');
});
