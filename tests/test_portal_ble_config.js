const assert = require('assert');
const fs = require('fs');
const vm = require('vm');
const { execFileSync } = require('child_process');

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
        querySelectorAll(selector) {
            return selector === 'input[type="radio"][name="keyboard_transport"]' ?
                ['none', 'usb', 'ble'].map(transport => elements['keyboard-transport-' + transport]).filter(Boolean) : [];
        },
        querySelector(selector) {
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
    const hidFragment = fs.readFileSync('src/app/web/hid.fragment.html', 'utf8');
    assert.match(hidFragment, /<h2>.*Keyboard &amp; Mouse<\/h2>/);
    const hidComponent = fs.readFileSync('src/app/components/hid_component.cpp', 'utf8');
    assert.match(hidComponent, /REGISTER_NAV_COMPONENT\(hid, "hid", "connectivity", "Keyboard & Mouse", 20, "hid"\)/);
    assert.match(hidFragment, /aria-describedby="keyboard-transport-help"/);
    assert.match(hidFragment, /id="keyboard-transport-help">USB enables keyboard and mouse control\. BLE enables keyboard control only\. Off disables keyboard and mouse control\./);
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
    const logger = fs.readFileSync('src/app/log_manager.cpp', 'utf8');
    assert.match(logger, /#include "board_config.h"[\s\S]*#if HAS_USB_HID/);
    assert.match(logger, /static USBCDC usb_diagnostics;/);
    assert.match(logger, /usb_diagnostics.begin\(baud\);/);
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
    assert.match(startup, /const bool defer_wifi_init\s*=\s*boot_mode == PowerMode::DutyCycleBle\s*\|\|\s*device_class_dispatch_defer_wifi_init\(&device_config, boot_mode\);\s*if \(!defer_wifi_init\) \{\s*wifi_manager_early_init\(\);/);
    assert.match(startup, /ble_telemetry_init\(device_config.device_name, device_config.ble_tx_power_dbm\)/);
    const storage = fs.readFileSync('src/app/config_manager.cpp', 'utf8');
    assert(!storage.includes('HAS_BLE_HID && HAS_USB_HID'));
    assert.match(storage, /#if HAS_BLE_HID \|\| HAS_USB_HID\s*#define KEY_KEYBOARD_TRANSPORT/);
    const loadStart = storage.indexOf('bool config_manager_load(DeviceConfig *config)');
    const preferencesBegin = storage.indexOf('preferences.begin(CONFIG_NAMESPACE, true)', loadStart);
    assert(loadStart >= 0 && preferencesBegin > loadStart);
    const earlyDefaults = storage.slice(loadStart, preferencesBegin);
    assert.match(earlyDefaults, /config->ble_burst_count = BLE_TELEMETRY_DEFAULT_BURST_COUNT;/);
    assert.match(earlyDefaults, /config->ble_adv_interval_ms = BLE_TELEMETRY_DEFAULT_ADV_INTERVAL_MS;/);
    assert.match(earlyDefaults, /config->ble_tx_power_dbm = CONFIG_DEFAULT_BLE_TX_POWER_DBM;/);
    assert.match(earlyDefaults, /config->keyboard_transport = keyboard_transport_default\(HAS_BLE_HID, HAS_USB_HID\);/);
    assert.match(storage, /config->ble_tx_power_dbm = preferences.getChar\(KEY_BLE_TX_POWER_DBM, CONFIG_DEFAULT_BLE_TX_POWER_DBM\);/);
    assert.match(storage, /putChar\(KEY_BLE_TX_POWER_DBM, config->ble_tx_power_dbm\)/);
    console.log('portal_ble_config: PASS');
})().catch(function (error) {
    console.error(error);
    process.exitCode = 1;
});