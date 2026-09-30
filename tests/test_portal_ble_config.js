const assert = require('assert');
const fs = require('fs');
const vm = require('vm');

const powers = [-12, -9, -6, -3, 0, 3, 6, 9];
const elements = {
    ble_burst_count: { value: '', disabled: false },
    ble_adv_interval_ms: { value: '', disabled: false },
    ble_tx_power_dbm: { value: '', disabled: false }
};
let stored = { ble_burst_count: 3, ble_adv_interval_ms: 100, ble_tx_power_dbm: 9 };
let submitted;
let pendingReboot = false;
const messages = [];
const context = {
    console,
    window: {},
    API_CONFIG: '/api/config',
    document: {
        getElementById(id) { return elements[id] || null; },
        querySelectorAll() { return []; },
        querySelector(selector) {
            const match = selector.match(/^\[name="([^"]+)"\]$/);
            return match ? elements[match[1]] || null : null;
        }
    },
    setPendingReboot() { pendingReboot = true; },
    showMessage(message, type) { messages.push({ message, type }); },
    async fetch(url, options) {
        if (!options) {
            assert.strictEqual(url, '/api/config');
            return { ok: true, json: async () => stored };
        }
        assert.strictEqual(url, '/api/config?no_reboot=1');
        assert.strictEqual(options.method, 'POST');
        submitted = JSON.parse(options.body);
        stored = Object.assign({}, stored, submitted);
        return { ok: true, json: async () => ({ success: true }) };
    }
};
vm.createContext(context);
for (const file of ['portal_config.js', 'portal_fragment_init.js']) {
    vm.runInContext(fs.readFileSync('src/app/web/' + file, 'utf8'), context);
}

(async function () {
    const fragment = fs.readFileSync('src/app/web/mode.fragment.html', 'utf8');
    const selector = fragment.match(/<select[^>]*id="ble_tx_power_dbm"[^>]*>([\s\S]*?)<\/select>/);
    assert(selector);
    assert.match(selector[0], /name="ble_tx_power_dbm"/);
    const options = Array.from(selector[1].matchAll(/<option value="(-?\d+)"/g), match => Number(match[1]));
    assert.deepStrictEqual(options, powers);
    assert.match(selector[1], /value="9" selected/);

    for (const power of powers) {
        for (const value of [power, String(power)]) {
            assert.strictEqual(context.validateConfig({ ble_tx_power_dbm: value }).valid, true);
        }
        stored.ble_tx_power_dbm = power;
        await context.loadConfig();
        assert.strictEqual(Number(elements.ble_tx_power_dbm.value), power);
        elements.ble_tx_power_dbm.value = String(power);
        pendingReboot = false;
        await context.saveFragmentConfig(true);
        assert.deepStrictEqual(submitted, {
            ble_burst_count: 3,
            ble_adv_interval_ms: 100,
            ble_tx_power_dbm: String(power)
        });
        assert.strictEqual(pendingReboot, true);
        elements.ble_tx_power_dbm.value = '';
        await context.loadConfig();
        assert.strictEqual(Number(elements.ble_tx_power_dbm.value), power);
    }

    for (const value of [-15, 12, -1, 1, 3.5, '', ' ', '3junk', 'NaN', null, false, {}, []]) {
        assert.strictEqual(context.validateConfig({ ble_tx_power_dbm: value }).valid, false, String(value));
    }
    assert.strictEqual(context.validateConfig({}).valid, true);
    assert.deepStrictEqual(messages, []);

    elements.ble_tx_power_dbm.value = '1';
    submitted = null;
    pendingReboot = false;
    await context.saveFragmentConfig(true);
    assert.strictEqual(submitted, null);
    assert.strictEqual(pendingReboot, false);
    assert.strictEqual(messages.pop().type, 'error');

    elements.ble_tx_power_dbm.disabled = true;
    await context.saveFragmentConfig(true);
    assert.strictEqual(submitted.ble_tx_power_dbm, undefined);
    delete elements.ble_tx_power_dbm;
    await context.loadConfig();
    await context.saveFragmentConfig(true);
    assert.strictEqual(submitted.ble_tx_power_dbm, undefined);
    assert.deepStrictEqual(messages, []);

    const startup = fs.readFileSync('src/app/app.ino', 'utf8');
    assert.match(startup, /const bool defer_wifi_init\s*=\s*boot_mode == PowerMode::DutyCycleBle\s*\|\|\s*device_class_dispatch_defer_wifi_init\(&device_config, boot_mode\);\s*if \(!defer_wifi_init\) \{\s*wifi_manager_early_init\(\);/);
    assert.match(startup, /ble_telemetry_init\(device_config.device_name, device_config.ble_tx_power_dbm\)/);
    const storage = fs.readFileSync('src/app/config_manager.cpp', 'utf8');
    const loadStart = storage.indexOf('bool config_manager_load(DeviceConfig *config)');
    const preferencesBegin = storage.indexOf('preferences.begin(CONFIG_NAMESPACE, true)', loadStart);
    assert(loadStart >= 0 && preferencesBegin > loadStart);
    const earlyDefaults = storage.slice(loadStart, preferencesBegin);
    assert.match(earlyDefaults, /config->ble_burst_count = BLE_TELEMETRY_DEFAULT_BURST_COUNT;/);
    assert.match(earlyDefaults, /config->ble_adv_interval_ms = BLE_TELEMETRY_DEFAULT_ADV_INTERVAL_MS;/);
    assert.match(earlyDefaults, /config->ble_tx_power_dbm = CONFIG_DEFAULT_BLE_TX_POWER_DBM;/);
    assert.match(storage, /config->ble_tx_power_dbm = preferences.getChar\(KEY_BLE_TX_POWER_DBM, CONFIG_DEFAULT_BLE_TX_POWER_DBM\);/);
    assert.match(storage, /putChar\(KEY_BLE_TX_POWER_DBM, config->ble_tx_power_dbm\)/);
    console.log('portal_ble_config: PASS');
})().catch(function (error) {
    console.error(error);
    process.exitCode = 1;
});