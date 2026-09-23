// E-paper Presentation fragment. Compiled only when HAS_LVGL_EPAPER is enabled.
window.registerConfigFields([
    'epaper_render_mode',
    'epaper_grayscale_binding_refresh_interval_ms',
    'epaper_bw_binding_refresh_interval_ms',
    'epaper_grayscale_min_refresh_interval_ms',
    'epaper_bw_min_refresh_interval_ms',
    'epaper_refresh_clock_on_minute_boundary',
    'epaper_bw_full_refresh_threshold'
]);

window.init_epaper_refresh_fragment = function () {
    var grayscale = document.getElementById('epaper-grayscale-settings');
    var bw = document.getElementById('epaper-bw-settings');
    var modeRadios = document.querySelectorAll('input[name="epaper_render_mode"]');
    var savedMode = '';

    function selectedMode() {
        var selected = document.querySelector('input[name="epaper_render_mode"]:checked');
        return selected ? selected.value : 'bw';
    }

    function updateModeSettings() {
        var mode = selectedMode();
        if (grayscale) grayscale.hidden = mode !== 'grayscale';
        if (bw) bw.hidden = mode !== 'bw';
        var refresh = document.getElementById('epaper-full-refresh-btn');
        if (refresh) refresh.hidden = mode !== 'bw';
    }

    modeRadios.forEach(function (radio) {
        radio.addEventListener('change', updateModeSettings);
    });

    loadConfig().then(function () {
        savedMode = window.deviceConfig && window.deviceConfig.epaper_render_mode || 'bw';
        var selected = document.querySelector('input[name="epaper_render_mode"][value="' + savedMode + '"]');
        if (selected) selected.checked = true;
        updateModeSettings();
    });

    var save = document.getElementById('epaper-refresh-save-btn');
    if (save) save.addEventListener('click', function () {
        saveFragmentConfig(selectedMode() !== savedMode);
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