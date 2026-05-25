// portal_fragment_init.js — Init functions called by portal_nav.js after fragment load.
// Convention: window['init_' + itemId.replace(/-/g, '_') + '_fragment']()
// Each function populates fragment fields and attaches event listeners.

// ============================================================================
// Shared: config save helper (no FormData needed)
// ============================================================================

/**
 * Gather config fields from the current DOM and save to /api/config.
 *
 * Saves are always sent with no_reboot=1 — the device never reboots from
 * a per-fragment Save click. Settings that require a reboot to take effect
 * surface a global pending-reboot banner via setPendingReboot(); the user
 * can batch several changes and click "Reboot Now" once at the end.
 *
 * @param {boolean} requiresReboot - True if this fragment's settings need a
 *                                   reboot to take effect (wifi, network,
 *                                   device name, mode, mqtt, ble, auth).
 *                                   When true, the pending-reboot banner is
 *                                   shown after a successful save.
 */
async function saveFragmentConfig(requiresReboot) {
    // Build config from DOM elements that exist in the current fragment
    var config = {};
    var fields = [
        'wifi_ssid', 'wifi_password', 'device_name', 'fixed_ip',
        'subnet_mask', 'gateway', 'dns1', 'dns2',
        'mqtt_host', 'mqtt_port', 'mqtt_username', 'mqtt_password',
        'operating_mode', 'duty_cycle_wake_seconds', 'mqtt_publish_interval_seconds',
        'portal_idle_timeout_seconds', 'wifi_backoff_max_seconds',
        'ble_burst_count', 'ble_adv_interval_ms',
        'mqtt_publish_scope',
        'basic_auth_enabled', 'basic_auth_username', 'basic_auth_password',
        'ble_enabled',
        'audio_volume', 'tap_beep', 'lp_beep',
        'backlight_brightness',
        'screen_saver_enabled', 'screen_saver_timeout_seconds',
        'screen_saver_fade_out_ms', 'screen_saver_fade_in_ms',
        'screen_saver_wake_on_touch', 'screen_saver_wake_binding',
        'epaper_url', 'epaper_rotation'
    ];
    fields.forEach(function (name) {
        // Radio groups: pick the checked option (if any).
        var radios = document.querySelectorAll('input[type="radio"][name="' + name + '"]');
        if (radios.length > 0) {
            var checked = document.querySelector('input[type="radio"][name="' + name + '"]:checked');
            if (checked && !checked.disabled) config[name] = checked.value;
            return;
        }
        var el = document.querySelector('[name="' + name + '"]');
        if (!el || el.disabled) return;
        if (el.type === 'checkbox') {
            config[name] = el.checked;
        } else {
            config[name] = el.value;
        }
    });

    // Validate
    var validation = validateConfig(config);
    if (!validation.valid) {
        showMessage(validation.message, 'error');
        return;
    }

    // Validate binding fields if available
    if (typeof bindingValidateHomeConfig === 'function') {
        var bv = bindingValidateHomeConfig();
        if (!bv.valid) {
            showMessage(bv.count + ' binding error' + (bv.count > 1 ? 's' : '') + ' — check highlighted fields', 'error');
            return;
        }
    }

    try {
        var response = await fetch('/api/config?no_reboot=1', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(config)
        });
        if (!response.ok) throw new Error('Failed to save configuration');
        var result = await response.json();
        if (result.success) {
            if (requiresReboot) {
                // Banner is the user feedback — skip the toast so it doesn't
                // cover the freshly-appeared "reboot required" banner.
                if (typeof setPendingReboot === 'function') setPendingReboot();
            } else {
                showMessage('Configuration saved', 'success');
            }
        } else {
            showMessage('Failed to save configuration', 'error');
        }
    } catch (error) {
        showMessage('Error saving: ' + error.message, 'error');
    }
}

/**
 * Wire loadConfig() + a save button for the common config-fragment pattern.
 * @param {string} saveBtnId - ID of the save button element
 * @param {boolean} requiresReboot - True if this fragment's settings need a
 *   reboot to take effect; false for live-apply settings (brightness, etc.).
 */
function initConfigFragment(saveBtnId, requiresReboot) {
    loadConfig();
    var btn = document.getElementById(saveBtnId);
    if (btn) btn.addEventListener('click', function () { saveFragmentConfig(requiresReboot); });
}

// ============================================================================
// Welcome
// ============================================================================

window.init_welcome_fragment = function () {
    // Render hero card when a board-defined primary category is active
    var primary = window._portalPrimary;
    var heroEl = document.getElementById('welcome-hero-card');
    if (primary && primary.fragment && heroEl) {
        heroEl.innerHTML =
            '<div class="card mb-3 hero-card">' +
            '<div class="card-body text-center">' +
            '<span class="hero-icon">' + (primary.icon || '') + '</span>' +
            '<h4>' + escAttr(primary.label || primary.category) + '</h4>' +
            '<a href="#' + encodeURIComponent(primary.fragment) + '" class="btn btn-primary">Open</a>' +
            '</div></div>';
        heroEl.classList.remove('d-none');
    }

    // Populate status cards from /api/health and /api/info
    getDeviceInfo().then(function (info) {
        if (!info) return;
        var el;
        el = document.getElementById('welcome-firmware');
        if (el) el.textContent = info.version || '—';
        el = document.getElementById('welcome-firmware-detail');
        if (el) el.textContent = info.chip_model || '';
    });

    fetchHealthOnce().then(function (h) {
        if (!h) return;
        var el;
        el = document.getElementById('welcome-wifi-status');
        if (el) el.textContent = h.ip_address ? 'Connected' : 'Disconnected';
        el = document.getElementById('welcome-wifi-detail');
        if (el && h.wifi_rssi) el.textContent = h.wifi_rssi + ' dBm';
        el = document.getElementById('welcome-mqtt-status');
        if (el) el.textContent = h.mqtt_connected ? 'Connected' : 'Disconnected';
        el = document.getElementById('welcome-mqtt-detail');
        if (el) el.textContent = h.mqtt_enabled ? '' : 'Not enabled';
        el = document.getElementById('welcome-uptime');
        if (el && h.uptime_seconds != null) {
            var s = h.uptime_seconds;
            var d = Math.floor(s / 86400); s %= 86400;
            var hr = Math.floor(s / 3600); s %= 3600;
            var m = Math.floor(s / 60);
            el.textContent = (d > 0 ? d + 'd ' : '') + hr + 'h ' + m + 'm';
        }
    });
};

// ============================================================================
// WiFi
// ============================================================================

window.init_wifi_fragment = function () {
    initConfigFragment('wifi-save-btn', true);
    // Show setup notice if in AP mode
    if (typeof portalMode !== 'undefined' && portalMode === 'core') {
        var notice = document.getElementById('setup-notice');
        if (notice) notice.style.display = 'block';
    }
};

// ============================================================================
// Setup wizard (AP-mode first-boot onboarding)
// ============================================================================
//
// Single-card form that combines wifi credentials with optional friendly
// name, portal auth, and static IP. Submits everything in one POST and
// reboots immediately — the deferred-reboot banner is not used here because
// onboarding is a one-shot hand-off, not iterative tweaking.

window.init_setup_fragment = function () {
    loadConfig();  // pre-fill device_name (and ssid if already set)

    // Checkbox → reveal/hide the corresponding field group.
    function wireToggle(checkboxId, groupId) {
        var cb = document.getElementById(checkboxId);
        var group = document.getElementById(groupId);
        if (!cb || !group) return;
        cb.addEventListener('change', function () {
            group.style.display = cb.checked ? '' : 'none';
        });
    }
    wireToggle('setup_enable_auth', 'setup-auth-fields');
    wireToggle('setup_use_static_ip', 'setup-static-ip-fields');

    var btn = document.getElementById('setup-save-btn');
    if (btn) btn.addEventListener('click', saveSetupWizard);
};

async function saveSetupWizard() {
    var ssid = (document.getElementById('wifi_ssid') || {}).value || '';
    if (!ssid.trim()) {
        showMessage('WiFi network name is required', 'error');
        return;
    }

    var authOn = !!(document.getElementById('setup_enable_auth') || {}).checked;
    var staticIpOn = !!(document.getElementById('setup_use_static_ip') || {}).checked;
    var nameEl = document.getElementById('device_name');
    var newDeviceName = nameEl ? nameEl.value : null;

    // Build payload: always include wifi + device_name; include auth / static
    // IP fields only when their gating checkbox is on. Unchecked groups are
    // simply omitted so the server keeps its existing defaults rather than
    // forcing us to send no-op clearing values (which would also trip the
    // AP-mode basic-auth security guard for non-first-boot sessions).
    var config = {
        wifi_ssid: ssid,
        wifi_password: (document.getElementById('wifi_password') || {}).value || '',
        device_name: newDeviceName || ''
    };
    if (authOn) {
        config.basic_auth_enabled = true;
        config.basic_auth_username = (document.getElementById('basic_auth_username') || {}).value || '';
        config.basic_auth_password = (document.getElementById('basic_auth_password') || {}).value || '';
    }
    if (staticIpOn) {
        config.fixed_ip    = (document.getElementById('fixed_ip')    || {}).value || '';
        config.subnet_mask = (document.getElementById('subnet_mask') || {}).value || '';
        config.gateway     = (document.getElementById('gateway')     || {}).value || '';
        config.dns1        = (document.getElementById('dns1')        || {}).value || '';
        config.dns2        = (document.getElementById('dns2')        || {}).value || '';
    }

    // Show reboot dialog up-front; if a friendly name was set, the dialog
    // will direct the user to http://<sanitized>.local after the device
    // comes back on the configured WiFi.
    showRebootDialog({
        title: 'Saving Configuration',
        message: 'Connecting to WiFi...',
        context: 'save',
        newDeviceName: newDeviceName
    });

    try {
        // No no_reboot=1 — we want the device to reboot now.
        var response = await fetch('/api/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(config)
        });
        if (!response.ok) {
            // Try to surface the server-provided message (e.g. the AP-mode
            // basic-auth guard's 403) rather than a generic fallback.
            var serverMsg = null;
            try {
                var body = await response.json();
                if (body && body.message) serverMsg = body.message;
            } catch (_) { /* non-JSON body — fall through */ }
            throw new Error(serverMsg || ('HTTP ' + response.status));
        }
        // Success path: the reboot dialog's reconnection poller handles the
        // rest (network switch + redirect to mDNS hostname).
    } catch (error) {
        // In AP mode the TCP connection often drops mid-response as the
        // device starts rebooting — that's normal and the reboot dialog
        // already shows the manual reconnect instructions for that case.
        if (!String(error.message).match(/Failed to fetch|NetworkError/)) {
            var overlay = document.getElementById('reboot-overlay');
            if (overlay) overlay.style.display = 'none';
            showMessage('Error saving: ' + error.message, 'error');
        }
    }
}

// ============================================================================
// Device Name
// ============================================================================

window.init_device_name_fragment = function () {
    initConfigFragment('device-name-save-btn', true);
};

// ============================================================================
// Network (static IP + security)
// ============================================================================

window.init_network_fragment = function () {
    initConfigFragment('network-save-btn', true);
    var secBtn = document.getElementById('security-save-btn');
    if (secBtn) secBtn.addEventListener('click', function () { saveFragmentConfig(true); });
};

// ============================================================================
// Operating Mode
// ============================================================================

window.init_mode_fragment = function () {
    initConfigFragment('mode-save-btn', true);

    // Hide the BLE option unless the firmware build advertises BLE telemetry.
    function applyCapsVisibility() {
        var caps = (window.__device_caps || {});
        var bleOpt = document.getElementById('mode_opt_duty_cycle_ble');
        if (bleOpt) bleOpt.style.display = caps.ble ? '' : 'none';
        var epOpt = document.getElementById('mode_opt_duty_cycle_epaper');
        if (epOpt) epOpt.style.display = caps.epaper ? '' : 'none';
        // If BLE was the persisted choice but the build no longer supports it,
        // fall back to always_on.
        if (!caps.ble) {
            var bleRadio = document.getElementById('operating_mode_duty_cycle_ble');
            if (bleRadio && bleRadio.checked) {
                var alwaysOn = document.getElementById('operating_mode_always_on');
                if (alwaysOn) alwaysOn.checked = true;
            }
        }
        if (!caps.epaper) {
            var epRadio = document.getElementById('operating_mode_duty_cycle_epaper');
            if (epRadio && epRadio.checked) {
                var alwaysOn = document.getElementById('operating_mode_always_on');
                if (alwaysOn) alwaysOn.checked = true;
            }
        }
    }

    function getSelectedMode() {
        var checked = document.querySelector('input[type="radio"][name="operating_mode"]:checked');
        return checked ? checked.value : 'always_on';
    }

    function updateModeVisibility() {
        var mode = getSelectedMode();
        var isDutyMqtt = (mode === 'duty_cycle_mqtt');
        var isDutyBle = (mode === 'duty_cycle_ble');
        var isDutyEpaper = (mode === 'duty_cycle_epaper');
        var isAnyDuty = isDutyMqtt || isDutyBle || isDutyEpaper;

        var dc = document.getElementById('duty-cycle-settings');
        if (dc) dc.style.display = isAnyDuty ? '' : 'none';
        // Wi-Fi backoff is meaningless for BLE.
        var backoff = document.getElementById('wifi-backoff-row');
        if (backoff) backoff.style.display = (isDutyMqtt || isDutyEpaper) ? '' : 'none';
        var ble = document.getElementById('ble-telemetry-settings');
        if (ble) ble.style.display = isDutyBle ? '' : 'none';
    }

    var radios = document.querySelectorAll('input[type="radio"][name="operating_mode"]');
    radios.forEach(function (r) { r.addEventListener('change', updateModeVisibility); });

    // loadConfig() runs asynchronously; observe selection until the populated
    // value differs from the initial DOM state (or we time out).
    var initial = getSelectedMode();
    var attempts = 0;
    var timer = setInterval(function () {
        attempts++;
        if (getSelectedMode() !== initial || (window.__device_caps && Object.keys(window.__device_caps).length) || attempts > 40) {
            clearInterval(timer);
            applyCapsVisibility();
            updateModeVisibility();
        }
    }, 50);

    applyCapsVisibility();
    updateModeVisibility();
};

// ============================================================================
// Factory Reset
// ============================================================================

window.init_factory_reset_fragment = function () {
    var btn = document.getElementById('reset-btn');
    if (btn) btn.addEventListener('click', function () {
        if (typeof resetConfig === 'function') resetConfig();
    });
};

// ============================================================================
// Brightness
// ============================================================================

window.init_brightness_fragment = function () {
    initConfigFragment('brightness-save-btn', false);
    var slider = document.getElementById('backlight_brightness');
    if (slider) slider.addEventListener('input', handleBrightnessChange);
    var screenSelect = document.getElementById('screen_selection');
    if (screenSelect) screenSelect.addEventListener('change', handleScreenChange);

    // Load screen selection dropdown if available
    getDeviceInfo().then(function (info) {
        if (!info || !info.screens || info.screens.length === 0) return;
        var group = document.getElementById('screen-selection-group');
        var sel = document.getElementById('screen_selection');
        if (group && sel) {
            group.style.display = '';
            sel.innerHTML = '';
            info.screens.forEach(function (s) {
                var opt = document.createElement('option');
                opt.value = s.index;
                opt.textContent = s.name;
                if (s.active) opt.selected = true;
                sel.appendChild(opt);
            });
        }
    });
};

// ============================================================================
// Screen Saver
// ============================================================================

window.init_screensaver_fragment = function () {
    initConfigFragment('screensaver-save-btn', false);
    // Initialize binding validator on inputs
    if (typeof bindingInitStaticInputs === 'function') bindingInitStaticInputs();
};

// ============================================================================
// Swipe Actions
// ============================================================================

window.init_swipe_actions_fragment = function () {
    if (typeof swipeInitEditors === 'function') swipeInitEditors();
    if (typeof loadSwipeActions === 'function') loadSwipeActions();
    actionEditorWireFragment(SWIPE_DIRECTIONS);
};

// ============================================================================
// Boot Actions
// ============================================================================

window.init_boot_actions_fragment = function () {
    if (typeof bootActionsInitEditors === 'function') bootActionsInitEditors();
    if (typeof loadBootActions === 'function') loadBootActions();
    actionEditorWireFragment(BOOT_ACTION_PREFIXES);
};

// ============================================================================
// Timers
// ============================================================================

window.init_timers_fragment = function () {
    if (typeof timerConfigInitEditors === 'function') timerConfigInitEditors();
    if (typeof loadTimerConfig === 'function') loadTimerConfig();
    actionEditorWireFragment(TIMER_EXPIRE_PREFIXES);
};

// ============================================================================
// MQTT
// ============================================================================

window.init_mqtt_fragment = function () {
    initConfigFragment('mqtt-save-btn', true);
};

// ============================================================================
// BLE Keyboard
// ============================================================================

window.init_ble_fragment = function () {
    initConfigFragment('ble-save-btn', true);
    // Start BLE status polling
    if (typeof loadBleStatus === 'function') loadBleStatus();
};

// ============================================================================
// Home Assistant
// ============================================================================

window.init_ha_discovery_fragment = function () {
    // Static informational fragment — no data to load
};

// ============================================================================
// Volume & Beep
// ============================================================================

window.init_volume_fragment = function () {
    initConfigFragment('volume-save-btn', false);
};

// ============================================================================
// E-Paper
// ============================================================================

window.init_epaper_fragment = function () {
    initConfigFragment('epaper-save-btn', false);
    var btn = document.getElementById('epaper-refresh-btn');
    var status = document.getElementById('epaper-refresh-status');

    // Status card auto-refresh
    var statusFields = {
        last:    document.getElementById('epaper-status-last'),
        count:   document.getElementById('epaper-status-count'),
        draw:    document.getElementById('epaper-status-draw'),
        sidecar: document.getElementById('epaper-status-sidecar'),
        battery: document.getElementById('epaper-status-battery'),
        crc:     document.getElementById('epaper-status-crc'),
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
        fetch('/api/component/epaper/status')
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
                if (statusFields.battery) statusFields.battery.textContent = (j.battery_mv && j.battery_mv > 0)
                    ? (j.battery_mv / 1000).toFixed(2) + ' V'
                    : 'Not available';
                if (statusFields.crc) {
                    var c = j.last_crc32 || 0;
                    statusFields.crc.innerHTML = '<code>0x' + c.toString(16).padStart(8, '0') + '</code>';
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
        fetch('/api/component/epaper/refresh', { method: 'POST' })
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

// ============================================================================
// Sound Files
// ============================================================================

window.init_sounds_fragment = function () {
    if (typeof loadSoundList === 'function') loadSoundList();
};

// ============================================================================
// Sensor Data
// ============================================================================

window.init_sensor_data_fragment = function () {
    // Sensor badges are populated by health widget polling
    if (typeof initHealthWidget === 'function') initHealthWidget();
};

// ============================================================================
// Thresholds
// ============================================================================

window.init_thresholds_fragment = function () {
    // Save button wired — backend API implementation pending
    var btn = document.getElementById('thresholds-save-btn');
    if (btn) btn.addEventListener('click', function () { saveFragmentConfig(false); });
};

// ============================================================================
// OTA Update (online)
// ============================================================================

window.init_ota_update_fragment = function () {
    // Populate GitHub Pages link
    getDeviceInfo().then(function (info) {
        if (typeof updateOnlineUpdateSection === 'function') updateOnlineUpdateSection(info);
    });
};

// ============================================================================
// Manual Upload
// ============================================================================

window.init_manual_upload_fragment = function () {
    var fileInput = document.getElementById('firmware-file');
    if (fileInput) fileInput.addEventListener('change', handleFileSelect);
    var uploadBtn = document.getElementById('upload-btn');
    if (uploadBtn) uploadBtn.addEventListener('click', uploadFirmware);
};

// ============================================================================
// Version Info
// ============================================================================

window.init_version_info_fragment = function () {
    var set = function (id, val) {
        var el = document.getElementById(id);
        if (el) el.textContent = val || '—';
    };
    getDeviceInfo().then(function (info) {
        if (!info) return;
        set('vi-firmware', info.version);
        set('vi-chip', info.chip_model);
        set('vi-cores', info.chip_cores);
        set('vi-freq', info.cpu_freq ? info.cpu_freq + ' MHz' : null);
        set('vi-flash', info.flash_chip_size ? (info.flash_chip_size / 1048576).toFixed(0) + ' MB' : null);
        set('vi-psram', info.psram_size ? (info.psram_size / 1048576).toFixed(1) + ' MB' : null);
        set('vi-sdk', info.idf_version);
        set('vi-device-name', info.hostname);
    });
    fetchHealthOnce().then(function (h) {
        if (!h) return;
        set('vi-ip', h.ip_address);
    });
};

// ============================================================================
// Pad Editor
// ============================================================================

window.init_pad_editor_fragment = function () {
    if (typeof padInit === 'function') padInit();
    if (typeof bindingInitStaticInputs === 'function') bindingInitStaticInputs();
};

// ============================================================================
// Button Defaults
// ============================================================================

window.init_button_defaults_fragment = function () {
    if (typeof padLoadButtonDefaultsFromDevice === 'function') padLoadButtonDefaultsFromDevice();
    if (typeof bindingInitStaticInputs === 'function') bindingInitStaticInputs();
    var btn = document.getElementById('btn-defaults-save-btn');
    if (btn) btn.addEventListener('click', function () {
        if (typeof padSaveButtonDefaults === 'function') padSaveButtonDefaults();
    });
};
