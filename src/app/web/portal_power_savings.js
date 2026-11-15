window.registerConfigFields(['auto_shutdown_timeout_seconds']);

window.init_power_savings_fragment = function () {
    loadConfig();
    document.getElementById('power-savings-save-btn').addEventListener('click', function () {
        const input = document.getElementById('auto_shutdown_timeout_seconds');
        if (!input.checkValidity()) {
            showMessage('Enter a whole number from 0 to 65535 seconds', 'error');
            return;
        }
        return saveFragmentConfig(false, ['auto_shutdown_timeout_seconds']);
    });
};

(function () {
    let lastSent = -Infinity;
    function notifyActivity(event) {
        if (!event.isTrusted) return;
        const now = performance.now();
        if (now - lastSent < 250) return;
        lastSent = now;
        fetch('/api/component/power-savings/activity', {method: 'POST'}).then(function (response) {
            if (!response.ok) console.warn('Power savings activity rejected:', response.status);
        }).catch(function (error) {
            console.warn('Could not report portal interaction:', error);
        });
    }
    ['pointerdown', 'keydown', 'input', 'wheel'].forEach(function (name) {
        document.addEventListener(name, notifyActivity, {passive: true});
    });
})();
