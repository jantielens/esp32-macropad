#include "ble_telemetry.h"

#if HAS_BLE

#include <BLEDevice.h>
#include <BLEAdvertising.h>
#include <BLEUUID.h>
#include <string>

// NimBLE headers (pulled in transitively by BLEDevice.h) redefine LOG_LEVEL_*
// as plain ints, colliding with our LogLevel enum.
#undef LOG_LEVEL_ERROR
#undef LOG_LEVEL_WARN
#undef LOG_LEVEL_INFO
#undef LOG_LEVEL_DEBUG
#undef LOG_LEVEL
#include "log_manager.h"

static const char *TAG = "BleTel";

// BTHome v2 service UUID (assigned 16-bit). Spec:
//   https://bthome.io/format/
#define BTHOME_SERVICE_UUID 0xFCD2

// BTHome v2 device info byte: bit 6 = version (v2), bit 5 = trigger device (0),
// bit 0 = encryption (0). 0x40 = unencrypted, non-trigger, v2.
#define BTHOME_DEVICE_INFO_V2 0x40

// Buffer caps (BLE adv payload is 31 bytes total; service-data overhead is
// ~5 bytes leaving ~26 bytes for BTHome content).
#define BLE_TELEMETRY_MAX_OBJECTS 8
#define BLE_TELEMETRY_MAX_PAYLOAD 24  // device-info byte + object entries

namespace {

struct PendingObject {
    uint8_t object_id;
    uint8_t value_len;
    uint8_t value_bytes[2];  // u8 / u16 / s16 only for now
};

PendingObject g_pending[BLE_TELEMETRY_MAX_OBJECTS];
size_t g_pending_count = 0;
bool g_initialized = false;
char g_device_name[24] = {0};

// Insert-or-update by object id. Returns false if buffer is full.
bool upsert(uint8_t object_id, const uint8_t *bytes, uint8_t len) {
    if (len == 0 || len > sizeof(PendingObject::value_bytes)) return false;
    for (size_t i = 0; i < g_pending_count; ++i) {
        if (g_pending[i].object_id == object_id) {
            g_pending[i].value_len = len;
            memcpy(g_pending[i].value_bytes, bytes, len);
            return true;
        }
    }
    if (g_pending_count >= BLE_TELEMETRY_MAX_OBJECTS) {
        LOGW(TAG, "Telemetry buffer full, dropping object 0x%02X", object_id);
        return false;
    }
    g_pending[g_pending_count].object_id = object_id;
    g_pending[g_pending_count].value_len = len;
    memcpy(g_pending[g_pending_count].value_bytes, bytes, len);
    ++g_pending_count;
    return true;
}

// Build the BTHome v2 payload. Returns the byte count written.
size_t build_payload(uint8_t *out, size_t out_cap) {
    if (out_cap < 1) return 0;
    size_t pos = 0;
    out[pos++] = BTHOME_DEVICE_INFO_V2;
    for (size_t i = 0; i < g_pending_count; ++i) {
        const PendingObject &o = g_pending[i];
        if (pos + 1 + o.value_len > out_cap) {
            LOGW(TAG, "Payload truncated at object 0x%02X (cap %u)", o.object_id, (unsigned)out_cap);
            break;
        }
        out[pos++] = o.object_id;
        for (uint8_t k = 0; k < o.value_len; ++k) out[pos++] = o.value_bytes[k];
    }
    return pos;
}

} // namespace

void ble_telemetry_init(const char *device_name) {
    if (g_initialized) return;
    const char *name = (device_name && device_name[0]) ? device_name : "esp32-telemetry";
    strlcpy(g_device_name, name, sizeof(g_device_name));
    BLEDevice::init(g_device_name);
    if (!BLEDevice::getInitialized()) {
        LOGE(TAG, "BLE stack init failed");
        return;
    }
    // Crank TX power to the max (+9 dBm). Default is ~0 dBm which is too weak
    // for boards with poor PCB antennas (e.g. ESP32-C3 Super Mini) to reliably
    // reach a BLE proxy like a Shelly across the room. ESP_PWR_LVL_P9 adds
    // ~9 dB → roughly 2.5x range improvement and dramatically better odds of
    // landing in a passive scanner's listen window.
    BLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_ADV);
    BLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_DEFAULT);
    g_initialized = true;
    LOGI(TAG, "BLE telemetry initialized as '%s' (TX +9 dBm)", g_device_name);
}

void ble_telemetry_deinit() {
    if (!g_initialized) return;
    BLEDevice::stopAdvertising();
    g_initialized = false;
}

bool ble_telemetry_set_u8(uint8_t object_id, uint8_t value) {
    return upsert(object_id, &value, 1);
}

bool ble_telemetry_set_u16(uint8_t object_id, uint16_t value) {
    uint8_t le[2] = { (uint8_t)(value & 0xFF), (uint8_t)((value >> 8) & 0xFF) };
    return upsert(object_id, le, 2);
}

bool ble_telemetry_set_s16(uint8_t object_id, int16_t value) {
    uint16_t u = (uint16_t)value;
    uint8_t le[2] = { (uint8_t)(u & 0xFF), (uint8_t)((u >> 8) & 0xFF) };
    return upsert(object_id, le, 2);
}

size_t ble_telemetry_pending_count() {
    return g_pending_count;
}

bool ble_telemetry_advertise_burst(uint8_t burst_count, uint16_t interval_ms) {
    if (!g_initialized) {
        LOGE(TAG, "advertise_burst called before init");
        return false;
    }
    if (g_pending_count == 0) {
        LOGW(TAG, "advertise_burst with no buffered values");
        return false;
    }

    // Clamp inputs to safe ranges.
    if (burst_count == 0) burst_count = 1;
    if (interval_ms < 20) interval_ms = 20;
    if (interval_ms > 10240) interval_ms = 10240;

    uint8_t payload[BLE_TELEMETRY_MAX_PAYLOAD];
    const size_t payload_len = build_payload(payload, sizeof(payload));

    BLEAdvertising *adv = BLEDevice::getAdvertising();
    if (!adv) {
        LOGE(TAG, "getAdvertising() returned null");
        return false;
    }

    // Primary advertisement: flags + BTHome service data only. The 31-byte BLE
    // adv budget fills fast (flags = 3 bytes, service-data overhead = 4 bytes),
    // so the device name is intentionally moved to the scan response. Without
    // this split, a long device name silently squeezes out the BTHome payload
    // and the device becomes invisible to BTHome receivers.
    BLEAdvertisementData adv_data;
    adv_data.setFlags(0x06);  // LE General Discoverable | BR/EDR Not Supported
    // Arduino-ESP32 BLE API expects String; use concat(buf, len) to preserve
    // binary bytes (including embedded NULs) in the service data blob.
    String svc_data;
    svc_data.concat((const char *)payload, payload_len);
    adv_data.setServiceData(BLEUUID((uint16_t)BTHOME_SERVICE_UUID), svc_data);
    adv->setAdvertisementData(adv_data);

    // Scan response carries the human-readable name for nRF Connect / phones.
    // HA's BTHome integration identifies the device by MAC, so this is purely
    // cosmetic on the receive side.
    BLEAdvertisementData scan_resp;
    scan_resp.setName(String(g_device_name));
    adv->setScanResponseData(scan_resp);

    // Convert ms to BLE adv interval units (0.625 ms each).
    const uint16_t interval_units = (uint16_t)((interval_ms * 1000UL) / 625UL);
    adv->setMinInterval(interval_units);
    adv->setMaxInterval(interval_units);
    adv->setScanResponse(true);

    LOGI(TAG, "Advertising BTHome burst: %u packets @ %u ms (payload %u bytes, %u objects)",
         (unsigned)burst_count, (unsigned)interval_ms,
         (unsigned)payload_len, (unsigned)g_pending_count);

    adv->start();
    // Hold advertising long enough for `burst_count` packets at `interval_ms` to
    // be transmitted. Add a small grace margin so the controller actually emits
    // the last packet before we stop.
    const uint32_t hold_ms = (uint32_t)burst_count * (uint32_t)interval_ms + 50UL;
    delay(hold_ms);
    adv->stop();

    return true;
}

#endif // HAS_BLE
