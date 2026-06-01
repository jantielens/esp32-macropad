// portal_brews_init.js — Fragment init functions for coffee-scale features.
// Separated from portal_fragment_init.js to isolate feature-branch code
// from the shared generic file, reducing merge conflict risk.
//
// Convention: window['init_' + itemId.replace(/-/g, '_') + '_fragment']()
// is called by portal_nav.js after loading the corresponding fragment HTML.

var scalePollTimer = null;

// ============================================================================
// Scale
// ============================================================================

window.init_scale_fragment = function () {
    if (scalePollTimer) clearInterval(scalePollTimer);
    scalePollTimer = null;
    initConfigFragment('scale-save-btn', false);

    // Tare button
    var tareBtn = document.getElementById('scale-tare-btn');
    if (tareBtn) tareBtn.addEventListener('click', function () {
        fetch('/api/scale/tare', { method: 'POST' })
            .then(function (r) { return r.json(); })
            .then(function (data) {
                if (data.ok) showMessage('Scale tared', 'success');
                else showMessage('Tare failed: ' + (data.error || 'Unknown'), 'error');
            })
            .catch(function (err) { showMessage('Tare failed: ' + err.message, 'error'); });
    });

    // Calibrate panel toggle
    var calBtn = document.getElementById('scale-calibrate-btn');
    var calPanel = document.getElementById('scale-calibrate-panel');
    if (calBtn && calPanel) {
        calBtn.addEventListener('click', function () {
            calPanel.style.display = calPanel.style.display === 'none' ? '' : 'none';
        });
    }
    var calCancel = document.getElementById('scale-cal-cancel-btn');
    if (calCancel && calPanel) {
        calCancel.addEventListener('click', function () { calPanel.style.display = 'none'; });
    }
    var calConfirm = document.getElementById('scale-cal-confirm-btn');
    if (calConfirm) calConfirm.addEventListener('click', function () {
        var weightInput = document.getElementById('scale-cal-weight');
        var weight = weightInput ? parseFloat(weightInput.value) : 0;
        if (!weight || weight <= 0) {
            showMessage('Enter a valid weight in grams', 'error');
            return;
        }
        fetch('/api/scale/calibrate', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ known_weight_g: weight })
        })
        .then(function (r) { return r.json(); })
        .then(function (data) {
            if (data.ok) {
                showMessage('Calibrated! Factor: ' + (data.calibration_factor || '').toString(), 'success');
                if (calPanel) calPanel.style.display = 'none';
            } else {
                showMessage('Calibration failed: ' + (data.error || 'Unknown'), 'error');
            }
        })
        .catch(function (err) { showMessage('Calibration failed: ' + err.message, 'error'); });
    });

    // Live scale status polling
    function pollScale() {
        fetch('/api/scale').then(function (r) { return r.json(); }).then(function (data) {
            var weightEl = document.getElementById('scale-weight');
            var flowBadge = document.getElementById('scale-flow-badge');
            if (weightEl) weightEl.textContent = data.weight_g !== undefined ? data.weight_g.toFixed(1) : '--';
            if (flowBadge && data.flow_rate !== undefined) {
                flowBadge.textContent = data.flow_rate.toFixed(1) + ' g/s';
                flowBadge.style.display = '';
            }
        }).catch(function () {});
    }
    pollScale();
    scalePollTimer = setInterval(pollScale, 2000);
};

// ============================================================================
// Brews
// ============================================================================

window.init_brews_fragment = function () {
    // Hide loading overlay (brews fragment doesn't use config form)
    var overlay = document.getElementById('form-loading-overlay');
    if (overlay) overlay.style.display = 'none';

    // Dynamically load Chart.js + annotation plugin if not yet available
    function loadScript(src) {
        return new Promise(function (resolve, reject) {
            if (document.querySelector('script[src="' + src + '"]')) { resolve(); return; }
            var s = document.createElement('script');
            s.src = src;
            s.onload = resolve;
            s.onerror = reject;
            document.head.appendChild(s);
        });
    }
    if (!window.Chart) {
        loadScript('https://cdn.jsdelivr.net/npm/chart.js@4')
            .then(function () { return loadScript('https://cdn.jsdelivr.net/npm/chartjs-plugin-annotation@3'); })
            .catch(function () {});
    }

    // Wire action bar buttons
    var exportBtn = document.getElementById('brew-export-all-btn');
    if (exportBtn) exportBtn.addEventListener('click', function () {
        if (typeof brewExportAll === 'function') brewExportAll();
    });

    var clearBtn = document.getElementById('brew-clear-all-btn');
    if (clearBtn) clearBtn.addEventListener('click', function () {
        if (typeof brewClearAll === 'function') brewClearAll();
    });

    var backBtn = document.getElementById('brew-back-btn');
    if (backBtn) backBtn.addEventListener('click', function () {
        if (typeof brewBackToList === 'function') brewBackToList();
    });

    // Import button triggers hidden file input
    var importBtn = document.getElementById('brew-import-btn');
    if (importBtn) importBtn.addEventListener('click', function () {
        var fileInput = document.getElementById('brew-import-file');
        if (fileInput) fileInput.click();
    });

    var importFile = document.getElementById('brew-import-file');
    if (importFile) importFile.addEventListener('change', function (e) {
        var file = e.target.files[0];
        if (file && typeof brewImport === 'function') {
            brewImport(file);
            e.target.value = '';
        }
    });

    // Load brew list
    if (typeof brewLoadList === 'function') brewLoadList();
};

// ============================================================================
// Brew Templates
// ============================================================================

window.init_brew_templates_fragment = function () {
    // Wire upload button
    var uploadBtn = document.getElementById('brew-tmpl-upload-btn');
    if (uploadBtn) uploadBtn.addEventListener('click', function () {
        var fileInput = document.getElementById('brew-tmpl-upload-file');
        if (fileInput) fileInput.click();
    });

    var uploadFile = document.getElementById('brew-tmpl-upload-file');
    if (uploadFile) uploadFile.addEventListener('change', function (e) {
        var file = e.target.files[0];
        if (file && typeof brewTmplUpload === 'function') {
            brewTmplUpload(file);
            e.target.value = '';
        }
    });

    // Wire back button
    var backBtn = document.getElementById('brew-tmpl-back-btn');
    if (backBtn) backBtn.addEventListener('click', function () {
        if (typeof brewTmplBackToList === 'function') brewTmplBackToList();
    });

    // Load templates
    if (typeof brewTmplLoad === 'function') brewTmplLoad();
};

// Register coffee-scale config fields with the shared save helper.
// saveFragmentConfig scans these names in the DOM — only fields that
// exist in the current fragment are actually collected.
if (typeof registerConfigFields === 'function') {
    registerConfigFields(['scale_smoothing']);
}
