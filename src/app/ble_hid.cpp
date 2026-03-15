#include "ble_hid.h"

#if HAS_BLE_HID

#include "key_sequence.h"

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEHIDDevice.h>
#include <BLESecurity.h>
#include <HIDKeyboardTypes.h>

// NimBLE headers redefine LOG_LEVEL_* as plain ints, colliding with our
// LogLevel enum.  Undefine them before including our logger.
#undef LOG_LEVEL_ERROR
#undef LOG_LEVEL_WARN
#undef LOG_LEVEL_INFO
#undef LOG_LEVEL_DEBUG
#undef LOG_LEVEL
#include "log_manager.h"

static const char* TAG = "BleHID";

// Minimal keyboard-only HID report descriptor.
//
// This deliberately removes consumer control for now so the HID report map,
// GATT Report Reference descriptor, and notification payload all describe the
// same thing: one keyboard input report with Report ID 1.
static const uint8_t HID_REPORT_MAP[] = {
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xa1, 0x01,        // Collection (Application)
    0x85, 0x01,        //   Report ID (1)

    // ---- Modifier byte (1 byte) ----
    0x05, 0x07,        //   Usage Page (Key Codes)
    0x19, 0xe0,        //   Usage Minimum (224) — Left Control
    0x29, 0xe7,        //   Usage Maximum (231) — Right GUI
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data, Variable, Absolute) — Modifier

    // ---- Reserved byte (1 byte) ----
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x01,        //   Input (Constant)

    // ---- Keyboard LEDs output (1 byte incl. padding) ----
    0x95, 0x05,        //   Report Count (5)
    0x75, 0x01,        //   Report Size (1)
    0x05, 0x08,        //   Usage Page (LEDs)
    0x19, 0x01,        //   Usage Minimum (1)
    0x29, 0x05,        //   Usage Maximum (5)
    0x91, 0x02,        //   Output (Data, Variable, Absolute)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x03,        //   Report Size (3)
    0x91, 0x01,        //   Output (Constant)

    // ---- Key array (6 bytes) ----
    0x95, 0x06,        //   Report Count (6)
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x65,        //   Logical Maximum (101)
    0x05, 0x07,        //   Usage Page (Key Codes)
    0x19, 0x00,        //   Usage Minimum (0)
    0x29, 0x65,        //   Usage Maximum (101)
    0x81, 0x00,        //   Input (Data, Array) — 6 keys

    0xc0,              // End Collection (Application)
};

// Report-mode packet is 8 bytes of keyboard data. Report ID 1 is conveyed by
// the Report Reference descriptor on the characteristic and the report map.
#define HID_REPORT_ID 1
#define HID_REPORT_SIZE 8
#define HID_BOOT_REPORT_SIZE 8

static BLEHIDDevice* hid            = nullptr;
static BLECharacteristic* inputChar = nullptr;
static BLECharacteristic* outputChar = nullptr;
static BLECharacteristic* bootInputChar = nullptr;
static BLEServer* bleServer         = nullptr;
static bool connected               = false;
static bool pairing_mode            = false;

// Deferred request state (set from LVGL task, processed in ble_hid_loop)
static volatile bool pending_pairing     = false;
static char pending_sequence[256]         = {};
static volatile bool has_pending_sequence = false;

// ============================================================================
// BLE Callbacks
// ============================================================================

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
            LOGI(TAG, "Paired (NimBLE encrypted=%d bonded=%d)",
                 desc->sec_state.encrypted, desc->sec_state.bonded);
            pairing_mode = false;
        } else {
            LOGW(TAG, "Auth complete — NOT encrypted");
        }
    }
#endif
};

// ============================================================================
// Bond helpers
// ============================================================================

static void clear_all_bonds() {
#if defined(CONFIG_BLUEDROID_ENABLED)
    int count = esp_ble_get_bond_device_num();
    if (count > 0) {
        esp_ble_bond_dev_t* devs = (esp_ble_bond_dev_t*)malloc(count * sizeof(esp_ble_bond_dev_t));
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

// ============================================================================
// Public API
// ============================================================================

void ble_hid_init(const char* device_name) {
    LOGI(TAG, "Initializing BLE HID keyboard as '%s'", device_name);

    BLEDevice::init(device_name);
    BLEDevice::setSecurityCallbacks(new HidSecurityCallbacks());

    bleServer = BLEDevice::createServer();
    bleServer->setCallbacks(new HidCallbacks());

    hid = new BLEHIDDevice(bleServer);
    inputChar = hid->inputReport(1);
    outputChar = hid->outputReport(1);
    bootInputChar = hid->bootInput();
    hid->bootOutput();

    if (outputChar) {
        outputChar->setCallbacks(new KeyboardOutputCallbacks());
    }

    hid->manufacturer()->setValue(device_name);
    hid->pnp(0x02, 0x02E5, 0xA111, 0x0210);
    hid->hidInfo(0x00, 0x01);
    hid->reportMap((uint8_t*)HID_REPORT_MAP, sizeof(HID_REPORT_MAP));
    hid->setBatteryLevel(100);

    hid->startServices();

    // Security: bonding + encryption, "Just Works" (no IO capability)
    BLESecurity* security = new BLESecurity();
    security->setAuthenticationMode(true, false, true);  // bonding=ON, MITM=OFF, SC=ON
    security->setCapability(ESP_IO_CAP_NONE);
    security->setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    security->setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

    // Start advertising
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
    LOGI(TAG, "Pairing mode — discoverable advertising started");
}

bool ble_hid_is_connected() {
    return connected;
}

void ble_hid_send_key(uint16_t usage, uint8_t modifiers) {
    if (!connected || !inputChar) {
        LOGD(TAG, "Not connected — key 0x%04X dropped", usage);
        return;
    }

    uint8_t report[HID_REPORT_SIZE] = {};
    uint8_t bootReport[HID_BOOT_REPORT_SIZE] = {};

    report[0] = modifiers;
    report[2] = (uint8_t)(usage & 0xFF);

    bootReport[0] = modifiers;
    bootReport[2] = (uint8_t)(usage & 0xFF);

    inputChar->setValue(report, sizeof(report));
    inputChar->notify();
    if (bootInputChar) {
        bootInputChar->setValue(bootReport, sizeof(bootReport));
        bootInputChar->notify();
    }

    delay(KS_DEFAULT_DELAY_MS);

    memset(report, 0, sizeof(report));
    memset(bootReport, 0, sizeof(bootReport));
    inputChar->setValue(report, sizeof(report));
    inputChar->notify();
    if (bootInputChar) {
        bootInputChar->setValue(bootReport, sizeof(bootReport));
        bootInputChar->notify();
    }
}

void ble_hid_send_consumer(uint16_t usage) {
    (void)usage;
    LOGW(TAG, "Consumer control is temporarily disabled in keyboard-only BLE HID mode");
}

void ble_hid_execute_sequence(const char* sequence) {
    if (!sequence || !sequence[0]) return;

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

        case KS_STEP_TEXT: {
            // Type each character using ASCII → HID mapping
            for (uint16_t c = 0; c < step.text.length; c++) {
                char ch = step.text.start[c];
                // Handle \" escape inside text literals
                if (ch == '\\' && c + 1 < step.text.length && step.text.start[c + 1] == '"') {
                    ch = '"';
                    c++; // skip the escaped char
                }
                uint16_t usage;
                uint8_t mods;
                if (ks_ascii_to_hid(ch, &usage, &mods)) {
                    ble_hid_send_key(usage, mods);
                }
            }
            break;
        }

        case KS_STEP_DELAY:
            delay(step.delay.ms);
            break;
        }

        // Inter-step delay (unless the step itself was a delay)
        if (step.type != KS_STEP_DELAY && i + 1 < seq.count) {
            // Only add inter-step delay if the next step isn't an explicit delay
            if (seq.steps[i + 1].type != KS_STEP_DELAY) {
                delay(KS_DEFAULT_DELAY_MS);
            }
        }
    }
}

void ble_hid_request_pairing() {
    pending_pairing = true;
}

void ble_hid_request_sequence(const char* sequence) {
    if (!sequence || !sequence[0]) return;
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
