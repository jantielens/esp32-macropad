// portal_config.js - Configuration form, loading, saving, and device controls
// Part of the ESP32 Macropad configuration portal.

/**
 * Load and display version information
 */
async function loadVersion() {
    try {
        const version = await getDeviceInfo(true);
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

/**
 * Load current configuration from device
 */
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
        const wifiPwdField = document.getElementById('wifi_password');
        if (wifiPwdField) {
            wifiPwdField.value = '';
            wifiPwdField.placeholder = hasConfig ? '(saved - leave blank to keep)' : '';
        }
        
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

        // MQTT scope
        setValueIfExists('mqtt_publish_scope', config.mqtt_publish_scope);

        const mqttPwdField = document.getElementById('mqtt_password');
        if (mqttPwdField) {
            mqttPwdField.value = '';
            mqttPwdField.placeholder = hasConfig ? '(saved - leave blank to keep)' : '';
        }

        // Basic Auth settings
        setCheckedIfExists('basic_auth_enabled', config.basic_auth_enabled);

        // BLE Keyboard settings
        if (config.ble_enabled !== undefined) {
            setCheckedIfExists('ble_enabled', config.ble_enabled);
            const bleSection = document.getElementById('ble-section');
            if (bleSection) bleSection.style.display = 'block';
            toggleBleContent();
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
        const authPwdField = document.getElementById('basic_auth_password');
        if (authPwdField) {
            authPwdField.value = '';
            const saved = config.basic_auth_password_set === true;
            authPwdField.placeholder = saved ? '(saved - leave blank to keep)' : '';
        }
        
        // Display settings - backlight brightness
        const brightness = config.backlight_brightness !== undefined ? config.backlight_brightness : 100;
        setValueIfExists('backlight_brightness', brightness);
        setTextIfExists('brightness-value', brightness);

        // Screen saver settings
        setCheckedIfExists('screen_saver_enabled', config.screen_saver_enabled);
        setValueIfExists('screen_saver_timeout_seconds', config.screen_saver_timeout_seconds);
        setValueIfExists('screen_saver_fade_out_ms', config.screen_saver_fade_out_ms);
        setValueIfExists('screen_saver_fade_in_ms', config.screen_saver_fade_in_ms);
        setCheckedIfExists('screen_saver_wake_on_touch', config.screen_saver_wake_on_touch);
        setValueIfExists('screen_saver_wake_binding', config.screen_saver_wake_binding);

        // E-paper settings (only present when firmware has HAS_EPAPER)
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
    
    if (!screenId) return;
    
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
            // Revert dropdown to previous value
            loadVersion(); // Refresh to get current screen
        }
        // Success - dropdown already shows new value
    } catch (error) {
        console.error('Error switching screen:', error);
        showMessage('Error switching screen: ' + error.message, 'error');
        // Revert dropdown to previous value
        loadVersion(); // Refresh to get current screen
    }
}

