// portal_config.js - Configuration form, loading, saving, and device controls
// Part of the ESP32 Macropad configuration portal.

/**
 * Load and display version information
 */
async function loadVersion() {
    try {
        const version = await getDeviceInfo();
        if (!version) return;

        // portalMode is derived from /api/info ap_active flag (previously a
        // separate /api/mode endpoint — removed to halve boot HTTP requests).
        portalMode = version.ap_active ? 'core' : 'full';

        // Health widget tuning + optional device-side history support
        healthConfigureFromDeviceInfo(deviceInfoCache);
        healthConfigureHistoryFromDeviceInfo(deviceInfoCache);

        // Hide/disable MQTT settings if firmware was built without MQTT support
        const mqttSection = document.getElementById('mqtt-settings-section');
        if (mqttSection && version.has_mqtt === false) {
            mqttSection.style.display = 'none';
            mqttSection.querySelectorAll('input, select, textarea').forEach(el => {
                el.disabled = true;
            });
        }

        document.getElementById('firmware-version').textContent = `Firmware v${version.version}`;
        const deviceName = version.device_name || version.project_display_name || version.wifi_hostname || 'Device';
        const deviceClass = version.device_class || version.project_display_name || 'Device';
        document.getElementById('portal-device-name').textContent = deviceName;
        document.getElementById('device-class').textContent = deviceClass;
        document.title = `${deviceName} — Configuration Portal`;
        document.getElementById('chip-info').textContent = 
            `${version.chip_model} rev ${version.chip_revision}`;
        document.getElementById('cpu-cores').textContent = 
            `${version.chip_cores} ${version.chip_cores === 1 ? 'Core' : 'Cores'}`;
        document.getElementById('cpu-freq').textContent = `${version.cpu_freq} MHz`;
        document.getElementById('flash-size').textContent = 
            `${formatBytes(version.flash_chip_size)} Flash`;
        document.getElementById('psram-status').textContent = 
            version.psram_size > 0 ? `${formatBytes(version.psram_size)} PSRAM` : 'No PSRAM';

        // Update Firmware page online update UI if present
        updateOnlineUpdateSection(version);
    } catch (error) {
        document.getElementById('firmware-version').textContent = 'Firmware v?.?.?';
        document.getElementById('chip-info').textContent = 'Chip info unavailable';
        document.getElementById('cpu-cores').textContent = '? Cores';
        document.getElementById('cpu-freq').textContent = '? MHz';
        document.getElementById('flash-size').textContent = '? MB Flash';
        document.getElementById('psram-status').textContent = 'Unknown';

        // Still attempt to update Firmware page UI if present
        updateOnlineUpdateSection(null);
    }
}

function updateOnlineUpdateSection(info) {
    const section = document.getElementById('online-update-section');
    if (!section) return; // Only on firmware page

    const linkEl = document.getElementById('github-pages-link');
    const deviceEl = document.getElementById('github-pages-device');
    const hasInfo = !!info;
    const owner = hasInfo ? (info.github_owner || '') : '';
    const repo = hasInfo ? (info.github_repo || '') : '';
    const deviceBase = window.location.origin;

    if (deviceEl) deviceEl.textContent = deviceBase;

    if (!owner || !repo) {
        if (linkEl) {
            linkEl.href = '#';
            linkEl.setAttribute('aria-disabled', 'true');
            linkEl.classList.add('disabled');
        }
        return;
    }

    const pagesBase = `https://${owner}.github.io/${repo}/`;
    const params = new URLSearchParams();
    params.set('device', deviceBase);

    const pagesUrl = `${pagesBase}?${params.toString()}`;

    if (linkEl) {
        linkEl.href = pagesUrl;
        linkEl.removeAttribute('aria-disabled');
        linkEl.classList.remove('disabled');
    }

}

function updateWriteOnlySecretField(fieldId, statusId, isSet, emptyMessage, valueName) {
    const name = valueName || 'value';
    const field = document.getElementById(fieldId);
    if (field) {
        field.value = '';
        field.placeholder = isSet
            ? 'Enter a new ' + name + ' to replace it'
            : 'Enter ' + name;
    }
    const status = document.getElementById(statusId);
    const updateStatus = function (hasPendingValue) {
        if (!status) return;
        const isPending = hasPendingValue === true;
        status.classList.add('secret-status');
        status.classList.toggle('is-set', isSet && !isPending);
        status.classList.toggle('is-empty', !isSet && !isPending);
        status.classList.toggle('is-pending', isPending);
        const indicator = document.createElement('span');
        indicator.className = 'status-dot ' + (isPending ? 'warning' : (isSet ? 'connected' : 'unconfigured'));
        indicator.setAttribute('aria-hidden', 'true');
        const statusText = isPending
            ? (isSet
                ? 'New ' + name + ' entered. It will replace the saved ' + name + ' when you save.'
                : 'New ' + name + ' entered. It will be saved when you save.')
            : (isSet
                ? 'Saved on device. Leave this field empty to keep it.'
                : (emptyMessage || 'Not configured.'));
        status.replaceChildren(indicator, document.createTextNode(statusText));
    };
    updateStatus(false);
    if (field) {
        field.oninput = function () {
            updateStatus(field.value.length > 0);
        };
    }
}

/**
 * Load current configuration from device
 */
function keyboardTransportLabel(transport) {
    return transport === 'none' ? 'Off' : (transport || '').toUpperCase();
}

function updateKeyboardTransportSetting() {
    const config = window.deviceConfig || {};
    const selected = document.querySelector('input[type="radio"][name="keyboard_transport"]:checked');
    const save = document.getElementById('hid-save-btn');
    if (save) save.disabled = !selected || selected.disabled || selected.value === config.keyboard_transport;
    const pending = document.getElementById('keyboard-pending');
    if (pending) {
        const needsReboot = config.keyboard_transport && config.keyboard_active_transport &&
            config.keyboard_transport !== config.keyboard_active_transport;
        pending.style.display = needsReboot ? '' : 'none';
        pending.textContent = needsReboot ? keyboardTransportLabel(config.keyboard_transport) + ' pending reboot' : '';
    }
}

function updateScreenSaverMode(config) {
    const settings = document.getElementById('screensaver-sleep-settings');
    if (!settings) return;
    const keepPanelAwake = config.screen_saver_keeps_panel_awake === true;
    settings.dataset.keepPanelAwake = String(keepPanelAwake);
    const labels = {
        'screensaver-description': keepPanelAwake ? 'Choose what the device shows, then when its backlight turns off.' : 'Choose what the device shows, then whether it turns the display off.',
        'screensaver-sleep-heading': keepPanelAwake ? 'Backlight Off' : 'Display Sleep',
        'screensaver-enabled-label': keepPanelAwake ? 'Turn off backlight after inactivity' : 'Turn off display after inactivity',
        'screensaver-sleep-help': keepPanelAwake ? 'Fade the backlight to black while the display controller stays active.' : 'Fade to black, then put the display panel to sleep.',
        'screensaver-timeout-label': keepPanelAwake ? 'Turn off backlight after' : 'Turn off display after',
        'screensaver-timeline-sleep-title': keepPanelAwake ? 'Backlight off' : 'Display off'
    };
    for (const id of Object.keys(labels)) {
        const label = document.getElementById(id);
        if (label) label.textContent = labels[id];
    }
}

async function loadConfig() {
    try {
        
        const response = await fetch(API_CONFIG);
        if (!response.ok) {
            throw new Error('Failed to load configuration');
        }
        
        const config = await response.json();
        // Cache for validation logic (e.g., whether passwords are already set)
        window.deviceConfig = config;
        // Cache compile-time capability map for fragments that gate UI on it.
        window.__device_caps = config.caps || {};
        if (typeof window.padUpdateSparklineEditor === 'function' &&
            document.getElementById('pad-edit-sparkline-data-binding')) {
            window.padUpdateSparklineEditor();
        }
        const hasConfig = config.wifi_ssid && config.wifi_ssid !== '';
        
        // Helper to safely set element value
        const setValueIfExists = (id, value) => {
            const element = document.getElementById(id);
            if (element) element.value = (value === 0 ? '0' : (value || ''));
        };

        const setCheckedIfExists = (id, checked) => {
            const element = document.getElementById(id);
            if (element && element.type === 'checkbox') {
                element.checked = !!checked;
            }
        };

        const setRadioIfExists = (name, value) => {
            if (value === undefined || value === null) return;
            const el = document.querySelector(
                'input[type="radio"][name="' + name + '"][value="' + value + '"]'
            );
            if (el) el.checked = true;
        };
        
        const setTextIfExists = (id, text) => {
            const element = document.getElementById(id);
            if (element) element.textContent = text;
        };
        
        // WiFi settings
        setValueIfExists('wifi_ssid', config.wifi_ssid);
        updateWriteOnlySecretField('wifi_password', 'wifi_password_status',
            config.wifi_password_set === true,
            hasConfig ? 'No password saved. This may be an open network.' : 'Not configured.',
            'password');
        
        // Device settings
        setValueIfExists('device_name', config.device_name);
        setTextIfExists('device_name_sanitized', (config.device_name_sanitized || 'esp32-xxxx') + '.local');
        
        // Fixed IP settings
        setValueIfExists('fixed_ip', config.fixed_ip);
        setValueIfExists('subnet_mask', config.subnet_mask);
        setValueIfExists('gateway', config.gateway);
        setValueIfExists('dns1', config.dns1);
        setValueIfExists('dns2', config.dns2);

        // MQTT settings
        setValueIfExists('mqtt_host', config.mqtt_host);
        setValueIfExists('mqtt_port', config.mqtt_port);
        setValueIfExists('mqtt_username', config.mqtt_username);

        // Power settings
        setRadioIfExists('operating_mode', config.operating_mode);
        setValueIfExists('duty_cycle_wake_seconds', config.duty_cycle_wake_seconds);
        setValueIfExists('mqtt_publish_interval_seconds', config.mqtt_publish_interval_seconds);
        setValueIfExists('portal_idle_timeout_seconds', config.portal_idle_timeout_seconds);
        setValueIfExists('wifi_backoff_max_seconds', config.wifi_backoff_max_seconds);

        // BLE telemetry settings (only present when firmware has HAS_BLE)
        if (config.ble_burst_count !== undefined) {
            setValueIfExists('ble_burst_count', config.ble_burst_count);
        }
        if (config.ble_adv_interval_ms !== undefined) {
            setValueIfExists('ble_adv_interval_ms', config.ble_adv_interval_ms);
        }
        if (config.ble_tx_power_dbm !== undefined) {
            setValueIfExists('ble_tx_power_dbm', config.ble_tx_power_dbm);
        }

        // MQTT scope
        setValueIfExists('mqtt_publish_scope', config.mqtt_publish_scope);

        updateWriteOnlySecretField('mqtt_password', 'mqtt_password_status', config.mqtt_password_set === true,
            '', 'password');
        updateWriteOnlySecretField('ha_token', 'ha_token_status', config.ha_token_set === true,
            '', 'token');

        // Basic Auth settings
        setCheckedIfExists('basic_auth_enabled', config.basic_auth_enabled);

        if (config.keyboard_transport !== undefined) {
            const caps = config.caps || {};
            const selectable = !!(caps.ble_hid || caps.usb_hid);
            ['none', 'usb', 'ble'].forEach(function (transport) {
                const available = transport === 'none' ? selectable : !!caps[transport + '_hid'];
                const radio = document.getElementById('keyboard-transport-' + transport);
                if (radio) {
                    radio.checked = config.keyboard_transport === transport;
                    radio.disabled = !available;
                }
                const label = document.querySelector('label[for="keyboard-transport-' + transport + '"]');
                if (label) {
                    label.style.display = available ? '' : 'none';
                }
            });
            ['keyboard-transport-choice', 'keyboard-save-bar'].forEach(function (id) {
                const el = document.getElementById(id);
                if (el) el.style.display = selectable ? '' : 'none';
            });
            const active = config.keyboard_active_transport || config.keyboard_transport;
            setTextIfExists('keyboard-active-transport', keyboardTransportLabel(active));
            setTextIfExists('keyboard-status', config.keyboard_status || 'Disconnected');
            setTextIfExists('keyboard-active-heading', active === 'none' ? 'Keyboard disabled' : 'Active ' + keyboardTransportLabel(active) + ' Connection');
            setTextIfExists('keyboard-device-name', (config.device_name || 'Keyboard') + ' USB');
            const content = document.getElementById('ble-content');
            if (content) content.style.display = config.keyboard_active_transport === 'ble' ? 'block' : 'none';
            const usbContent = document.getElementById('usb-content');
            if (usbContent) usbContent.style.display = config.keyboard_active_transport === 'usb' ? 'block' : 'none';
            updateKeyboardTransportSetting();
        }

        // Audio settings
        if (config.audio_volume !== undefined) {
            const vol = config.audio_volume;
            setValueIfExists('audio_volume', vol);
            setTextIfExists('audio_volume_value', vol);
            setValueIfExists('tap_beep', config.tap_beep);
            setValueIfExists('lp_beep', config.lp_beep);
            const audioSection = document.getElementById('audio-section');
            if (audioSection) audioSection.style.display = 'block';
        }

        setValueIfExists('basic_auth_username', config.basic_auth_username);
        updateWriteOnlySecretField('basic_auth_password', 'basic_auth_password_status',
            config.basic_auth_password_set === true, '', 'password');

        // MCP server settings
        var mcpCard = document.getElementById('mcp-card');
        if (mcpCard) {
            // Hide the whole card when the firmware was built without HAS_MCP.
            mcpCard.style.display = (config.caps && config.caps.mcp === false) ? 'none' : '';
        }
        if (config.mcp_enabled !== undefined) {
            setCheckedIfExists('mcp_enabled', config.mcp_enabled);
            setCheckedIfExists('mcp_control_enabled', config.mcp_control_enabled);
            setCheckedIfExists('mcp_authoring_enabled', config.mcp_authoring_enabled);
            const mcpEndpoint = document.getElementById('mcp_endpoint_url');
            if (mcpEndpoint) mcpEndpoint.value = 'http://' + window.location.host + '/mcp';
            const mcpStatus = document.getElementById('mcp_token_status');
            if (mcpStatus) {
                mcpStatus.textContent = config.mcp_token_set === true
                    ? 'A token is set (hidden). Generate a new one to replace it.'
                    : 'No token generated yet. Generate one to use the MCP server.';
            }
        }
        
        // Display settings - backlight brightness
        setValueIfExists('display_rotation', config.display_rotation !== undefined ? config.display_rotation : 0);
        const brightness = config.backlight_brightness !== undefined ? config.backlight_brightness : 100;
        const brightnessMin = config.backlight_brightness_min !== undefined ? config.backlight_brightness_min : 5;
        const brightnessSlider = document.getElementById('backlight_brightness');
        if (brightnessSlider) brightnessSlider.min = brightnessMin;
        setValueIfExists('backlight_brightness', brightness);
        setTextIfExists('brightness-value', brightness);
        const backlightTimeoutSettings = document.getElementById('backlight-timeout-settings');
        if (backlightTimeoutSettings) backlightTimeoutSettings.hidden = config.screen_saver_backlight_only !== true;

        // Screen saver settings
        setCheckedIfExists('screen_saver_enabled', config.screen_saver_enabled);
        setValueIfExists('screen_saver_timeout_seconds', config.screen_saver_timeout_seconds);
        setValueIfExists('screen_saver_fade_out_ms', config.screen_saver_fade_out_ms);
        setValueIfExists('screen_saver_fade_in_ms', config.screen_saver_fade_in_ms);
        setCheckedIfExists('screen_saver_wake_on_touch', config.screen_saver_wake_on_touch);
        setValueIfExists('screen_saver_wake_binding', config.screen_saver_wake_binding);
        setCheckedIfExists('idle_screen_enabled', config.idle_screen_enabled);
        setValueIfExists('idle_screen_timeout_seconds', config.idle_screen_timeout_seconds);
        setValueIfExists('idle_screen_pad', config.idle_screen_pad);
        updateScreenSaverMode(config);
        if (typeof window.screensaverTimelineUpdate === 'function') window.screensaverTimelineUpdate();

        // E-paper settings (only present when firmware has HAS_EPAPER_PANEL)
        if (config.epaper_url !== undefined) {
            setValueIfExists('epaper_url', config.epaper_url);
        }
        if (config.epaper_rotation !== undefined) {
            setValueIfExists('epaper_rotation', config.epaper_rotation);
        }
        if (config.epaper_overlay_enabled !== undefined) {
            setCheckedIfExists('epaper_overlay_enabled', config.epaper_overlay_enabled);
        }
        if (config.epaper_overlay_position !== undefined) {
            setValueIfExists('epaper_overlay_position', config.epaper_overlay_position);
        }
        if (config.epaper_overlay_color !== undefined) {
            setValueIfExists('epaper_overlay_color', config.epaper_overlay_color);
        }
        if (config.epaper_overlay_items !== undefined) {
            var items = config.epaper_overlay_items | 0;
            setValueIfExists('epaper_overlay_items', items);
            var iconEl = document.getElementById('epaper_overlay_item_icon');
            var pctEl = document.getElementById('epaper_overlay_item_pct');
            var timeEl = document.getElementById('epaper_overlay_item_time');
            var cycleEl = document.getElementById('epaper_overlay_item_cycle');
            if (iconEl) iconEl.checked = !!(items & 0x1);
            if (pctEl) pctEl.checked = !!(items & 0x2);
            if (timeEl) timeEl.checked = !!(items & 0x4);
            if (cycleEl) cycleEl.checked = !!(items & 0x8);
        }
        if (config.epaper_frontlight_brightness !== undefined) {
            setValueIfExists('epaper_frontlight_brightness', config.epaper_frontlight_brightness);
        }
        if (config.epaper_frontlight_duration_s !== undefined) {
            setValueIfExists('epaper_frontlight_duration_s', config.epaper_frontlight_duration_s);
        }
        if (config.epaper_frontlight_supported) {
            var flCard = document.getElementById('epaper-frontlight-card');
            if (flCard) flCard.hidden = false;
        }

        // Device-class fields registered via window.registerConfigFields([...]).
        // Symmetric with saveFragmentConfig(): any name added to
        // window.__extra_config_fields is auto-populated here from the
        // matching key on the /api/config response. Element id must equal
        // the field name. Checkbox/radio handling matches save semantics.
        if (window.__extra_config_fields && window.__extra_config_fields.length) {
            window.__extra_config_fields.forEach(function (name) {
                if (config[name] === undefined) return;
                var el = document.getElementById(name);
                if (!el) return;
                if (el.type === 'checkbox') {
                    el.checked = !!config[name];
                } else if (el.type === 'radio') {
                    setRadioIfExists(name, config[name]);
                } else {
                    setValueIfExists(name, config[name]);
                }
            });
        }
    } catch (error) {
        showMessage('Error loading configuration: ' + error.message, 'error');
        console.error('Load error:', error);
    }
}

/**
 * Validate configuration fields
 * @param {Object} config - Configuration object to validate
 * @returns {Object} { valid: boolean, message: string }
 */
function validateConfig(config) {
    // Validate required fields only if they exist on this page
    if (config.wifi_ssid !== undefined && (!config.wifi_ssid || config.wifi_ssid.trim() === '')) {
        return { valid: false, message: 'WiFi SSID is required' };
    }
    
    if (config.device_name !== undefined && (!config.device_name || config.device_name.trim() === '')) {
        return { valid: false, message: 'Device name is required' };
    }
    
    // Validate fixed IP configuration only if on network page
    if (config.fixed_ip !== undefined && config.fixed_ip && config.fixed_ip.trim() !== '') {
        if (!config.subnet_mask || config.subnet_mask.trim() === '') {
            return { valid: false, message: 'Subnet mask is required when using fixed IP' };
        }
        if (!config.gateway || config.gateway.trim() === '') {
            return { valid: false, message: 'Gateway is required when using fixed IP' };
        }
    }

    // Validate Basic Auth only if fields exist on this page
    if (config.basic_auth_enabled === true) {
        const user = (config.basic_auth_username || '').trim();
        const pass = (config.basic_auth_password || '').trim();
        const passwordAlreadySet = !!(window.deviceConfig && window.deviceConfig.basic_auth_password_set === true);

        if (!user) {
            return { valid: false, message: 'Basic Auth username is required when enabled' };
        }
        // Only require a password if none is already set.
        if (!passwordAlreadySet && !pass) {
            return { valid: false, message: 'Basic Auth password is required the first time you enable it' };
        }
    }

    if (config.ble_tx_power_dbm !== undefined) {
        const value = config.ble_tx_power_dbm;
        const power = Number(value);
        if ((typeof value !== 'string' && typeof value !== 'number') ||
            String(value).trim() === '' || !Number.isInteger(power) ||
            power < -12 || power > 9 || power % 3 !== 0) {
            return { valid: false, message: 'BLE TX power must be -12 to +9 dBm in 3 dB steps' };
        }
    }

    if (config.epaper_frame_offline_refreshes_between_syncs !== undefined) {
        const offlineRefreshes = Number(config.epaper_frame_offline_refreshes_between_syncs);
        if (!Number.isInteger(offlineRefreshes) || offlineRefreshes < 0 || offlineRefreshes > 16) {
            return { valid: false, message: 'Offline refreshes between syncs must be an integer from 0 to 16' };
        }
        if (offlineRefreshes > 0 &&
            (config.epaper_frame_source_mode !== 'service' ||
             config.epaper_frame_sd_cache_enabled !== true)) {
            return { valid: false, message: 'Offline refreshes require Service mode and SD image caching' };
        }
    }
    
    return { valid: true };
}

/**
 * Reset configuration to defaults
 */
async function resetConfig() {
    if (!confirm('Factory reset will erase ALL settings, pads, button defaults, icons, sounds, timers, swipe/boot actions, indexed stores (e.g. sessions), and BLE pairings. The device will reboot into AP mode. Continue?')) {
        return;
    }
    
    // Show unified dialog (no auto-reconnect for AP mode)
    showRebootDialog({
        title: 'Factory Reset',
        message: 'Resetting configuration...',
        context: 'reset'
    });
    
    try {
        const response = await fetch(API_CONFIG, {
            method: 'DELETE'
        });
        
        if (!response.ok) {
            throw new Error('Failed to reset configuration');
        }
        
        const result = await response.json();
        if (result.success) {
            // Update message
            document.getElementById('reboot-message').textContent = 'Configuration reset. Device restarting in AP mode...';
        } else {
            // Hide overlay and show error
            document.getElementById('reboot-overlay').style.display = 'none';
            showMessage('Error: ' + (result.message || 'Unknown error'), 'error');
        }
    } catch (error) {
        // If reset request fails (e.g., device already rebooting), assume success
        if (error.message.includes('Failed to fetch') || error.message.includes('NetworkError')) {
            document.getElementById('reboot-message').textContent = 'Configuration reset. Device restarting in AP mode...';
        } else {
            // Hide overlay and show error
            document.getElementById('reboot-overlay').style.display = 'none';
            showMessage('Error resetting configuration: ' + error.message, 'error');
            console.error('Reset error:', error);
        }
    }
}



/**
 * Update brightness slider background gradient based on value
 * @param {number} brightness - Brightness value (0-100)
 */
// Brightness slider uses Bootstrap's default .form-range styling (matches volume slider).
// No custom background painting — that would paint the entire input element,
// not just the track, producing a thick rectangular bar instead of a thin track.

/**
 * Handle brightness slider changes - update device immediately
 * @param {Event} event - Input event from slider
 */
async function handleBrightnessChange(event) {
    const brightness = parseInt(event.target.value);
    
    // Update displayed value
    const valueDisplay = document.getElementById('brightness-value');
    if (valueDisplay) {
        valueDisplay.textContent = brightness;
    }
    
    // Send brightness update to device immediately (no persist)
    try {
        const response = await fetch('/api/component/display/brightness', {
            method: 'PUT',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({ brightness: brightness })
        });
        
        if (!response.ok) {
            console.error('Failed to update brightness:', response.statusText);
        }
    } catch (error) {
        console.error('Error updating brightness:', error);
    }
}

/**
 * Handle screen selection change - switch screens immediately
 * @param {Event} event - Change event from select dropdown
 */
async function handleScreenChange(event) {
    const screenId = event.target.value;
    
    if (!screenId) return false;
    
    try {
        const response = await fetch('/api/component/display/screen', {
            method: 'PUT',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({ screen: screenId })
        });
        
        if (!response.ok) {
            console.error('Failed to switch screen:', response.statusText);
            showMessage('Failed to switch screen', 'error');
            return false;
        }
        return true;
    } catch (error) {
        console.error('Error switching screen:', error);
        showMessage('Error switching screen: ' + error.message, 'error');
        return false;
    }
}
