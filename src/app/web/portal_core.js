// portal_core.js - Shared utilities, globals, and navigation
// Part of the ESP32 Macropad configuration portal.

/**
 * Configuration Portal JavaScript
 * Handles configuration form, OTA updates, and device reboots
 * Supports core mode (AP) and full mode (WiFi connected)
 * Multi-page support: home, network, firmware
 */

// ============================================================================
// Extensible config-field registration
// ============================================================================
//
// Per-feature modules (e.g. e-paper, shutter-tester, future device classes)
// register the NVS keys their fragments edit via
// window.registerConfigFields([...]). loadConfig() (portal_config.js) and
// saveFragmentConfig() (portal_fragment_init.js) both merge the registered
// set with their static core lists.
//
// MUST live in portal_core.js (first chunk, always-on) so device-class
// chunks gated by IS_*/HAS_* flags can register at module top-level even
// when they precede the shell chunk in concatenation order.
window.__extra_config_fields = window.__extra_config_fields || [];
window.registerConfigFields = function (names) {
    if (!names || !names.length) return;
    Array.prototype.push.apply(window.__extra_config_fields, names);
};

async function copyTextToClipboard(text) {
    if (navigator.clipboard && navigator.clipboard.writeText) {
        try {
            await navigator.clipboard.writeText(text);
            return;
        } catch (err) {
            console.warn('Clipboard API failed, using fallback:', err);
        }
    }

    var temp = document.createElement('textarea');
    temp.value = text;
    temp.setAttribute('readonly', '');
    temp.style.position = 'absolute';
    temp.style.left = '-9999px';
    document.body.appendChild(temp);
    try {
        temp.select();
        if (!document.execCommand('copy')) {
            throw new Error('Browser rejected clipboard copy');
        }
    } finally {
        document.body.removeChild(temp);
    }
}

// ---------------------------------------------------------------------------
// Fetch concurrency limiter
// ---------------------------------------------------------------------------
// ESP-Hosted SDIO transports (ESP32-P4 + external ESP32-C6) allocate AsyncTCP
// TX buffers from MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL. Concurrent responses
// fragment that pool and can drive copy_buff allocations to NULL, asserting
// inside transport_drv_sta_tx. Capping browser-side concurrency to 2 keeps
// the largest free DMA-internal block above safe-water-mark in practice.
// Applies to all window.fetch callers in the portal; navigations and image
// loads are not affected.
(function () {
    var MAX_INFLIGHT = 2;
    var inflight = 0;
    var queue = [];
    var originalFetch = window.fetch.bind(window);

    function pump() {
        while (inflight < MAX_INFLIGHT && queue.length > 0) {
            const job = queue.shift();
            inflight++;
            originalFetch(job.input, job.init).then(function (res) {
                inflight--;
                job.resolve(res);
                pump();
            }, function (err) {
                inflight--;
                job.reject(err);
                pump();
            });
        }
    }

    window.fetch = function (input, init) {
        return new Promise(function (resolve, reject) {
            queue.push({ input: input, init: init, resolve: resolve, reject: reject });
            pump();
        });
    };
})();

// API endpoints
const API_CONFIG = '/api/config';
// ?catalog=1 adds the action-authoring catalog to the response. Kept off
// API_VERSION, which the reboot connection poller calls up to 40 times.
const API_INFO = '/api/info?catalog=1';
const API_UPDATE = '/api/update';
const API_REBOOT = '/api/reboot';
const API_VERSION = '/api/info'; // Used for connection polling

let selectedFile = null;
let portalMode = 'full'; // 'core' or 'full'

let deviceInfoCache = null;
let deviceInfoInflight = null;

/**
 * Fetch /api/info with session-scope caching.
 *
 * Multiple fragments and helpers need device info; on a cold portal load
 * up to six independent fetch('/api/info') calls would race, each consuming
 * AsyncTCP TX buffers from the same DMA-internal heap. This helper returns
 * the cached payload after the first successful fetch and coalesces
 * concurrent first-time callers into a single network request.
 *
 * @param {boolean} forceRefresh - Bypass the cache and refetch.
 * @returns {Promise<object|null>} Parsed /api/info JSON, or null on error.
 */
function getDeviceInfo(forceRefresh) {
    if (!forceRefresh && deviceInfoCache) {
        return Promise.resolve(deviceInfoCache);
    }
    if (deviceInfoInflight) {
        return deviceInfoInflight;
    }
    deviceInfoInflight = fetch(API_INFO)
        .then(function (r) { return r.ok ? r.json() : null; })
        .then(function (info) {
            deviceInfoInflight = null;
            if (info) deviceInfoCache = info;
            return info;
        })
        .catch(function (err) {
            deviceInfoInflight = null;
            console.error('getDeviceInfo:', err);
            return null;
        });
    return deviceInfoInflight;
}

/**
 * Show a Bootstrap-styled toast notification.
 * Appends a toast div to #toast-container and auto-removes it after 5s.
 * No Bootstrap JS required — uses CSS classes + setTimeout for animation.
 * @param {string} message - Message text
 * @param {string} type - 'info', 'success', or 'error'
 */
function showMessage(message, type) {
    type = type || 'info';
    var container = document.getElementById('toast-container');
    if (!container) { console.warn('showMessage:', message); return; }

    var bgClass = type === 'success' ? 'bg-success'
                : type === 'error'   ? 'bg-danger'
                :                      'bg-secondary';

    var toast = document.createElement('div');
    toast.className = 'toast align-items-center text-white border-0 show ' + bgClass;
    toast.setAttribute('role', 'alert');
    toast.setAttribute('aria-live', 'assertive');
    toast.innerHTML =
        '<div class="d-flex">' +
        '<div class="toast-body">' + message + '</div>' +
        '<button type="button" class="btn-close btn-close-white me-2 m-auto" aria-label="Close"></button>' +
        '</div>';

    toast.querySelector('button').addEventListener('click', function () {
        container.removeChild(toast);
    });

    container.appendChild(toast);
    setTimeout(function () {
        if (toast.parentNode) container.removeChild(toast);
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
 * Sanitize a device name into a valid mDNS hostname label.
 * Mirrors the backend logic in config_manager_sanitize_device_name():
 * lowercase, alphanumeric + hyphens only, collapse whitespace/underscores
 * to a single hyphen, strip leading/trailing hyphens.
 *
 * @param {string} name - Raw device name from the user
 * @returns {string} mDNS-safe hostname label (without the .local suffix)
 */
function sanitizeForMDNS(name) {
    return (name || '').toLowerCase()
        .replace(/[^a-z0-9\s\-_]/g, '')
        .replace(/[\s_]+/g, '-')
        .replace(/-+/g, '-')
        .replace(/^-|-$/g, '');
}

/**
 * Update sanitized device name field
 */
function updateSanitizedName() {
    const deviceNameField = document.getElementById('device_name');
    const sanitizedField = document.getElementById('device_name_sanitized');
    
    // Only proceed if both elements exist
    if (!deviceNameField || !sanitizedField) return;
    
    const sanitized = sanitizeForMDNS(deviceNameField.value);
    sanitizedField.textContent = (sanitized || 'esp32-xxxx') + '.local';
}

function escAttr(s) {
    return (s || '').replace(/&/g,'&amp;').replace(/"/g,'&quot;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
}

// ---------------------------------------------------------------------------
// Pending-reboot banner
// ---------------------------------------------------------------------------
// Fragments that change reboot-required settings (wifi, network including
// the security/auth card, device name, mode, mqtt, ble) save with
// no_reboot=1 and call setPendingReboot().
// The banner survives fragment navigation (the shell is a SPA, fragments load
// via XHR so JS state persists) but is intentionally NOT persisted to
// sessionStorage: a full page reload (Ctrl+R) clears it. That gives the user
// a trivial escape hatch when the device was rebooted out-of-band (CLI
// deploy, power cycle, factory reset from another tab) and the banner has
// become stale.
let pendingRebootFlag = false;

function setPendingReboot() {
    var wasPending = pendingRebootFlag;
    pendingRebootFlag = true;
    updatePendingRebootBanner();
    // If the banner was already visible, pulse it so the user gets feedback
    // that another change was accepted (we suppressed the toast for these).
    if (wasPending) {
        var el = document.getElementById('reboot-pending-banner');
        if (el) {
            el.classList.remove('pulse');
            // Force reflow so re-adding the class restarts the animation.
            void el.offsetWidth;
            el.classList.add('pulse');
        }
    }
}

function clearPendingReboot() {
    pendingRebootFlag = false;
    updatePendingRebootBanner();
}

function updatePendingRebootBanner() {
    var el = document.getElementById('reboot-pending-banner');
    if (!el) return;
    el.style.display = pendingRebootFlag ? 'flex' : 'none';
}

function initPendingRebootBanner() {
    updatePendingRebootBanner();
    var btn = document.getElementById('reboot-pending-btn');
    if (!btn) return;
    btn.addEventListener('click', function () {
        // Don't pass newDeviceName: it's meant for "device was renamed,
        // redirect to new mDNS hostname" flows. From the banner we're just
        // applying pending changes on the same network/host, so the dialog's
        // default (window.location.origin) is correct.
        showRebootDialog({
            title: 'Rebooting Device',
            message: 'Applying configuration changes...',
            context: 'save'
        });
        clearPendingReboot();
        fetch(API_REBOOT, { method: 'POST' }).catch(function () {
            // The TCP connection often drops mid-response when the device
            // reboots; the reboot dialog's reconnection poller handles the
            // wait-and-redirect, so swallow this error.
        });
    });
}

if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', initPendingRebootBanner);
} else {
    initPendingRebootBanner();
}
