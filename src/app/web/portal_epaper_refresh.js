// E-paper Presentation fragment. Compiled only when HAS_LVGL_EPAPER is enabled.
window.registerConfigFields([
    'epaper_render_mode',
    'epaper_bw_conversion',
    'epaper_bw_threshold',
    'epaper_vcom_mv',
    'epaper_binding_refresh_interval_ms',
    'epaper_min_refresh_interval_ms',
    'epaper_refresh_clock_on_minute_boundary',
    'epaper_full_refresh_threshold'
]);

window.init_epaper_refresh_fragment = function () {
    var savedMode = '';
    var savedConversion = '';
    var savedThreshold = 128;
    var savedVcom = 0;
    var vcom = document.getElementById('epaper_vcom_mv');
    var vcomVolts = document.getElementById('epaper-vcom-volts');
    var conversion = document.getElementById('epaper_bw_conversion');
    var threshold = document.getElementById('epaper_bw_threshold');
    var thresholdRange = document.getElementById('epaper-bw-threshold-range');
    var save = document.getElementById('epaper-refresh-save-btn');
    if (save) save.disabled = true;

    function selectedMode() {
        var selected = document.querySelector('input[name="epaper_render_mode"]:checked');
        return selected ? selected.value : 'bw';
    }

    function updateControls() {
        conversion.disabled = selectedMode() !== 'bw';
        document.getElementById('epaper-bw-threshold-controls').hidden = conversion.value !== 'threshold';
        threshold.disabled = conversion.disabled || conversion.value !== 'threshold';
        thresholdRange.disabled = threshold.disabled;
        thresholdRange.value = threshold.value;
    }

    document.querySelectorAll('input[name="epaper_render_mode"]').forEach(function (input) {
        input.addEventListener('change', updateControls);
    });
    conversion.addEventListener('change', updateControls);
    threshold.addEventListener('input', function () { thresholdRange.value = threshold.value; });
    thresholdRange.addEventListener('input', function () { threshold.value = thresholdRange.value; });

    loadConfig().then(function () {
        savedMode = window.deviceConfig && window.deviceConfig.epaper_render_mode || 'bw';
        savedConversion = window.deviceConfig.epaper_bw_conversion;
        savedThreshold = Number(window.deviceConfig.epaper_bw_threshold);
        savedVcom = Number(window.deviceConfig.epaper_vcom_mv);
        vcomVolts.value = String(-savedVcom / 1000);
        var defaultVcom = Number(window.deviceConfig.epaper_default_vcom_mv);
        document.getElementById('epaper-vcom-default').textContent = defaultVcom ?
            '0 uses the current board default: -' + (defaultVcom / 1000).toFixed(3) + ' V.' :
            '0 uses the current board default. This board has no specified default; drivers that require VCOM need an explicit panel voltage.';
        var supported = window.deviceConfig && window.deviceConfig.epaper_supported_render_modes || ['bw', 'grayscale'];
        document.querySelectorAll('input[name="epaper_render_mode"]').forEach(function (input) {
            input.disabled = supported.indexOf(input.value) === -1;
        });
        var selected = document.querySelector('input[name="epaper_render_mode"][value="' + savedMode + '"]');
        if (selected) selected.checked = true;
        updateControls();
        if (save) save.disabled = false;
    });

    document.getElementById('epaper-vcom-default-btn').addEventListener('click', function () {
        vcomVolts.value = '0';
    });

    if (save) save.addEventListener('click', function () {
        if (!threshold.disabled && !threshold.checkValidity()) {
            threshold.reportValidity();
            return;
        }
        if (!vcomVolts.checkValidity()) {
            vcomVolts.reportValidity();
            return;
        }
        vcom.value = String(Math.round(-Number(vcomVolts.value) * 1000));
        saveFragmentConfig(function (config) {
            return config.epaper_render_mode !== savedMode ||
                config.epaper_bw_conversion !== undefined && config.epaper_bw_conversion !== savedConversion ||
                config.epaper_bw_threshold !== undefined && Number(config.epaper_bw_threshold) !== savedThreshold ||
                config.epaper_vcom_mv !== undefined && Number(config.epaper_vcom_mv) !== savedVcom;
        });
    });

    var refresh = document.getElementById('epaper-full-refresh-btn');
    if (refresh) refresh.addEventListener('click', async function () {
        refresh.disabled = true;
        try {
            var response = await fetch('/api/component/epaper-refresh/full-refresh', { method: 'POST' });
            showMessage(response.ok ? 'Full refresh queued' : 'Could not queue full refresh', response.ok ? 'success' : 'error');
        } finally {
            refresh.disabled = false;
        }
    });
};