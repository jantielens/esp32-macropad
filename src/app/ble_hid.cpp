#include "ble_hid.h"

#if HAS_BLE_HID

#include "config_manager.h"
#include "key_sequence.h"

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLESecurity.h>
#include <HIDTypes.h>
#include <esp_system.h>
#include <host/ble_att.h>
#include <host/ble_gatt.h>
#include <host/ble_hs_mbuf.h>
#include <host/ble_store.h>
#include <host/ble_uuid.h>
#include <os/os_mbuf.h>

// NimBLE headers redefine LOG_LEVEL_* as plain ints, colliding with our
// LogLevel enum. Undefine them before including our logger.
#undef LOG_LEVEL_ERROR
#undef LOG_LEVEL_WARN
#undef LOG_LEVEL_INFO
#undef LOG_LEVEL_DEBUG
#undef LOG_LEVEL
#include "log_manager.h"

static const char* TAG = "BleHID";

#define HID_KEYBOARD 0x03C1
#define KEYBOARD_ID 0x01
#define MEDIA_KEYS_ID 0x02

typedef struct {
    uint8_t modifiers;
    uint8_t reserved;
    uint8_t keys[6];
} KeyReport;

typedef struct {
    uint16_t usage;
} ConsumerReport;

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
    END_COLLECTION(0),

    USAGE_PAGE(1),      0x0C,
    USAGE(1),           0x01,
    COLLECTION(1),      0x01,
    REPORT_ID(1),       MEDIA_KEYS_ID,
    LOGICAL_MINIMUM(1), 0x00,
    LOGICAL_MAXIMUM(2), 0xFF, 0x03,
    USAGE_MINIMUM(1),   0x00,
    USAGE_MAXIMUM(2),   0xFF, 0x03,
    REPORT_SIZE(1),     0x10,
    REPORT_COUNT(1),    0x01,
    HIDINPUT(1),        0x00,
    END_COLLECTION(0)
};

static BLEServer* bleServer = nullptr;
static volatile bool connected = false;
static volatile bool pairing_mode = false;
static volatile bool hid_service_ready = false;
static volatile bool advertising_active = false;
static volatile bool init_error = false;
static volatile bool owner_claimed = false;

static uint8_t protocolMode = 0x01;
static uint8_t keyboardLedState = 0x00;
static uint8_t hidControlPoint = 0x00;
static const uint8_t hidInfoData[] = {0x11, 0x01, 0x00, 0x01};
static KeyReport keyboardInputReport = {};
static ConsumerReport consumerInputReport = {};

static uint16_t protocolModeHandle = 0;
static uint16_t hidInfoHandle = 0;
static uint16_t hidControlHandle = 0;
static uint16_t reportMapHandle = 0;
static uint16_t keyboardInputHandle = 0;
static uint16_t keyboardOutputHandle = 0;
static uint16_t consumerInputHandle = 0;

static uint8_t protocolModeToken = 0;
static uint8_t hidInfoToken = 0;
static uint8_t hidControlToken = 0;
static uint8_t reportMapToken = 0;
static uint8_t keyboardInputToken = 0;
static uint8_t keyboardOutputToken = 0;
static uint8_t consumerInputToken = 0;
static uint8_t keyboardInputRefToken = 0;
static uint8_t keyboardOutputRefToken = 0;
static uint8_t consumerInputRefToken = 0;

static const uint8_t keyboardInputReportRef[] = {KEYBOARD_ID, 0x01};
static const uint8_t keyboardOutputReportRef[] = {KEYBOARD_ID, 0x02};
static const uint8_t consumerInputReportRef[] = {MEDIA_KEYS_ID, 0x01};

static const ble_uuid16_t uuidHidService = BLE_UUID16_INIT(0x1812);
static const ble_uuid16_t uuidProtocolMode = BLE_UUID16_INIT(0x2A4E);
static const ble_uuid16_t uuidHidInfo = BLE_UUID16_INIT(0x2A4A);
static const ble_uuid16_t uuidHidControlPoint = BLE_UUID16_INIT(0x2A4C);
static const ble_uuid16_t uuidReportMap = BLE_UUID16_INIT(0x2A4B);
static const ble_uuid16_t uuidReport = BLE_UUID16_INIT(0x2A4D);
static const ble_uuid16_t uuidReportReference = BLE_UUID16_INIT(0x2908);

static volatile bool pending_pairing = false;
static char pending_sequence[256] = {};
static volatile bool has_pending_sequence = false;

// Single-owner BLE policy state
static const unsigned long PAIRING_TIMEOUT_MS = 60000;  // 60s pairing window
static volatile unsigned long pairing_deadline = 0;
static volatile uint16_t active_conn_handle = 0xFFFF;

// Peer metadata (captured on auth complete)
static char peer_addr_str[18] = {};
static char peer_id_addr_str[18] = {};
static char ble_name_str[40] = {};
static char device_base_name[40] = {};
static volatile bool peer_bonded = false;
static volatile bool peer_encrypted = false;

static void build_ble_name(const char* base_name, const char* identity_addr, char* out, size_t out_len) {
    if (out == nullptr || out_len == 0) return;

    const char* base = (base_name && base_name[0]) ? base_name : "BLE Keyboard";
    if (identity_addr == nullptr || identity_addr[0] == '\0') {
        strlcpy(out, base, out_len);
        return;
    }

    char compact_addr[13] = {};
    size_t compact_len = 0;
    const size_t addr_len = strlen(identity_addr);
    for (size_t i = 0; i < addr_len && compact_len < sizeof(compact_addr) - 1; ++i) {
        const char c = identity_addr[i];
        if (c == ':') continue;
        compact_addr[compact_len++] = c;
    }
    compact_addr[compact_len] = '\0';

    if (compact_len < 4) {
        strlcpy(out, base, out_len);
        return;
    }

    const char* suffix = compact_addr + (compact_len - 4);

    snprintf(out, out_len, "%s %s", base, suffix);
}

static bool load_identity_address(BLEAddress* out_addr) {
    if (out_addr == nullptr) return false;

    char stored_addr[18] = {};
    if (!config_manager_get_ble_identity_addr(stored_addr, sizeof(stored_addr))) {
        return false;
    }

    *out_addr = BLEAddress(String(stored_addr), BLE_ADDR_RANDOM);
    return true;
}

static bool load_identity_address_string(char* out, size_t out_len) {
    if (out == nullptr || out_len == 0) return false;
    return config_manager_get_ble_identity_addr(out, out_len);
}

static BLEAddress generate_random_static_identity_address() {
    uint8_t addr[6];
    for (size_t i = 0; i < sizeof(addr); ++i) {
        addr[i] = static_cast<uint8_t>(esp_random() & 0xFF);
    }

    // Static random BLE identity: top two bits of the most significant octet must be 1.
    addr[0] &= 0x3F;
    addr[0] |= 0xC0;

    return BLEAddress(addr, BLE_ADDR_RANDOM);
}

static bool persist_new_identity_address(BLEAddress* out_addr) {
    if (out_addr == nullptr) return false;

    BLEAddress next_addr = generate_random_static_identity_address();
    const String next_addr_str = next_addr.toString();
    if (!config_manager_set_ble_identity_addr(next_addr_str.c_str())) {
        LOGE(TAG, "Failed to persist rotated BLE identity");
        return false;
    }

    *out_addr = next_addr;
    LOGI(TAG, "Rotated BLE identity to %s", next_addr_str.c_str());
    return true;
}

static void configure_identity_address() {
    BLEAddress identity_addr;
    if (!load_identity_address(&identity_addr)) {
        LOGI(TAG, "Using default BLE identity address");
        return;
    }

    if (!BLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM)) {
        LOGW(TAG, "Failed to switch BLE stack to random identity address");
        return;
    }

    if (!BLEDevice::setOwnAddr(identity_addr)) {
        LOGW(TAG, "Failed to apply stored BLE identity address");
        return;
    }

    LOGI(TAG, "Using BLE identity address %s", identity_addr.toString().c_str());
}

static bool start_advertising() {
    if (!hid_service_ready) return false;
    BLEDevice::startAdvertising();
    advertising_active = true;
    return advertising_active;
}

static void format_ble_addr(const uint8_t addr[6], char* out, size_t out_len) {
    snprintf(out, out_len, "%02X:%02X:%02X:%02X:%02X:%02X",
             addr[5], addr[4], addr[3], addr[2], addr[1], addr[0]);
}

static void clear_peer_metadata() {
    peer_addr_str[0] = '\0';
    peer_id_addr_str[0] = '\0';
    peer_bonded = false;
    peer_encrypted = false;
}

static int hid_access(uint16_t conn_handle, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt, void* arg);

static ble_gatt_dsc_def keyboardInputDescriptors[] = {
    {&uuidReportReference.u, BLE_ATT_F_READ, 0, hid_access, &keyboardInputRefToken},
    {nullptr, 0, 0, nullptr, nullptr},
};

static ble_gatt_dsc_def keyboardOutputDescriptors[] = {
    {&uuidReportReference.u, BLE_ATT_F_READ, 0, hid_access, &keyboardOutputRefToken},
    {nullptr, 0, 0, nullptr, nullptr},
};

static ble_gatt_dsc_def consumerInputDescriptors[] = {
    {&uuidReportReference.u, BLE_ATT_F_READ, 0, hid_access, &consumerInputRefToken},
    {nullptr, 0, 0, nullptr, nullptr},
};

static ble_gatt_chr_def hidCharacteristics[] = {
    {&uuidProtocolMode.u, hid_access, &protocolModeToken, nullptr,
     BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE_NO_RSP, 0, &protocolModeHandle, nullptr},
    {&uuidHidInfo.u, hid_access, &hidInfoToken, nullptr,
     BLE_GATT_CHR_F_READ, 0, &hidInfoHandle, nullptr},
    {&uuidHidControlPoint.u, hid_access, &hidControlToken, nullptr,
     BLE_GATT_CHR_F_WRITE_NO_RSP, 0, &hidControlHandle, nullptr},
    {&uuidReportMap.u, hid_access, &reportMapToken, nullptr,
     BLE_GATT_CHR_F_READ, 0, &reportMapHandle, nullptr},
    {&uuidReport.u, hid_access, &keyboardInputToken, keyboardInputDescriptors,
     BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, 0, &keyboardInputHandle, nullptr},
    {&uuidReport.u, hid_access, &keyboardOutputToken, keyboardOutputDescriptors,
     BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_WRITE | BLE_GATT_CHR_F_WRITE_NO_RSP, 0, &keyboardOutputHandle, nullptr},
    {&uuidReport.u, hid_access, &consumerInputToken, consumerInputDescriptors,
     BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY, 0, &consumerInputHandle, nullptr},
    {nullptr, nullptr, nullptr, nullptr, 0, 0, nullptr, nullptr},
};

static const ble_gatt_svc_def hidServices[] = {
    {BLE_GATT_SVC_TYPE_PRIMARY, &uuidHidService.u, nullptr, hidCharacteristics},
    {0, nullptr, nullptr, nullptr},
};

static int append_flat_value(struct os_mbuf* om, const void* data, uint16_t len) {
    return os_mbuf_append(om, data, len) == 0 ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
}

static int copy_write_value(const struct os_mbuf* om, void* data, uint16_t max_len, uint16_t* out_len) {
    const int rc = ble_hs_mbuf_to_flat(om, data, max_len, out_len);
    return rc == 0 ? 0 : BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
}

static int hid_access(uint16_t, uint16_t attr_handle, struct ble_gatt_access_ctxt* ctxt, void* arg) {
    uint16_t out_len = 0;

    if (arg == &protocolModeToken) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            return append_flat_value(ctxt->om, &protocolMode, sizeof(protocolMode));
        }

        uint8_t value = 0;
        const int rc = copy_write_value(ctxt->om, &value, sizeof(value), &out_len);
        if (rc != 0 || out_len != sizeof(value)) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        if (value > 0x01) {
            return BLE_ATT_ERR_UNLIKELY;
        }
        protocolMode = value;
        LOGI(TAG, "Protocol mode=%u", protocolMode);
        return 0;
    }

    if (arg == &hidInfoToken) {
        return append_flat_value(ctxt->om, hidInfoData, sizeof(hidInfoData));
    }

    if (arg == &hidControlToken) {
        uint8_t value = 0;
        const int rc = copy_write_value(ctxt->om, &value, sizeof(value), &out_len);
        if (rc != 0 || out_len != sizeof(value)) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        hidControlPoint = value;
        LOGI(TAG, "HID control point=0x%02X", hidControlPoint);
        return 0;
    }

    if (arg == &reportMapToken) {
        return append_flat_value(ctxt->om, HID_REPORT_MAP, sizeof(HID_REPORT_MAP));
    }

    if (arg == &keyboardInputToken) {
        return append_flat_value(ctxt->om, &keyboardInputReport, sizeof(keyboardInputReport));
    }

    if (arg == &keyboardOutputToken) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            return append_flat_value(ctxt->om, &keyboardLedState, sizeof(keyboardLedState));
        }

        uint8_t value = 0;
        const int rc = copy_write_value(ctxt->om, &value, sizeof(value), &out_len);
        if (rc != 0 || out_len != sizeof(value)) {
            return BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN;
        }
        keyboardLedState = value;
        LOGI(TAG, "Keyboard LED output=0x%02X", keyboardLedState);
        return 0;
    }

    if (arg == &consumerInputToken) {
        return append_flat_value(ctxt->om, &consumerInputReport, sizeof(consumerInputReport));
    }

    if (arg == &keyboardInputRefToken) {
        return append_flat_value(ctxt->om, keyboardInputReportRef, sizeof(keyboardInputReportRef));
    }

    if (arg == &keyboardOutputRefToken) {
        return append_flat_value(ctxt->om, keyboardOutputReportRef, sizeof(keyboardOutputReportRef));
    }

    if (arg == &consumerInputRefToken) {
        return append_flat_value(ctxt->om, consumerInputReportRef, sizeof(consumerInputReportRef));
    }

    LOGW(TAG, "Unhandled HID access op=%u handle=%u", ctxt->op, attr_handle);
    return BLE_ATT_ERR_UNLIKELY;
}

static bool init_hid_service() {
    const int count_rc = ble_gatts_count_cfg(hidServices);
    if (count_rc != 0) {
        LOGE(TAG, "ble_gatts_count_cfg failed: %d", count_rc);
        return false;
    }

    const int add_rc = ble_gatts_add_svcs(hidServices);
    if (add_rc != 0) {
        LOGE(TAG, "ble_gatts_add_svcs failed: %d", add_rc);
        return false;
    }

    return true;
}

static void send_report_notification(uint16_t value_handle, const void* data, uint16_t size) {
    if (!connected || !hid_service_ready || value_handle == 0 || bleServer == nullptr) {
        return;
    }

    struct os_mbuf* om = ble_hs_mbuf_from_flat(data, size);
    if (om == nullptr) {
        LOGW(TAG, "Failed to allocate HID report mbuf (handle=%u size=%u)", value_handle, size);
        return;
    }

    const int rc = ble_gatts_notify_custom(bleServer->getConnId(), value_handle, om);
    if (rc != 0) {
        LOGW(TAG, "ble_gatts_notify_custom failed (handle=%u rc=%d)", value_handle, rc);
    }
}

class HidCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer*, ble_gap_conn_desc* desc) override {
        if (connected) {
            LOGW(TAG, "Rejecting second connection (handle=%u)", desc->conn_handle);
            ble_gap_terminate(desc->conn_handle, BLE_ERR_REM_USER_CONN_TERM);
            return;
        }
        connected = true;
        advertising_active = false;
        active_conn_handle = desc->conn_handle;
        format_ble_addr(desc->peer_ota_addr.val, peer_addr_str, sizeof(peer_addr_str));
        LOGI(TAG, "Host connected (handle=%u addr=%s)", desc->conn_handle, peer_addr_str);
    }

    void onDisconnect(BLEServer*, ble_gap_conn_desc* desc) override {
        if (desc->conn_handle != active_conn_handle) return;
        connected = false;
        active_conn_handle = 0xFFFF;
        clear_peer_metadata();
        LOGI(TAG, "Host disconnected");
        if (!hid_service_ready) return;
        start_advertising();
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
            owner_claimed = true;
            config_manager_set_ble_owner_claimed(true);
        } else {
            LOGW(TAG, "Pairing FAILED (reason=0x%02X)", auth.fail_reason);
        }
    }
#elif defined(CONFIG_NIMBLE_ENABLED)
    void onAuthenticationComplete(ble_gap_conn_desc* desc) override {
        // Capture peer metadata from the encryption result
        peer_encrypted = desc->sec_state.encrypted;
        peer_bonded = desc->sec_state.bonded;
        format_ble_addr(desc->peer_ota_addr.val, peer_addr_str, sizeof(peer_addr_str));
        format_ble_addr(desc->peer_id_addr.val, peer_id_addr_str, sizeof(peer_id_addr_str));

        if (desc->sec_state.encrypted) {
            LOGI(TAG, "Paired (encrypted=%d bonded=%d id_addr=%s)",
                 desc->sec_state.encrypted,
                 desc->sec_state.bonded,
                 peer_id_addr_str);
            pairing_mode = false;
            pairing_deadline = 0;
            owner_claimed = true;
            config_manager_set_ble_owner_claimed(true);
            return;
        }

        // Not encrypted — reject if not in pairing mode
        if (!pairing_mode) {
            LOGW(TAG, "Unbonded peer rejected (not in pairing mode)");
            ble_gap_terminate(desc->conn_handle, BLE_ERR_AUTH_FAIL);
            return;
        }

        // In pairing mode, failed encryption means the peer did not complete a
        // usable fresh pair. Keep the logic simple and disconnect; the user can
        // trigger a new live re-pairing attempt from the portal or a pad button.
        LOGW(TAG, "Pairing failed during pairing mode - disconnecting");
        ble_gap_terminate(desc->conn_handle, BLE_ERR_AUTH_FAIL);
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
    keyboardInputReport = report;
    send_report_notification(keyboardInputHandle, &keyboardInputReport, sizeof(keyboardInputReport));
}

void ble_hid_init(const char* device_name, bool force_pairing_mode) {
    LOGI(TAG, "Initializing BLE HID keyboard as '%s'", device_name);

    init_error = false;
    connected = false;
    peer_encrypted = false;
    peer_bonded = false;
    clear_peer_metadata();
    owner_claimed = config_manager_get_ble_owner_claimed();
    strlcpy(device_base_name, device_name ? device_name : "", sizeof(device_base_name));

    char identity_addr_str[18] = {};
    load_identity_address_string(identity_addr_str, sizeof(identity_addr_str));
    build_ble_name(device_name, identity_addr_str, ble_name_str, sizeof(ble_name_str));

    BLEDevice::init(ble_name_str);
    if (!BLEDevice::getInitialized()) {
        LOGE(TAG, "BLE stack init failed — BLE unavailable on this board");
        init_error = true;
        return;
    }
    configure_identity_address();
    static HidSecurityCallbacks secCb;
    BLEDevice::setSecurityCallbacks(&secCb);

    bleServer = BLEDevice::createServer();
    if (bleServer == nullptr) {
        LOGE(TAG, "BLEDevice::createServer() returned null");
        init_error = true;
        return;
    }
    bleServer->setCallbacks(new HidCallbacks());

    hid_service_ready = init_hid_service();
    if (!hid_service_ready) {
        LOGE(TAG, "Manual HID service registration failed");
        init_error = true;
        return;
    }

    static BLESecurity security;
    security.setAuthenticationMode(true, true, false);
    security.setCapability(ESP_IO_CAP_NONE);
    security.setInitEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);
    security.setRespEncryptionKey(ESP_BLE_ENC_KEY_MASK | ESP_BLE_ID_KEY_MASK);

    BLEAdvertising* adv = BLEDevice::getAdvertising();
    adv->setAppearance(HID_KEYBOARD);
    adv->addServiceUUID(BLEUUID((uint16_t)0x1812));
    adv->setScanResponse(true);
    adv->setMinPreferred(0x06);
    adv->setMaxPreferred(0x12);

    if (force_pairing_mode) {
        LOGI(TAG, "Entering BLE pairing mode");
        clear_all_bonds();
        clear_peer_metadata();
        owner_claimed = false;
        config_manager_set_ble_owner_claimed(false);
        pairing_mode = true;
        pairing_deadline = millis() + PAIRING_TIMEOUT_MS;
    } else if (!owner_claimed) {
        LOGI(TAG, "No BLE owner recorded - starting in pairing mode");
        pairing_mode = true;
        pairing_deadline = millis() + PAIRING_TIMEOUT_MS;
    } else {
        pairing_mode = false;
        pairing_deadline = 0;
    }

    start_advertising();

    LOGI(TAG, "BLE HID ready, advertising started");
}

void ble_hid_start_pairing() {
    LOGI(TAG, "Starting live BLE re-pairing");

    // 1. Disconnect any active peer
    if (connected && active_conn_handle != 0xFFFF) {
        ble_gap_terminate(active_conn_handle, BLE_ERR_REM_USER_CONN_TERM);
        delay(100);
    }

    // 2. Rotate identity so the host sees a fresh peripheral
    BLEAddress next_addr;
    if (!persist_new_identity_address(&next_addr)) {
        LOGW(TAG, "Continuing re-pairing without BLE identity rotation");
    }

    // 3. Full NimBLE stack teardown (keep BT controller memory for reinit)
    BLEDevice::deinit(false);
    bleServer = nullptr;
    hid_service_ready = false;
    advertising_active = false;
    connected = false;
    active_conn_handle = 0xFFFF;

    // 4. Clear owner claim in NVS
    config_manager_set_ble_owner_claimed(false);

    // 5. Reinitialize with fresh identity and pairing mode
    ble_hid_init(device_base_name, true);
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
    if (!connected) {
        LOGD(TAG, "Not connected - consumer 0x%04X dropped", usage);
        return;
    }

    consumerInputReport.usage = usage;
    send_report_notification(consumerInputHandle, &consumerInputReport, sizeof(consumerInputReport));

    delay(KS_DEFAULT_DELAY_MS);

    consumerInputReport.usage = 0;
    send_report_notification(consumerInputHandle, &consumerInputReport, sizeof(consumerInputReport));
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

    // Auto-close pairing window on timeout
    if (pairing_mode && pairing_deadline != 0 && millis() >= pairing_deadline) {
        pairing_mode = false;
        pairing_deadline = 0;
        LOGI(TAG, "Pairing mode timed out");
    }
}

bool ble_hid_is_pairing() {
    return pairing_mode;
}

const char* ble_hid_peer_addr() {
    return peer_addr_str;
}

const char* ble_hid_name() {
    return ble_name_str;
}

const char* ble_hid_peer_id_addr() {
    return peer_id_addr_str;
}

bool ble_hid_is_bonded() {
    return peer_bonded;
}

bool ble_hid_is_encrypted() {
    return peer_encrypted;
}

bool ble_hid_is_initialized() {
    return hid_service_ready;
}

const char* ble_hid_state() {
    if (init_error) return "error";
    if (!hid_service_ready) return "idle";
    if (pairing_mode) {
        if (connected && !peer_encrypted) return "connecting";
        return "pairing";
    }
    if (connected && peer_encrypted) return "secured";
    if (connected) return "connecting";
    if (advertising_active) {
        return owner_claimed ? "claimed" : "advertising";
    }
    if (owner_claimed) return "claimed";
    return "idle";
}

const char* ble_hid_status() {
    const char* state = ble_hid_state();
    if (strcmp(state, "pairing") == 0) return "pairing";
    if (strcmp(state, "secured") == 0) return "connected";
    if (strcmp(state, "error") == 0) return "error";
    return "ready";
}

#endif // HAS_BLE_HID
