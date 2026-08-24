window.init_camera_fragment = function () {
    var base = '/api/component/camera/config';
    var status = document.getElementById('camera-status');
    var quality = document.getElementById('camera-jpeg-quality');
    var qualityValue = document.getElementById('camera-jpeg-quality-value');
    var dimensions = document.getElementById('camera-output-dimensions');
    var exposure = document.getElementById('camera-exposure-lines');
    var exposureValue = document.getElementById('camera-exposure-lines-value');
    var whiteBalanceRed = document.getElementById('camera-wb-red');
    var whiteBalanceBlue = document.getElementById('camera-wb-blue');
    var rawMode = document.getElementById('camera-raw-mode');
    var saveButton = document.getElementById('camera-save-btn');
    var captureButton = document.getElementById('camera-capture-btn');
    var preview = document.getElementById('camera-preview');
    var previewCard = document.getElementById('camera-preview-card');
    var download = document.getElementById('camera-download-btn');
    var currentUrl = null;

    function setStatus(message, isError) {
        status.textContent = message;
        status.classList.toggle('text-danger', !!isError);
    }

    function saveCurrentSettings() {
        var parts = dimensions.value.split('x');
        return fetch(base, {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({
                jpeg_quality: Number(quality.value),
                output_width: Number(parts[0]),
                output_height: Number(parts[1]),
                exposure_lines: Number(exposure.value),
                white_balance_red_q8: Number(whiteBalanceRed.value),
                white_balance_blue_q8: Number(whiteBalanceBlue.value)
            })
        }).then(function (response) {
            if (!response.ok) throw new Error('Unable to save camera settings');
        });
    }

    quality.addEventListener('input', function () {
        qualityValue.textContent = quality.value;
    });
    exposure.addEventListener('input', function () {
        exposureValue.textContent = exposure.value;
    });

    fetch(base).then(function (response) {
        if (!response.ok) throw new Error('Camera configuration unavailable');
        return response.json();
    }).then(function (config) {
        quality.min = config.jpeg_quality_min;
        quality.max = config.jpeg_quality_max;
        quality.value = config.jpeg_quality;
        qualityValue.textContent = config.jpeg_quality;
        exposure.min = config.exposure_lines_min;
        exposure.max = config.exposure_lines_max;
        exposure.value = config.exposure_lines;
        exposureValue.textContent = config.exposure_lines;
        whiteBalanceRed.min = config.white_balance_q8_min;
        whiteBalanceRed.max = config.white_balance_q8_max;
        whiteBalanceRed.value = config.white_balance_red_q8;
        whiteBalanceBlue.min = config.white_balance_q8_min;
        whiteBalanceBlue.max = config.white_balance_q8_max;
        whiteBalanceBlue.value = config.white_balance_blue_q8;
        dimensions.innerHTML = '';
        config.output_dimensions.forEach(function (option) {
            var value = option.width + 'x' + option.height;
            var el = document.createElement('option');
            el.value = value;
            el.textContent = value;
            el.selected = option.width === config.output_width && option.height === config.output_height;
            dimensions.appendChild(el);
        });
        rawMode.textContent = 'Sensor mode: ' + config.raw_width + 'x' + config.raw_height + ' ' + config.raw_pixel_format;
        setStatus(config.detected ? 'Camera ready' : 'Camera not detected', !config.detected);
        captureButton.disabled = !config.detected;
    }).catch(function (error) {
        setStatus(error.message, true);
        saveButton.disabled = true;
        captureButton.disabled = true;
    });

    saveButton.addEventListener('click', function () {
        saveCurrentSettings().then(function () {
            setStatus('Camera settings saved', false);
        }).catch(function (error) {
            setStatus(error.message, true);
        });
    });

    captureButton.addEventListener('click', function () {
        captureButton.disabled = true;
        setStatus('Capturing...', false);
        saveCurrentSettings().then(function () {
            return fetch('/api/camera/snapshot.jpg', { cache: 'no-store' });
        }).then(function (response) {
            if (!response.ok) throw new Error('Capture failed');
            return response.blob();
        }).then(function (blob) {
            if (currentUrl) URL.revokeObjectURL(currentUrl);
            currentUrl = URL.createObjectURL(blob);
            preview.src = currentUrl;
            previewCard.hidden = false;
            download.href = currentUrl;
            download.classList.remove('disabled');
            setStatus('Capture complete (' + Math.round(blob.size / 1024) + ' KB)', false);
        }).catch(function (error) {
            setStatus(error.message, true);
        }).finally(function () {
            captureButton.disabled = false;
        });
    });
};