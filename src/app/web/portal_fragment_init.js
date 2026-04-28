// portal_fragment_init.js — Init functions called by portal_nav.js after fragment load.
// Convention: window['init_' + itemId.replace(/-/g, '_') + '_fragment']()
// Each function populates fragment fields and attaches event listeners.

// ============================================================================
// Shared: config save helper (no FormData needed)
// ============================================================================

/**
 * Gather config fields from the current DOM and save to /api/config.
 * @param {boolean} reboot - If true, device reboots after save.
 */
async function saveFragmentConfig(reboot) {
    // Build config from DOM elements that exist in the current fragment
    var config = {};
    var fields = [
        'wifi_ssid', 'wifi_password', 'device_name', 'fixed_ip',
        'subnet_mask', 'gateway', 'dns1', 'dns2',
        'mqtt_host', 'mqtt_port', 'mqtt_username', 'mqtt_password',
        'power_mode', 'cycle_interval_seconds', 'portal_idle_timeout_seconds', 'wifi_backoff_max_seconds',
        'mqtt_publish_scope',
        'basic_auth_enabled', 'basic_auth_username', 'basic_auth_password',
        'ble_enabled',
        'audio_volume', 'tap_beep', 'lp_beep',
        'backlight_brightness',
        'screen_saver_enabled', 'screen_saver_timeout_seconds',
        'screen_saver_fade_out_ms', 'screen_saver_fade_in_ms',
        'screen_saver_wake_on_touch', 'screen_saver_wake_binding'
    ];
    fields.forEach(function (name) {
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

    if (reboot) {
        var currentDeviceName = document.getElementById('device_name');
        showRebootDialog({
            title: 'Saving Configuration',
            message: 'Saving configuration...',
            context: 'save',
            newDeviceName: currentDeviceName ? currentDeviceName.value : null
        });
    }

    try {
        var url = '/api/config' + (reboot ? '' : '?no_reboot=1');
        var response = await fetch(url, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(config)
        });
        if (!response.ok) throw new Error('Failed to save configuration');
        var result = await response.json();
        if (result.success) {
            if (reboot) {
                var msg = document.getElementById('reboot-message');
                if (msg) msg.textContent = 'Configuration saved. Device is rebooting...';
            } else {
                showMessage('Configuration saved successfully!', 'success');
            }
        } else {
            showMessage('Failed to save configuration', 'error');
        }
    } catch (error) {
        if (reboot && (error.message.includes('Failed to fetch') || error.message.includes('NetworkError'))) {
            var msg = document.getElementById('reboot-message');
            if (msg) msg.textContent = 'Configuration saved. Device is rebooting...';
        } else {
            var overlay = document.getElementById('reboot-overlay');
            if (overlay) overlay.style.display = 'none';
            showMessage('Error saving: ' + error.message, 'error');
        }
    }
}

/**
 * Wire loadConfig() + a save button for the common config-fragment pattern.
 * @param {string} saveBtnId - ID of the save button element
 * @param {boolean} reboot - Whether saving should trigger a device reboot
 */
function initConfigFragment(saveBtnId, reboot) {
    loadConfig();
    var btn = document.getElementById(saveBtnId);
    if (btn) btn.addEventListener('click', function () { saveFragmentConfig(reboot); });
}

// ============================================================================
// Welcome
// ============================================================================

window.init_welcome_fragment = function () {
    // Populate status cards from /api/health and /api/info
    fetch('/api/info').then(function (r) { return r.json(); }).then(function (info) {
        var el;
        el = document.getElementById('welcome-firmware');
        if (el) el.textContent = info.version || '—';
        el = document.getElementById('welcome-firmware-detail');
        if (el) el.textContent = info.chip_model || '';
    }).catch(function () {});

    fetch('/api/health').then(function (r) { return r.json(); }).then(function (h) {
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
    }).catch(function () {});
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
    fetch('/api/info').then(function (r) { return r.json(); }).then(function (info) {
        if (info.screens && info.screens.length > 0) {
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
        }
    }).catch(function () {});
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
    fetch('/api/sounds/list').then(function (r) { return r.ok ? r.json() : []; })
        .then(function (sounds) {
            if (typeof actionEditorPopulateSounds === 'function') actionEditorPopulateSounds(SWIPE_DIRECTIONS, sounds);
        }).catch(function () {});
};

// ============================================================================
// Boot Actions
// ============================================================================

window.init_boot_actions_fragment = function () {
    if (typeof bootActionsInitEditors === 'function') bootActionsInitEditors();
    if (typeof loadBootActions === 'function') loadBootActions();
    fetch('/api/sounds/list').then(function (r) { return r.ok ? r.json() : []; })
        .then(function (sounds) {
            if (typeof actionEditorPopulateSounds === 'function') actionEditorPopulateSounds(BOOT_ACTION_PREFIXES, sounds);
        }).catch(function () {});
};

// ============================================================================
// Timers
// ============================================================================

window.init_timers_fragment = function () {
    if (typeof timerConfigInitEditors === 'function') timerConfigInitEditors();
    if (typeof loadTimerConfig === 'function') loadTimerConfig();
    fetch('/api/sounds/list').then(function (r) { return r.ok ? r.json() : []; })
        .then(function (sounds) {
            if (typeof actionEditorPopulateSounds === 'function') actionEditorPopulateSounds(TIMER_EXPIRE_PREFIXES, sounds);
        }).catch(function () {});
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
    fetch('/api/info').then(function (r) { return r.json(); }).then(function (info) {
        if (typeof updateOnlineUpdateSection === 'function') updateOnlineUpdateSection(info);
    }).catch(function () {});
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
    fetch('/api/info').then(function (r) { return r.json(); }).then(function (info) {
        set('vi-firmware', info.version);
        set('vi-chip', info.chip_model);
        set('vi-cores', info.chip_cores);
        set('vi-freq', info.cpu_freq ? info.cpu_freq + ' MHz' : null);
        set('vi-flash', info.flash_chip_size ? (info.flash_chip_size / 1048576).toFixed(0) + ' MB' : null);
        set('vi-psram', info.psram_size ? (info.psram_size / 1048576).toFixed(1) + ' MB' : null);
        set('vi-sdk', info.idf_version);
        set('vi-device-name', info.hostname);
    }).catch(function () {});
    fetch('/api/health').then(function (r) { return r.json(); }).then(function (h) {
        set('vi-ip', h.ip_address);
    }).catch(function () {});
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
