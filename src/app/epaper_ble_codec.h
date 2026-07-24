#ifndef EPAPER_BLE_CODEC_H
#define EPAPER_BLE_CODEC_H

#include <stddef.h>
#include <stdint.h>

constexpr uint16_t EPAPER_BLE_COMPANY_ID = 0xFFFF;
constexpr uint8_t EPAPER_BLE_PROTOCOL_VERSION = 1;
constexpr uint8_t EPAPER_BLE_PACKET_ASSIGNMENT = 1;
constexpr uint8_t EPAPER_BLE_PACKET_ACK = 2;
constexpr size_t EPAPER_BLE_PACKET_SIZE = 22;
constexpr size_t EPAPER_BLE_ACK_AUTH_INPUT_SIZE = 18;
constexpr size_t EPAPER_BLE_AUTH_TAG_SIZE = 4;
constexpr size_t EPAPER_BLE_MANUFACTURER_DATA_SIZE =
    sizeof(EPAPER_BLE_COMPANY_ID) + EPAPER_BLE_PACKET_SIZE;

constexpr uint8_t EPAPER_BLE_FLAG_VALID = 0x01;
constexpr uint8_t EPAPER_BLE_RESERVED_FLAGS_MASK = 0x0E;
constexpr uint8_t EPAPER_BLE_FORMAT_G16Z = 0x01;
constexpr uint8_t EPAPER_BLE_FORMAT_JPEG = 0x02;
constexpr uint8_t EPAPER_BLE_FORMAT_G16P = 0x03;

constexpr char EPAPER_BLE_ACK_CONTEXT[] =
    "esp32-macropad/epaper-ble-ack/v1";

static_assert(3 + 2 + EPAPER_BLE_MANUFACTURER_DATA_SIZE <= 31,
              "E-paper BLE packet exceeds legacy advertisement capacity");

enum class EpaperBleCodecResult : uint8_t {
    Ok,
    NotOurs,
    InvalidLength,
    UnsupportedVersion,
    WrongPacketType,
    AuthenticationFailed,
    AssignmentMismatch,
};

struct EpaperBleAssignmentPacket {
    bool valid;
    uint8_t reserved_flags;
    uint8_t image_format;
    uint32_t device_key;
    uint32_t revision;
    uint8_t image_key[8];
    uint32_t content_crc32;
};

struct EpaperBleAckPacket {
    uint8_t result;
    uint32_t device_key;
    uint32_t revision;
    uint8_t image_key[8];
    uint8_t auth_tag[EPAPER_BLE_AUTH_TAG_SIZE];
};

bool epaper_ble_sha256(const uint8_t *data, size_t data_len, uint8_t out[32]);
bool epaper_ble_hmac_sha256(const uint8_t *key, size_t key_len,
                           const uint8_t *data, size_t data_len,
                           uint8_t out[32]);
uint32_t epaper_ble_device_key(const char *device_id);
bool epaper_ble_image_key(const char *canonical_image_id, uint8_t out[8]);
uint32_t epaper_ble_crc32(const uint8_t *data, size_t data_len);

bool epaper_ble_encode_assignment(const EpaperBleAssignmentPacket &packet,
                                  uint8_t out[EPAPER_BLE_PACKET_SIZE]);
EpaperBleCodecResult epaper_ble_decode_assignment(
    uint16_t company_id, const uint8_t *data, size_t data_len,
    EpaperBleAssignmentPacket *packet);

bool epaper_ble_encode_ack(const EpaperBleAckPacket &packet,
                           const char *device_api_key,
                           uint8_t out[EPAPER_BLE_PACKET_SIZE]);
EpaperBleCodecResult epaper_ble_decode_ack(
    uint16_t company_id, const uint8_t *data, size_t data_len,
    EpaperBleAckPacket *packet);
EpaperBleCodecResult epaper_ble_verify_ack(
    uint16_t company_id, const uint8_t *data, size_t data_len,
    const char *device_api_key, uint32_t expected_device_key,
    uint32_t expected_revision, const uint8_t expected_image_key[8],
    EpaperBleAckPacket *packet);

int epaper_ble_revision_compare(uint32_t left, uint32_t right);
bool epaper_ble_codec_self_test();

#endif
