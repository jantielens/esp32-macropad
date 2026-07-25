window.init_epaper_ble_bridge_fragment = function () {
    var rows = Array.from(document.querySelectorAll('[data-bridge-frame]'));
    var saveButton = document.getElementById('epaper-ble-bridge-save');

    function updateRow(row) {
        var enabled = row.querySelector('.bridge-frame-enabled').checked;
        row.querySelectorAll('.bridge-frame-fields input').forEach(function (input) {
            input.disabled = !enabled;
        });
    }

    rows.forEach(function (row) {
        var enabled = row.querySelector('.bridge-frame-enabled');
        enabled.addEventListener('change', function () { updateRow(row); });
        updateRow(row);
    });

    fetch('/api/config')
        .then(function (response) { return response.ok ? response.json() : null; })
        .then(function (config) {
            if (!config) return;
            var frames = Array.isArray(config.epaper_ble_bridge_frames)
                ? config.epaper_ble_bridge_frames : [];
            rows.forEach(function (row, index) {
                var frame = frames[index];
                row.querySelector('.bridge-frame-enabled').checked = !!frame;
                row.querySelector('.bridge-frame-site-url').value = frame ? (frame.site_url || '') : '';
                row.querySelector('.bridge-frame-device-id').value = frame ? (frame.device_id || '') : '';
                row.querySelector('.bridge-frame-api-key').value = '';
                row.querySelector('.bridge-frame-key-status').textContent = frame && frame.api_key_set
                    ? 'API key is saved. Leave empty to keep it.'
                    : 'Enter the frame API key.';
                updateRow(row);
            });
        })
        .catch(function () { showMessage('Failed to load frame assignments', 'error'); });

    if (!saveButton) return;
    saveButton.addEventListener('click', async function () {
        var frames = [];
        for (var index = 0; index < rows.length; index++) {
            var row = rows[index];
            if (!row.querySelector('.bridge-frame-enabled').checked) continue;
            var siteUrl = row.querySelector('.bridge-frame-site-url').value.trim();
            var deviceId = row.querySelector('.bridge-frame-device-id').value.trim();
            var apiKey = row.querySelector('.bridge-frame-api-key').value;
            if (!siteUrl || !deviceId) {
                showMessage('Frame ' + (index + 1) + ' requires a site URL and device ID.', 'error');
                return;
            }
            frames.push({ site_url: siteUrl, device_id: deviceId, api_key: apiKey });
        }

        saveButton.disabled = true;
        try {
            var response = await fetch('/api/config?no_reboot=1', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ epaper_ble_bridge_frames: frames })
            });
            var result = await response.json().catch(function () { return null; });
            if (!response.ok) {
                throw new Error(result && result.message ? result.message : ('HTTP ' + response.status));
            }
            showMessage('Frame assignments saved', 'success');
            rows.forEach(function (row) {
                var key = row.querySelector('.bridge-frame-api-key');
                if (key.value) {
                    key.value = '';
                    row.querySelector('.bridge-frame-key-status').textContent =
                        'API key is saved. Leave empty to keep it.';
                }
            });
        } catch (error) {
            showMessage('Error saving: ' + error.message, 'error');
        } finally {
            saveButton.disabled = false;
        }
    });
};