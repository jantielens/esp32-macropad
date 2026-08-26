window.init_camera_motion_fragment = function () {
    var base = '/api/component/camera-motion/config';
    var enabled = document.getElementById('camera-motion-enabled');
    var analyzeEvery = document.getElementById('camera-motion-analyze-every-nth-frame');
    var analyzeEveryValue = document.getElementById('camera-motion-analyze-every-nth-frame-value');
    var analysisRate = document.getElementById('camera-motion-analysis-rate');
    var sensitivity = document.getElementById('camera-motion-sensitivity');
    var sensitivityValue = document.getElementById('camera-motion-sensitivity-value');
    var holdSeconds = document.getElementById('camera-presence-hold-seconds');
    var holdSecondsValue = document.getElementById('camera-presence-hold-seconds-value');
    var saveButton = document.getElementById('camera-motion-save-btn');

    function saveSettings() {
        return fetch(base, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                motion_enabled: enabled.checked,
                motion_analyze_every_nth_frame: Number(analyzeEvery.value),
                motion_sensitivity: Number(sensitivity.value),
                presence_hold_seconds: Number(holdSeconds.value)
            })
        }).then(function (response) {
            if (!response.ok) throw new Error('Unable to save motion settings');
        });
    }

    function updateAnalysisRate(captureFps) {
        analyzeEveryValue.textContent = analyzeEvery.value;
        analysisRate.textContent = (captureFps / Number(analyzeEvery.value)).toFixed(2).replace(/\.00$/, '');
    }
    analyzeEvery.addEventListener('input', function () {
        updateAnalysisRate(Number(analyzeEvery.dataset.captureFps));
    });
    sensitivity.addEventListener('input', function () {
        sensitivityValue.textContent = sensitivity.value;
    });
    holdSeconds.addEventListener('input', function () {
        holdSecondsValue.textContent = holdSeconds.value;
    });

    fetch(base).then(function (response) {
        if (!response.ok) throw new Error('Motion sensing configuration unavailable');
        return response.json();
    }).then(function (config) {
        enabled.checked = config.motion_enabled;
        analyzeEvery.min = config.motion_analyze_every_nth_frame_min;
        analyzeEvery.max = config.motion_analyze_every_nth_frame_max;
        analyzeEvery.value = config.motion_analyze_every_nth_frame;
        analyzeEvery.dataset.captureFps = config.capture_fps;
        updateAnalysisRate(config.capture_fps);
        sensitivity.min = config.motion_sensitivity_min;
        sensitivity.max = config.motion_sensitivity_max;
        sensitivity.value = config.motion_sensitivity;
        sensitivityValue.textContent = config.motion_sensitivity;
        holdSeconds.min = config.presence_hold_seconds_min;
        holdSeconds.max = config.presence_hold_seconds_max;
        holdSeconds.value = config.presence_hold_seconds;
        holdSecondsValue.textContent = config.presence_hold_seconds;
    }).catch(function (error) {
        alert(error.message);
        saveButton.disabled = true;
    });

    saveButton.addEventListener('click', function () {
        saveButton.disabled = true;
        saveSettings().then(function () {
            saveButton.textContent = 'Motion settings saved';
        }).catch(function (error) {
            alert(error.message);
        }).finally(function () {
            saveButton.disabled = false;
            setTimeout(function () {
                saveButton.textContent = 'Save motion settings';
            }, 2000);
        });
    });
};