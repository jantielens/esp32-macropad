// portal_core.js - Shared utilities, globals, and navigation
// Part of the ESP32 Macropad configuration portal.

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
    } else if (path === '/pads.html') {
        currentPage = 'pad';
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

function escAttr(s) {
    return (s || '').replace(/&/g,'&amp;').replace(/"/g,'&quot;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
}
