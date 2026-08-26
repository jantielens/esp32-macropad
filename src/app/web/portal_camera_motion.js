window.init_camera_motion_fragment = function () {
    var base = '/api/component/camera-motion/config';
    var enabled = document.getElementById('camera-motion-enabled');
    var fps = document.getElementById('camera-motion-fps');
    var fpsValue = document.getElementById('camera-motion-fps-value');
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
                motion_fps: Number(fps.value),
                motion_sensitivity: Number(sensitivity.value),
                presence_hold_seconds: Number(holdSeconds.value)
            })
        }).then(function (response) {
            if (!response.ok) throw new Error('Unable to save motion settings');
        });
    }

    fps.addEventListener('input', function () {
        fpsValue.textContent = fps.value;
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
        fps.min = config.motion_fps_min;
        fps.max = config.motion_fps_max;
        fps.value = config.motion_fps;
        fpsValue.textContent = config.motion_fps;
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