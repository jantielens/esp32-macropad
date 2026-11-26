const assert = require('assert');
const fs = require('fs');
const vm = require('vm');
const { execFileSync } = require('child_process');

const powers = [-12, -9, -6, -3, 0, 3, 6, 9];
const elements = {
    ble_burst_count: { value: '', disabled: false },
    ble_adv_interval_ms: { value: '', disabled: false },
};
let stored = { ble_burst_count: 3, ble_adv_interval_ms: 100 };
let submitted;
let pendingReboot = false;
let healthPollingStopped = false;
let saveFailure = false;
const messages = [];
let modeVisibilityTimer;
let operatingRadios = [];
const context = {
    console,
    window: {
        __extra_config_fields: [],
        registerConfigFields(names) { this.__extra_config_fields.push(...names); }
    },
    setInterval(callback) { modeVisibilityTimer = callback; return 1; },
    clearInterval() {},
    stopHealthPolling() { healthPollingStopped = true; },
    API_CONFIG: '/api/config',
    document: {
        getElementById(id) { return elements[id] || null; },
        querySelectorAll(selector) {
            if (selector === 'input[type="radio"][name="operating_mode"]') return operatingRadios;
            return selector === 'input[type="radio"][name="keyboard_transport"]' ?
                ['none', 'usb', 'ble'].map(transport => elements['keyboard-transport-' + transport]).filter(Boolean) : [];
        },
        querySelector(selector) {
            if (selector.startsWith('input[type="radio"][name="operating_mode"]')) {
                const value = selector.match(/\[value="([^"]+)"\]/);
                return operatingRadios.find(radio => value ? radio.value === value[1] : radio.checked) || null;
            }
            const label = selector.match(/^label\[for="([^"]+)"\]$/);
            if (label) return elements[label[1] + '-label'] || null;
            if (selector === 'input[type="radio"][name="keyboard_transport"]:checked') {
                return ['none', 'usb', 'ble'].map(transport => elements['keyboard-transport-' + transport]).find(el => el && el.checked) || null;
            }
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
        if (saveFailure) return { ok: false, json: async () => ({ success: false, message: 'NVS write failed' }) };
        stored = Object.assign({}, stored, submitted);
        return { ok: true, json: async () => ({ success: true }) };
    }
};
vm.createContext(context);
for (const file of ['portal_config.js', 'portal_fragment_init.js', 'portal_bluetooth.js']) {
    vm.runInContext(fs.readFileSync('src/app/web/' + file, 'utf8'), context);
}

(async function () {
    const sensorFields = { ...elements };
    for (const name of Object.keys(sensorFields)) delete elements[name];
    const modeFragment = fs.readFileSync('src/app/web/mode.fragment.html', 'utf8');
    operatingRadios = [...modeFragment.matchAll(/<input[^>]*name="operating_mode"[^>]*id="([^"]+)"[^>]*value="([^"]+)"/g)]
        .map(match => {
            let checked = false;
            const radio = { value: match[2], disabled: false, listeners: {},
                addEventListener(event, listener) { this.listeners[event] = listener; } };
            Object.defineProperty(radio, 'checked', {
                get() { return checked; },
                set(value) {
                    if (value) operatingRadios.forEach(other => { if (other !== radio) other.checked = false; });
                    checked = value;
                }
            });
            elements[match[1]] = radio;
            return radio;
        });
    assert(operatingRadios.some(radio => radio.value === 'offline'), 'Offline must be selectable in the production fragment');
    for (const id of ['duty-cycle-settings', 'wifi-backoff-row', 'ble-telemetry-settings',
        'mode_opt_duty_cycle_ble']) elements[id] = { style: {} };
    let saveMode;
    elements['mode-save-btn'] = { addEventListener(event, listener) { saveMode = listener; } };
    stored = { caps: { ble: true } };
    context.window.init_mode_fragment();
    await context.loadConfig();
    modeVisibilityTimer();
    assert.strictEqual(elements.operating_mode_always_on.checked, true, 'Missing mode defaults to online');
    for (const mode of ['duty_cycle_mqtt', 'duty_cycle_ble', 'offline']) {
        elements['operating_mode_' + mode].checked = true;
        elements['operating_mode_' + mode].listeners.change();
        assert.strictEqual(elements['duty-cycle-settings'].style.display, mode === 'offline' ? 'none' : '');
        assert.strictEqual(elements['ble-telemetry-settings'].style.display, mode === 'duty_cycle_ble' ? '' : 'none');
    }
    pendingReboot = false;
    await saveMode();
    assert.deepStrictEqual(submitted, { operating_mode: 'offline' });
    assert.strictEqual(pendingReboot, true);
    assert.strictEqual(context.window.deviceConfig.operating_mode, 'offline', 'Save updates validation before a reboot or reload');
    await context.loadConfig();
    assert.strictEqual(elements.operating_mode_offline.checked, true, 'Offline round-trips through config loading');
    assert.strictEqual(context.validateConfig({ wifi_ssid: '' }).valid, true);
    assert.strictEqual(context.validateConfig({ operating_mode: 'offline', wifi_ssid: ' ' }).valid, true);
    assert.strictEqual(context.validateConfig({ operating_mode: 'always_on', wifi_ssid: '' }).valid, false,
        'A submitted online mode overrides the saved Offline mode');
    assert.strictEqual(context.validateConfig({ operating_mode: 'offline', ap_password: 'short' }).valid, false);
    assert.strictEqual(context.validateConfig({ operating_mode: 'offline', device_name: '' }).valid, false);
    elements.operating_mode_always_on.checked = true;
    saveFailure = true;
    pendingReboot = false;
    await saveMode();
    assert.strictEqual(context.window.deviceConfig.operating_mode, 'offline', 'Failed saves must not change validation mode');
    assert.strictEqual(pendingReboot, false);
    messages.pop();
    saveFailure = false;
    await saveMode();
    assert.strictEqual(context.validateConfig({ wifi_ssid: '', wifi_failure_mode: 'offline' }).valid, false,
        'Continue offline fallback still requires an SSID for online operation');
    for (const name of Object.keys(elements)) delete elements[name];
    operatingRadios = [];
    stored = { operating_mode: 'offline', wifi_ssid: '', ap_password: '' };
    elements.wifi_ssid = { value: '', disabled: false };
    await context.loadConfig();
    await context.saveFragmentConfig(true);
    assert.deepStrictEqual(submitted, { wifi_ssid: '' }, 'WiFi fragment accepts empty SSID in saved Offline mode');
    delete elements.wifi_ssid;
    Object.assign(elements, sensorFields);
    stored = { ble_burst_count: 3, ble_adv_interval_ms: 100 };
    await context.loadConfig();
    await context.saveFragmentConfig(true);
    assert.deepStrictEqual(submitted, {ble_burst_count: 3, ble_adv_interval_ms: 100});
    assert.strictEqual(pendingReboot, true);
    messages.length = 0;
    const powerField = 'bluetooth_tx_power_dbm';
    const eventFields = ['bthome_event_burst_ms', 'bthome_event_adv_interval_ms'];
    const bluetoothFields = [powerField, ...eventFields];
    const sensorElements = {};
    for (const name of ['ble_burst_count', 'ble_adv_interval_ms']) {
        sensorElements[name] = elements[name];
        delete elements[name];
    }
    for (const name of bluetoothFields) elements[name] = { value: '', disabled: true };
    for (const id of ['bluetooth-power-settings', 'bthome-event-settings']) elements[id] = {disabled: true};
    for (const id of ['bluetooth-unavailable', 'bluetooth-power-pending', 'bluetooth-power-unavailable']) elements[id] = {hidden: true};
    const clicks = {};
    for (const id of ['bluetooth-power-save-btn', 'bthome-save-btn']) {
        elements[id] = {disabled: true, addEventListener(event, callback) { clicks[id] = callback; }};
    }
    stored = {caps: {ble: true, ble_hid: false, bthome_events: true, bluetooth_tx_power: true},
        bluetooth_tx_power_dbm: 9, bluetooth_active_tx_power_dbm: 9,
        bthome_event_burst_ms: 350, bthome_event_adv_interval_ms: 100};
    await context.window.init_bluetooth_fragment();
    assert.deepStrictEqual(context.window.__extra_config_fields, bluetoothFields);
    assert.strictEqual(elements['bluetooth-power-settings'].disabled, false);
    assert.strictEqual(elements['bthome-event-settings'].disabled, false);
    assert.strictEqual(elements['bluetooth-unavailable'].hidden, true);
    assert.strictEqual(elements['bluetooth-power-pending'].hidden, true);
    for (const power of powers) {
        for (const value of [power, String(power)]) {
            assert.strictEqual(context.validateBluetoothConfig({ bluetooth_tx_power_dbm: value }).valid, true);
        }
        elements.bluetooth_tx_power_dbm.value = String(power);
        elements.bthome_event_burst_ms.value = 'invalid unsaved timing';
        pendingReboot = false;
        await clicks['bluetooth-power-save-btn']();
        assert.deepStrictEqual(submitted, {bluetooth_tx_power_dbm: String(power)});
        assert.strictEqual(pendingReboot, power !== 9);
        assert.strictEqual(stored.bluetooth_active_tx_power_dbm, 9);
        await context.loadBluetoothConfig();
        assert.strictEqual(Number(elements.bluetooth_tx_power_dbm.value), power);
        assert.strictEqual(elements['bluetooth-power-pending'].hidden, power === 9);
        if (power === 9) assert.strictEqual(messages.pop().type, 'success');
        else assert.deepStrictEqual(messages, []);
    }

    for (const value of [-15, 12, -1, 1, 3.5, '', ' ', '3junk', 'NaN', ' 3', '+3', '1e2', null, false, {}, []]) {
        assert.strictEqual(context.validateBluetoothConfig({ bluetooth_tx_power_dbm: value }).valid, false, String(value));
    }
    assert.strictEqual(context.validateConfig({}).valid, true);
    assert.deepStrictEqual(messages, []);

    elements.bluetooth_tx_power_dbm.value = '1';
    submitted = null;
    pendingReboot = false;
    await clicks['bluetooth-power-save-btn']();
    assert.strictEqual(submitted, null);
    assert.strictEqual(pendingReboot, false);
    assert.strictEqual(messages.pop().type, 'error');
    elements.bthome_event_burst_ms.value = '900';
    elements.bthome_event_adv_interval_ms.value = '300';
    pendingReboot = false;
    await clicks['bthome-save-btn']();
    assert.deepStrictEqual(submitted, {bthome_event_burst_ms: '900', bthome_event_adv_interval_ms: '300'});
    assert.strictEqual(stored.bluetooth_tx_power_dbm, '9');
    assert.strictEqual(pendingReboot, false);
    assert.strictEqual(messages.pop().type, 'success');
    elements.bthome_event_burst_ms.value = '';
    await context.loadBluetoothConfig();
    assert.strictEqual(Number(elements.bthome_event_burst_ms.value), 900);
    assert.strictEqual(context.validateBluetoothConfig({bthome_event_burst_ms: 899}).valid, false);
    assert.strictEqual(context.validateBluetoothConfig({bthome_event_burst_ms: 1000}).valid, true);
    const full = {bthome_event_burst_ms: 100, bthome_event_adv_interval_ms: 20};
    assert.strictEqual(context.validateBluetoothConfig(full).valid, true);
    for (const key of eventFields) {
        for (const value of [null, false, true, [], {}, 3.5, '', ' ', '100junk', '+100', '1e2', '2147483648', undefined]) {
            assert.strictEqual(context.validateBluetoothConfig({...full, [key]: value}).valid, false, key + ':' + String(value));
        }
    }
    for (const caps of [{ble: false, ble_hid: true, bluetooth_tx_power: true},
        {ble: false, ble_hid: true, bluetooth_tx_power: false},
        {ble: false, ble_hid: false, bthome_events: false}]) {
        stored.caps = caps;
        await context.loadBluetoothConfig();
        assert.strictEqual(elements['bthome-save-btn'].disabled, !caps.ble_hid);
        assert.strictEqual(elements['bluetooth-power-save-btn'].disabled, !caps.bluetooth_tx_power);
        assert.strictEqual(elements['bluetooth-power-unavailable'].hidden, !caps.ble_hid || !!caps.bluetooth_tx_power);
        assert.strictEqual(elements['bluetooth-unavailable'].hidden, !!caps.ble_hid);
    }
    stored.caps = {bthome_events: true, ble: true, ble_hid: false, bluetooth_tx_power: true};
    await context.loadBluetoothConfig();
    elements.bthome_event_burst_ms.value = '1000';
    saveFailure = true;
    await clicks['bthome-save-btn']();
    assert.match(messages.pop().message, /NVS write failed/);
    assert.strictEqual(Number(stored.bthome_event_burst_ms), 900);
    assert.strictEqual(Number(context.window.deviceConfig.bthome_event_burst_ms), 900);
    assert.strictEqual(pendingReboot, false);
    elements.bluetooth_tx_power_dbm.value = '-3';
    await clicks['bluetooth-power-save-btn']();
    assert.match(messages.pop().message, /NVS write failed/);
    assert.strictEqual(Number(stored.bluetooth_tx_power_dbm), 9);
    assert.strictEqual(pendingReboot, false);
    saveFailure = false;
    await clicks['bluetooth-power-save-btn']();
    messages.pop();
    assert.strictEqual(pendingReboot, true);
    stored.bluetooth_active_tx_power_dbm = -3; // Mock a reboot.
    await context.loadBluetoothConfig();
    assert.strictEqual(elements['bluetooth-power-pending'].hidden, true);
    for (const name of [...bluetoothFields, 'bthome-event-settings', 'bluetooth-power-settings',
        'bluetooth-unavailable', 'bluetooth-power-pending', 'bluetooth-power-unavailable',
        'bthome-save-btn', 'bluetooth-power-save-btn']) delete elements[name];
    Object.assign(elements, sensorElements);
    await context.saveFragmentConfig(true);
    for (const name of bluetoothFields) assert.strictEqual(submitted[name], undefined);

    for (const id of ['keyboard-transport-choice', 'keyboard-save-bar', 'keyboard-active-transport', 'keyboard-active-heading', 'keyboard-pending', 'keyboard-status', 'ble-content', 'usb-content']) {
        elements[id] = { style: {}, textContent: '' };
    }
    elements['hid-save-btn'] = { disabled: true };
    elements['keyboard-device-name'] = { textContent: '' };
    for (const transport of ['none', 'usb', 'ble']) {
        elements['keyboard-transport-' + transport] = { value: transport, checked: false, disabled: true };
        elements['keyboard-transport-' + transport + '-label'] = { style: {} };
    }
    for (const caps of [{ble_hid: true, usb_hid: true}, {ble_hid: true, usb_hid: false}, {ble_hid: false, usb_hid: true}]) {
        stored = { device_name: 'Kitchen Pad', caps, keyboard_transport: caps.usb_hid ? 'usb' : 'ble', keyboard_active_transport: caps.usb_hid ? 'usb' : 'ble', keyboard_status: 'ready' };
        await context.loadConfig();
        assert.strictEqual(elements['keyboard-transport-choice'].style.display, '');
        assert.strictEqual(elements['keyboard-save-bar'].style.display, '');
        assert.strictEqual(elements['keyboard-transport-usb'].disabled, !caps.usb_hid);
        assert.strictEqual(elements['keyboard-transport-ble'].disabled, !caps.ble_hid);
        assert.strictEqual(elements['keyboard-transport-none'].disabled, false);
        assert.strictEqual(elements['keyboard-transport-none-label'].style.display, '');
        assert.strictEqual(elements['keyboard-transport-usb-label'].style.display, caps.usb_hid ? '' : 'none');
        assert.strictEqual(elements['keyboard-transport-ble-label'].style.display, caps.ble_hid ? '' : 'none');
        assert.strictEqual(elements['ble-content'].style.display, stored.keyboard_active_transport === 'ble' ? 'block' : 'none');
        assert.strictEqual(elements['keyboard-active-transport'].textContent, stored.keyboard_active_transport.toUpperCase());
        assert.strictEqual(elements['keyboard-status'].textContent, 'ready');
        assert.strictEqual(elements['keyboard-device-name'].textContent, 'Kitchen Pad USB');
        assert.strictEqual(elements['hid-save-btn'].disabled, true);
        pendingReboot = false;
        await context.saveFragmentConfig(true);
        assert.strictEqual(submitted.keyboard_transport, stored.keyboard_transport);
        assert.strictEqual(pendingReboot, true);
        messages.length = 0;
        stored.keyboard_transport = 'none';
        stored.keyboard_active_transport = 'none';
        stored.keyboard_status = 'disabled';
        await context.loadConfig();
        assert.strictEqual(elements['keyboard-transport-none'].checked, true);
        assert.strictEqual(elements['keyboard-active-heading'].textContent, 'Keyboard disabled');
        assert.strictEqual(elements['usb-content'].style.display, 'none');
        assert.strictEqual(elements['ble-content'].style.display, 'none');
        await context.saveFragmentConfig(true);
        assert.strictEqual(submitted.keyboard_transport, 'none');
        messages.length = 0;
    }

    stored = {caps: {usb_hid: true, ble_hid: true}, keyboard_transport: 'usb', keyboard_active_transport: 'usb', keyboard_status: 'ready'};
    await context.loadConfig();
    elements['keyboard-transport-usb'].checked = false;
    elements['keyboard-transport-ble'].checked = true;
    context.updateKeyboardTransportSetting();
    assert.strictEqual(elements['hid-save-btn'].disabled, false);
    assert.strictEqual(elements['keyboard-active-heading'].textContent, 'Active USB Connection');
    assert.strictEqual(elements['keyboard-pending'].style.display, 'none');
    await context.saveFragmentConfig(true);
    assert.strictEqual(elements['hid-save-btn'].disabled, true);
    assert.strictEqual(elements['keyboard-active-heading'].textContent, 'Active USB Connection');
    assert.strictEqual(elements['keyboard-pending'].textContent, 'BLE pending reboot');
    assert.strictEqual(elements['keyboard-pending'].style.display, '');
    await context.loadConfig();
    assert.strictEqual(elements['keyboard-pending'].textContent, 'BLE pending reboot');
    elements['keyboard-transport-ble'].checked = false;
    elements['keyboard-transport-none'].checked = true;
    context.updateKeyboardTransportSetting();
    assert.strictEqual(elements['hid-save-btn'].disabled, false);
    await context.saveFragmentConfig(true);
    assert.strictEqual(submitted.keyboard_transport, 'none');
    assert.strictEqual(elements['keyboard-pending'].textContent, 'Off pending reboot');
    assert.strictEqual(elements['keyboard-active-heading'].textContent, 'Active USB Connection');
    elements['keyboard_layout'] = {value: ''};
    stored.keyboard_layout = 'us';
    await context.loadConfig();
    assert.strictEqual(elements['keyboard_layout'].value, 'us');
    assert.strictEqual(elements['hid-save-btn'].disabled, true);
    elements['keyboard_layout'].value = 'us-intl';
    context.updateKeyboardTransportSetting();
    assert.strictEqual(elements['hid-save-btn'].disabled, false);
    await context.saveFragmentConfig(true);
    assert.strictEqual(submitted.keyboard_layout, 'us-intl');
    assert.strictEqual(elements['hid-save-btn'].disabled, true);
    await context.loadConfig();
    assert.strictEqual(elements['keyboard_layout'].value, 'us-intl');
    delete elements['keyboard_layout'];
    const hidFragment = fs.readFileSync('src/app/web/hid.fragment.html', 'utf8');
    assert.match(hidFragment, /<h2>.*Keyboard, Mouse &amp; Gamepad<\/h2>/);
    const hidComponent = fs.readFileSync('src/app/components/hid_component.cpp', 'utf8');
    assert.match(hidComponent, /REGISTER_NAV_COMPONENT\(hid, "hid", "connectivity", "Keyboard, Mouse & Gamepad", 20, "hid"\)/);
    assert.match(hidFragment, /aria-describedby="keyboard-transport-help"/);
    assert.match(hidFragment, /id="keyboard-transport-help">USB or BLE enables keyboard, mouse, and gamepad control\. Off disables all HID control\./);
    assert.match(hidFragment, /gap:12px/);
    assert.match(hidFragment, /<section[^>]*id="keyboard-transport-choice"/);
    assert.match(hidFragment, /<section[^>]*aria-labelledby="keyboard-active-heading"/);
    assert(!hidFragment.includes('border-top pt-3 mt-3'));
    assert.strictEqual(typeof context.window.init_hid_fragment, 'function');
    assert(!fs.existsSync('src/app/web/ble.fragment.html'));
    const renderArgs = ['tools/_render_html_template.py', '--web-dir', 'src/app/web',
        '--input', 'src/app/web/hid.fragment.html', '--project-name', 'esp32-macropad',
        '--project-display-name', 'ESP32 Macropad', '--firmware-version', 'test'];
    const withBle = execFileSync('python3', renderArgs, { encoding: 'utf8' });
    const withoutBle = execFileSync('python3', [...renderArgs, '--without-ble-hid'], { encoding: 'utf8' });
    for (const id of ['ble-content', 'ble-pair-btn', 'ble-badge-bonded', 'ble-peer-addr']) {
        assert(withBle.includes('id="' + id + '"'));
        assert(!withoutBle.includes('id="' + id + '"'));
    }
    assert(withoutBle.includes('id="keyboard-status"'));
    assert(withoutBle.includes('id="usb-content"'));
    assert(!withBle.includes('{{HID_BLE}}') && !withoutBle.includes('{{HID_BLE}}'));
    delete elements['ble-content'];
    stored = {caps: {usb_hid: true, ble_hid: false}, keyboard_transport: 'usb', keyboard_active_transport: 'usb', keyboard_status: 'ready'};
    await context.loadConfig();
    assert.strictEqual(elements['keyboard-active-heading'].textContent, 'Active USB Connection');

    const startup = fs.readFileSync('src/app/app.ino', 'utf8');
    const configApi = fs.readFileSync('src/app/web_portal_config.cpp', 'utf8');
    const capabilityMapStart = configApi.indexOf('JsonObject caps =');
    assert(capabilityMapStart >= 0);
    assert(configApi.indexOf('caps["bluetooth_tx_power"] = ble_backend_power_supported();') > capabilityMapStart,
        'Register Bluetooth power support in the final capability map, not a map that gets replaced');
    const logger = fs.readFileSync('src/app/log_manager.cpp', 'utf8');
    assert.match(logger, /Serial.begin\(baud\);/);
    assert.match(logger, /Serial.print\(line\);/);
    assert(!logger.includes('USBCDC'));
    const usbBackend = fs.readFileSync('src/app/usb_hid.cpp', 'utf8');
    assert.match(usbBackend, /kReportTimeoutMs = 5;/);
    const mouseBackend = usbBackend.slice(usbBackend.indexOf('bool usb_hid_send_mouse_report('),
        usbBackend.indexOf('void usb_hid_init('));
    assert.match(mouseBackend, /can_submit\([\s\S]*return tud_hid_n_report\(0, HID_REPORT_ID_MOUSE/);
    assert.match(mouseBackend, /HID_PROTOCOL_BOOT/);
    assert(!mouseBackend.includes('hid->SendReport'));
    assert.match(startup, /#if HAS_BLE_HID \|\| HAS_USB_HID\s*keyboard_hid_loop\(\);\s*#endif/);
    assert.match(usbBackend, /if \(enable_hid\) \{\s*static USBHID active_hid[\s\S]*static USBHIDKeyboard keyboard;[\s\S]*static USBHIDConsumerControl consumer;/);
    assert.match(startup, /usb_hid_init\(device_config.device_name, device_config.keyboard_transport == KeyboardTransport::Usb\);/);
    assert.match(startup, /const bool defer_wifi_init\s*=\s*boot_mode == PowerMode::Offline\s*\|\|\s*boot_mode == PowerMode::DutyCycleBle\s*\|\|\s*device_class_dispatch_defer_wifi_init\(&device_config, boot_mode\);\s*if \(!defer_wifi_init\) \{\s*wifi_manager_early_init\(\);/);
    assert.match(startup, /ble_backend_configure_power\(device_config.bluetooth_tx_power_dbm\)/);
    const storage = fs.readFileSync('src/app/config_manager.cpp', 'utf8');
    assert(!storage.includes('HAS_BLE_HID && HAS_USB_HID'));
    assert.match(storage, /#if HAS_BLE_HID \|\| HAS_USB_HID\s*#define KEY_KEYBOARD_TRANSPORT/);
    const loadStart = storage.indexOf('bool config_manager_load(DeviceConfig *config)');
    const preferencesBegin = storage.indexOf('preferences.begin(CONFIG_NAMESPACE, true)', loadStart);
    assert(loadStart >= 0 && preferencesBegin > loadStart);
    const earlyDefaults = storage.slice(loadStart, preferencesBegin);
    assert.match(earlyDefaults, /config->ble_burst_count = BLE_TELEMETRY_DEFAULT_BURST_COUNT;/);
    assert.match(earlyDefaults, /config->ble_adv_interval_ms = BLE_TELEMETRY_DEFAULT_ADV_INTERVAL_MS;/);
    assert.match(earlyDefaults, /config->keyboard_transport = keyboard_transport_default\(HAS_BLE_HID, HAS_USB_HID\);/);
    let sleepClick;
    elements['sleep-seconds'] = { value: '', disabled: false };
    elements['sleep-status'] = { textContent: '' };
    elements['sleep-now-btn'] = {
        disabled: true,
        addEventListener(event, callback) {
            assert.strictEqual(event, 'click');
            sleepClick = callback;
        }
    };
    stored.duty_cycle_wake_seconds = 90;
    const configFetch = context.fetch;
    let sleepRequest;
    let sleepAccepted = false;
    context.fetch = async function (url, options) {
        if (!url.startsWith('/api/sleep')) return configFetch(url, options);
        sleepRequest = { url, options };
        return {
            ok: sleepAccepted,
            json: async () => ({ success: sleepAccepted, message: 'Configuration write in progress' })
        };
    };
    context.window.init_mode_fragment();
    await context.loadConfig();
    assert.strictEqual(Number(elements['sleep-seconds'].value), 90);
    assert.strictEqual(elements['sleep-now-btn'].disabled, false);

    elements['sleep-seconds'].value = '0';
    await sleepClick();
    assert.strictEqual(sleepRequest, undefined);
    assert.strictEqual(messages.pop().type, 'error');
    elements['sleep-seconds'].value = '5';
    await sleepClick();
    assert.strictEqual(sleepRequest.url, '/api/sleep?seconds=5');
    assert.strictEqual(sleepRequest.options.method, 'POST');
    assert.strictEqual(elements['sleep-now-btn'].disabled, false);
    assert.match(messages.pop().message, /Configuration write in progress/);
    assert.strictEqual(healthPollingStopped, false);

    await context.loadConfig();
    assert.strictEqual(elements['sleep-seconds'].value, '5');
    sleepAccepted = true;
    submitted = null;
    pendingReboot = false;
    await sleepClick();
    assert.strictEqual(stored.duty_cycle_wake_seconds, 90);
    assert.strictEqual(submitted, null);
    assert.strictEqual(pendingReboot, false);
    assert.strictEqual(healthPollingStopped, true);
    assert.strictEqual(elements['sleep-now-btn'].disabled, true);
    assert.strictEqual(elements['sleep-seconds'].disabled, true);
    assert.match(elements['sleep-status'].textContent, /sleeping for 5 seconds/);
    console.log('portal_ble_config: PASS');
})().catch(function (error) {
    console.error(error);
    process.exitCode = 1;
});