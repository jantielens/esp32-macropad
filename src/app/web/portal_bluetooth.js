function updateBluetoothSettings() {
    const settings = document.getElementById('bthome-event-settings');
    if (!settings) return;
    const config = window.deviceConfig || {};
    const caps = config.caps || {};
    const supported = caps.bthome_events === true ||
        (caps.bthome_events === undefined && (caps.ble === true || caps.ble_hid === true));
    const powerSupported = supported && caps.bluetooth_tx_power === true;
    settings.disabled = !supported;
    for (const id of ['bluetooth-power-save-btn', 'bthome-save-btn', 'bluetooth-power-settings']) {
        const el = document.getElementById(id);
        if (el) el.disabled = !(id === 'bthome-save-btn' ? supported : powerSupported);
    }
    const unavailable = document.getElementById('bluetooth-unavailable');
    if (unavailable) unavailable.hidden = supported;
    const powerUnavailable = document.getElementById('bluetooth-power-unavailable');
    if (powerUnavailable) powerUnavailable.hidden = !supported || powerSupported;
    const pending = document.getElementById('bluetooth-power-pending');
    if (pending) pending.hidden = !powerSupported ||
        Number(config.bluetooth_tx_power_dbm) === Number(config.bluetooth_active_tx_power_dbm);
    for (const name of ['bluetooth_tx_power_dbm', 'bthome_event_burst_ms', 'bthome_event_adv_interval_ms']) {
        const field = document.getElementById(name);
        if (field) field.disabled = !(name === 'bluetooth_tx_power_dbm' ? powerSupported : supported);
    }
}

async function loadBluetoothConfig() {
    await loadConfig();
    updateBluetoothSettings();
}

function validateBluetoothConfig(config) {
    if (Object.prototype.hasOwnProperty.call(config, 'bluetooth_tx_power_dbm')) {
        const value = config.bluetooth_tx_power_dbm;
        const power = Number(value);
        if ((typeof value !== 'string' && typeof value !== 'number') ||
            !/^-?\d+$/.test(String(value)) || !Number.isInteger(power) ||
            power < -12 || power > 9 || power % 3 !== 0) {
            return { valid: false, message: 'Bluetooth transmit power must be -12 to +9 dBm in 3 dB steps' };
        }
    }
    const bthomeFields = ['bthome_event_burst_ms', 'bthome_event_adv_interval_ms'];
    if (bthomeFields.some(name => Object.prototype.hasOwnProperty.call(config, name))) {
        const retained = window.deviceConfig || {};
        const values = {};
        for (const name of bthomeFields) {
            const value = Object.prototype.hasOwnProperty.call(config, name) ? config[name] : retained[name];
            if ((typeof value !== 'string' && typeof value !== 'number') ||
                !/^-?\d+$/.test(String(value)) || !Number.isSafeInteger(Number(value))) {
                return { valid: false, message: 'BTHome event settings must be decimal integers' };
            }
            values[name] = Number(value);
        }
        const burst = values.bthome_event_burst_ms;
        const interval = values.bthome_event_adv_interval_ms;
        if (burst < 100 || burst > 1000 || interval < 20 || interval > 300 || burst < 3 * interval) {
            return { valid: false, message: 'BTHome burst must be 100-1000 ms, interval 20-300 ms, and burst at least three intervals' };
        }
    }
    return { valid: true };
}

function saveBluetoothConfig(fieldNames, requiresReboot) {
    return saveFragmentConfig(function (config) {
        for (const name of fieldNames) {
            if (config[name] !== undefined) window.deviceConfig[name] = Number(config[name]);
        }
        updateBluetoothSettings();
        return requiresReboot &&
            Number(config.bluetooth_tx_power_dbm) !== Number(window.deviceConfig.bluetooth_active_tx_power_dbm);
    }, fieldNames, validateBluetoothConfig);
}

window.init_bluetooth_fragment = async function () {
    const eventFields = ['bthome_event_burst_ms', 'bthome_event_adv_interval_ms'];
    if (!window.__bluetooth_config_fields_registered) {
        window.registerConfigFields(['bluetooth_tx_power_dbm'].concat(eventFields));
        window.__bluetooth_config_fields_registered = true;
    }
    await loadBluetoothConfig();
    const powerButton = document.getElementById('bluetooth-power-save-btn');
    if (powerButton) powerButton.addEventListener('click', function () {
        return saveBluetoothConfig(['bluetooth_tx_power_dbm'], true);
    });
    const eventButton = document.getElementById('bthome-save-btn');
    if (eventButton) eventButton.addEventListener('click', function () {
        return saveBluetoothConfig(eventFields, false);
    });
};
