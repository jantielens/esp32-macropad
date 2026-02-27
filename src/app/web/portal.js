/**
 * Configuration Portal JavaScript
 * Handles configuration form, OTA updates, and device reboots
 * Supports core mode (AP) and full mode (WiFi connected)
 * Multi-page support: home, network, firmware
 */

// API endpoints
const API_CONFIG = '/api/config';
const API_INFO = '/api/info';
const API_MODE = '/api/mode';
const API_UPDATE = '/api/update';
const API_REBOOT = '/api/reboot';
const API_VERSION = '/api/info'; // Used for connection polling

let selectedFile = null;
let portalMode = 'full'; // 'core' or 'full'
let currentPage = 'home'; // Current page: 'home', 'network', or 'firmware'

let deviceInfoCache = null;

/**
 * Scroll input into view when focused (prevents mobile keyboard from covering it)
 * @param {Event} event - Focus event
 */
function handleInputFocus(event) {
    // Small delay to let the keyboard animation start
    setTimeout(() => {
        const input = event.target;
        const rect = input.getBoundingClientRect();
        const viewportHeight = window.innerHeight;
        
        // Estimate keyboard height (typically 40-50% of viewport on mobile)
        const estimatedKeyboardHeight = viewportHeight * 0.45;
        const availableHeight = viewportHeight - estimatedKeyboardHeight;
        
        // Calculate if input would be covered by keyboard
        const inputBottom = rect.bottom;
        
        // Only scroll if the input would be covered by the keyboard
        if (inputBottom > availableHeight) {
            // Scroll just enough to show the input with some padding
            const padding = 20; // 20px padding above input
            const scrollAmount = inputBottom - availableHeight + padding;
            
            window.scrollTo({
                top: window.scrollY + scrollAmount,
                behavior: 'smooth'
            });
        }
    }, 300); // Wait for keyboard animation
}

/**
 * Detect current page and highlight active navigation tab
 */
function initNavigation() {
    const path = window.location.pathname;
    
    if (path === '/' || path === '/home.html') {
        currentPage = 'home';
    } else if (path === '/network.html') {
        currentPage = 'network';
    } else if (path === '/firmware.html') {
        currentPage = 'firmware';
    }
    
    // Highlight active tab
    document.querySelectorAll('.nav-tab').forEach(tab => {
        const page = tab.getAttribute('data-page');
        if (page === currentPage) {
            tab.classList.add('active');
        } else {
            tab.classList.remove('active');
        }
    });
}

/**
 * Display a message to the user
 * @param {string} message - Message text
 * @param {string} type - Message type: 'info', 'success', or 'error'
 */
function showMessage(message, type = 'info') {
    const statusDiv = document.getElementById('status-message');
    statusDiv.textContent = message;
    statusDiv.className = `message ${type}`;
    statusDiv.style.display = 'block';
    
    setTimeout(() => {
        statusDiv.style.display = 'none';
    }, 5000);
}

/**
 * Show unified reboot overlay and handle reconnection
 * @param {Object} options - Configuration options
 * @param {string} options.title - Dialog title (e.g., 'Device Rebooting')
 * @param {string} options.message - Main message to display
 * @param {string} options.context - Context: 'save', 'ota', 'reboot', 'reset'
 * @param {string} options.newDeviceName - Optional new device name if changed
 * @param {boolean} options.showProgress - Show progress bar (for OTA)
 */
function showRebootDialog(options) {
    const {
        title = 'Device Rebooting',
        message = 'Please wait while the device restarts...',
        context = 'reboot',
        newDeviceName = null,
        showProgress = false
    } = options;

    const overlay = document.getElementById('reboot-overlay');
    const titleElement = document.getElementById('reboot-title');
    const rebootMsg = document.getElementById('reboot-message');
    const rebootSubMsg = document.getElementById('reboot-submessage');
    const reconnectStatus = document.getElementById('reconnect-status');
    const progressContainer = document.getElementById('reboot-progress-container');
    const spinner = document.getElementById('reboot-spinner');

    // Robustness: if the overlay template isn't present for some reason, fail gracefully.
    if (!overlay || !titleElement || !rebootMsg || !rebootSubMsg || !reconnectStatus) {
        console.error('Reboot overlay elements missing; cannot show reboot dialog');
        try {
            alert(message);
        } catch (_) {
            // ignore
        }
        return;
    }

    // Set dialog content
    titleElement.textContent = title;
    rebootMsg.textContent = message;
    
    // Show/hide progress bar
    if (progressContainer) {
        progressContainer.style.display = showProgress ? 'block' : 'none';
    }
    
    // Show/hide spinner
    if (spinner) {
        spinner.style.display = showProgress ? 'none' : 'block';
    }
    
    // Handle AP mode reset (no auto-reconnect)
    if (context === 'reset') {
        rebootSubMsg.textContent = 'Device will restart in AP mode. You must manually reconnect to the WiFi access point.';
        reconnectStatus.style.display = 'none';
        overlay.style.display = 'flex';
        return; // Don't start polling for AP mode
    }
    
    // Handle OTA (no auto-reconnect yet - wait for upload to complete)
    if (context === 'ota') {
        rebootSubMsg.textContent = 'Uploading firmware...';
        reconnectStatus.style.display = 'none';
        overlay.style.display = 'flex';
        return; // Don't start polling yet - OTA handler will start it after upload
    }
    
    // For save/reboot cases, show best-effort reconnection message and start polling
    const targetAddress = newDeviceName ? `http://${sanitizeForMDNS(newDeviceName)}.local` : window.location.origin;

    // Special case: when saving from AP/core mode, the client usually must switch WiFi networks.
    // Automatic polling from this browser session is unlikely to succeed until the user reconnects.
    if (context === 'save' && (portalMode === 'core' || isInCaptivePortal())) {
        rebootSubMsg.innerHTML = `Device will restart and may switch networks.<br>` +
            `<small style="color: #888; margin-top: 8px; display: block;">` +
            `Reconnect your phone/PC to the configured WiFi, then open: ` +
            `<code style="color: #667eea; font-weight: 600;">${targetAddress}</code>` +
            `</small>`;
        reconnectStatus.style.display = 'none';
        overlay.style.display = 'flex';
        return;
    }

    rebootSubMsg.innerHTML = `Attempting automatic reconnection...<br><small style="color: #888; margin-top: 8px; display: block;">If this fails, manually navigate to: <code style="color: #667eea; font-weight: 600;">${targetAddress}</code></small>`;
    reconnectStatus.style.display = 'block';

    overlay.style.display = 'flex';

    // Start unified reconnection process
    startReconnection({
        context,
        newDeviceName,
        statusElement: reconnectStatus,
        messageElement: rebootMsg
    });
}

/**
 * Detect if running in a captive portal browser
 * @returns {boolean} True if in captive portal
 */
function isInCaptivePortal() {
    const ua = window.navigator.userAgent;
    
    // Android captive portal indicators
    if (ua.includes('Android')) {
        if (ua.includes('CaptiveNetworkSupport') || 
            ua.includes('wv') || // WebView indicator
            document.referrer.includes('captiveportal')) {
            return true;
        }
    }
    
    // iOS captive portal
    if (ua.includes('iPhone') || ua.includes('iPad')) {
        if (ua.includes('CaptiveNetworkSupport')) {
            return true;
        }
    }
    
    return false;
}

/**
 * Generate sanitized mDNS name from device name
 * @param {string} deviceName - Device name to sanitize
 * @returns {string} Sanitized mDNS hostname
 */
function sanitizeForMDNS(deviceName) {
    return deviceName.toLowerCase()
        .replace(/[^a-z0-9\s\-_]/g, '')
        .replace(/[\s_]+/g, '-')
        .replace(/-+/g, '-')
        .replace(/^-|-$/g, '');
}

/**
 * Show captive portal warning with device address and handle user response
 */
function showCaptivePortalWarning() {
    const modal = document.getElementById('captive-portal-warning');
    const deviceName = document.getElementById('device_name').value.trim();
    const mdnsName = sanitizeForMDNS(deviceName);
    const deviceUrl = `http://${mdnsName}.local`;
    
    // Show the device address
    document.getElementById('device-mdns-address').textContent = deviceUrl;
    modal.style.display = 'flex';
    
    // Continue button - proceed with save
    document.getElementById('continue-save-btn').onclick = () => {
        modal.style.display = 'none';
        // Re-trigger the save (flag already set, so it will proceed)
        document.getElementById('config-form').dispatchEvent(new Event('submit'));
    };
    
    // Cancel button
    document.getElementById('cancel-save-btn').onclick = () => {
        modal.style.display = 'none';
        window.captivePortalWarningShown = false; // Reset flag if cancelled
    };
}

/**
 * Unified reconnection logic for all reboot scenarios
 * @param {Object} options - Reconnection options
 * @param {string} options.context - Context: 'save', 'ota', 'reboot'
 * @param {string} options.newDeviceName - Optional new device name if changed
 * @param {HTMLElement} options.statusElement - Status message element
 * @param {HTMLElement} options.messageElement - Main message element
 */
async function startReconnection(options) {
    const { context, newDeviceName, statusElement, messageElement } = options;
    
    // Initial delay: device needs time to start rebooting
    await new Promise(resolve => setTimeout(resolve, 2000));
    
    let attempts = 0;
    const maxAttempts = 40; // 2s initial + (40 × 3s) = 122 seconds total
    const checkInterval = 3000; // Poll every 3 seconds
    
    // Determine target URL
    let targetUrl = null;
    if (newDeviceName) {
        const mdnsName = sanitizeForMDNS(newDeviceName);
        targetUrl = `http://${mdnsName}.local`;
    }
    
    const checkConnection = async () => {
        attempts++;
        
        // Try new address first (if device name changed), then current location as fallback
        const urlsToTry = targetUrl 
            ? [targetUrl + API_VERSION, window.location.origin + API_VERSION]
            : [window.location.origin + API_VERSION];
        
        // Update status with progress
        const elapsed = 2 + (attempts * 3);
        statusElement.textContent = `Checking connection (attempt ${attempts}/${maxAttempts}, ${elapsed}s elapsed)...`;
        
        for (const url of urlsToTry) {
            try {
                const response = await fetch(url, { 
                    cache: 'no-cache',
                    mode: 'cors',
                    signal: AbortSignal.timeout(3000)
                });
                
                if (response.ok) {
                    messageElement.textContent = 'Device is back online!';
                    statusElement.textContent = 'Redirecting...';
                    const redirectUrl = targetUrl || window.location.origin;
                    setTimeout(() => {
                        window.location.href = redirectUrl;
                    }, 1000);
                    return;
                }
            } catch (e) {
                // Connection failed, try next URL
                console.debug(`Connection attempt ${attempts} failed for ${url}:`, e.message);
            }
        }
        
        // All URLs failed, continue trying
        if (attempts < maxAttempts) {
            setTimeout(checkConnection, checkInterval);
        } else {
            // Timeout - provide manual fallback
            const fallbackUrl = targetUrl || window.location.origin;
            messageElement.textContent = 'Automatic reconnection failed';
            statusElement.innerHTML = 
                `<div style="color:#e74c3c; margin-bottom: 10px;">Could not reconnect after ${2 + (maxAttempts * 3)} seconds.</div>` +
                `<div style="margin-top: 10px;">Please manually navigate to:<br>` +
                `<a href="${fallbackUrl}" style="color:#667eea; font-weight: 600; font-size: 16px;">${fallbackUrl}</a></div>` +
                `<div style="margin-top: 15px; font-size: 13px; color: #888;">` +
                `Possible issues: WiFi connection failed, incorrect credentials, or device taking longer to boot.</div>`;
        }
    };
    
    checkConnection();
}

/**
 * Update sanitized device name field
 */
function updateSanitizedName() {
    const deviceNameField = document.getElementById('device_name');
    const sanitizedField = document.getElementById('device_name_sanitized');
    
    // Only proceed if both elements exist
    if (!deviceNameField || !sanitizedField) return;
    
    const deviceName = deviceNameField.value;
    
    // Sanitize: lowercase, alphanumeric + hyphens
    let sanitized = deviceName.toLowerCase()
        .replace(/[^a-z0-9\s\-_]/g, '')
        .replace(/[\s_]+/g, '-')
        .replace(/-+/g, '-')
        .replace(/^-|-$/g, '');
    
    sanitizedField.textContent = (sanitized || 'esp32-xxxx') + '.local';
}

/**
 * Load portal mode (core vs full)
 */
async function loadMode() {
    try {
        const response = await fetch(API_MODE);
        if (!response.ok) return;
        
        const mode = await response.json();
        portalMode = mode.mode || 'full';
        
        // Show/hide additional settings based on mode (only if element exists)
        const additionalSettings = document.getElementById('additional-settings');
        if (additionalSettings) {
            if (portalMode === 'core') {
                additionalSettings.style.display = 'none';
            } else {
                additionalSettings.style.display = 'block';
            }
        }
        
        // Hide Home and Firmware navigation buttons in AP mode (core mode)
        if (portalMode === 'core') {
            document.querySelectorAll('.nav-tab[data-page="home"], .nav-tab[data-page="firmware"]').forEach(tab => {
                tab.style.display = 'none';
            });
            
            // Show setup notice on network page
            const setupNotice = document.getElementById('setup-notice');
            if (setupNotice) {
                setupNotice.style.display = 'block';
            }
            
            // Hide unnecessary buttons on network page (only "Save and Reboot" makes sense)
            const saveOnlyBtn = document.getElementById('save-only-btn');
            const rebootBtn = document.getElementById('reboot-btn');
            if (saveOnlyBtn) saveOnlyBtn.style.display = 'none';
            if (rebootBtn) rebootBtn.style.display = 'none';
            
            // Change primary button text to be more intuitive
            const submitBtn = document.querySelector('#config-form button[type="submit"]');
            if (submitBtn) {
                submitBtn.textContent = 'Save & Connect';
            }

            // Hide security settings in AP/core mode
            // (auth is intentionally disabled during onboarding/recovery)
            const securitySection = document.getElementById('security-section');
            if (securitySection) {
                securitySection.style.display = 'none';
                securitySection.querySelectorAll('input, select, textarea').forEach(el => {
                    el.disabled = true;
                });
            }
        }
    } catch (error) {
        console.error('Error loading mode:', error);
    }
}

/**
 * Load and display version information
 */
async function loadVersion() {
    try {
        const response = await fetch(API_INFO);
        if (!response.ok) return;
        
        const version = await response.json();
        deviceInfoCache = version;

        // Health widget tuning + optional device-side history support
        healthConfigureFromDeviceInfo(deviceInfoCache);
        healthConfigureHistoryFromDeviceInfo(deviceInfoCache);

        // Strategy B: Hide/disable MQTT settings if firmware was built without MQTT support
        const mqttSection = document.getElementById('mqtt-settings-section');
        if (mqttSection && version.has_mqtt === false) {
            mqttSection.style.display = 'none';
            mqttSection.querySelectorAll('input, select, textarea').forEach(el => {
                el.disabled = true;
            });
        }

        // Hide/disable display settings if firmware was built without backlight support
        const displaySection = document.getElementById('display-settings-section');
        if (displaySection) {
            if (version.has_backlight === true || version.has_display === true) {
                displaySection.style.display = 'block';
            } else {
                displaySection.style.display = 'none';
                displaySection.querySelectorAll('input').forEach(el => {
                    el.disabled = true;
                });
            }
        }

        // Hide/disable BLE settings if firmware was built without BLE support
        const bleSection = document.getElementById('ble-settings-section');
        if (bleSection) {
            if (version.has_ble === true) {
                bleSection.style.display = 'block';
            } else {
                bleSection.style.display = 'none';
                bleSection.querySelectorAll('input, select, textarea').forEach(el => {
                    el.disabled = true;
                });
            }
        }
        
        // Populate screen selection dropdown if device has display
        const screenSelect = document.getElementById('screen_selection');
        const screenGroup = document.getElementById('screen-selection-group');
        if (screenSelect && screenGroup && version.has_display === true && version.available_screens) {
            // Clear existing options
            screenSelect.innerHTML = '';
            
            // Add option for each available screen
            version.available_screens.forEach(screen => {
                const option = document.createElement('option');
                option.value = screen.id;
                option.textContent = screen.name;
                if (screen.id === version.current_screen) {
                    option.selected = true;
                }
                screenSelect.appendChild(option);
            });
            
            // Show screen selection group
            screenGroup.style.display = 'block';
        } else if (screenGroup) {
            screenGroup.style.display = 'none';
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
        
        // Dummy setting
        setValueIfExists('dummy_setting', config.dummy_setting);

        // MQTT settings
        setValueIfExists('mqtt_host', config.mqtt_host);
        setValueIfExists('mqtt_port', config.mqtt_port);
        setValueIfExists('mqtt_username', config.mqtt_username);

        // Power / transport settings
        setValueIfExists('power_mode', config.power_mode);
        setValueIfExists('publish_transport', config.publish_transport);
        setValueIfExists('cycle_interval_seconds', config.cycle_interval_seconds);
        setValueIfExists('portal_idle_timeout_seconds', config.portal_idle_timeout_seconds);
        setValueIfExists('wifi_backoff_max_seconds', config.wifi_backoff_max_seconds);

        // BLE timing
        setValueIfExists('ble_adv_burst_ms', config.ble_adv_burst_ms);
        setValueIfExists('ble_adv_gap_ms', config.ble_adv_gap_ms);
        setValueIfExists('ble_adv_bursts', config.ble_adv_bursts);
        setValueIfExists('ble_adv_interval_ms', config.ble_adv_interval_ms);

        // MQTT scope
        setValueIfExists('mqtt_publish_scope', config.mqtt_publish_scope);

        const mqttPwdField = document.getElementById('mqtt_password');
        if (mqttPwdField) {
            mqttPwdField.value = '';
            mqttPwdField.placeholder = hasConfig ? '(saved - leave blank to keep)' : '';
        }

        // Basic Auth settings
        setCheckedIfExists('basic_auth_enabled', config.basic_auth_enabled);
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
        updateBrightnessSliderBackground(brightness);

        // Screen saver settings
        setCheckedIfExists('screen_saver_enabled', config.screen_saver_enabled);
        setValueIfExists('screen_saver_timeout_seconds', config.screen_saver_timeout_seconds);
        setValueIfExists('screen_saver_fade_out_ms', config.screen_saver_fade_out_ms);
        setValueIfExists('screen_saver_fade_in_ms', config.screen_saver_fade_in_ms);
        setCheckedIfExists('screen_saver_wake_on_touch', config.screen_saver_wake_on_touch);
        
        // Hide loading overlay (silent load)
        const overlay = document.getElementById('form-loading-overlay');
        if (overlay) overlay.style.display = 'none';
    } catch (error) {
        // Hide loading overlay even on error so form is usable
        const overlay = document.getElementById('form-loading-overlay');
        if (overlay) overlay.style.display = 'none';
        showMessage('Error loading configuration: ' + error.message, 'error');
        console.error('Load error:', error);
    }
}

/**
 * Extract form fields that exist on the current page
 * @param {FormData} formData - Form data to extract from
 * @returns {Object} Configuration object with only fields present on page
 */
function extractFormFields(formData) {
    // Helper to get value only if field exists
    const getFieldValue = (name) => {
        const element = document.querySelector(`[name="${name}"]`);
        if (!element || element.disabled) return null;
        return element ? formData.get(name) : null;
    };

    const getCheckboxValue = (name) => {
        const element = document.querySelector(`[name="${name}"]`);
        if (!element || element.disabled) return null;
        if (element.type !== 'checkbox') return formData.get(name);
        // Explicit boolean so unchecked can be persisted as false.
        return element.checked;
    };
    
    // Build config from only the fields that exist on this page
    const config = {};
    const fields = ['wifi_ssid', 'wifi_password', 'device_name', 'fixed_ip', 
                    'subnet_mask', 'gateway', 'dns1', 'dns2', 'dummy_setting',
                    'mqtt_host', 'mqtt_port', 'mqtt_username', 'mqtt_password',
                    'power_mode', 'publish_transport', 'cycle_interval_seconds', 'portal_idle_timeout_seconds', 'wifi_backoff_max_seconds',
                    'ble_adv_burst_ms', 'ble_adv_gap_ms', 'ble_adv_bursts', 'ble_adv_interval_ms',
                    'mqtt_publish_scope',
                    'basic_auth_enabled', 'basic_auth_username', 'basic_auth_password',
                    'backlight_brightness',
                    'screen_saver_enabled', 'screen_saver_timeout_seconds', 'screen_saver_fade_out_ms', 'screen_saver_fade_in_ms', 'screen_saver_wake_on_touch'];
    
    fields.forEach(field => {
        const element = document.querySelector(`[name="${field}"]`);
        const value = (element && element.type === 'checkbox') ? getCheckboxValue(field) : getFieldValue(field);
        if (value !== null) config[field] = value;
    });
    
    return config;
}

/**
 * Validate configuration fields
 * @param {Object} config - Configuration object to validate
 * @returns {Object} { valid: boolean, message: string }
 */
function validateConfig(config) {
    // Validate required fields only if they exist on this page
    if (config.wifi_ssid !== undefined && (!config.wifi_ssid || config.wifi_ssid.trim() === '')) {
        const mode = (config.power_mode || (window.deviceConfig && window.deviceConfig.power_mode) || '').toLowerCase();
        const transport = (config.publish_transport || (window.deviceConfig && window.deviceConfig.publish_transport) || '').toLowerCase();
        const bleOnly = (transport === 'ble') && (mode === 'duty_cycle' || mode === 'always_on');
        if (!bleOnly) {
            return { valid: false, message: 'WiFi SSID is required' };
        }
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
 * Save configuration to device
 * @param {Event} event - Form submit event
 */
async function saveConfig(event) {
    event.preventDefault();
    
    // Check if in captive portal and show warning (only once)
    if (isInCaptivePortal() && !window.captivePortalWarningShown) {
        window.captivePortalWarningShown = true;
        showCaptivePortalWarning();
        return;
    }
    
    const formData = new FormData(event.target);
    const config = extractFormFields(formData);
    
    // Validate configuration
    const validation = validateConfig(config);
    if (!validation.valid) {
        showMessage(validation.message, 'error');
        return;
    }
    
    const currentDeviceNameField = document.getElementById('device_name');
    const currentDeviceName = currentDeviceNameField ? currentDeviceNameField.value : null;
    
    // Show overlay immediately
    showRebootDialog({
        title: 'Saving Configuration',
        message: 'Saving configuration...',
        context: 'save',
        newDeviceName: currentDeviceName
    });
    
    try {
        const response = await fetch(API_CONFIG, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify(config)
        });
        
        if (!response.ok) {
            throw new Error('Failed to save configuration');
        }
        
        const result = await response.json();
        if (result.success) {
            // Update dialog message
            document.getElementById('reboot-message').textContent = 'Configuration saved. Device is rebooting...';
        }
    } catch (error) {
        // If save request fails (e.g., device already rebooting), assume success
        if (error.message.includes('Failed to fetch') || error.message.includes('NetworkError')) {
            document.getElementById('reboot-message').textContent = 'Configuration saved. Device is rebooting...';
        } else {
            // Hide overlay and show error
            document.getElementById('reboot-overlay').style.display = 'none';
            showMessage('Error saving configuration: ' + error.message, 'error');
            console.error('Save error:', error);
        }
    }
}

/**
 * Save configuration without rebooting
 */
async function saveOnly(event) {
    event.preventDefault();
    
    const formData = new FormData(document.getElementById('config-form'));
    const config = extractFormFields(formData);
    
    // Validate configuration
    const validation = validateConfig(config);
    if (!validation.valid) {
        showMessage(validation.message, 'error');
        return;
    }
    
    try {
        showMessage('Saving configuration...', 'info');
        
        // Add no_reboot parameter to prevent automatic reboot
        const response = await fetch(API_CONFIG + '?no_reboot=1', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify(config)
        });
        
        if (!response.ok) {
            throw new Error('Failed to save configuration');
        }
        
        const result = await response.json();
        if (result.success) {
            showMessage('Configuration saved successfully!', 'success');
        } else {
            showMessage('Failed to save configuration', 'error');
        }
    } catch (error) {
        showMessage('Error saving configuration: ' + error.message, 'error');
        console.error('Save error:', error);
    }
}

/**
 * Reboot device without saving
 */
async function rebootDevice() {
    if (!confirm('Reboot the device without saving any changes?')) {
        return;
    }

    // Show unified dialog immediately (do not wait on network)
    showRebootDialog({
        title: 'Device Rebooting',
        message: 'Device is rebooting...',
        context: 'reboot'
    });
    
    try {
        const response = await fetch(API_REBOOT, {
            method: 'POST',
            signal: AbortSignal.timeout(1500)
        });

        // If the device responds with an explicit error, surface it.
        if (!response.ok) {
            throw new Error('Failed to reboot device');
        }
    } catch (error) {
        // Network failure/timeout is expected when the device reboots quickly.
        // Only surface errors that clearly indicate the reboot request was rejected.
        if (error.message && error.message.includes('Failed to reboot device')) {
            const overlay = document.getElementById('reboot-overlay');
            if (overlay) overlay.style.display = 'none';
            showMessage('Error rebooting device: ' + error.message, 'error');
            console.error('Reboot error:', error);
        }
    }
}

/**
 * Reset configuration to defaults
 */
async function resetConfig() {
    if (!confirm('Factory reset will erase all settings and reboot the device into AP mode. Continue?')) {
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
 * Handle firmware file selection
 * @param {Event} event - File input change event
 */
function handleFileSelect(event) {
    selectedFile = event.target.files[0];
    const uploadBtn = document.getElementById('upload-btn');
    
    if (selectedFile && selectedFile.name.endsWith('.bin')) {
        uploadBtn.disabled = false;
        showMessage(`Selected: ${selectedFile.name} (${(selectedFile.size / 1024).toFixed(1)} KB)`, 'info');
    } else {
        uploadBtn.disabled = true;
        if (selectedFile) {
            showMessage('Please select a .bin file', 'error');
            selectedFile = null;
        }
    }
}

/**
 * Upload firmware file to device
 */
async function uploadFirmware() {
    if (!selectedFile) {
        showMessage('Please select a firmware file', 'error');
        return;
    }
    
    const uploadBtn = document.getElementById('upload-btn');
    const fileInput = document.getElementById('firmware-file');
    
    uploadBtn.disabled = true;
    fileInput.disabled = true;
    
    // Show unified reboot dialog with progress bar
    showRebootDialog({
        title: 'Firmware Update',
        message: 'Uploading firmware...',
        context: 'ota',
        showProgress: true
    });
    
    const overlay = document.getElementById('reboot-overlay');
    const message = document.getElementById('reboot-message');
    const progressFill = document.getElementById('reboot-progress-fill');
    const progressText = document.getElementById('reboot-progress-text');
    const progressContainer = document.getElementById('reboot-progress-container');
    const reconnectStatus = document.getElementById('reconnect-status');
    
    const formData = new FormData();
    formData.append('firmware', selectedFile);
    
    const xhr = new XMLHttpRequest();
    
    let uploadComplete = false;
    
    xhr.upload.addEventListener('progress', (e) => {
        if (e.lengthComputable) {
            const percent = Math.round((e.loaded / e.total) * 100);
            progressFill.style.width = percent + '%';
            progressText.textContent = percent + '%';
            
            // When upload reaches 95%+, show installing message
            if (percent >= 95 && !uploadComplete) {
                uploadComplete = true;
                message.textContent = 'Installing firmware & rebooting...';
                
                // After a short delay, transition to reconnection
                setTimeout(() => {
                    progressContainer.style.display = 'none';
                    document.getElementById('reboot-spinner').style.display = 'block';
                    reconnectStatus.style.display = 'block';
                    
                    // Start unified reconnection
                    const currentDeviceName = document.getElementById('device_name').value;
                    const targetAddress = window.location.origin;
                    document.getElementById('reboot-submessage').innerHTML = 
                        `Attempting automatic reconnection...<br><small style="color: #888; margin-top: 8px; display: block;">If this fails, manually navigate to: <code style="color: #667eea; font-weight: 600;">${targetAddress}</code></small>`;
                    
                    startReconnection({
                        context: 'ota',
                        newDeviceName: null,
                        statusElement: reconnectStatus,
                        messageElement: message
                    });
                }, 2000);
            }
        }
    });
    
    xhr.addEventListener('load', () => {
        console.log('[OTA] XHR load event, status:', xhr.status);
        // Upload complete handler - may not always fire if device reboots quickly
        if (!uploadComplete && xhr.status === 200) {
            uploadComplete = true;
            progressFill.style.width = '100%';
            progressText.textContent = '100%';
            message.textContent = 'Installing firmware & rebooting...';
            
            setTimeout(() => {
                progressContainer.style.display = 'none';
                document.getElementById('reboot-spinner').style.display = 'block';
                reconnectStatus.style.display = 'block';
                
                // Start unified reconnection
                const currentDeviceName = document.getElementById('device_name').value;
                const targetAddress = window.location.origin;
                document.getElementById('reboot-submessage').innerHTML = 
                    `Attempting automatic reconnection...<br><small style="color: #888; margin-top: 8px; display: block;">If this fails, manually navigate to: <code style="color: #667eea; font-weight: 600;">${targetAddress}</code></small>`;
                
                startReconnection({
                    context: 'ota',
                    newDeviceName: null,
                    statusElement: reconnectStatus,
                    messageElement: message
                });
            }, 2000);
        }
    });
    
    xhr.addEventListener('error', () => {
        console.log('[OTA] XHR error event, uploadComplete:', uploadComplete);
        // Network error - if upload was near complete, assume device is rebooting
        if (uploadComplete) {
            console.log('[OTA] Upload was complete, treating error as device rebooting');
            // Already handled in progress event
        } else {
            console.log('[OTA] Upload failed with network error');
            message.textContent = 'Upload failed: Network error';
            progressContainer.style.display = 'none';
            uploadBtn.disabled = false;
            fileInput.disabled = false;
            
            // Close dialog after 3 seconds
            setTimeout(() => {
                overlay.style.display = 'none';
            }, 3000);
        }
    });
    
    xhr.open('POST', API_UPDATE);
    xhr.send(formData);
}



/**
 * Update brightness slider background gradient based on value
 * @param {number} brightness - Brightness value (0-100)
 */
function updateBrightnessSliderBackground(brightness) {
    const slider = document.getElementById('backlight_brightness');
    if (slider) {
        const percentage = brightness;
        slider.style.background = `linear-gradient(to right, #007aff 0%, #007aff ${percentage}%, #e5e5e5 ${percentage}%, #e5e5e5 100%)`;
    }
}

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
    
    // Update slider background
    updateBrightnessSliderBackground(brightness);
    
    // Send brightness update to device immediately (no persist)
    try {
        const response = await fetch('/api/display/brightness', {
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
        const response = await fetch('/api/display/screen', {
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

/**
 * Initialize page on DOM ready
 */
document.addEventListener('DOMContentLoaded', () => {
    // Initialize navigation highlighting
    initNavigation();
    
    // Attach event handlers (check if elements exist for multi-page support)
    const configForm = document.getElementById('config-form');
    if (configForm) {
        configForm.addEventListener('submit', saveConfig);
    }
    
    const saveOnlyBtn = document.getElementById('save-only-btn');
    if (saveOnlyBtn) {
        saveOnlyBtn.addEventListener('click', saveOnly);
    }
    
    const rebootBtn = document.getElementById('reboot-btn');
    if (rebootBtn) {
        rebootBtn.addEventListener('click', rebootDevice);
    }
    
    const resetBtn = document.getElementById('reset-btn');
    if (resetBtn) {
        resetBtn.addEventListener('click', resetConfig);
    }
    
    const firmwareFile = document.getElementById('firmware-file');
    if (firmwareFile) {
        firmwareFile.addEventListener('change', handleFileSelect);
    }
    
    const uploadBtn = document.getElementById('upload-btn');
    if (uploadBtn) {
        uploadBtn.addEventListener('click', uploadFirmware);
    }

    // Firmware page: GitHub Pages link is populated in updateOnlineUpdateSection()
    
    const deviceName = document.getElementById('device_name');
    if (deviceName) {
        deviceName.addEventListener('input', updateSanitizedName);
    }
    
    // Add focus handlers for all inputs to prevent keyboard from covering them
    const inputs = document.querySelectorAll('input[type="text"], input[type="password"], textarea');
    inputs.forEach(input => {
        input.addEventListener('focus', handleInputFocus);
    });
    
    // Add brightness slider event handler
    const brightnessSlider = document.getElementById('backlight_brightness');
    if (brightnessSlider) {
        brightnessSlider.addEventListener('input', handleBrightnessChange);
    }
    
    // Add screen selection dropdown event handler
    const screenSelect = document.getElementById('screen_selection');
    if (screenSelect) {
        screenSelect.addEventListener('change', handleScreenChange);
    }
    
    // Load initial data
    loadMode();
    
    // Only load config if config form exists (home and network pages)
    if (configForm) {
        loadConfig();
    } else {
        // Hide loading overlay on pages without config form (firmware page)
        const overlay = document.getElementById('form-loading-overlay');
        if (overlay) overlay.style.display = 'none';
    }
    
    loadVersion();
    
    // Initialize health widget
    initHealthWidget();
});

// ===== HEALTH WIDGET =====

const API_HEALTH = '/api/health';
const API_HEALTH_HISTORY = '/api/health/history';

let healthExpanded = false;
let healthPollTimer = null;

const HEALTH_POLL_INTERVAL_DEFAULT_MS = 5000;
const HEALTH_HISTORY_DEFAULT_SECONDS = 300;
let healthPollIntervalMs = HEALTH_POLL_INTERVAL_DEFAULT_MS;
let healthHistoryMaxSamples = 60;

let healthDeviceHistoryAvailable = false;
let healthDeviceHistoryPeriodMs = HEALTH_POLL_INTERVAL_DEFAULT_MS;
let healthLastHistoryFetchMs = 0;

const healthHistory = {
    cpu: [],
    cpuTs: [],
    heapInternalFree: [],
    heapInternalFreeTs: [],
    heapInternalFreeMin: [],
    heapInternalFreeMax: [],
    psramFree: [],
    psramFreeTs: [],
    psramFreeMin: [],
    psramFreeMax: [],
    heapInternalLargest: [],
    heapInternalLargestTs: [],
    heapInternalLargestMin: [],
    heapInternalLargestMax: [],
};

const healthSeriesStats = {
    cpu: { min: null, max: null },
    heapInternalFree: { min: null, max: null },
    psramFree: { min: null, max: null },
    heapInternalLargest: { min: null, max: null },
};

function healthComputeMinMaxMulti(arrays) {
    const list = Array.isArray(arrays) ? arrays : [];
    let min = Infinity;
    let max = -Infinity;
    let seen = false;

    for (let k = 0; k < list.length; k++) {
        const arr = list[k];
        if (!Array.isArray(arr) || arr.length < 1) continue;
        for (let i = 0; i < arr.length; i++) {
            const v = arr[i];
            if (typeof v !== 'number' || !isFinite(v)) continue;
            seen = true;
            if (v < min) min = v;
            if (v > max) max = v;
        }
    }

    if (!seen || !isFinite(min) || !isFinite(max)) return { min: null, max: null };
    return { min, max };
}

function healthUpdateSeriesStats({ hasPsram = null } = {}) {
    const resolvedHasPsram = (typeof hasPsram === 'boolean') ? hasPsram : (healthHistory.psramFree && healthHistory.psramFree.length > 0);
    {
        const mm = healthComputeMinMaxMulti([healthHistory.cpu]);
        healthSeriesStats.cpu.min = mm.min;
        healthSeriesStats.cpu.max = mm.max;
    }
    {
        const mm = healthComputeMinMaxMulti([
            healthHistory.heapInternalFree,
            healthHistory.heapInternalFreeMin,
            healthHistory.heapInternalFreeMax,
        ]);
        healthSeriesStats.heapInternalFree.min = mm.min;
        healthSeriesStats.heapInternalFree.max = mm.max;
    }
    if (resolvedHasPsram) {
        const mm = healthComputeMinMaxMulti([
            healthHistory.psramFree,
            healthHistory.psramFreeMin,
            healthHistory.psramFreeMax,
        ]);
        healthSeriesStats.psramFree.min = mm.min;
        healthSeriesStats.psramFree.max = mm.max;
    } else {
        healthSeriesStats.psramFree.min = null;
        healthSeriesStats.psramFree.max = null;
    }
    {
        const mm = healthComputeMinMaxMulti([
            healthHistory.heapInternalLargest,
            healthHistory.heapInternalLargestMin,
            healthHistory.heapInternalLargestMax,
        ]);
        healthSeriesStats.heapInternalLargest.min = mm.min;
        healthSeriesStats.heapInternalLargest.max = mm.max;
    }
}

function healthConfigureFromDeviceInfo(info) {
    const pollMs = (info && typeof info.health_poll_interval_ms === 'number') ? info.health_poll_interval_ms : HEALTH_POLL_INTERVAL_DEFAULT_MS;
    const windowSeconds = (info && typeof info.health_history_seconds === 'number') ? info.health_history_seconds : HEALTH_HISTORY_DEFAULT_SECONDS;

    healthPollIntervalMs = Math.max(1000, Math.min(60000, Math.trunc(pollMs)));
    const seconds = Math.max(30, Math.min(3600, Math.trunc(windowSeconds)));
    healthHistoryMaxSamples = Math.max(10, Math.min(600, Math.floor((seconds * 1000) / healthPollIntervalMs)));
}

function healthConfigureHistoryFromDeviceInfo(info) {
    healthDeviceHistoryAvailable = (info && info.health_history_available === true);
    const p = (info && typeof info.health_history_period_ms === 'number') ? info.health_history_period_ms : null;
    healthDeviceHistoryPeriodMs = (typeof p === 'number' && isFinite(p) && p > 0) ? Math.trunc(p) : healthPollIntervalMs;

    const pointsWrap = document.getElementById('health-points-wrap');
    const sparklinesWrap = document.getElementById('health-sparklines-wrap');
    if (pointsWrap) pointsWrap.style.display = healthDeviceHistoryAvailable ? 'none' : '';
    if (sparklinesWrap) sparklinesWrap.style.display = healthDeviceHistoryAvailable ? '' : 'none';
}

function healthMakeSyntheticTs(count, periodMs) {
    const n = (typeof count === 'number' && isFinite(count)) ? Math.max(0, Math.trunc(count)) : 0;
    const p = (typeof periodMs === 'number' && isFinite(periodMs)) ? Math.max(1, Math.trunc(periodMs)) : HEALTH_POLL_INTERVAL_DEFAULT_MS;
    const now = Date.now();
    const out = new Array(n);
    for (let i = 0; i < n; i++) {
        // Oldest sample first.
        const age = (n - 1 - i) * p;
        out[i] = now - age;
    }
    return out;
}

function healthReplaceArray(dst, src) {
    if (!Array.isArray(dst)) return;
    dst.length = 0;
    if (Array.isArray(src)) {
        for (let i = 0; i < src.length; i++) dst.push(src[i]);
    }
}

async function updateHealthHistory({ hasPsram = null } = {}) {
    if (!healthDeviceHistoryAvailable) return;
    if (!healthExpanded) return;

    const now = Date.now();
    const minInterval = Math.max(1500, healthDeviceHistoryPeriodMs);
    if (now - healthLastHistoryFetchMs < minInterval) return;
    healthLastHistoryFetchMs = now;

    try {
        const resp = await fetch(API_HEALTH_HISTORY);
        if (!resp.ok) return;
        const hist = await resp.json();
        if (!hist || hist.available !== true) return;

        const periodMs = (typeof hist.period_ms === 'number' && isFinite(hist.period_ms) && hist.period_ms > 0) ? Math.trunc(hist.period_ms) : healthDeviceHistoryPeriodMs;
        const ts = healthMakeSyntheticTs(Array.isArray(hist.cpu_usage) ? hist.cpu_usage.length : 0, periodMs);

        healthReplaceArray(healthHistory.cpu, hist.cpu_usage);
        healthReplaceArray(healthHistory.cpuTs, ts);

        healthReplaceArray(healthHistory.heapInternalFree, hist.heap_internal_free);
        healthReplaceArray(healthHistory.heapInternalFreeTs, ts);
        healthReplaceArray(healthHistory.heapInternalFreeMin, hist.heap_internal_free_min_window);
        healthReplaceArray(healthHistory.heapInternalFreeMax, hist.heap_internal_free_max_window);

        healthReplaceArray(healthHistory.psramFree, hist.psram_free);
        healthReplaceArray(healthHistory.psramFreeTs, ts);
        healthReplaceArray(healthHistory.psramFreeMin, hist.psram_free_min_window);
        healthReplaceArray(healthHistory.psramFreeMax, hist.psram_free_max_window);

        healthReplaceArray(healthHistory.heapInternalLargest, hist.heap_internal_largest);
        healthReplaceArray(healthHistory.heapInternalLargestTs, ts);
        healthReplaceArray(healthHistory.heapInternalLargestMin, hist.heap_internal_largest_min_window);
        healthReplaceArray(healthHistory.heapInternalLargestMax, hist.heap_internal_largest_max_window);

        healthUpdateSeriesStats({ hasPsram });
        healthDrawSparklinesOnly({ hasPsram });
    } catch (e) {
        console.error('Failed to fetch /api/health/history:', e);
    }
}

function formatUptime(seconds) {
    const days = Math.floor(seconds / 86400);
    const hours = Math.floor((seconds % 86400) / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);
    const secs = Math.floor(seconds % 60);

    if (days > 0) return `${days}d ${hours}h ${minutes}m`;
    if (hours > 0) return `${hours}h ${minutes}m ${secs}s`;
    if (minutes > 0) return `${minutes}m ${secs}s`;
    return `${secs}s`;
}

function formatBytes(bytes) {
    if (bytes === null || bytes === undefined) return '--';
    const b = Number(bytes);
    if (!Number.isFinite(b)) return '--';

    if (b >= 1024 * 1024) return `${(b / (1024 * 1024)).toFixed(2)} MB`;
    if (b >= 1024) return `${(b / 1024).toFixed(1)} KB`;
    return `${Math.round(b)} B`;
}

function getSignalStrength(rssi) {
    if (rssi >= -50) return 'Excellent';
    if (rssi >= -60) return 'Good';
    if (rssi >= -70) return 'Fair';
    if (rssi >= -80) return 'Weak';
    return 'Very Weak';
}

function healthPushSample(arr, value) {
    if (!Array.isArray(arr)) return;
    if (typeof value !== 'number' || !isFinite(value)) return;
    arr.push(value);
    while (arr.length > healthHistoryMaxSamples) arr.shift();
}

function healthPushSampleWithTs(valuesArr, tsArr, value, ts) {
    if (!Array.isArray(valuesArr) || !Array.isArray(tsArr)) return;
    if (typeof value !== 'number' || !isFinite(value)) return;
    if (typeof ts !== 'number' || !isFinite(ts)) return;
    valuesArr.push(value);
    tsArr.push(ts);
    while (valuesArr.length > healthHistoryMaxSamples) valuesArr.shift();
    while (tsArr.length > healthHistoryMaxSamples) tsArr.shift();
}

function healthFormatAgeMs(ageMs) {
    if (typeof ageMs !== 'number' || !isFinite(ageMs)) return '';
    const s = Math.max(0, Math.round(ageMs / 1000));
    if (s < 60) return `${s}s`;
    const m = Math.floor(s / 60);
    const r = s % 60;
    if (m < 60) return `${m}m ${r}s`;
    const h = Math.floor(m / 60);
    const rm = m % 60;
    return `${h}h ${rm}m`;
}

function healthFormatTimeOfDay(ts) {
    try {
        return new Date(ts).toLocaleTimeString([], { hour12: false });
    } catch (_) {
        return '';
    }
}

let healthSparklineTooltipEl = null;
function healthEnsureSparklineTooltip() {
    if (healthSparklineTooltipEl) return healthSparklineTooltipEl;
    const el = document.createElement('div');
    el.className = 'health-sparkline-tooltip';
    el.style.display = 'none';
    document.body.appendChild(el);
    healthSparklineTooltipEl = el;
    return el;
}

function healthTooltipSetVisible(visible) {
    const el = healthEnsureSparklineTooltip();
    el.style.display = visible ? 'block' : 'none';
}

function healthTooltipSetContent(html) {
    const el = healthEnsureSparklineTooltip();
    el.innerHTML = html;
}

function healthTooltipSetPosition(clientX, clientY) {
    const el = healthEnsureSparklineTooltip();

    const pad = 12;
    let x = (clientX || 0) + pad;
    let y = (clientY || 0) + pad;

    const vw = window.innerWidth || 0;
    const vh = window.innerHeight || 0;

    const maxW = (vw > 0) ? Math.max(140, vw - pad * 2) : 320;
    const desiredW = 280;
    el.style.width = `${Math.min(desiredW, maxW)}px`;
    el.style.maxWidth = `${maxW}px`;

    const prevDisplay = el.style.display;
    el.style.display = 'block';
    const rect = el.getBoundingClientRect();
    el.style.display = prevDisplay;

    if (vw > 0 && rect.width > 0 && x + rect.width + pad > vw) {
        x = Math.max(pad, vw - rect.width - pad);
    }
    if (vh > 0 && rect.height > 0 && y + rect.height + pad > vh) {
        y = Math.max(pad, vh - rect.height - pad);
    }

    el.style.left = `${x}px`;
    el.style.top = `${y}px`;
}

function healthSparklineIndexFromEvent(canvas, clientX) {
    if (!canvas) return null;
    const rect = canvas.getBoundingClientRect();
    const w = rect.width || 0;
    if (w <= 0) return null;
    const x = (clientX - rect.left);
    const t = Math.max(0, Math.min(1, x / w));
    return t;
}

const healthSparklineHoverIndex = {
    'health-sparkline-cpu': null,
    'health-sparkline-heap': null,
    'health-sparkline-psram': null,
    'health-sparkline-largest': null,
};

function healthSetSparklineHoverIndex(canvasId, index) {
    if (!canvasId) return;
    if (!(canvasId in healthSparklineHoverIndex)) return;
    if (typeof index !== 'number' || !isFinite(index)) {
        healthSparklineHoverIndex[canvasId] = null;
        return;
    }
    healthSparklineHoverIndex[canvasId] = Math.trunc(index);
}

function healthGetSparklineHoverIndex(canvasId) {
    if (!canvasId) return null;
    if (!(canvasId in healthSparklineHoverIndex)) return null;
    const v = healthSparklineHoverIndex[canvasId];
    return (typeof v === 'number' && isFinite(v)) ? Math.trunc(v) : null;
}

function healthFormatBytes(bytes) {
    if (typeof bytes !== 'number' || !isFinite(bytes)) return '—';
    return formatBytes(bytes);
}

function healthFormatBytesKB(bytes) {
    if (typeof bytes !== 'number' || !isFinite(bytes)) return '—';
    const kb = bytes / 1024;
    const decimals = (kb >= 1000) ? 0 : 1;
    return `${kb.toFixed(decimals)} KB`;
}

function sparklineDraw(canvas, values, {
    color = '#667eea',
    strokeWidth = 2,
    min = null,
    max = null,
    bandMin = null,
    bandMax = null,
    bandColor = 'rgba(102, 126, 234, 0.18)',
    highlightIndex = null,
    highlightRadius = 3.25,
    highlightFill = 'rgba(255,255,255,0.95)',
    highlightStroke = null,
    highlightStrokeWidth = 2,
} = {}) {
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const w = canvas.width;
    const h = canvas.height;
    ctx.clearRect(0, 0, w, h);

    const data = Array.isArray(values) ? values : [];
    if (data.length < 1) {
        ctx.strokeStyle = 'rgba(0,0,0,0.08)';
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(0, h - 1);
        ctx.lineTo(w, h - 1);
        ctx.stroke();
        return;
    }

    const bandMinArr = Array.isArray(bandMin) ? bandMin : null;
    const bandMaxArr = Array.isArray(bandMax) ? bandMax : null;

    let vmin = (typeof min === 'number') ? min : Infinity;
    let vmax = (typeof max === 'number') ? max : -Infinity;
    if (!(typeof min === 'number') || !(typeof max === 'number')) {
        for (let i = 0; i < data.length; i++) {
            const v = data[i];
            if (typeof v === 'number' && isFinite(v)) {
                if (v < vmin) vmin = v;
                if (v > vmax) vmax = v;
            }
            if (bandMinArr && i < bandMinArr.length) {
                const bmin = bandMinArr[i];
                if (typeof bmin === 'number' && isFinite(bmin)) {
                    if (bmin < vmin) vmin = bmin;
                    if (bmin > vmax) vmax = bmin;
                }
            }
            if (bandMaxArr && i < bandMaxArr.length) {
                const bmax = bandMaxArr[i];
                if (typeof bmax === 'number' && isFinite(bmax)) {
                    if (bmax < vmin) vmin = bmax;
                    if (bmax > vmax) vmax = bmax;
                }
            }
        }
    }
    if (!isFinite(vmin) || !isFinite(vmax)) {
        vmin = 0;
        vmax = 1;
    } else if (vmin === vmax) {
        const eps = Math.max(1, Math.abs(vmin) * 0.01);
        vmin = vmin - eps;
        vmax = vmax + eps;
    }

    const pad = 4;
    const xStep = (data.length >= 2) ? ((w - pad * 2) / (data.length - 1)) : 0;
    const yScale = (h - pad * 2) / (vmax - vmin);

    ctx.strokeStyle = 'rgba(0,0,0,0.06)';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(0, h - 1);
    ctx.lineTo(w, h - 1);
    ctx.stroke();

    if (bandMinArr && bandMaxArr && data.length >= 2) {
        const n = Math.min(data.length, bandMinArr.length, bandMaxArr.length);
        if (n >= 2) {
            ctx.fillStyle = bandColor;
            ctx.beginPath();
            for (let i = 0; i < n; i++) {
                const bmax = bandMaxArr[i];
                if (typeof bmax !== 'number' || !isFinite(bmax)) continue;
                const x = pad + i * xStep;
                const y = h - pad - ((bmax - vmin) * yScale);
                if (i === 0) ctx.moveTo(x, y);
                else ctx.lineTo(x, y);
            }
            for (let i = n - 1; i >= 0; i--) {
                const bmin = bandMinArr[i];
                if (typeof bmin !== 'number' || !isFinite(bmin)) continue;
                const x = pad + i * xStep;
                const y = h - pad - ((bmin - vmin) * yScale);
                ctx.lineTo(x, y);
            }
            ctx.closePath();
            ctx.fill();
        }
    }

    ctx.strokeStyle = color;
    ctx.lineWidth = strokeWidth;
    ctx.lineJoin = 'round';
    ctx.lineCap = 'round';

    if (data.length === 1) {
        const v = data[0];
        const x = pad;
        const y = h - pad - ((v - vmin) * yScale);
        ctx.beginPath();
        ctx.arc(x, y, 2.5, 0, Math.PI * 2);
        ctx.stroke();
        return;
    }

    ctx.beginPath();
    for (let i = 0; i < data.length; i++) {
        const v = data[i];
        const x = pad + i * xStep;
        const y = h - pad - ((v - vmin) * yScale);
        if (i === 0) ctx.moveTo(x, y);
        else ctx.lineTo(x, y);
    }
    ctx.stroke();

    if (typeof highlightIndex === 'number' && isFinite(highlightIndex)) {
        const idx = Math.max(0, Math.min(data.length - 1, Math.trunc(highlightIndex)));
        const v = data[idx];
        if (typeof v === 'number' && isFinite(v)) {
            const x = pad + idx * xStep;
            const y = h - pad - ((v - vmin) * yScale);
            const strokeCol = highlightStroke || color;
            const r = Math.max(2.0, highlightRadius);

            ctx.fillStyle = highlightFill;
            ctx.beginPath();
            ctx.arc(x, y, r, 0, Math.PI * 2);
            ctx.fill();

            ctx.strokeStyle = strokeCol;
            ctx.lineWidth = highlightStrokeWidth;
            ctx.beginPath();
            ctx.arc(x, y, r, 0, Math.PI * 2);
            ctx.stroke();
        }
    }
}

function healthDrawSparklinesOnly({ hasPsram = null } = {}) {
    const resolvedHasPsram = (typeof hasPsram === 'boolean') ? hasPsram : (healthHistory.psramFree && healthHistory.psramFree.length > 0);

    sparklineDraw(document.getElementById('health-sparkline-cpu'), healthHistory.cpu, {
        color: '#667eea',
        min: 0,
        max: 100,
        highlightIndex: healthGetSparklineHoverIndex('health-sparkline-cpu'),
    });

    sparklineDraw(document.getElementById('health-sparkline-heap'), healthHistory.heapInternalFree, {
        color: '#34c759',
        bandMin: healthHistory.heapInternalFreeMin,
        bandMax: healthHistory.heapInternalFreeMax,
        bandColor: 'rgba(52, 199, 89, 0.18)',
        highlightIndex: healthGetSparklineHoverIndex('health-sparkline-heap'),
    });

    sparklineDraw(document.getElementById('health-sparkline-psram'), healthHistory.psramFree, {
        color: '#0a84ff',
        bandMin: healthHistory.psramFreeMin,
        bandMax: healthHistory.psramFreeMax,
        bandColor: 'rgba(10, 132, 255, 0.18)',
        highlightIndex: resolvedHasPsram ? healthGetSparklineHoverIndex('health-sparkline-psram') : null,
    });

    sparklineDraw(document.getElementById('health-sparkline-largest'), healthHistory.heapInternalLargest, {
        color: '#ff2d55',
        bandMin: healthHistory.heapInternalLargestMin,
        bandMax: healthHistory.heapInternalLargestMax,
        bandColor: 'rgba(255, 45, 85, 0.16)',
        highlightIndex: healthGetSparklineHoverIndex('health-sparkline-largest'),
    });
}

function healthAttachSparklineTooltip(canvas, getPayloadForIndex) {
    if (!canvas || typeof getPayloadForIndex !== 'function') return;
    if (canvas.dataset && canvas.dataset.healthTooltipAttached === '1') return;
    if (canvas.dataset) canvas.dataset.healthTooltipAttached = '1';

    let hideTimer = null;
    const clearHideTimer = () => {
        if (hideTimer) {
            clearTimeout(hideTimer);
            hideTimer = null;
        }
    };

    const hide = () => {
        clearHideTimer();
        healthSetSparklineHoverIndex(canvas.id, null);
        healthDrawSparklinesOnly({
            hasPsram: (() => {
                const wrap = document.getElementById('health-sparkline-psram-wrap');
                return wrap ? (wrap.style.display !== 'none') : null;
            })(),
        });
        healthTooltipSetVisible(false);
    };

    const showAt = (clientX, clientY) => {
        clearHideTimer();
        const t = healthSparklineIndexFromEvent(canvas, clientX);
        if (t === null) return;

        const payload = getPayloadForIndex(t);
        if (!payload) return;

        if (typeof payload.index === 'number' && isFinite(payload.index)) {
            const prev = healthGetSparklineHoverIndex(canvas.id);
            const next = Math.trunc(payload.index);
            if (prev !== next) {
                healthSetSparklineHoverIndex(canvas.id, next);
                healthDrawSparklinesOnly({
                    hasPsram: (() => {
                        const wrap = document.getElementById('health-sparkline-psram-wrap');
                        return wrap ? (wrap.style.display !== 'none') : null;
                    })(),
                });
            }
        }

        healthTooltipSetContent(payload.html);
        healthTooltipSetPosition(clientX, clientY);
        healthTooltipSetVisible(true);
    };

    canvas.addEventListener('mousemove', (e) => {
        showAt(e.clientX, e.clientY);
    });
    canvas.addEventListener('mouseleave', hide);

    canvas.addEventListener('touchstart', (e) => {
        if (!e.touches || e.touches.length < 1) return;
        const t0 = e.touches[0];
        showAt(t0.clientX, t0.clientY);
    }, { passive: true });
    canvas.addEventListener('touchmove', (e) => {
        if (!e.touches || e.touches.length < 1) return;
        const t0 = e.touches[0];
        showAt(t0.clientX, t0.clientY);
    }, { passive: true });
    canvas.addEventListener('touchend', () => {
        clearHideTimer();
        hideTimer = setTimeout(hide, 1200);
    }, { passive: true });
}

function healthInitSparklineTooltips() {
    const formatMinMaxDeltaLine = (minVal, maxVal, fmt) => {
        if (typeof fmt !== 'function') {
            fmt = (v) => String(v);
        }
        if (typeof minVal !== 'number' || !isFinite(minVal) || typeof maxVal !== 'number' || !isFinite(maxVal)) {
            return 'min: —, max: —, <span class="health-sparkline-tooltip-delta">Δ —</span>';
        }
        const delta = Math.max(0, maxVal - minVal);
        return `min: ${fmt(minVal)}, max: ${fmt(maxVal)}, <span class="health-sparkline-tooltip-delta">Δ ${fmt(delta)}</span>`;
    };

    const tooltipHtml = ({ title, age, hero, windowLineHtml, sparklineLineHtml }) => {
        const win = windowLineHtml ? `<div class="health-sparkline-tooltip-line">${windowLineHtml}</div>` : '';
        return (
            `<div class="health-sparkline-tooltip-header">` +
                `<div class="health-sparkline-tooltip-title">${title || ''}</div>` +
                `<div class="health-sparkline-tooltip-age">${age || ''}</div>` +
            `</div>` +
            `<div class="health-sparkline-tooltip-hero">${hero || '—'}</div>` +
            win +
            `<div class="health-sparkline-tooltip-section">Sparkline window</div>` +
            `<div class="health-sparkline-tooltip-line">${sparklineLineHtml || 'min: —, max: —, <span class="health-sparkline-tooltip-delta">Δ —</span>'}</div>`
        );
    };

    const cpuCanvas = document.getElementById('health-sparkline-cpu');
    healthAttachSparklineTooltip(cpuCanvas, (t) => {
        const v = healthHistory.cpu;
        const ts = healthHistory.cpuTs;
        const n = v.length;
        if (n < 1) return null;
        const i = Math.max(0, Math.min(n - 1, Math.round(t * (n - 1))));
        const val = v[i];
        const tsv = ts[i];
        const age = healthFormatAgeMs(Date.now() - tsv);
        const smin = healthSeriesStats.cpu.min;
        const smax = healthSeriesStats.cpu.max;

        const sparklineLine = formatMinMaxDeltaLine(
            (typeof smin === 'number') ? smin : NaN,
            (typeof smax === 'number') ? smax : NaN,
            (x) => `${Math.trunc(x)}%`
        );

        return {
            index: i,
            html: tooltipHtml({
                title: 'CPU Usage',
                age,
                hero: (typeof val === 'number' && isFinite(val)) ? `${val}%` : '—',
                windowLineHtml: null,
                sparklineLineHtml: sparklineLine,
            }),
        };
    });

    const heapCanvas = document.getElementById('health-sparkline-heap');
    healthAttachSparklineTooltip(heapCanvas, (t) => {
        const v = healthHistory.heapInternalFree;
        const ts = healthHistory.heapInternalFreeTs;
        const bmin = healthHistory.heapInternalFreeMin;
        const bmax = healthHistory.heapInternalFreeMax;
        const n = v.length;
        if (n < 1) return null;
        const i = Math.max(0, Math.min(n - 1, Math.round(t * (n - 1))));
        const val = v[i];
        const tsv = ts[i];
        const wmin = (i < bmin.length) ? bmin[i] : val;
        const wmax = (i < bmax.length) ? bmax[i] : val;
        const age = healthFormatAgeMs(Date.now() - tsv);
        const smin = healthSeriesStats.heapInternalFree.min;
        const smax = healthSeriesStats.heapInternalFree.max;

        const windowLine = formatMinMaxDeltaLine(wmin, wmax, healthFormatBytes);
        const sparklineLine = formatMinMaxDeltaLine(smin, smax, healthFormatBytes);

        return {
            index: i,
            html: tooltipHtml({
                title: 'Internal Free Heap',
                age,
                hero: healthFormatBytes(val),
                windowLineHtml: windowLine,
                sparklineLineHtml: sparklineLine,
            }),
        };
    });

    const psramCanvas = document.getElementById('health-sparkline-psram');
    healthAttachSparklineTooltip(psramCanvas, (t) => {
        const v = healthHistory.psramFree;
        const ts = healthHistory.psramFreeTs;
        const bmin = healthHistory.psramFreeMin;
        const bmax = healthHistory.psramFreeMax;
        const n = v.length;
        if (n < 1) return null;
        const i = Math.max(0, Math.min(n - 1, Math.round(t * (n - 1))));
        const val = v[i];
        const tsv = ts[i];
        const wmin = (i < bmin.length) ? bmin[i] : val;
        const wmax = (i < bmax.length) ? bmax[i] : val;
        const age = healthFormatAgeMs(Date.now() - tsv);
        const smin = healthSeriesStats.psramFree.min;
        const smax = healthSeriesStats.psramFree.max;

        const windowLine = formatMinMaxDeltaLine(wmin, wmax, healthFormatBytesKB);
        const sparklineLine = formatMinMaxDeltaLine(smin, smax, healthFormatBytesKB);

        return {
            index: i,
            html: tooltipHtml({
                title: 'PSRAM Free',
                age,
                hero: healthFormatBytesKB(val),
                windowLineHtml: windowLine,
                sparklineLineHtml: sparklineLine,
            }),
        };
    });

    const largestCanvas = document.getElementById('health-sparkline-largest');
    healthAttachSparklineTooltip(largestCanvas, (t) => {
        const v = healthHistory.heapInternalLargest;
        const ts = healthHistory.heapInternalLargestTs;
        const bmin = healthHistory.heapInternalLargestMin;
        const bmax = healthHistory.heapInternalLargestMax;
        const n = v.length;
        if (n < 1) return null;
        const i = Math.max(0, Math.min(n - 1, Math.round(t * (n - 1))));
        const val = v[i];
        const tsv = ts[i];
        const wmin = (i < bmin.length) ? bmin[i] : val;
        const wmax = (i < bmax.length) ? bmax[i] : val;
        const age = healthFormatAgeMs(Date.now() - tsv);
        const smin = healthSeriesStats.heapInternalLargest.min;
        const smax = healthSeriesStats.heapInternalLargest.max;

        const windowLine = formatMinMaxDeltaLine(wmin, wmax, healthFormatBytes);
        const sparklineLine = formatMinMaxDeltaLine(smin, smax, healthFormatBytes);

        return {
            index: i,
            html: tooltipHtml({
                title: 'Internal Largest Block',
                age,
                hero: healthFormatBytes(val),
                windowLineHtml: windowLine,
                sparklineLineHtml: sparklineLine,
            }),
        };
    });
}

function renderHealth(health) {
    // Compact badge
    const cpuBadge = document.getElementById('health-cpu');
    if (cpuBadge) {
        cpuBadge.textContent = (health.cpu_usage === null) ? 'CPU --' : `CPU ${health.cpu_usage}%`;
    }

    // Trigger breathing animation on status dots
    const dot = document.getElementById('health-status-dot');
    if (dot) {
        dot.classList.remove('breathing');
        void dot.offsetWidth;
        dot.classList.add('breathing');
    }
    const dotExpanded = document.getElementById('health-status-dot-expanded');
    if (dotExpanded) {
        dotExpanded.classList.remove('breathing');
        void dotExpanded.offsetWidth;
        dotExpanded.classList.add('breathing');
    }

    // System
    const uptimeEl = document.getElementById('health-uptime');
    if (uptimeEl) uptimeEl.textContent = formatUptime(health.uptime_seconds);
    const resetEl = document.getElementById('health-reset');
    if (resetEl) resetEl.textContent = health.reset_reason || 'Unknown';

    // CPU
    const cpuEl = document.getElementById('health-cpu-full');
    if (cpuEl) cpuEl.textContent = (health.cpu_usage === null) ? '—' : `${health.cpu_usage}%`;
    const tempEl = document.getElementById('health-temp');
    if (tempEl) tempEl.textContent = (health.cpu_temperature !== null) ? `${health.cpu_temperature}°C` : 'N/A';

    // Memory
    const heapEl = document.getElementById('health-heap');
    if (heapEl) heapEl.textContent = formatBytes(health.heap_free);
    const heapMinEl = document.getElementById('health-heap-min');
    if (heapMinEl) heapMinEl.textContent = formatBytes(health.heap_min);
    const heapFragEl = document.getElementById('health-heap-frag');
    if (heapFragEl) {
        if (typeof health.heap_fragmentation_max_window === 'number') {
            heapFragEl.textContent = `${health.heap_fragmentation}% (max ${health.heap_fragmentation_max_window}%)`;
        } else {
            heapFragEl.textContent = `${health.heap_fragmentation}%`;
        }
    }

    const internalMinEl = document.getElementById('health-internal-min');
    if (internalMinEl) internalMinEl.textContent = healthFormatBytes(health.heap_internal_min);

    const internalLargestEl = document.getElementById('health-internal-largest');
    if (internalLargestEl) {
        if (typeof health.heap_internal_largest_min_window === 'number') {
            internalLargestEl.textContent = `${healthFormatBytes(health.heap_internal_largest)} (min ${healthFormatBytes(health.heap_internal_largest_min_window)})`;
        } else {
            internalLargestEl.textContent = healthFormatBytes(health.heap_internal_largest);
        }
    }

    const hasPsram = (
        (deviceInfoCache && typeof deviceInfoCache.psram_size === 'number' && deviceInfoCache.psram_size > 0) ||
        (typeof health.psram_free === 'number' && health.psram_free > 0)
    );

    const psramMinWrap = document.getElementById('health-psram-min-wrap');
    if (psramMinWrap) psramMinWrap.style.display = hasPsram ? '' : 'none';
    const psramMinEl = document.getElementById('health-psram-min');
    if (psramMinEl) psramMinEl.textContent = hasPsram ? healthFormatBytes(health.psram_min) : '—';

    const psramFragWrap = document.getElementById('health-psram-frag-wrap');
    if (psramFragWrap) psramFragWrap.style.display = hasPsram ? '' : 'none';
    const psramFragEl = document.getElementById('health-psram-frag');
    if (psramFragEl) {
        if (hasPsram && typeof health.psram_fragmentation_max_window === 'number') {
            psramFragEl.textContent = `${health.psram_fragmentation}% (max ${health.psram_fragmentation_max_window}%)`;
        } else {
            psramFragEl.textContent = hasPsram ? `${health.psram_fragmentation}%` : '—';
        }
    }

    // Flash
    const flashEl = document.getElementById('health-flash');
    if (flashEl) {
        flashEl.textContent = `${formatBytes(health.flash_used)} / ${formatBytes(health.flash_total)}`;
    }

    // Filesystem
    const fsEl = document.getElementById('health-fs');
    if (fsEl) {
        if (health.fs_mounted === null) {
            fsEl.textContent = 'Not present';
        } else if (!health.fs_mounted) {
            fsEl.textContent = 'Not mounted';
        } else if (health.fs_used_bytes !== null && health.fs_total_bytes !== null) {
            fsEl.textContent = `FFat ${formatBytes(health.fs_used_bytes)} / ${formatBytes(health.fs_total_bytes)}`;
        } else {
            fsEl.textContent = 'FFat mounted';
        }
    }

    // Network
    const ipEl = document.getElementById('health-ip');
    const rssiEl = document.getElementById('health-rssi');

    if (health.wifi_rssi !== null) {
        const strength = getSignalStrength(health.wifi_rssi);
        if (rssiEl) rssiEl.textContent = `${health.wifi_rssi} dBm (${strength})`;
        if (ipEl) ipEl.textContent = health.ip_address || 'N/A';
    } else {
        if (rssiEl) rssiEl.textContent = 'Not connected';
        if (ipEl) ipEl.textContent = 'N/A';
    }

    // MQTT
    const mqttEl = document.getElementById('health-mqtt');
    if (mqttEl) {
        if (!health.mqtt_enabled) {
            mqttEl.textContent = 'Disabled';
        } else {
            const status = health.mqtt_connected ? 'Connected' : 'Disconnected';
            const pub = health.mqtt_publish_enabled ? 'publish on' : 'publish off';
            const age = (health.mqtt_health_publish_age_ms === null) ? 'age --' : `age ${(health.mqtt_health_publish_age_ms / 1000).toFixed(0)}s`;
            mqttEl.textContent = `${status} (${pub}, ${age})`;
        }
    }

    // Display
    const displayRow = document.getElementById('health-display-row');
    const displayEl = document.getElementById('health-display');
    if (displayRow && displayEl) {
        const hasDisplay = deviceInfoCache && deviceInfoCache.has_display;
        displayRow.style.display = hasDisplay ? 'flex' : 'none';
        if (hasDisplay) {
            if (health.display_fps === null || health.display_fps === undefined) {
                displayEl.textContent = 'N/A';
            } else if (typeof health.display_lv_timer_us === 'number' && typeof health.display_present_us === 'number') {
                displayEl.textContent = `${health.display_fps} fps, ${(health.display_lv_timer_us / 1000).toFixed(1)}ms / ${(health.display_present_us / 1000).toFixed(1)}ms`;
            } else {
                displayEl.textContent = `${health.display_fps} fps`;
            }
        }
    }
}

function renderSensorsSection(health) {
    const section = document.getElementById('sensors-section');
    const badges = document.getElementById('sensor-badges');
    if (!section || !badges) return;

    const sensors = health && typeof health === 'object' ? health.sensors : null;

    const keys = sensors ? Object.keys(sensors) : [];
    if (keys.length === 0) {
        section.style.display = 'none';
        return;
    }

    section.style.display = 'block';
    badges.innerHTML = '';

    keys.sort();
    keys.forEach(key => {
        const value = sensors[key];
        const display = (value === undefined) ? '—' : JSON.stringify(value);
        const badge = document.createElement('span');
        badge.className = 'sensor-badge';
        badge.textContent = `${key}=${display}`;
        badges.appendChild(badge);
    });
}

async function updateHealth() {
    try {
        const response = await fetch(API_HEALTH);
        if (!response.ok) return;

        const health = await response.json();

        const cpuUsage = (typeof health.cpu_usage === 'number' && isFinite(health.cpu_usage)) ? Math.floor(health.cpu_usage) : null;
        const hasPsram = (
            (deviceInfoCache && typeof deviceInfoCache.psram_size === 'number' && deviceInfoCache.psram_size > 0) ||
            (typeof health.psram_free === 'number' && health.psram_free > 0)
        );

        // Update point-in-time rows (shown when history is unavailable).
        const ptCpu = document.getElementById('health-point-cpu-value');
        if (ptCpu) ptCpu.textContent = (cpuUsage !== null) ? `${cpuUsage}%` : '—';
        const ptHeap = document.getElementById('health-point-heap-value');
        if (ptHeap) ptHeap.textContent = healthFormatBytes(health.heap_internal_free);
        const ptPsramWrap = document.getElementById('health-point-psram-wrap');
        if (ptPsramWrap) ptPsramWrap.style.display = hasPsram ? '' : 'none';
        const ptPsram = document.getElementById('health-point-psram-value');
        if (ptPsram) ptPsram.textContent = hasPsram ? healthFormatBytes(health.psram_free) : '—';
        const ptLargest = document.getElementById('health-point-largest-value');
        if (ptLargest) ptLargest.textContent = healthFormatBytes(health.heap_internal_largest);

        // Update sparkline header values.
        const cpuSparkValue = document.getElementById('health-sparkline-cpu-value');
        if (cpuSparkValue) cpuSparkValue.textContent = (cpuUsage !== null) ? `${cpuUsage}%` : '—';

        const heapSparkValue = document.getElementById('health-sparkline-heap-value');
        if (heapSparkValue) heapSparkValue.textContent = healthFormatBytes(health.heap_internal_free);

        const psramWrap = document.getElementById('health-sparkline-psram-wrap');
        if (psramWrap) psramWrap.style.display = hasPsram ? '' : 'none';
        const psramSparkValue = document.getElementById('health-sparkline-psram-value');
        if (psramSparkValue) psramSparkValue.textContent = hasPsram ? healthFormatBytes(health.psram_free) : '—';

        const largestSparkValue = document.getElementById('health-sparkline-largest-value');
        if (largestSparkValue) largestSparkValue.textContent = healthFormatBytes(health.heap_internal_largest);

        renderHealth(health);
        renderSensorsSection(health);
        if (healthExpanded) {
            await updateHealthHistory({ hasPsram });
        }
    } catch (error) {
        console.error('Failed to fetch health stats:', error);
    }
}

function toggleHealthWidget() {
    healthExpanded = !healthExpanded;
    const expandedEl = document.getElementById('health-expanded');
    if (!expandedEl) return;

    expandedEl.style.display = healthExpanded ? 'block' : 'none';
    if (healthExpanded) {
        updateHealth();
        updateHealthHistory({
            hasPsram: (() => {
                const wrap = document.getElementById('health-sparkline-psram-wrap');
                return wrap ? (wrap.style.display !== 'none') : null;
            })(),
        });
    } else {
        healthTooltipSetVisible(false);
    }
}

function initHealthWidget() {
    const healthBadge = document.getElementById('health-badge');
    if (healthBadge) {
        healthBadge.addEventListener('click', toggleHealthWidget);
    }
    const closeBtn = document.getElementById('health-close');
    if (closeBtn) {
        closeBtn.addEventListener('click', toggleHealthWidget);
    }

    // Configure polling based on device info if available.
    healthConfigureFromDeviceInfo(deviceInfoCache);
    healthConfigureHistoryFromDeviceInfo(deviceInfoCache);

    // Attach hover/touch tooltips once.
    healthInitSparklineTooltips();

    // Start polling. loadVersion() fills deviceInfoCache asynchronously; we tune interval after first info fetch.
    const startPolling = () => {
        if (healthPollTimer) {
            clearInterval(healthPollTimer);
            healthPollTimer = null;
        }
        healthConfigureFromDeviceInfo(deviceInfoCache);
        healthConfigureHistoryFromDeviceInfo(deviceInfoCache);
        healthPollTimer = setInterval(updateHealth, healthPollIntervalMs);
    };

    // Initial
    updateHealth();
    startPolling();

    // Re-tune polling once deviceInfoCache becomes available.
    setTimeout(startPolling, 1500);
}
