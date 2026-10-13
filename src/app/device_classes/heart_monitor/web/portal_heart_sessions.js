(function () {
  'use strict';
  let current = null;
  const escape = value => String(value ?? '').replace(/[&<>"']/g, character => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[character]));
  const numeric = (value, digits = 1) => Number.isFinite(value) ? value.toFixed(digits) : 'Unavailable';
  window.init_heart_sessions_fragment = function () {
    if (current) current.dispose();
    const root = document.getElementById('heart-sessions-root');
    if (!root) return;
    const element = id => root.querySelector('#' + id);
    const controller = new AbortController();
    let entries = [], timer = null, loading = false, disposed = false, reportView = null;
    let importActive = false;
    function dispose() {
      disposed = true;controller.abort();clearTimeout(timer);observer.disconnect();
      if (reportView) reportView.dispose();
      document.body.classList.remove('heart-session-print');
    }
    const observer = new MutationObserver(() => {if (!root.isConnected) dispose();});
    observer.observe(document.getElementById('content-pane') || document.body, {childList:true});
    current = {dispose};
    function render() {
      const query = element('heart-session-search').value.toLowerCase();
      const selected = entries.filter(entry => entry.session_id.includes(query));
      element('heart-session-list').innerHTML = selected.length ? `<table class="heart-session-table"><thead><tr><th>Session</th><th>Started</th><th>Duration</th><th>Reliable coverage</th><th>Mean BPM</th><th>Status</th><th>Actions</th></tr></thead><tbody>${selected.map(entry => `<tr><td><button class="btn btn-link" data-open="${entry.session_id}">${entry.session_id}</button></td><td>${Number.isFinite(entry.started_unix_time_s) && entry.started_unix_time_s > 0 ? escape(new Date(entry.started_unix_time_s * 1000).toLocaleString()) : 'Clock unavailable'}</td><td>${numeric(entry.duration_s,2)} s</td><td>${numeric(entry.reliable_coverage * 100)}%</td><td>${entry.sufficient_reliable_data ? numeric(entry.reliable_bpm_mean) : 'Unavailable'}</td><td>${entry.complete ? 'Complete' : 'Interrupted'}</td><td><a class="btn btn-sm btn-outline-secondary" href="/api/heart/sessions/${entry.session_id}" download="capture-${entry.session_id}.json">JSON</a><a class="btn btn-sm btn-outline-secondary" href="/api/heart/sessions/${entry.session_id}/raw.csv" download="capture-${entry.session_id}.csv">CSV</a><button class="btn btn-sm btn-outline-danger" data-delete="${entry.session_id}" title="Delete session ${entry.session_id}" aria-label="Delete session ${entry.session_id}"><span class="portal-icon" aria-hidden="true">delete</span></button></td></tr>`).join('')}</tbody></table>` : '<p>No matching sessions.</p>';
    }
    async function refresh() {
      if (disposed || loading) return;
      loading = true;clearTimeout(timer);element('heart-session-refresh').disabled = true;
      try {
        const response = await fetch('/api/heart/sessions', {signal:controller.signal});
        if (!response.ok) throw Error(`Session list: HTTP ${response.status}`);
        const data = await response.json();
        if (disposed) return;
        if (!Array.isArray(data.entries)) throw Error('Invalid session catalog');
        entries = data.entries.filter(entry => /^[0-9a-f]{8}$/.test(entry.session_id) && entry.id === entry.session_id);
        importActive = Boolean(data.import?.active);
        element('heart-session-import').disabled = importActive;
        element('heart-session-status').textContent = `${entries.length} saved sessions${importActive ? ` · Indexing existing captures: ${data.import.scanned || 0} checked, ${data.import.added || 0} added` : ''}${data.import?.failed ? ` · ${data.import.failed} captures could not be indexed` : ''}`;
        render();
        if (importActive) timer = setTimeout(refresh, 2000);
      } catch (error) {
        if (!disposed) element('heart-session-status').textContent = error.message;
      } finally {loading = false;if (!disposed) element('heart-session-refresh').disabled = false;}
    }
    function showList() {
      if (reportView) {reportView.dispose();reportView = null;}
      element('heart-session-detail-view').hidden = true;element('heart-session-list-view').hidden = false;
    }
    root.addEventListener('click', async event => {
      const button = event.target.closest('[data-open],[data-delete]');
      if (!button || !root.contains(button)) return;
      const id = button.dataset.open || button.dataset.delete;
      if (!/^[0-9a-f]{8}$/.test(id)) return;
      if (button.dataset.open) {
        if (reportView) reportView.dispose();
        element('heart-session-list-view').hidden = true;element('heart-session-detail-view').hidden = false;
        reportView = window.heartSessionReport.mount(root.querySelector('.heart-report'), id);
      } else if (confirm(`Delete session ${id}, including its report and raw capture?`)) {
        button.disabled = true;
        try {
          const response = await fetch(`/api/heart/sessions/${id}`, {method:'DELETE',signal:controller.signal});
          if (!response.ok) throw Error(`Delete: HTTP ${response.status}`);
          await refresh();
        } catch (error) {if (!disposed) {element('heart-session-status').textContent = error.message;button.disabled = false;}}
      }
    }, {signal:controller.signal});
    element('heart-session-search').addEventListener('input', render, {signal:controller.signal});
    element('heart-session-refresh').addEventListener('click', refresh, {signal:controller.signal});
    element('heart-session-back').addEventListener('click', showList, {signal:controller.signal});
    element('heart-session-print').addEventListener('click', () => {if (reportView) window.print();}, {signal:controller.signal});
    element('heart-session-import').addEventListener('click', async () => {
      element('heart-session-import').disabled = true;
      try {
        const response = await fetch('/api/heart/sessions/import', {method:'POST',signal:controller.signal});
        if (!response.ok) throw Error(`Rescan: HTTP ${response.status}`);
        await refresh();
      } catch (error) {if (!disposed) {element('heart-session-status').textContent = error.message;element('heart-session-import').disabled = importActive;}}
    }, {signal:controller.signal});
    refresh();
  };
})();