#include "board_config.h"

#if IS_EPAPER_BLE_BRIDGE

#if !defined(CONFIG_NIMBLE_ENABLED)
#error "E-paper BLE bridge requires the NimBLE host stack"
#endif

#include "epaper_ble_bridge_runtime.h"

#include "epaper_ble_bridge_config.h"
#include "epaper_ble_bridge_logic.h"
#include "epaper_ble_codec.h"
#include "log_manager.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <BLEAdvertisedDevice.h>
#include <BLEAdvertising.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <WiFiClientSecure.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <freertos/task.h>
#include <host/ble_gap.h>
#include <string.h>

#undef LOG_LEVEL_ERROR
#undef LOG_LEVEL_WARN
#undef LOG_LEVEL_INFO
#undef LOG_LEVEL_DEBUG
#undef LOG_LEVEL

namespace {

constexpr char TAG[] = "EpaperBleBridge";
constexpr uint32_t kSitePollIntervalMs = 5000;
constexpr uint32_t kHttpTimeoutMs = 8000;
constexpr size_t kEtagMaxLen = 96;
constexpr size_t kUrlMaxLen = 768;
constexpr uint32_t kWorkerStackBytes = 8192;
constexpr uint32_t kRadioStackBytes = 4096;
constexpr uint32_t kScanRecycleIntervalMs = 5000;

struct FrameRuntime {
    EpaperBleAssignmentPacket assignment;
    uint32_t last_site_success_ms;
    uint32_t next_poll_ms;
    uint32_t last_ack_revision;
    uint8_t poll_failures;
    char etag[kEtagMaxLen];
};

struct PendingAck {
    bool pending;
    bool in_flight;
    uint8_t failure_count;
    uint32_t next_attempt_ms;
    EpaperBleAckPacket packet;
};

FrameRuntime g_frames[EPAPER_BLE_BRIDGE_MAX_FRAMES] = {};
PendingAck g_acks[EPAPER_BLE_BRIDGE_MAX_FRAMES] = {};
EpaperBleBridgeConfig g_runtime_config = {};
SemaphoreHandle_t g_config_mutex = nullptr;
portMUX_TYPE g_state_mux = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t g_worker_task = nullptr;
TaskHandle_t g_radio_task = nullptr;
BLEAdvertising *g_advertising = nullptr;
BLEScan *g_scan = nullptr;
uint8_t g_next_advertisement = 0;
uint32_t g_config_generation = 0;
uint32_t g_last_scan_recycle_ms = 0;

void bytes_to_hex(const uint8_t *input, size_t input_len, char *output) {
    static constexpr char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < input_len; ++i) {
        output[i * 2] = digits[input[i] >> 4];
        output[i * 2 + 1] = digits[input[i] & 0x0F];
    }
    output[input_len * 2] = '\0';
}

bool hex_to_bytes(const char *input, uint8_t *output, size_t output_len) {
    if (!input || strlen(input) != output_len * 2) return false;
    for (size_t i = 0; i < output_len; ++i) {
        char pair[3] = {input[i * 2], input[i * 2 + 1], '\0'};
        char *end = nullptr;
        const unsigned long value = strtoul(pair, &end, 16);
        if (!end || *end != '\0') return false;
        output[i] = (uint8_t)value;
    }
    return true;
}

bool append_url_encoded(char *output, size_t output_len, size_t *position,
                        const char *value) {
    static constexpr char digits[] = "0123456789ABCDEF";
    for (const uint8_t *cursor = (const uint8_t *)value; *cursor; ++cursor) {
        const bool unreserved = (*cursor >= 'a' && *cursor <= 'z') ||
                                (*cursor >= 'A' && *cursor <= 'Z') ||
                                (*cursor >= '0' && *cursor <= '9') ||
                                *cursor == '-' || *cursor == '_' ||
                                *cursor == '.' || *cursor == '~';
        const size_t required = unreserved ? 1 : 3;
        if (*position + required >= output_len) return false;
        if (unreserved) {
            output[(*position)++] = (char)*cursor;
        } else {
            output[(*position)++] = '%';
            output[(*position)++] = digits[*cursor >> 4];
            output[(*position)++] = digits[*cursor & 0x0F];
        }
    }
    output[*position] = '\0';
    return true;
}

bool build_endpoint(const EpaperBleBridgeFrameConfig &config,
                    const char *endpoint, char *output, size_t output_len) {
    const size_t base_len = strlen(config.site_url);
    const bool has_slash = base_len && config.site_url[base_len - 1] == '/';
    const int written = snprintf(output, output_len, "%s%sapi/assignment/%s?device_id=",
                                 config.site_url, has_slash ? "" : "/", endpoint);
    if (written < 0 || (size_t)written >= output_len) return false;
    size_t position = (size_t)written;
    if (!append_url_encoded(output, output_len, &position, config.device_id) ||
        position + 5 >= output_len) {
        return false;
    }
    memcpy(output + position, "&key=", 6);
    position += 5;
    return append_url_encoded(output, output_len, &position, config.api_key);
}

bool copy_config(uint8_t index, EpaperBleBridgeFrameConfig *output,
                 uint8_t *frame_count = nullptr,
                 TickType_t wait_ticks = pdMS_TO_TICKS(100),
                 uint32_t *generation = nullptr) {
    if (!g_config_mutex || xSemaphoreTake(g_config_mutex, wait_ticks) != pdTRUE) {
        return false;
    }
    const uint8_t count = g_runtime_config.frame_count;
    if (frame_count) *frame_count = count;
    const bool valid = index < count;
    if (valid && output) *output = g_runtime_config.frames[index];
    xSemaphoreGive(g_config_mutex);
    if (generation) {
        portENTER_CRITICAL(&g_state_mux);
        *generation = g_config_generation;
        portEXIT_CRITICAL(&g_state_mux);
    }
    return valid;
}

int perform_request(HTTPClient &http, const EpaperBleBridgeFrameConfig &config,
                    const char *endpoint, bool post, const String &payload,
                    const char *etag, String *response, char *response_etag,
                    size_t response_etag_len) {
    char url[kUrlMaxLen];
    if (!build_endpoint(config, endpoint, url, sizeof(url))) return 0;
    WiFiClient plain;
    WiFiClientSecure secure;
    bool began = false;
    if (strncmp(url, "https://", 8) == 0) {
        secure.setInsecure();
        began = http.begin(secure, url);
    } else {
        began = http.begin(plain, url);
    }
    if (!began) return 0;
    http.setTimeout(kHttpTimeoutMs);
    const char *headers[] = {"ETag"};
    http.collectHeaders(headers, 1);
    if (etag && etag[0]) http.addHeader("If-None-Match", etag);
    if (post) http.addHeader("Content-Type", "application/json");
    const int status = post ? http.POST(payload) : http.GET();
    if (response && status == HTTP_CODE_OK) *response = http.getString();
    if (response_etag && response_etag_len) {
        strlcpy(response_etag, http.header("ETag").c_str(), response_etag_len);
    }
    http.end();
    vTaskDelay(pdMS_TO_TICKS(100));
    return status;
}

bool parse_assignment(const String &body, uint32_t device_key,
                      EpaperBleAssignmentPacket *output) {
    JsonDocument document;
    if (deserializeJson(document, body) != DeserializationError::Ok) return false;
    const char *image_key = document["image_key"] | "";
    const char *format = document["format"] | "";
    EpaperBleAssignmentPacket packet = {};
    packet.valid = true;
    packet.device_key = device_key;
    packet.revision = document["revision"] | 0U;
    packet.content_crc32 = document["content_crc32"] | 0U;
    if (strcmp(format, "g16z") == 0) {
        packet.image_format = EPAPER_BLE_FORMAT_G16Z;
    } else if (strcmp(format, "jpeg") == 0) {
        packet.image_format = EPAPER_BLE_FORMAT_JPEG;
    } else {
        return false;
    }
    if (!packet.revision || !hex_to_bytes(image_key, packet.image_key,
                                           sizeof(packet.image_key))) {
        return false;
    }
    *output = packet;
    return true;
}

void poll_frame(uint8_t index, uint32_t now_ms) {
    EpaperBleBridgeFrameConfig config = {};
    uint32_t generation = 0;
    if (!copy_config(index, &config, nullptr, pdMS_TO_TICKS(100), &generation) ||
        WiFi.status() != WL_CONNECTED) return;
    FrameRuntime snapshot = {};
    portENTER_CRITICAL(&g_state_mux);
    snapshot = g_frames[index];
    portEXIT_CRITICAL(&g_state_mux);
    if ((int32_t)(now_ms - snapshot.next_poll_ms) < 0) return;

    HTTPClient http;
    String response;
    char response_etag[kEtagMaxLen] = {};
    const int status = perform_request(http, config, "current", false, String(),
                                       snapshot.etag, &response, response_etag,
                                       sizeof(response_etag));
    EpaperBleAssignmentPacket assignment = snapshot.assignment;
    const EpaperBleBridgeSiteAction action = epaper_ble_bridge_site_action(status);
    bool succeeded = false;
    if (action == EpaperBleBridgeSiteAction::ReplaceAssignment) {
        succeeded = parse_assignment(response, epaper_ble_device_key(config.device_id),
                                     &assignment);
    } else if (action == EpaperBleBridgeSiteAction::ClearAssignment) {
        memset(&assignment, 0, sizeof(assignment));
        assignment.device_key = epaper_ble_device_key(config.device_id);
        succeeded = true;
    } else if (action == EpaperBleBridgeSiteAction::KeepAssignment) {
        succeeded = true;
    }

    portENTER_CRITICAL(&g_state_mux);
    if (generation != g_config_generation) {
        portEXIT_CRITICAL(&g_state_mux);
        return;
    }
    FrameRuntime &frame = g_frames[index];
    if (succeeded) {
        frame.assignment = assignment;
        frame.last_site_success_ms = now_ms;
        frame.poll_failures = 0;
        frame.next_poll_ms = now_ms + kSitePollIntervalMs;
        if (response_etag[0]) strlcpy(frame.etag, response_etag, sizeof(frame.etag));
    } else {
        if (frame.poll_failures < UINT8_MAX) ++frame.poll_failures;
        frame.next_poll_ms = now_ms +
            epaper_ble_bridge_retry_delay_ms(frame.poll_failures - 1);
    }
    portEXIT_CRITICAL(&g_state_mux);
}

void forward_ack(uint8_t index, uint32_t now_ms) {
    EpaperBleBridgeFrameConfig config = {};
    uint32_t generation = 0;
    if (!copy_config(index, &config, nullptr, pdMS_TO_TICKS(100), &generation) ||
        WiFi.status() != WL_CONNECTED) return;
    PendingAck pending = {};
    portENTER_CRITICAL(&g_state_mux);
    if (g_acks[index].pending && !g_acks[index].in_flight &&
        (int32_t)(now_ms - g_acks[index].next_attempt_ms) >= 0) {
        g_acks[index].in_flight = true;
        pending = g_acks[index];
    }
    portEXIT_CRITICAL(&g_state_mux);
    if (!pending.pending) return;

    char image_key[17];
    bytes_to_hex(pending.packet.image_key, sizeof(pending.packet.image_key), image_key);
    JsonDocument document;
    document["revision"] = pending.packet.revision;
    document["image_key"] = image_key;
    String payload;
    serializeJson(document, payload);
    HTTPClient http;
    String response;
    char response_etag[kEtagMaxLen] = {};
    const int status = perform_request(http, config, "ack", true, payload,
                                       nullptr, &response, response_etag,
                                       sizeof(response_etag));
    const bool terminal = epaper_ble_bridge_ack_status_terminal(status);
    EpaperBleAssignmentPacket successor = {};
    bool has_successor = false;
    if (status == HTTP_CODE_OK) {
        has_successor = parse_assignment(
            response, epaper_ble_device_key(config.device_id), &successor);
    } else if (status == HTTP_CODE_NO_CONTENT) {
        successor.device_key = epaper_ble_device_key(config.device_id);
        has_successor = true;
    }
    portENTER_CRITICAL(&g_state_mux);
    if (generation != g_config_generation) {
        portEXIT_CRITICAL(&g_state_mux);
        return;
    }
    PendingAck &current = g_acks[index];
    if (current.packet.revision == pending.packet.revision &&
        memcmp(current.packet.image_key, pending.packet.image_key,
               sizeof(current.packet.image_key)) == 0) {
        current.in_flight = false;
        if (terminal) {
            current.pending = false;
            g_frames[index].last_ack_revision = pending.packet.revision;
            if (has_successor) {
                g_frames[index].assignment = successor;
                g_frames[index].last_site_success_ms = now_ms;
                g_frames[index].poll_failures = 0;
                g_frames[index].next_poll_ms = now_ms + kSitePollIntervalMs;
                if (response_etag[0]) {
                    strlcpy(g_frames[index].etag, response_etag,
                            sizeof(g_frames[index].etag));
                }
            } else {
                g_frames[index].next_poll_ms = now_ms;
            }
        } else {
            if (current.failure_count < UINT8_MAX) ++current.failure_count;
            current.next_attempt_ms = now_ms +
                epaper_ble_bridge_retry_delay_ms(current.failure_count - 1);
        }
    }
    portEXIT_CRITICAL(&g_state_mux);
}

void worker_task(void *) {
    for (;;) {
        const uint32_t now_ms = millis();
        uint8_t frame_count = 0;
        copy_config(0, nullptr, &frame_count);
        for (uint8_t index = 0; index < frame_count; ++index) {
            forward_ack(index, now_ms);
            poll_frame(index, now_ms);
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

class AckCallbacks final : public BLEAdvertisedDeviceCallbacks {
    void onResult(BLEAdvertisedDevice device) override {
        if (!device.haveManufacturerData()) return;
        const String data = device.getManufacturerData();
        if (data.length() != EPAPER_BLE_PACKET_SIZE + 2) return;
        const uint8_t *bytes = (const uint8_t *)data.c_str();
        const uint16_t company_id = (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
        for (uint8_t index = 0; index < EPAPER_BLE_BRIDGE_MAX_FRAMES; ++index) {
            EpaperBleBridgeFrameConfig config = {};
            uint32_t generation = 0;
            if (!copy_config(index, &config, nullptr, 0, &generation)) break;
            EpaperBleAssignmentPacket assignment = {};
            portENTER_CRITICAL(&g_state_mux);
            if (generation != g_config_generation) {
                portEXIT_CRITICAL(&g_state_mux);
                return;
            }
            assignment = g_frames[index].assignment;
            portEXIT_CRITICAL(&g_state_mux);
            EpaperBleAckPacket ack = {};
            if (!assignment.valid ||
                epaper_ble_verify_ack(company_id, bytes + 2, EPAPER_BLE_PACKET_SIZE,
                                      config.api_key, assignment.device_key,
                                      assignment.revision, assignment.image_key,
                                      &ack) != EpaperBleCodecResult::Ok) {
                continue;
            }
            portENTER_CRITICAL(&g_state_mux);
            if (generation != g_config_generation) {
                portEXIT_CRITICAL(&g_state_mux);
                return;
            }
            PendingAck &pending = g_acks[index];
            if (g_frames[index].last_ack_revision == ack.revision) {
                portEXIT_CRITICAL(&g_state_mux);
                break;
            }
            if (!pending.pending || pending.packet.revision != ack.revision ||
                memcmp(pending.packet.image_key, ack.image_key,
                       sizeof(ack.image_key)) != 0) {
                memset(&pending, 0, sizeof(pending));
                pending.pending = true;
                pending.packet = ack;
                pending.next_attempt_ms = millis();
            }
            portEXIT_CRITICAL(&g_state_mux);
            break;
        }
    }
};

AckCallbacks g_ack_callbacks;

void advertise_frame(uint8_t index, uint32_t now_ms) {
    EpaperBleBridgeFrameConfig config = {};
    uint32_t generation = 0;
    if (!copy_config(index, &config, nullptr, pdMS_TO_TICKS(100), &generation)) {
        return;
    }
    EpaperBleAssignmentPacket packet = {};
    uint32_t last_site_success_ms = 0;
    portENTER_CRITICAL(&g_state_mux);
    if (generation != g_config_generation) {
        portEXIT_CRITICAL(&g_state_mux);
        return;
    }
    packet = g_frames[index].assignment;
    last_site_success_ms = g_frames[index].last_site_success_ms;
    portEXIT_CRITICAL(&g_state_mux);
    packet = epaper_ble_bridge_advertised_packet(
        packet, epaper_ble_device_key(config.device_id), now_ms,
        last_site_success_ms);
    uint8_t manufacturer_data[EPAPER_BLE_PACKET_SIZE + 2];
    manufacturer_data[0] = (uint8_t)(EPAPER_BLE_COMPANY_ID & 0xFF);
    manufacturer_data[1] = (uint8_t)(EPAPER_BLE_COMPANY_ID >> 8);
    if (!epaper_ble_encode_assignment(packet, manufacturer_data + 2)) return;
    String data;
    data.concat((const char *)manufacturer_data, sizeof(manufacturer_data));
    BLEAdvertisementData advertisement;
    advertisement.setFlags(0x04);
    advertisement.setManufacturerData(data);
    g_advertising->stop();
    g_advertising->setAdvertisementData(advertisement);
    g_advertising->setAdvertisementType(BLE_GAP_CONN_MODE_NON);
    g_advertising->setScanResponse(false);
    g_advertising->setMinInterval(160);
    g_advertising->setMaxInterval(160);
    g_advertising->start();
}

void radio_task(void *) {
    for (;;) {
        const uint32_t now_ms = millis();
        uint8_t frame_count = 0;
        copy_config(0, nullptr, &frame_count);
        if (frame_count) {
            if (g_next_advertisement >= frame_count) g_next_advertisement = 0;
            advertise_frame(g_next_advertisement, now_ms);
            g_next_advertisement = epaper_ble_bridge_next_frame(
                g_next_advertisement, frame_count);
        }
        if ((uint32_t)(now_ms - g_last_scan_recycle_ms) >=
            kScanRecycleIntervalMs) {
            g_scan->stop();
            g_scan->clearResults();
            g_scan->start(0, nullptr, false);
            g_last_scan_recycle_ms = now_ms;
        }
        vTaskDelay(pdMS_TO_TICKS(EPAPER_BLE_BRIDGE_ADV_INTERVAL_MS));
    }
}

}  // namespace

void epaper_ble_bridge_runtime_reload_config() {
    if (!g_config_mutex) return;
    if (xSemaphoreTake(g_config_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        g_runtime_config = g_epaper_ble_bridge_config;
        xSemaphoreGive(g_config_mutex);
    }
    portENTER_CRITICAL(&g_state_mux);
    ++g_config_generation;
    memset(g_frames, 0, sizeof(g_frames));
    memset(g_acks, 0, sizeof(g_acks));
    portEXIT_CRITICAL(&g_state_mux);
}

void epaper_ble_bridge_runtime_setup() {
    if (!epaper_ble_codec_self_test()) {
        LOGE(TAG, "BLE codec self-test failed; bridge disabled");
        return;
    }
    g_config_mutex = xSemaphoreCreateMutex();
    if (!g_config_mutex) {
        LOGE(TAG, "Config mutex allocation failed; bridge disabled");
        return;
    }
    epaper_ble_bridge_runtime_reload_config();
    BLEDevice::init("E-Paper BLE Bridge");
    if (!BLEDevice::getInitialized()) {
        LOGE(TAG, "BLE initialization failed; bridge disabled");
        return;
    }
    BLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_ADV);
    BLEDevice::setPower(ESP_PWR_LVL_P9, ESP_BLE_PWR_TYPE_SCAN);
    g_advertising = BLEDevice::getAdvertising();
    g_scan = BLEDevice::getScan();
    if (!g_advertising || !g_scan) {
        LOGE(TAG, "BLE advertiser or scanner unavailable; bridge disabled");
        return;
    }
    g_scan->setActiveScan(false);
    g_scan->setInterval(100);
    g_scan->setWindow(100);
    g_scan->setAdvertisedDeviceCallbacks(&g_ack_callbacks, true);
    if (!g_scan->start(0, nullptr, false)) {
        LOGE(TAG, "Passive ACK scan failed to start");
    }
    if (xTaskCreate(worker_task, "epaper_ble_http", kWorkerStackBytes, nullptr, 1,
                    &g_worker_task) != pdPASS) {
        g_worker_task = nullptr;
        LOGE(TAG, "HTTP worker task creation failed");
    }
    if (xTaskCreate(radio_task, "epaper_ble_radio", kRadioStackBytes, nullptr, 2,
                    &g_radio_task) != pdPASS) {
        g_radio_task = nullptr;
        LOGE(TAG, "BLE radio task creation failed");
    }
}

void epaper_ble_bridge_runtime_loop() {
    // BLE and HTTP work run on dedicated tasks; the main loop never blocks.
}

#endif  // IS_EPAPER_BLE_BRIDGE