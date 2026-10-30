#pragma once

#include <Arduino.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#define CONFIG_MANAGER_H
#define LOG_LEVEL 4
#define ESP_IO_CAP_NONE 3
#define ESP_BLE_ENC_KEY_MASK 1
#define ESP_BLE_ID_KEY_MASK 2
#define BLE_ERR_REM_USER_CONN_TERM 0x13
#define BLE_ERR_AUTH_FAIL 0x05
#define BLE_ERR_PINKEY_MISSING 0x06
#define BLE_HS_HCI_ERR(reason) (0x200 + (reason))
#define BLE_ATT_ERR_INVALID_ATTR_VALUE_LEN 0x0d
#define BLE_ATT_ERR_INSUFFICIENT_RES 0x11
#define BLE_ATT_ERR_INSUFFICIENT_ENC 0x0f
#define BLE_ATT_ERR_UNLIKELY 0x0e
#define BLE_ATT_F_READ 1
#define BLE_GATT_ACCESS_OP_READ_CHR 0
#define BLE_GATT_CHR_F_READ 1
#define BLE_GATT_CHR_F_WRITE_NO_RSP 2
#define BLE_GATT_CHR_F_WRITE 4
#define BLE_GATT_CHR_F_NOTIFY 8
#define BLE_GATT_SVC_TYPE_PRIMARY 1
#define BLE_GAP_EVENT_DISCONNECT 1
#define BLE_GAP_EVENT_PARING_COMPLETE 2
#define BLE_GAP_EVENT_REPEAT_PAIRING 3
#define BLE_UUID16_INIT(value) {{value}}

struct ble_addr_t { uint8_t type; uint8_t val[6]; };
struct ble_gap_conn_desc {
    uint16_t conn_handle;
    ble_addr_t peer_ota_addr;
    ble_addr_t peer_id_addr;
    struct { bool encrypted; bool bonded; } sec_state;
};
struct ble_gap_event {
    int type = 0;
    struct { ble_gap_conn_desc conn; int reason; } disconnect = {};
    struct { uint16_t conn_handle; int status; } pairing_complete = {};
    struct { uint16_t conn_handle; } repeat_pairing = {};
};
struct ble_store_key_sec { ble_addr_t peer_addr; uint8_t idx; };
struct ble_store_value_sec { bool ltk_present; };
struct os_mbuf { std::vector<uint8_t> bytes; };
struct ble_uuid_t { uint16_t value; };
struct ble_uuid16_t { ble_uuid_t u; };
struct ble_gatt_access_ctxt { uint8_t op; os_mbuf* om; };
using TestGattAccess = int (*)(uint16_t, uint16_t, ble_gatt_access_ctxt*, void*);
struct ble_gatt_dsc_def {
    const ble_uuid_t* uuid;
    uint16_t flags;
    uint8_t min_key_size;
    TestGattAccess access;
    void* arg;
};
struct ble_gatt_chr_def {
    const ble_uuid_t* uuid;
    TestGattAccess access;
    void* arg;
    ble_gatt_dsc_def* descriptors;
    uint16_t flags;
    uint8_t min_key_size;
    uint16_t* val_handle;
    const ble_uuid_t** presentation;
};
struct ble_gatt_svc_def {
    uint8_t type;
    const ble_uuid_t* uuid;
    const ble_uuid_t** includes;
    ble_gatt_chr_def* characteristics;
};

namespace TestBle {
inline uint32_t clock_ms = 0;
inline bool claimed = false;
inline std::string owner;
inline bool stored_bond = false;
inline ble_addr_t stored_peer = {};
inline unsigned clears = 0;
inline unsigned allocations = 0;
inline unsigned notifications = 0;
inline bool bonding = false;
inline bool mitm = false;
inline bool secure_connections = false;
inline uint8_t capability = 0;
inline std::vector<std::pair<uint16_t, int>> terminations;
inline std::vector<uint8_t> last_report;
inline const ble_gatt_svc_def* services = nullptr;
inline int (*gap_handler)(ble_gap_event*, void*) = nullptr;
}

inline size_t strlcpy(char* out, const char* text, size_t capacity) {
    if (capacity) std::snprintf(out, capacity, "%s", text);
    return std::strlen(text);
}
inline void delay(unsigned long duration) { TestBle::clock_ms += duration; }
inline bool config_manager_get_ble_owner_claimed() { return TestBle::claimed; }
inline void config_manager_set_ble_owner_claimed(bool claimed) { TestBle::claimed = claimed; }
inline void config_manager_get_ble_owner_addr(char* out, size_t capacity) {
    strlcpy(out, TestBle::owner.c_str(), capacity);
}
inline void config_manager_set_ble_owner_addr(const char* owner) { TestBle::owner = owner; }
inline int ble_store_clear() {
    ++TestBle::clears;
    TestBle::stored_bond = false;
    return 0;
}
inline int ble_store_read_our_sec(const ble_store_key_sec* key, ble_store_value_sec* value) {
    if (!TestBle::stored_bond
        || std::memcmp(&key->peer_addr, &TestBle::stored_peer, sizeof(ble_addr_t)) != 0) return 1;
    value->ltk_present = true;
    return 0;
}
inline int ble_gap_terminate(uint16_t handle, int reason) {
    TestBle::terminations.emplace_back(handle, reason);
    return 0;
}
inline int ble_gatts_count_cfg(const ble_gatt_svc_def*) { return 0; }
inline int ble_gatts_add_svcs(const ble_gatt_svc_def* services) {
    TestBle::services = services;
    uint16_t handle = 1;
    for (auto* characteristic = services->characteristics; characteristic->uuid; ++characteristic) {
        *characteristic->val_handle = handle++;
    }
    return 0;
}
inline int os_mbuf_append(os_mbuf* buffer, const void* data, uint16_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    buffer->bytes.insert(buffer->bytes.end(), bytes, bytes + size);
    return 0;
}
inline int ble_hs_mbuf_to_flat(const os_mbuf* buffer, void* data, uint16_t capacity, uint16_t* size) {
    if (buffer->bytes.size() > capacity) return 1;
    *size = buffer->bytes.size();
    std::memcpy(data, buffer->bytes.data(), *size);
    return 0;
}
inline os_mbuf* ble_hs_mbuf_from_flat(const void* data, uint16_t size) {
    ++TestBle::allocations;
    auto* buffer = new os_mbuf;
    os_mbuf_append(buffer, data, size);
    return buffer;
}
inline int ble_gatts_notify_custom(uint16_t, uint16_t, os_mbuf* buffer) {
    ++TestBle::notifications;
    TestBle::last_report = buffer->bytes;
    delete buffer;
    return 0;
}

class BLEServer;
class BLEServerCallbacks {
public:
    virtual ~BLEServerCallbacks() = default;
    virtual void onConnect(BLEServer*, ble_gap_conn_desc*) {}
    virtual void onDisconnect(BLEServer*, ble_gap_conn_desc*) {}
};
class BLESecurityCallbacks {
public:
    virtual ~BLESecurityCallbacks() = default;
    virtual uint32_t onPassKeyRequest() { return 0; }
    virtual void onPassKeyNotify(uint32_t) {}
    virtual bool onConfirmPIN(uint32_t) { return false; }
    virtual bool onSecurityRequest() { return false; }
    virtual void onAuthenticationComplete(ble_gap_conn_desc*, int) {}
};
class BLEServer {
public:
    BLEServerCallbacks* callbacks = nullptr;
    void setCallbacks(BLEServerCallbacks* replacement) {
        delete callbacks;
        callbacks = replacement;
    }
};
class BLEUUID {
public:
    explicit BLEUUID(uint16_t) {}
};
class BLEAdvertising {
public:
    void setAppearance(uint16_t) {}
    void addServiceUUID(BLEUUID) {}
    void setScanResponse(bool) {}
    void setMinPreferred(uint16_t) {}
    void setMaxPreferred(uint16_t) {}
};
class BLEDevice {
public:
    inline static BLEServer server;
    inline static BLEAdvertising advertising;
    inline static BLESecurityCallbacks* security = nullptr;
    static void init(const char*) {}
    static bool getInitialized() { return true; }
    static void setSecurityCallbacks(BLESecurityCallbacks* callbacks) { security = callbacks; }
    static void setCustomGapHandler(int (*handler)(ble_gap_event*, void*)) { TestBle::gap_handler = handler; }
    static BLEServer* createServer() { return &server; }
    static BLEAdvertising* getAdvertising() { return &advertising; }
    static void startAdvertising() {}
    static void stopAdvertising() {}
};
class BLESecurity {
public:
    void setAuthenticationMode(bool bonding, bool mitm, bool secure_connections) {
        TestBle::bonding = bonding;
        TestBle::mitm = mitm;
        TestBle::secure_connections = secure_connections;
    }
    void setCapability(uint8_t capability) { TestBle::capability = capability; }
    void setInitEncryptionKey(uint8_t) {}
    void setRespEncryptionKey(uint8_t) {}
};