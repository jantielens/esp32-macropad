#include "bthome_events.h"

#if HAS_BLE || HAS_BLE_HID

#include "ble_hid.h"
#include "ble_backend.h"
#include "ota_activity.h"
#include "reboot_service.h"
#include "log_manager.h"
static constexpr uint8_t kBthomeLogLevel = LOG_LEVEL;
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/portmacro.h>
#if BLE_BACKEND == BLE_BACKEND_NIMBLE_ARDUINO
#include <NimBLEDevice.h>
#else
#include <BLEDevice.h>
#include <BLEAdvertising.h>
#endif
#if defined(CONFIG_NIMBLE_ENABLED) && BLE_BACKEND == BLE_BACKEND_ARDUINO_CORE
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
bool radio_owned = false;
bthome::EventSettings pending_settings;
bthome::EventSettings active_settings;
uint32_t event_started_ms = 0;
uint32_t active_ota_epoch = 0;
uint8_t packet_id = 0;

bool release_event_radio() {
#if HAS_BLE_HID
    if (!ble_hid_pause_advertising(false)) {
        LOGE(kBthomeTag, "Could not restore HID advertising");
        return false;
    }
#endif
    radio_owned = false;
    return true;
}

bool start_event(const ButtonEvent& event, const bthome::EventSettings& settings) {
    if (!ble_backend_prepare(event_device_name)) return false;
    if (!ble_backend_initialized()) {
        LOGE(kBthomeTag, "BLE unavailable; event dropped");
        return false;
    }
#if HAS_BLE_HID
    if (!ble_hid_pause_advertising(true)) {
        LOGE(kBthomeTag, "Could not pause HID advertising; event dropped");
        return false;
    }
#endif
    radio_owned = true;
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
#if BLE_BACKEND == BLE_BACKEND_NIMBLE_ARDUINO
    NimBLEAdvertising* advertiser = NimBLEDevice::getAdvertising();
    if (!advertiser) {
        LOGE(kBthomeTag, "Advertiser unavailable; event dropped");
        release_event_radio();
        return false;
    }
    NimBLEAdvertisementData data;
    advertiser->enableScanResponse(false);
    advertiser->setMinInterval(settings.adv_interval_ms * 8 / 5);
    advertiser->setMaxInterval(settings.adv_interval_ms * 8 / 5);
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
    parameters.itvl_min = parameters.itvl_max = settings.adv_interval_ms * 8 / 5;
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
    advertiser->setMinInterval(settings.adv_interval_ms * 8 / 5);
    advertiser->setMaxInterval(settings.adv_interval_ms * 8 / 5);
    started = advertiser->setAdvertisementData(data) && advertiser->start();
    if (!started) LOGE(kBthomeTag, "Advertising failed; event dropped");
#endif
    if (!started) release_event_radio();
    return started;
}
}

bool bthome_events_apply_settings(const bthome::EventSettings& settings) {
    if (!bthome::valid_event_settings(settings)) {
        LOGE(kBthomeTag, "Invalid event settings rejected");
        return false;
    }
    portENTER_CRITICAL(&queue_mux);
    pending_settings = settings;
    portEXIT_CRITICAL(&queue_mux);
    return true;
}

void bthome_events_configure(const char* device_name, const bthome::EventSettings& settings) {
    if (!bthome_events_apply_settings(settings)) return;
    strlcpy(event_device_name, device_name && device_name[0] ? device_name : "esp32-macropad",
            sizeof(event_device_name));
    const bool available =
#if BLE_BACKEND == BLE_BACKEND_NIMBLE_ARDUINO && !HAS_BLE_HID
        ble_backend_initialized();
#else
        true;
#endif
    portENTER_CRITICAL(&queue_mux);
    configured = available;
    portEXIT_CRITICAL(&queue_mux);
}

bool bthome_events_prepare() {
    const bool available = ble_backend_prepare(event_device_name);
    portENTER_CRITICAL(&queue_mux);
    configured = available;
    portEXIT_CRITICAL(&queue_mux);
    return available;
}

bool bthome_events_send(uint8_t button, uint8_t event) {
    if (!bthome::valid_button_event(button, event) || ota_activity_is_active() || reboot_is_ota_mode()) {
        LOGW(kBthomeTag, "Event rejected (invalid button/event or OTA active or mode)");
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
            uint32_t(now - event_started_ms) < active_settings.burst_ms) return;
#if BLE_BACKEND == BLE_BACKEND_NIMBLE_ARDUINO
        const bool stopped = NimBLEDevice::getAdvertising()->stop();
#else
        const bool stopped = BLEDevice::getAdvertising()->stop();
#endif
        if (!stopped) {
            LOGE(kBthomeTag, "Could not stop event advertising");
            return;
        }
        event_active = false;
    }
    if (radio_owned && !release_event_radio()) return;

    ButtonEvent pending{};
    bthome::EventSettings settings;
    bool have_event = false;
    portENTER_CRITICAL(&queue_mux);
    if (queue_count) {
        pending = event_queue[queue_head];
        queue_head = (queue_head + 1) % kEventQueueSize;
        --queue_count;
        have_event = true;
        settings = pending_settings;
    }
    portEXIT_CRITICAL(&queue_mux);
    if (!have_event) return;
    if (ota_active || pending.ota_epoch != epoch) {
        LOGW(kBthomeTag, "Queued event cancelled by OTA");
        return;
    }
    if (start_event(pending, settings)) {
        event_active = true;
        active_settings = settings;
        event_started_ms = millis();
        active_ota_epoch = epoch;
        LOGI(kBthomeTag, "Advertising Button %u event 0x%02X", pending.button, pending.event);
    }
}

#endif
