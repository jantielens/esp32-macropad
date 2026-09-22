// E-paper Presentation fragment. Compiled only when HAS_EPAPER_PRESENTATION is enabled.
window.registerConfigFields([
    'panel_mode',
    'grayscale_binding_refresh_interval_ms',
    'bw_binding_refresh_interval_ms',
    'grayscale_min_presentation_interval_ms',
    'bw_min_presentation_interval_ms',
    'refresh_clock_values_on_minute_boundary',
    'bw_full_update_threshold'
]);

window.init_epaper_presentation_fragment = function () {
    var grayscale = document.getElementById('epaper-grayscale-settings');
    var bw = document.getElementById('epaper-bw-settings');
    var modeRadios = document.querySelectorAll('input[name="panel_mode"]');
    var savedMode = '';

    function selectedMode() {
        var selected = document.querySelector('input[name="panel_mode"]:checked');
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
        savedMode = window.deviceConfig && window.deviceConfig.panel_mode || 'bw';
        var selected = document.querySelector('input[name="panel_mode"][value="' + savedMode + '"]');
        if (selected) selected.checked = true;
        updateModeSettings();
    });

    var save = document.getElementById('epaper-presentation-save-btn');
    if (save) save.addEventListener('click', function () {
        saveFragmentConfig(selectedMode() !== savedMode);
    });

    var refresh = document.getElementById('epaper-full-refresh-btn');
    if (refresh) refresh.addEventListener('click', async function () {
        refresh.disabled = true;
        try {
            var response = await fetch('/api/component/epaper-presentation/full-refresh', { method: 'POST' });
            showMessage(response.ok ? 'Full refresh queued' : 'Could not queue full refresh', response.ok ? 'success' : 'error');
        } finally {
            refresh.disabled = false;
        }
    });
};