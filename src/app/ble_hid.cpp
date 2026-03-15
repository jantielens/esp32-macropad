#include "ble_hid.h"

#if HAS_BLE_HID

#include "key_sequence.h"

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEHIDDevice.h>
#include <BLESecurity.h>
#include <HIDTypes.h>

// NimBLE headers redefine LOG_LEVEL_* as plain ints, colliding with our
// LogLevel enum. Undefine them before including our logger.
#undef LOG_LEVEL_ERROR
#undef LOG_LEVEL_WARN
#undef LOG_LEVEL_INFO
#undef LOG_LEVEL_DEBUG
#undef LOG_LEVEL
#include "log_manager.h"

static const char* TAG = "BleHID";

#define KEYBOARD_ID 0x01
typedef struct {
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t keys[6];
} KeyReport;

static const uint8_t HID_REPORT_MAP[] = {
    USAGE_PAGE(1),      0x01,
    USAGE(1),           0x06,
    COLLECTION(1),      0x01,
    REPORT_ID(1),       KEYBOARD_ID,
    USAGE_PAGE(1),      0x07,
    USAGE_MINIMUM(1),   0xE0,
    USAGE_MAXIMUM(1),   0xE7,
    LOGICAL_MINIMUM(1), 0x00,
    LOGICAL_MAXIMUM(1), 0x01,
    REPORT_SIZE(1),     0x01,
    REPORT_COUNT(1),    0x08,
    HIDINPUT(1),        0x02,
    REPORT_COUNT(1),    0x01,
    REPORT_SIZE(1),     0x08,
    HIDINPUT(1),        0x01,
    REPORT_COUNT(1),    0x05,
    REPORT_SIZE(1),     0x01,
    USAGE_PAGE(1),      0x08,
    USAGE_MINIMUM(1),   0x01,
    USAGE_MAXIMUM(1),   0x05,
    HIDOUTPUT(1),       0x02,
    REPORT_COUNT(1),    0x01,
    REPORT_SIZE(1),     0x03,
    HIDOUTPUT(1),       0x01,
    REPORT_COUNT(1),    0x06,
    REPORT_SIZE(1),     0x08,
    LOGICAL_MINIMUM(1), 0x00,
    LOGICAL_MAXIMUM(1), 0x65,
    USAGE_PAGE(1),      0x07,
    USAGE_MINIMUM(1),   0x00,
    USAGE_MAXIMUM(1),   0x65,
    HIDINPUT(1),        0x00,
    END_COLLECTION(0)
};

static BLEHIDDevice* hid = nullptr;
static BLECharacteristic* inputKeyboard = nullptr;
static BLECharacteristic* outputKeyboard = nullptr;
static BLEServer* bleServer = nullptr;
static bool connected = false;
static bool pairing_mode = false;

static volatile bool pending_pairing = false;
static char pending_sequence[256] = {};
static volatile bool has_pending_sequence = false;

class HidCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer*) override {
        connected = true;
        LOGI(TAG, "Host connected");
    }

    void onDisconnect(BLEServer*) override {
        connected = false;
        LOGI(TAG, "Host disconnected");
        BLEDevice::startAdvertising();
    }
};

class KeyboardOutputCallbacks : public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic* characteristic) override {
        const String value = characteristic->getValue();
        if (value.length() > 0) {
            const uint8_t leds = static_cast<uint8_t>(value[0]);
            LOGI(TAG, "Keyboard LED output=0x%02X", leds);
        } else {
            LOGI(TAG, "Keyboard LED output cleared");
        }
    }
};

class HidSecurityCallbacks : public BLESecurityCallbacks {
    uint32_t onPassKeyRequest() override { return 0; }
    void onPassKeyNotify(uint32_t) override {}
    bool onConfirmPIN(uint32_t) override { return true; }
    bool onSecurityRequest() override { return true; }

#if defined(CONFIG_BLUEDROID_ENABLED)
    void onAuthenticationComplete(esp_ble_auth_cmpl_t auth) override {
        if (auth.success) {
            LOGI(TAG, "Paired (Bluedroid mode=%d)", auth.auth_mode);
            pairing_mode = false;
        } else {
            LOGW(TAG, "Pairing FAILED (reason=0x%02X)", auth.fail_reason);
        }
    }
#elif defined(CONFIG_NIMBLE_ENABLED)
    void onAuthenticationComplete(ble_gap_conn_desc* desc) override {
        if (desc->sec_state.encrypted) {
            LOGI(TAG, "Paired (encrypted=%d bonded=%d)",
                 desc->sec_state.encrypted,
                 desc->sec_state.bonded);
            pairing_mode = false;
            return;
        }

        LOGW(TAG, "Auth complete - NOT encrypted");
    }
#endif
};

static void clear_all_bonds() {
#if defined(CONFIG_BLUEDROID_ENABLED)
    int count = esp_ble_get_bond_device_num();
    if (count > 0) {
        esp_ble_bond_dev_t* devs = static_cast<esp_ble_bond_dev_t*>(malloc(count * sizeof(esp_ble_bond_dev_t)));
        if (devs) {
            esp_ble_get_bond_device_list(&count, devs);
            for (int i = 0; i < count; i++) {
                esp_ble_remove_bond_device(devs[i].bd_addr);
            }
            free(devs);
        }
    }
    LOGI(TAG, "Cleared %d bonded device(s)", count);
#elif defined(CONFIG_NIMBLE_ENABLED)
    ble_store_clear();
    LOGI(TAG, "Cleared all NimBLE bonds");
#endif
}

static void send_keyboard_report(const KeyReport& report) {
    if (!connected || !inputKeyboard) {
        return;
    }

    inputKeyboard->setValue(reinterpret_cast<const uint8_t*>(&report), sizeof(report));
    inputKeyboard->notify();
}

void ble_hid_init(const char* device_name) {
    LOGI(TAG, "Initializing BLE HID keyboard as '%s'", device_name);

    BLEDevice::init(device_name);
    BLEDevice::setSecurityCallbacks(new HidSecurityCallbacks());

    bleServer = BLEDevice::createServer();
    bleServer->setCallbacks(new HidCallbacks());

    hid = new BLEHIDDevice(bleServer);
    inputKeyboard = hid->inputReport(KEYBOARD_ID);
    outputKeyboard = hid->outputReport(KEYBOARD_ID);

    if (outputKeyboard) {
        outputKeyboard->setCallbacks(new KeyboardOutputCallbacks());
    }

    BLECharacteristic* manufacturerChar = hid->manufacturer();
    if (manufacturerChar) {
        manufacturerChar->setValue("Espressif");
    }
    hid->pnp(0x02, 0xE502, 0xA111, 0x0210);
    hid->hidInfo(0x00, 0x01);
    hid->reportMap(const_cast<uint8_t*>(HID_REPORT_MAP), sizeof(HID_REPORT_MAP));
    hid->startServices();
    hid->setBatteryLevel(100);

    BLESecurity* security = new BLESecurity();
    security->setAuthenticationMode(true, true, false);
    security->setCapability(ESP_IO_CAP_NONE);
    security->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    security->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->setAppearance(HID_KEYBOARD);
    adv->addServiceUUID(hid->hidService()->getUUID());
    adv->setScanResponse(true);
    adv->setMinPreferred(0x06);
    adv->setMaxPreferred(0x12);
    BLEDevice::startAdvertising();

    LOGI(TAG, "BLE HID ready, advertising started");
}

void ble_hid_start_pairing() {
    LOGI(TAG, "Starting BLE pairing mode");

    if (connected && bleServer) {
        bleServer->disconnect(bleServer->getConnId());
        delay(100);
    }

    clear_all_bonds();
    pairing_mode = true;
    BLEDevice::startAdvertising();
    LOGI(TAG, "Pairing mode - discoverable advertising started");
}

bool ble_hid_is_connected() {
    return connected;
}

void ble_hid_send_key(uint16_t usage, uint8_t modifiers) {
    if (!connected) {
        LOGD(TAG, "Not connected - key 0x%04X dropped", usage);
        return;
    }

    KeyReport report = {};
    report.modifiers = modifiers;
    report.keys[0] = static_cast<uint8_t>(usage & 0xFF);
    send_keyboard_report(report);

    delay(KS_DEFAULT_DELAY_MS);

    KeyReport released = {};
    send_keyboard_report(released);
}

void ble_hid_send_consumer(uint16_t usage) {
    (void)usage;
    LOGW(TAG, "Consumer control is temporarily disabled: current ESP32 BLE HID wrapper crashes on a second input report characteristic");
}

void ble_hid_execute_sequence(const char* sequence) {
    if (!sequence || !sequence[0]) {
        return;
    }

    KsSequence seq;
    if (!ks_parse(sequence, &seq)) {
        LOGW(TAG, "Parse error: %s", seq.error);
        return;
    }

    for (uint8_t i = 0; i < seq.count; i++) {
        const KsStep& step = seq.steps[i];

        switch (step.type) {
        case KS_STEP_KEY:
            if (step.key.usage_type == KS_USAGE_CONSUMER) {
                ble_hid_send_consumer(step.key.usage);
            } else {
                ble_hid_send_key(step.key.usage, step.key.modifiers);
            }
            break;

        case KS_STEP_TEXT:
            for (uint16_t c = 0; c < step.text.length; c++) {
                char ch = step.text.start[c];
                if (ch == '\\' && c + 1 < step.text.length && step.text.start[c + 1] == '"') {
                    ch = '"';
                    c++;
                }

                uint16_t usage;
                uint8_t mods;
                if (ks_ascii_to_hid(ch, &usage, &mods)) {
                    ble_hid_send_key(usage, mods);
                }
            }
            break;

        case KS_STEP_DELAY:
            delay(step.delay.ms);
            break;
        }

        if (step.type != KS_STEP_DELAY && i + 1 < seq.count && seq.steps[i + 1].type != KS_STEP_DELAY) {
            delay(KS_DEFAULT_DELAY_MS);
        }
    }
}

void ble_hid_request_pairing() {
    pending_pairing = true;
}

void ble_hid_request_sequence(const char* sequence) {
    if (!sequence || !sequence[0]) {
        return;
    }

    strlcpy(pending_sequence, sequence, sizeof(pending_sequence));
    has_pending_sequence = true;
}

void ble_hid_loop() {
    if (pending_pairing) {
        pending_pairing = false;
        ble_hid_start_pairing();
    }

    if (has_pending_sequence) {
        has_pending_sequence = false;
        ble_hid_execute_sequence(pending_sequence);
        pending_sequence[0] = '\0';
    }
}

#endif // HAS_BLE_HID