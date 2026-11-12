#include "bthome_events.h"

#if HAS_BLE || HAS_BLE_HID

#include "ble_hid.h"
#include "ota_activity.h"
#include "log_manager.h"
static constexpr uint8_t kBthomeLogLevel = LOG_LEVEL;
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#if defined(BOARD_RETERMINAL_STICKY_INTERACTIVE)
#include <NimBLEDevice.h>
#else
#include <BLEDevice.h>
#include <BLEAdvertising.h>
#endif
#ifdef ARDUINO
#include <esp_heap_caps.h>
#include <esp32-hal-bt.h>
#if defined(BOARD_RETERMINAL_STICKY_INTERACTIVE)
// NimBLE-Arduino 2.5.0 predates the core's BLE memory-retention constructor.
#include <esp32-hal-alloc-ble-mem.h>
#endif
#endif
#if defined(CONFIG_NIMBLE_ENABLED) && !defined(BOARD_RETERMINAL_STICKY_INTERACTIVE)
#include <host/ble_hs_id.h>
#endif

#undef LOG_LEVEL_ERROR
#undef LOG_LEVEL_WARN
#undef LOG_LEVEL_INFO
#undef LOG_LEVEL_DEBUG
#undef LOG_LEVEL
#define LOG_LEVEL kBthomeLogLevel
#include "log_manager.h"

namespace {
constexpr const char* kBthomeTag = "BTHome";
constexpr uint16_t kEventIntervalMs = 100;
constexpr uint32_t kEventBurstMs = 350;
constexpr size_t kEventQueueSize = 4;
struct ButtonEvent {
    uint8_t button;
    uint8_t event;
    uint32_t ota_epoch;
};
ButtonEvent event_queue[kEventQueueSize];
size_t queue_head = 0;
size_t queue_count = 0;
portMUX_TYPE queue_mux = portMUX_INITIALIZER_UNLOCKED;
char event_device_name[32] = {};
bool configured = false;
bool event_active = false;
uint32_t event_started_ms = 0;
uint32_t active_ota_epoch = 0;
uint8_t packet_id = 0;

bool ble_initialized() {
#if defined(BOARD_RETERMINAL_STICKY_INTERACTIVE)
    return NimBLEDevice::isInitialized();
#else
    return BLEDevice::getInitialized();
#endif
}

void restore_hid_advertising() {
#if HAS_BLE_HID
    if (!ble_hid_pause_advertising(false))
        LOGE(kBthomeTag, "Could not restore HID advertising");
#endif
}

bool start_event(const ButtonEvent& event) {
#if !defined(BOARD_RETERMINAL_STICKY_INTERACTIVE)
    if (!ble_initialized()) BLEDevice::init(event_device_name);
#endif
    if (!ble_initialized()) {
        LOGE(kBthomeTag, "BLE unavailable; event dropped");
        return false;
    }
#if HAS_BLE_HID
    if (!ble_hid_pause_advertising(true)) {
        LOGE(kBthomeTag, "Could not pause HID advertising; event dropped");
        return false;
    }
#endif
    // Include all positions, even for Button 1, so HA always uses button_N.
    uint8_t advertisement[10 + 2 * bthome::button_count] = {
        0x02, 0x01, 0x06,
        0x1A, 0x16, 0xD2, 0xFC, 0x44,
        0x00, packet_id++
    };
    for (uint8_t index = 0; index < bthome::button_count; ++index) {
        advertisement[10 + 2 * index] = 0x3A;
        advertisement[11 + 2 * index] = index + 1 == event.button ? event.event : 0;
    }
    static_assert(sizeof(advertisement) <= 31, "BTHome event exceeds legacy advertisement budget");

    bool started;
#if defined(BOARD_RETERMINAL_STICKY_INTERACTIVE)
    NimBLEAdvertising* advertiser = NimBLEDevice::getAdvertising();
    NimBLEAdvertisementData data;
    advertiser->enableScanResponse(false);
    advertiser->setMinInterval(kEventIntervalMs * 8 / 5);
    advertiser->setMaxInterval(kEventIntervalMs * 8 / 5);
    started = advertiser->setConnectableMode(BLE_GAP_CONN_MODE_NON) &&
        advertiser->setDiscoverableMode(BLE_GAP_DISC_MODE_NON) &&
        data.addData(advertisement, sizeof(advertisement)) &&
        advertiser->setAdvertisementData(data) && advertiser->start();
    if (!started) LOGE(kBthomeTag, "Broadcast advertising failed; event dropped");
#elif defined(CONFIG_NIMBLE_ENABLED)
    // Arduino BLEAdvertising::start rejects a full GATT connection table even
    // for nonconnectable advertisements. These events need no connection slot.
    uint8_t address_type = 0;
    ble_gap_adv_params parameters = {};
    parameters.conn_mode = BLE_GAP_CONN_MODE_NON;
    parameters.disc_mode = BLE_GAP_DISC_MODE_NON;
    parameters.itvl_min = parameters.itvl_max = kEventIntervalMs * 8 / 5;
    int result = ble_hs_id_infer_auto(0, &address_type);
    if (result == 0) result = ble_gap_adv_set_data(advertisement, sizeof(advertisement));
    if (result == 0)
        result = ble_gap_adv_start(address_type, nullptr, BLE_HS_FOREVER, &parameters,
            [](ble_gap_event*, void*) { return 0; }, nullptr);
    started = result == 0;
    if (!started) LOGE(kBthomeTag, "Advertising failed (rc=%d); event dropped", result);
#else
    BLEAdvertising* advertiser = BLEDevice::getAdvertising();
    advertiser->reset();
    BLEAdvertisementData data;
    data.addData(reinterpret_cast<char*>(advertisement), sizeof(advertisement));
    advertiser->setAdvertisementType(ADV_TYPE_NONCONN_IND);
    advertiser->setScanResponse(false);
    advertiser->setMinInterval(kEventIntervalMs * 8 / 5);
    advertiser->setMaxInterval(kEventIntervalMs * 8 / 5);
    started = advertiser->setAdvertisementData(data) && advertiser->start();
    if (!started) LOGE(kBthomeTag, "Advertising failed; event dropped");
#endif
    if (!started) restore_hid_advertising();
    return started;
}
}

void bthome_events_configure(const char* device_name) {
    strlcpy(event_device_name, device_name && device_name[0] ? device_name : "esp32-macropad",
            sizeof(event_device_name));
#if defined(BOARD_RETERMINAL_STICKY_INTERACTIVE)
    if (!ble_initialized()) {
#ifdef ARDUINO
        LOGI(kBthomeTag, "Initializing broadcast-only BLE: internal free=%u largest=%u released=%u",
            unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
            unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
            unsigned(btMemReleased(BT_MODE_BLE)));
#endif
        NimBLEDevice::init(event_device_name);
        if (ble_initialized()) {
            LOGI(kBthomeTag, "Broadcast-only BLE initialized");
        } else {
            LOGE(kBthomeTag, "BLE initialization failed; BTHome events unavailable");
#ifdef ARDUINO
            LOGE(kBthomeTag, "Internal free=%u largest=%u released=%u",
                unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
                unsigned(btMemReleased(BT_MODE_BLE)));
#endif
        }
#ifdef ARDUINO
        LOGI(kBthomeTag, "After BLE: internal free=%u largest=%u PSRAM free=%u",
            unsigned(heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
            unsigned(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)),
            unsigned(heap_caps_get_free_size(MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)));
#endif
    }
#endif
    portENTER_CRITICAL(&queue_mux);
#if defined(BOARD_RETERMINAL_STICKY_INTERACTIVE)
    configured = ble_initialized();
#else
    configured = true;
#endif
    portEXIT_CRITICAL(&queue_mux);
}

bool bthome_events_send(uint8_t button, uint8_t event) {
    if (!bthome::valid_button_event(button, event) || ota_activity_is_active()) {
        LOGW(kBthomeTag, "Event rejected (invalid button/event or OTA active)");
        return false;
    }
    const ButtonEvent pending{button, event, ota_activity_epoch()};
    portENTER_CRITICAL(&queue_mux);
    const bool accepted = configured && queue_count < kEventQueueSize;
    if (accepted) {
        event_queue[(queue_head + queue_count) % kEventQueueSize] = pending;
        ++queue_count;
    }
    portEXIT_CRITICAL(&queue_mux);
    if (!accepted) LOGW(kBthomeTag, "Event rejected (not configured or queue full)");
    return accepted;
}

void bthome_events_loop() {
    const uint32_t now = millis();
    const bool ota_active = ota_activity_is_active();
    const uint32_t epoch = ota_activity_epoch();
    if (event_active) {
        if (!ota_active && epoch == active_ota_epoch &&
            uint32_t(now - event_started_ms) < kEventBurstMs) return;
#if defined(BOARD_RETERMINAL_STICKY_INTERACTIVE)
        const bool stopped = NimBLEDevice::getAdvertising()->stop();
#else
        const bool stopped = BLEDevice::getAdvertising()->stop();
#endif
        if (!stopped) {
            LOGE(kBthomeTag, "Could not stop event advertising");
            return;
        }
        event_active = false;
        restore_hid_advertising();
    }

    ButtonEvent pending{};
    bool have_event = false;
    portENTER_CRITICAL(&queue_mux);
    if (queue_count) {
        pending = event_queue[queue_head];
        queue_head = (queue_head + 1) % kEventQueueSize;
        --queue_count;
        have_event = true;
    }
    portEXIT_CRITICAL(&queue_mux);
    if (!have_event) return;
    if (ota_active || pending.ota_epoch != epoch) {
        LOGW(kBthomeTag, "Queued event cancelled by OTA");
        return;
    }
    if (start_event(pending)) {
        event_active = true;
        event_started_ms = millis();
        active_ota_epoch = epoch;
        LOGI(kBthomeTag, "Advertising Button %u event 0x%02X", pending.button, pending.event);
    }
}

#endif
