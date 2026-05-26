// ============================================================================
// E-Paper — split into 4 pages: Status / Image & Schedule / Overlay / VCOM.
// ============================================================================

// Register the NVS config keys edited by the e-paper fragments. The shared
// saveFragmentConfig() helper in portal_fragment_init.js merges these with
// the core key list before scraping the DOM, so they round-trip through
// /api/config without editing the shared file.
if (typeof window.registerConfigFields === 'function') {
    window.registerConfigFields([
        'epaper_url', 'epaper_rotation',
        'epaper_overlay_enabled', 'epaper_overlay_position',
        'epaper_overlay_color', 'epaper_overlay_items',
        'epaper_frontlight_brightness', 'epaper_frontlight_duration_s'
    ]);
}

// ---- Status page -----------------------------------------------------------
// Read-only dl + "Refresh e-paper now" action. Auto-refreshes every 5s.
window.init_epaper_status_fragment = function () {
    var btn = document.getElementById('epaper-refresh-btn');
    var status = document.getElementById('epaper-refresh-status');

    var statusFields = {
        last:    document.getElementById('epaper-status-last'),
        count:   document.getElementById('epaper-status-count'),
        draw:    document.getElementById('epaper-status-draw'),
        sidecar: document.getElementById('epaper-status-sidecar'),
        battery: document.getElementById('epaper-status-battery'),
        crc:     document.getElementById('epaper-status-crc'),
        rssi:    document.getElementById('epaper-status-rssi'),
        loop:    document.getElementById('epaper-status-loop'),
        tWifi:   document.getElementById('epaper-status-t-wifi'),
        tDraw:   document.getElementById('epaper-status-t-draw'),
        tMqtt:   document.getElementById('epaper-status-t-mqtt'),
        crcAttempts: document.getElementById('epaper-status-crc-attempts'),
    };
    function fmtDrawResult(r) {
        if (r === 'updated') return 'Updated';
        if (r === 'skipped') return 'Skipped (unchanged)';
        if (r === 'fetch_failed') return 'Fetch failed';
        if (r === 'draw_failed') return 'Draw failed';
        if (r === 'disabled') return 'Not attempted';
        return 'Unknown';
    }
    function fmtSecondsAgo(s) {
        if (s == null) return null;
        if (s < 60) return s + ' s ago';
        if (s < 3600) return Math.floor(s / 60) + ' min ago';
        if (s < 86400) return Math.floor(s / 3600) + ' h ago';
        return Math.floor(s / 86400) + ' d ago';
    }
    function loadStatus() {
        fetch('/api/component/epaper-status/status')
            .then(function (r) { return r.ok ? r.json() : null; })
            .then(function (j) {
                if (!j) return;
                if (statusFields.last) {
                    var ago = fmtSecondsAgo(j.last_refresh_seconds_ago);
                    if (ago) {
                        statusFields.last.textContent = ago;
                    } else if (j.last_refresh_status === 'clock_unsynced') {
                        statusFields.last.textContent = 'Unknown (clock not synced)';
                    } else {
                        statusFields.last.textContent = 'Never (since cold boot)';
                    }
                }
                if (statusFields.count)   statusFields.count.textContent   = j.refresh_count != null ? String(j.refresh_count) : '—';
                if (statusFields.draw)    statusFields.draw.textContent    = fmtDrawResult(j.last_result);
                if (statusFields.sidecar) statusFields.sidecar.textContent = (j.sidecar_http_status && j.sidecar_http_status > 0)
                    ? String(j.sidecar_http_status)
                    : 'N/A';
                if (statusFields.battery) {
                    var pct = (j.battery_pct != null) ? j.battery_pct : null;
                    var mv = j.battery_mv;
                    if (mv && mv > 0) {
                        var pctStr = (pct != null) ? (' (' + pct + '%)') : '';
                        statusFields.battery.textContent = (mv / 1000).toFixed(2) + ' V' + pctStr;
                    } else {
                        statusFields.battery.textContent = 'Not available';
                    }
                }
                if (statusFields.crc) {
                    var c = j.last_crc32 || 0;
                    statusFields.crc.innerHTML = '<code>0x' + c.toString(16).padStart(8, '0') + '</code>';
                }
                var t = j.timing || {};
                function fmtMs(v) {
                    if (v == null || v === 0) return 'N/A';
                    return v + ' ms';
                }
                if (statusFields.rssi) {
                    statusFields.rssi.textContent = t.wifi_rssi
                        ? (t.wifi_rssi + ' dBm')
                        : 'N/A';
                }
                if (statusFields.loop)  statusFields.loop.textContent  = fmtMs(t.total_active_ms);
                if (statusFields.tWifi) statusFields.tWifi.textContent = fmtMs(t.boot_to_wifi_ms);
                if (statusFields.tDraw) statusFields.tDraw.textContent = fmtMs(t.crc_to_draw_ms);
                if (statusFields.tMqtt) statusFields.tMqtt.textContent = fmtMs(t.draw_to_mqtt_ms);
                if (statusFields.crcAttempts) {
                    statusFields.crcAttempts.textContent = (j.crc_retry_count && j.crc_retry_count > 0)
                        ? String(j.crc_retry_count)
                        : 'N/A';
                }
            })
            .catch(function () { /* silent */ });
    }
    loadStatus();
    var statusTimer = setInterval(loadStatus, 5000);
    window.addEventListener('hashchange', function once() {
        clearInterval(statusTimer);
        window.removeEventListener('hashchange', once);
    });

    if (!btn) return;
    btn.addEventListener('click', function () {
        btn.disabled = true;
        if (status) { status.textContent = 'Refreshing… (this can take 10–30 seconds)'; }
        fetch('/api/component/epaper-status/refresh', { method: 'POST' })
            .then(function (r) { return r.json().catch(function () { return { success: false, message: 'Bad response' }; }); })
            .then(function (j) {
                if (status) {
                    if (j.success) {
                        var elapsed = j.elapsed_ms ? (' (' + j.elapsed_ms + ' ms)') : '';
                        status.textContent = (j.result === 'skipped' ? 'Skipped (image unchanged)' : 'Refresh complete') + elapsed;
                    } else {
                        status.textContent = 'Failed: ' + (j.message || j.result || 'unknown error');
                    }
                }
                loadStatus();
            })
            .catch(function (e) {
                if (status) status.textContent = 'Failed: ' + e;
            })
            .finally(function () { btn.disabled = false; });
    });
};

// ---- Image & Schedule page -------------------------------------------------
// Image URL, rotation, wake interval, WiFi backoff, frontlight (if board has
// one). Saved via shared /api/config.
window.init_epaper_image_fragment = function () {
    initConfigFragment('epaper-image-save-btn', false);

    // Wire the "(button-only)" hint to the wake-seconds input.
    var wakeInput = document.getElementById('duty_cycle_wake_seconds');
    var wakeHelp  = document.getElementById('epaper-wake-help');
    var wakeHelpDefault = wakeHelp ? wakeHelp.innerHTML : '';
    function updateWakeHint() {
        if (!wakeInput || !wakeHelp) return;
        var v = parseInt(wakeInput.value, 10);
        if (v === 0) {
            wakeHelp.innerHTML = '<strong>Button-only mode:</strong> the device only wakes when the WAKE button is pressed (no timer).';
        } else {
            wakeHelp.innerHTML = wakeHelpDefault;
        }
    }
    if (wakeInput) {
        wakeInput.addEventListener('input', updateWakeHint);
        // Run once after the form has loaded its current value.
        setTimeout(updateWakeHint, 200);
    }
};

// ---- Status Overlay page ---------------------------------------------------
// Overlay enabled/position/color + per-item bitmask. Saved via shared
// /api/config.
window.init_epaper_overlay_fragment = function () {
    initConfigFragment('epaper-overlay-save-btn', false);

    // Overlay item checkboxes -> hidden bitmask.
    var overlayItemEls = [
        document.getElementById('epaper_overlay_item_icon'),
        document.getElementById('epaper_overlay_item_pct'),
        document.getElementById('epaper_overlay_item_time'),
        document.getElementById('epaper_overlay_item_cycle')
    ];
    var overlayHidden = document.getElementById('epaper_overlay_items');
    function syncOverlayItems() {
        if (!overlayHidden) return;
        var bits = 0;
        overlayItemEls.forEach(function (el) {
            if (el && el.checked) {
                bits |= parseInt(el.getAttribute('data-overlay-bit'), 10) || 0;
            }
        });
        overlayHidden.value = bits;
    }
    overlayItemEls.forEach(function (el) {
        if (el) el.addEventListener('change', syncOverlayItems);
    });
    // Sync once after load values have populated checkboxes.
    setTimeout(syncOverlayItems, 250);
};

// ---- VCOM page -------------------------------------------------------------
// Read / write / preview the TPS65186 VCOM calibration voltage. No shared
// /api/config save — all writes are direct component actions.
window.init_epaper_vcom_fragment = function () {
    var vcomReadBtn = document.getElementById('epaper-vcom-read-btn');
    var vcomCurrent = document.getElementById('epaper-vcom-current');
    var vcomWriteBtn = document.getElementById('epaper-vcom-write-btn');
    var vcomTestBtn = document.getElementById('epaper-vcom-test-btn');
    var vcomValueEl = document.getElementById('epaper_vcom_value');
    var vcomStatus = document.getElementById('epaper-vcom-status');

    function setVcomStatus(text, isErr) {
        if (!vcomStatus) return;
        vcomStatus.textContent = text;
        vcomStatus.style.color = isErr ? '#c00' : '';
    }

    function parseVcomInput() {
        if (!vcomValueEl) return null;
        var value = parseFloat(vcomValueEl.value);
        if (isNaN(value) || !(value < 0 && value >= -5)) {
            return null;
        }
        return value;
    }

    if (vcomReadBtn) {
        vcomReadBtn.addEventListener('click', function () {
            vcomReadBtn.disabled = true;
            setVcomStatus('Reading…', false);
            fetch('/api/component/epaper-vcom/vcom')
                .then(function (r) { return r.json(); })
                .then(function (j) {
                    if (j && j.vcom != null) {
                        if (vcomCurrent) vcomCurrent.textContent = Number(j.vcom).toFixed(2) + ' V';
                        if (vcomValueEl && !vcomValueEl.value) vcomValueEl.value = Number(j.vcom).toFixed(2);
                        setVcomStatus('Read OK', false);
                    } else {
                        if (vcomCurrent) vcomCurrent.textContent = 'N/A';
                        setVcomStatus(j && j.message ? j.message : 'VCOM read failed', true);
                    }
                })
                .catch(function (e) { setVcomStatus('Error: ' + e, true); })
                .finally(function () { vcomReadBtn.disabled = false; });
        });
    }

    if (vcomWriteBtn) {
        vcomWriteBtn.addEventListener('click', function () {
            var v = parseVcomInput();
            if (v === null) {
                setVcomStatus('VCOM must be a negative number between -5.0 and 0.', true);
                return;
            }
            if (!confirm('Write VCOM = ' + v.toFixed(2) + ' V to the TPS65186 EEPROM?\n\n'
                       + 'The EEPROM is rated for ~100,000 program cycles. Only continue if you really need to change this value.')) {
                return;
            }
            vcomWriteBtn.disabled = true;
            setVcomStatus('Programming EEPROM…', false);
            var body = 'value=' + encodeURIComponent(v.toFixed(3));
            fetch('/api/component/epaper-vcom/vcom', {
                method: 'POST',
                headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
                body: body
            })
                .then(function (r) { return r.json(); })
                .then(function (j) {
                    if (j && j.success) {
                        if (vcomCurrent) vcomCurrent.textContent = Number(v).toFixed(2) + ' V';
                        setVcomStatus('VCOM programmed: ' + Number(v).toFixed(2) + ' V', false);
                    } else {
                        setVcomStatus(j && j.message ? j.message : 'Write failed', true);
                    }
                })
                .catch(function (e) { setVcomStatus('Error: ' + e, true); })
                .finally(function () { vcomWriteBtn.disabled = false; });
        });
    }

    if (vcomTestBtn) {
        vcomTestBtn.addEventListener('click', function () {
            vcomTestBtn.disabled = true;
            // If the user has typed a candidate VCOM value, preview it via the
            // TPS65186 volatile registers (no EEPROM write). Empty input falls
            // back to the currently programmed EEPROM value.
            var previewUrl = '/api/component/epaper-vcom/vcom-test-pattern';
            var previewLabel = 'programmed';
            var pv = parseVcomInput();
            if (pv !== null) {
                previewUrl += '?vcom=' + encodeURIComponent(pv.toFixed(2));
                previewLabel = pv.toFixed(2) + ' V (preview)';
            }
            setVcomStatus('Drawing test pattern with VCOM = ' + previewLabel + '…', false);
            fetch(previewUrl, { method: 'POST' })
                .then(function (r) { return r.json(); })
                .then(function (j) {
                    setVcomStatus(j && j.success ? 'Test pattern drawn — look at the panel.' : 'Failed', !j || !j.success);
                })
                .catch(function (e) { setVcomStatus('Error: ' + e, true); })
                .finally(function () { vcomTestBtn.disabled = false; });
        });
    }
};
