'use strict';

const boardSelect = document.getElementById('board-select');
const slotSelect = document.getElementById('slot-select');
const searchInput = document.getElementById('search');
const flagFilter = document.getElementById('flag-filter');
let reports = [];
let currentReport;
let sortKey = 'flash';
let descending = true;

function setText(id, text) {
  document.getElementById(id).textContent = text;
}

function kib(value) {
  return (value / 1024).toLocaleString('en', { minimumFractionDigits: 2, maximumFractionDigits: 2 });
}

function size(value) {
  return value == null ? 'Unavailable' : `${kib(value)} KiB`;
}

function segment(kind, bytes, denominator) {
  const node = document.createElement('span');
  node.className = kind;
  node.style.width = `${denominator > 0 ? Math.min(100, Math.max(0, 100 * bytes / denominator)) : 0}%`;
  node.title = `${kind === 'gap' ? 'Unreconciled' : kind}: ${size(bytes)}`;
  return node;
}

function renderFlashLayout(slot) {
  const context = currentReport.context;
  const partitions = context.partitions || [];
  const declared = context.flash_size_bytes;
  const binary = context.application_binary_bytes;
  const extent = context.partition_extent_bytes || 0;
  const total = Math.max(declared || 0, extent);
  const bar = document.getElementById('flash-layout-bar');
  const legend = document.getElementById('partition-legend');
  bar.replaceChildren();
  legend.replaceChildren();
  setText('layout-heading', declared && declared >= extent ? 'Full flash layout' : 'Partition layout');
  const source = context.flash_size_source === 'demo' ? 'synthetic' : 'board metadata, not measured';
  setText('flash-source', declared ? `${(declared / 1048576).toFixed(2)} MiB / ${source}` : 'Total flash capacity unavailable');
  setText('partition-extent', partitions.length ? `Partition table ends at ${(extent / 1048576).toFixed(2)} MiB` : 'Partition table unavailable');
  setText('unmapped-total', partitions.length && declared > extent ? `${((declared - extent) / 1048576).toFixed(2)} MiB unmapped tail` : partitions.length && !declared ? 'Bar uses partition extent only' : '');
  bar.hidden = !partitions.length || !total;
  document.getElementById('app-headroom-legend').hidden = binary == null || !partitions.some(partition => partition.type === 0);
  let cursor = 0;
  const descriptions = [];
  function appendRegion(label, kind, offset, bytes, isApp = false) {
    if (!bytes) return;
    const headroom = isApp && binary != null ? Math.max(0, bytes - binary) : null;
    const description = `${label}: ${size(bytes)} / offset 0x${offset.toString(16)}`
      + (headroom == null ? '' : ` / projected headroom with this build: ${size(headroom)}`);
    const node = segment(kind, bytes, total);
    node.classList.add('layout-region');
    node.dataset.label = label;
    node.title = description;
    if (headroom > 0) {
      const unused = document.createElement('span');
      unused.className = 'projected-free';
      unused.style.width = `${100 * headroom / bytes}%`;
      unused.title = `${label}: ${size(headroom)} projected headroom; installed contents not measured`;
      node.append(unused);
    }
    if (bytes / total >= 0.06) {
      const text = document.createElement('span');
      text.className = 'region-label';
      text.textContent = label;
      node.append(text);
    }
    bar.append(node);
    const item = document.createElement('span');
    item.title = description;
    const swatch = document.createElement('i');
    swatch.className = kind;
    item.append(swatch, document.createTextNode(`${label} / ${(bytes / 1048576).toFixed(2)} MiB`));
    legend.append(item);
    descriptions.push(description);
  }
  for (const partition of partitions) {
    if (partition.offset > cursor) appendRegion('Outside partitions', 'outside-partitions', cursor, partition.offset - cursor);
    const selected = slot && partition.type === 0 && partition.offset === slot.offset;
    const kind = partition.type === 0 ? selected ? 'selected-app' : 'ota-app'
      : partition.label === 'extensions' ? 'extension-partition'
      : ['storage', 'spiffs', 'littlefs', 'ffat'].includes(partition.label) ? 'storage-partition' : 'other-partition';
    appendRegion(partition.label, kind, partition.offset, partition.size_bytes, partition.type === 0);
    cursor = partition.offset + partition.size_bytes;
  }
  if (partitions.length && total > cursor) appendRegion('Unmapped tail', 'unmapped', cursor, total - cursor);
  bar.setAttribute('aria-label', descriptions.length ? descriptions.join('; ') : 'Partition layout unavailable');
}

function renderCapacity() {
  const context = currentReport.context;
  const binary = context.application_binary_bytes;
  const partitions = context.app_partitions;
  const slot = partitions[Number(slotSelect.value)];
  setText('footprint-heading', `${slot ? slot.label : 'Application'} / Build footprint`);
  renderFlashLayout(slot);
  const total = currentReport.attributed_flash_bytes;
  const ram = currentReport.subsystems.reduce((sum, row) => sum + row.static_ram, 0);
  setText('binary-value', size(binary));
  setText('binary-detail', binary == null ? 'No application image' : `${binary.toLocaleString('en')} bytes`);
  setText('ram-value', size(ram));
  setText('usage-value', slot && binary != null && slot.size_bytes > 0 ? `${(100 * binary / slot.size_bytes).toFixed(1)}%` : 'Unavailable');
  setText('slot-detail', slot ? `${slot.label} / ${size(slot.size_bytes)}` : 'No partition table');
  const headroom = slot && binary != null ? slot.size_bytes - binary : null;
  setText('headroom-label', headroom != null && headroom < 0 ? 'Flash shortfall' : 'Headroom');
  setText('headroom-value', size(headroom == null ? null : Math.abs(headroom)));
  setText('headroom-detail', headroom == null ? 'Capacity unavailable' : headroom < 0 ? 'Application exceeds this slot' : 'Available in reference app slot');
  document.getElementById('headroom-value').classList.toggle('danger', headroom != null && headroom < 0);
  document.getElementById('usage-value').classList.toggle('danger', headroom != null && headroom < 0);
  const bar = document.getElementById('capacity-bar');
  bar.replaceChildren();
  const denominator = Math.max(total, binary || 0, slot ? slot.size_bytes : 0);
  const columns = ['code', 'data', 'assets'];
  for (const kind of columns) {
    const bytes = currentReport.subsystems.reduce((sum, row) => sum + row[kind], 0);
    bar.append(segment(kind, bytes, denominator));
  }
  if (binary != null && binary > total) bar.append(segment('gap', binary - total, denominator));
  if (slot && binary != null && slot.size_bytes > Math.max(binary, total)) {
    bar.append(segment('free', slot.size_bytes - Math.max(binary, total), denominator));
  }
  bar.setAttribute('aria-label', `Attributed payload ${size(total)}; application ${size(binary)}; slot ${slot ? size(slot.size_bytes) : 'unavailable'}`);
  setText('attribution-total', `Attributed payload: ${size(total)}`);
  setText('gap-total', binary == null ? 'Reconciliation unavailable' : `Binary minus attributed: ${size(binary - total)}`);
  setText('table-flash-total', kib(total));
  setText('table-ram-total', kib(ram));
}

function renderTable() {
  const query = searchInput.value.trim().toLowerCase();
  const allRows = currentReport.subsystems;
  const rows = allRows.filter(row => (!flagFilter.checked || row.flags.length > 0)
    && `${row.name} ${row.flags.join(' ')}`.toLowerCase().includes(query));
  rows.sort((left, right) => {
    const compare = sortKey === 'name' ? left.name.localeCompare(right.name)
      : left[sortKey === 'percent' ? 'flash' : sortKey] - right[sortKey === 'percent' ? 'flash' : sortKey];
    return (descending ? -compare : compare) || left.name.localeCompare(right.name);
  });
  const maximum = Math.max(1, ...allRows.map(row => row.flash));
  const body = document.getElementById('subsystem-rows');
  body.replaceChildren();
  for (const row of rows) {
    const tableRow = document.createElement('tr');
    tableRow.dataset.subsystem = row.id;
    const name = document.createElement('th');
    name.scope = 'row';
    name.textContent = row.name;
    const flags = document.createElement('td');
    if (row.flags.length) {
      for (const flag of row.flags) {
        const flagName = document.createElement('span');
        flagName.className = 'flag-name';
        flagName.textContent = flag;
        flagName.title = 'Owning flag; build state not captured';
        flags.append(flagName);
      }
    } else {
      flags.textContent = 'No mapped flag';
      flags.className = 'flag-empty';
    }
    if (row.flags.length) {
      const mobileFlags = document.createElement('span');
      mobileFlags.className = 'mobile-flags';
      for (const flag of flags.children) mobileFlags.append(flag.cloneNode(true));
      name.append(mobileFlags);
    }
    const composition = document.createElement('td');
    composition.className = 'composition-column';
    const bar = document.createElement('div');
    bar.className = 'row-bar';
    const description = `Code ${size(row.code)}, other data ${size(row.data)}, web assets ${size(row.assets)}`;
    bar.title = description;
    bar.setAttribute('role', 'img');
    bar.setAttribute('aria-label', description);
    for (const kind of ['code', 'data', 'assets']) bar.append(segment(kind, row[kind], maximum));
    composition.append(bar);
    const flash = document.createElement('td');
    flash.className = 'number';
    flash.textContent = kib(row.flash);
    flash.title = description;
    const share = document.createElement('td');
    share.className = 'share';
    share.textContent = `${(currentReport.attributed_flash_bytes ? 100 * row.flash / currentReport.attributed_flash_bytes : 0).toFixed(2)}%`;
    const ram = document.createElement('td');
    ram.className = 'number';
    ram.textContent = kib(row.static_ram);
    tableRow.append(name, flags, composition, flash, share, ram);
    body.append(tableRow);
  }
  for (const header of document.querySelectorAll('th[data-key]')) {
    const active = header.dataset.key === sortKey;
    header.setAttribute('aria-sort', active ? descending ? 'descending' : 'ascending' : 'none');
    header.querySelector('.sort-marker').textContent = active ? descending ? 'v' : '^' : '';
  }
  setText('row-count', `${rows.length} of ${allRows.length}`);
  document.getElementById('empty-state').hidden = rows.length > 0;
}

function selectReport() {
  currentReport = reports[Number(boardSelect.value)];
  const context = currentReport.context;
  const demo = currentReport.source === 'demo';
  const provenance = currentReport.provenance || {};
  setText('source-label', demo ? 'DEMO DATA / Synthetic scenario' : provenance.run_url ? 'GITHUB BUILD / Existing artifacts' : 'LOCAL BUILD / Existing artifacts');
  const buildProvenance = document.getElementById('build-provenance');
  buildProvenance.replaceChildren();
  buildProvenance.hidden = !provenance.commit;
  if (provenance.commit) {
    buildProvenance.append(`${provenance.ref} / ${provenance.commit.slice(0, 12)}${currentReport.fqbn ? ` / ${currentReport.fqbn}` : ''}`);
    try {
      const run = new URL(provenance.run_url);
      const repository = run.pathname.match(/^\/([^/]+\/[^/]+)\/actions\/runs\/\d+$/);
      if (run.protocol === 'https:' && repository) {
        const link = document.createElement('a');
        link.href = run.href;
        link.textContent = 'Build run';
        buildProvenance.append(' / ', link);
        if (/^v\d+\.\d+\.\d+/.test(provenance.ref)) {
          const release = document.createElement('a');
          release.href = `${run.origin}/${repository[1]}/releases/tag/${encodeURIComponent(provenance.ref)}`;
          release.textContent = provenance.ref;
          buildProvenance.append(' / Release ', release);
        }
      }
    } catch {}
  }
  document.getElementById('source-label').classList.toggle('demo', demo);
  setText('build-time', context.map_timestamp_utc ? `Map modified: ${new Date(context.map_timestamp_utc).toISOString().replace('T', ' ').slice(0, 19)} UTC` : 'No build timestamp');
  const warnings = document.getElementById('warnings');
  warnings.replaceChildren();
  const messages = [...context.warnings];
  if (context.application_binary_bytes != null && context.application_binary_bytes < currentReport.attributed_flash_bytes) {
    messages.push('Attributed payload exceeds the binary. These artifacts are not reconciled.');
  }
  for (const message of messages) {
    const item = document.createElement('li');
    item.textContent = message;
    warnings.append(item);
  }
  warnings.hidden = messages.length === 0;
  slotSelect.replaceChildren();
  context.app_partitions.forEach((slot, index) => slotSelect.add(new Option(slot.label, String(index))));
  if (!context.app_partitions.length) slotSelect.add(new Option('Unavailable', ''));
  slotSelect.disabled = context.app_partitions.length < 2;
  document.getElementById('slot-picker').hidden = new Set(context.app_partitions.map(slot => slot.size_bytes)).size < 2;
  const preferred = context.app_partitions.findIndex(slot => slot.selected);
  if (preferred >= 0) slotSelect.value = String(preferred);
  renderCapacity();
  renderTable();
}

boardSelect.addEventListener('change', () => {
  selectReport();
  const url = new URL(location.href);
  url.searchParams.set('board', currentReport.board);
  history.replaceState(null, '', url);
});
slotSelect.addEventListener('change', renderCapacity);
searchInput.addEventListener('input', renderTable);
flagFilter.addEventListener('change', renderTable);
for (const button of document.querySelectorAll('[data-sort]')) {
  button.addEventListener('click', () => {
    descending = sortKey === button.dataset.sort ? !descending : button.dataset.sort !== 'name';
    sortKey = button.dataset.sort;
    renderTable();
  });
}

async function loadReports() {
  const status = document.getElementById('load-status');
  try {
    const response = await fetch('reports.json', { cache: 'no-store' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const dataset = await response.json();
    if (dataset.schema_version !== 1 || !Array.isArray(dataset.reports)) {
      throw new Error('Unsupported report dataset');
    }
    document.getElementById('installer-link').hidden = dataset.installer_url !== '../index.html';
    reports = dataset.reports;
    boardSelect.replaceChildren();
    const notice = document.getElementById('run-notice');
    const missing = dataset.missing_boards || [];
    notice.textContent = missing.length ? `Missing reports (failed, skipped, or collection unavailable): ${missing.join(', ')}` : '';
    notice.hidden = !missing.length;
    if (!reports.length) {
      status.textContent = 'No board reports were collected for this run.';
      boardSelect.add(new Option('No report available', ''));
      boardSelect.disabled = true;
      return;
    }
    reports.forEach((report, index) => boardSelect.add(new Option(report.board, String(index))));
    boardSelect.disabled = false;
    const requestedBoard = new URLSearchParams(location.search).get('board');
    const requestedIndex = reports.findIndex(report => report.board === requestedBoard);
    if (requestedIndex >= 0) boardSelect.value = String(requestedIndex);
    else if (requestedBoard) {
      notice.append(` No report for ${requestedBoard}; showing an available board.`);
      notice.hidden = false;
    }
    selectReport();
    status.hidden = true;
    document.getElementById('report').hidden = false;
  } catch (error) {
    status.textContent = `Report unavailable: ${error.message}`;
    status.classList.add('danger');
    boardSelect.replaceChildren(new Option('No report available', ''));
    boardSelect.disabled = true;
    document.getElementById('report').hidden = true;
  }
}

loadReports();