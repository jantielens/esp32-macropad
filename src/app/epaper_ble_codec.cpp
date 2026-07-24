#include "board_config.h"

#if HAS_BLE

#include "epaper_ble_codec.h"

#include <string.h>

#if defined(ARDUINO_ARCH_ESP32)
#if !defined(CONFIG_NIMBLE_ENABLED)
#error "E-paper BLE requires the NimBLE host stack."
#endif
#include <mbedtls/md.h>
#include <mbedtls/sha256.h>
#elif defined(EPAPER_BLE_CODEC_HOST_BACKEND)
extern bool epaper_ble_host_sha256(const uint8_t *data, size_t data_len,
                                   uint8_t out[32]);
extern bool epaper_ble_host_hmac_sha256(const uint8_t *key, size_t key_len,
                                        const uint8_t *data, size_t data_len,
                                        uint8_t out[32]);
#else
#error "Define EPAPER_BLE_CODEC_HOST_BACKEND for host codec builds."
#endif

namespace {

constexpr uint8_t kHeaderAssignment =
    (EPAPER_BLE_PROTOCOL_VERSION << 4) | EPAPER_BLE_PACKET_ASSIGNMENT;
constexpr uint8_t kHeaderAck =
    (EPAPER_BLE_PROTOCOL_VERSION << 4) | EPAPER_BLE_PACKET_ACK;

void write_u32_le(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)value;
    out[1] = (uint8_t)(value >> 8);
    out[2] = (uint8_t)(value >> 16);
    out[3] = (uint8_t)(value >> 24);
}

uint32_t read_u32_le(const uint8_t *data) {
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8) |
           ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

EpaperBleCodecResult validate_header(uint16_t company_id, const uint8_t *data,
                                     size_t data_len, uint8_t packet_type) {
    if (company_id != EPAPER_BLE_COMPANY_ID) {
        return EpaperBleCodecResult::NotOurs;
    }
    if (!data || data_len != EPAPER_BLE_PACKET_SIZE) {
        return EpaperBleCodecResult::InvalidLength;
    }
    if ((data[0] >> 4) != EPAPER_BLE_PROTOCOL_VERSION) {
        return EpaperBleCodecResult::UnsupportedVersion;
    }
    if ((data[0] & 0x0F) != packet_type) {
        return EpaperBleCodecResult::WrongPacketType;
    }
    return EpaperBleCodecResult::Ok;
}

bool constant_time_equal(const uint8_t *left, const uint8_t *right,
                         size_t length) {
    if (!left || !right) return false;
    uint8_t difference = 0;
    for (size_t i = 0; i < length; ++i) {
        difference |= left[i] ^ right[i];
    }
    return difference == 0;
}

bool ack_tag(const char *device_api_key, const uint8_t packet_prefix[18],
             uint8_t out[EPAPER_BLE_AUTH_TAG_SIZE]) {
    if (!device_api_key || !packet_prefix || !out) return false;
    uint8_t derived_key[32];
    uint8_t full_mac[32];
    const uint8_t *api_key = reinterpret_cast<const uint8_t *>(device_api_key);
    const uint8_t *context =
        reinterpret_cast<const uint8_t *>(EPAPER_BLE_ACK_CONTEXT);
    const bool ok = epaper_ble_hmac_sha256(
                        api_key, strlen(device_api_key), context,
                        sizeof(EPAPER_BLE_ACK_CONTEXT) - 1, derived_key) &&
                    epaper_ble_hmac_sha256(
                        derived_key, sizeof(derived_key), packet_prefix,
                        EPAPER_BLE_ACK_AUTH_INPUT_SIZE, full_mac);
    if (ok) memcpy(out, full_mac, EPAPER_BLE_AUTH_TAG_SIZE);
    memset(derived_key, 0, sizeof(derived_key));
    memset(full_mac, 0, sizeof(full_mac));
    return ok;
}

}  // namespace

bool epaper_ble_sha256(const uint8_t *data, size_t data_len, uint8_t out[32]) {
    if ((!data && data_len) || !out) return false;
#if defined(ARDUINO_ARCH_ESP32)
    return mbedtls_sha256(data, data_len, out, 0) == 0;
#else
    return epaper_ble_host_sha256(data, data_len, out);
#endif
}

bool epaper_ble_hmac_sha256(const uint8_t *key, size_t key_len,
                           const uint8_t *data, size_t data_len,
                           uint8_t out[32]) {
    if ((!key && key_len) || (!data && data_len) || !out) return false;
#if defined(ARDUINO_ARCH_ESP32)
    const mbedtls_md_info_t *info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    return info && mbedtls_md_hmac(info, key, key_len, data, data_len, out) == 0;
#else
    return epaper_ble_host_hmac_sha256(key, key_len, data, data_len, out);
#endif
}

uint32_t epaper_ble_device_key(const char *device_id) {
    if (!device_id) return 0;
    uint8_t digest[32];
    if (!epaper_ble_sha256(reinterpret_cast<const uint8_t *>(device_id),
                           strlen(device_id), digest)) {
        return 0;
    }
    return read_u32_le(digest);
}

bool epaper_ble_image_key(const char *canonical_image_id, uint8_t out[8]) {
    if (!canonical_image_id || !out) return false;
    uint8_t digest[32];
    if (!epaper_ble_sha256(
            reinterpret_cast<const uint8_t *>(canonical_image_id),
            strlen(canonical_image_id), digest)) {
        return false;
    }
    memcpy(out, digest, 8);
    return true;
}

uint32_t epaper_ble_crc32(const uint8_t *data, size_t data_len) {
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < data_len; ++i) {
        crc ^= data[i];
        for (uint8_t bit = 0; bit < 8; ++bit) {
            crc = (crc >> 1) ^ (0xEDB88320U & (0U - (crc & 1U)));
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

bool epaper_ble_encode_assignment(const EpaperBleAssignmentPacket &packet,
                                  uint8_t out[EPAPER_BLE_PACKET_SIZE]) {
    if (!out) return false;
    out[0] = kHeaderAssignment;
    out[1] = (packet.valid ? EPAPER_BLE_FLAG_VALID : 0) |
             (packet.reserved_flags & EPAPER_BLE_RESERVED_FLAGS_MASK) |
             ((packet.image_format & 0x0F) << 4);
    write_u32_le(out + 2, packet.device_key);
    write_u32_le(out + 6, packet.revision);
    memcpy(out + 10, packet.image_key, 8);
    write_u32_le(out + 18, packet.content_crc32);
    return true;
}

EpaperBleCodecResult epaper_ble_decode_assignment(
    uint16_t company_id, const uint8_t *data, size_t data_len,
    EpaperBleAssignmentPacket *packet) {
    const EpaperBleCodecResult result = validate_header(
        company_id, data, data_len, EPAPER_BLE_PACKET_ASSIGNMENT);
    if (result != EpaperBleCodecResult::Ok || !packet) return result;
    packet->valid = (data[1] & EPAPER_BLE_FLAG_VALID) != 0;
    packet->reserved_flags = data[1] & EPAPER_BLE_RESERVED_FLAGS_MASK;
    packet->image_format = (data[1] >> 4) & 0x0F;
    packet->device_key = read_u32_le(data + 2);
    packet->revision = read_u32_le(data + 6);
    memcpy(packet->image_key, data + 10, 8);
    packet->content_crc32 = read_u32_le(data + 18);
    return EpaperBleCodecResult::Ok;
}

bool epaper_ble_encode_ack(const EpaperBleAckPacket &packet,
                           const char *device_api_key,
                           uint8_t out[EPAPER_BLE_PACKET_SIZE]) {
    if (!out) return false;
    out[0] = kHeaderAck;
    out[1] = packet.result;
    write_u32_le(out + 2, packet.device_key);
    write_u32_le(out + 6, packet.revision);
    memcpy(out + 10, packet.image_key, 8);
    return ack_tag(device_api_key, out, out + 18);
}

EpaperBleCodecResult epaper_ble_decode_ack(
    uint16_t company_id, const uint8_t *data, size_t data_len,
    EpaperBleAckPacket *packet) {
    const EpaperBleCodecResult result = validate_header(
        company_id, data, data_len, EPAPER_BLE_PACKET_ACK);
    if (result != EpaperBleCodecResult::Ok || !packet) return result;
    packet->result = data[1];
    packet->device_key = read_u32_le(data + 2);
    packet->revision = read_u32_le(data + 6);
    memcpy(packet->image_key, data + 10, 8);
    memcpy(packet->auth_tag, data + 18, EPAPER_BLE_AUTH_TAG_SIZE);
    return EpaperBleCodecResult::Ok;
}

EpaperBleCodecResult epaper_ble_verify_ack(
    uint16_t company_id, const uint8_t *data, size_t data_len,
    const char *device_api_key, uint32_t expected_device_key,
    uint32_t expected_revision, const uint8_t expected_image_key[8],
    EpaperBleAckPacket *packet) {
    EpaperBleAckPacket decoded = {};
    const EpaperBleCodecResult result = epaper_ble_decode_ack(
        company_id, data, data_len, &decoded);
    if (result != EpaperBleCodecResult::Ok) return result;
    if (decoded.result != 1 || decoded.device_key != expected_device_key ||
        decoded.revision != expected_revision ||
        !constant_time_equal(decoded.image_key, expected_image_key, 8)) {
        return EpaperBleCodecResult::AssignmentMismatch;
    }
    uint8_t expected_tag[EPAPER_BLE_AUTH_TAG_SIZE];
    if (!ack_tag(device_api_key, data, expected_tag) ||
        !constant_time_equal(expected_tag, decoded.auth_tag,
                             EPAPER_BLE_AUTH_TAG_SIZE)) {
        return EpaperBleCodecResult::AuthenticationFailed;
    }
    if (packet) *packet = decoded;
    return EpaperBleCodecResult::Ok;
}

int epaper_ble_revision_compare(uint32_t left, uint32_t right) {
    if (left == right) return 0;
    return (uint32_t)(left - right) < 0x80000000U ? 1 : -1;
}

bool epaper_ble_codec_self_test() {
    static const uint8_t expected_image_key[8] = {
        0x9c, 0xe8, 0x5b, 0x84, 0x9b, 0x50, 0x4e, 0xb7,
    };
    static const uint8_t expected_tag[4] = {0x2c, 0xda, 0x9f, 0x16};
    EpaperBleAckPacket ack = {};
    ack.result = 1;
    ack.device_key = epaper_ble_device_key("frame-a");
    ack.revision = 0x12345678U;
    memcpy(ack.image_key, expected_image_key, sizeof(ack.image_key));
    uint8_t encoded[EPAPER_BLE_PACKET_SIZE];
    uint8_t image_key[8];
    return ack.device_key == 2843833127U &&
           epaper_ble_image_key("photos/2026/07/sunrise.jpg", image_key) &&
           constant_time_equal(image_key, expected_image_key, sizeof(image_key)) &&
           epaper_ble_encode_ack(ack, "test-device-api-key-01", encoded) &&
           constant_time_equal(encoded + 18, expected_tag, sizeof(expected_tag));
}

#endif  // HAS_BLE