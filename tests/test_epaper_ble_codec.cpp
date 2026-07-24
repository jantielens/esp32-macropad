#include "epaper_ble_codec.h"

#include <ArduinoJson.h>
#include <assert.h>
#include <fstream>
#include <string.h>

namespace {

uint8_t hex_nibble(char value) {
    if (value >= '0' && value <= '9') return (uint8_t)(value - '0');
    if (value >= 'a' && value <= 'f') return (uint8_t)(value - 'a' + 10);
    assert(false);
    return 0;
}

void parse_hex(const char *text, uint8_t *out, size_t output_len) {
    assert(text && strlen(text) == output_len * 2);
    for (size_t i = 0; i < output_len; ++i) {
        out[i] = (uint8_t)((hex_nibble(text[i * 2]) << 4) |
                           hex_nibble(text[i * 2 + 1]));
    }
}

JsonDocument load_vectors() {
    std::ifstream input("tests/epaper_ble_vectors.json");
    assert(input.good());
    std::string json((std::istreambuf_iterator<char>(input)),
                     std::istreambuf_iterator<char>());
    JsonDocument document;
    assert(deserializeJson(document, json) == DeserializationError::Ok);
    return document;
}

void test_vectors(JsonDocument &vectors) {
    for (JsonObject vector : vectors["device_ids"].as<JsonArray>()) {
        const char *value = vector["value"];
        assert(epaper_ble_device_key(value) == vector["device_key_le"].as<uint32_t>());
        uint8_t digest[32];
        assert(epaper_ble_sha256(reinterpret_cast<const uint8_t *>(value),
                                 strlen(value), digest));
        uint8_t expected[4];
        parse_hex(vector["sha256_prefix_hex"], expected, sizeof(expected));
        assert(memcmp(digest, expected, sizeof(expected)) == 0);
    }
    uint8_t image_key[8];
    uint8_t expected_image_key[8];
    parse_hex(vectors["image_key_hex"], expected_image_key,
              sizeof(expected_image_key));
    assert(epaper_ble_image_key(vectors["image_id"], image_key));
    assert(memcmp(image_key, expected_image_key, sizeof(image_key)) == 0);
        assert(vectors["format_codes"]["g16z"].as<uint8_t>() ==
            EPAPER_BLE_FORMAT_G16Z);
        assert(vectors["format_codes"]["jpeg"].as<uint8_t>() ==
            EPAPER_BLE_FORMAT_JPEG);
        assert(vectors["format_codes"]["g16p"].as<uint8_t>() ==
            EPAPER_BLE_FORMAT_G16P);
    const char *crc_input = vectors["crc_input"];
    assert(epaper_ble_crc32(reinterpret_cast<const uint8_t *>(crc_input),
                           strlen(crc_input)) == vectors["crc32"].as<uint32_t>());
    assert(epaper_ble_codec_self_test());
}

void test_assignment() {
    EpaperBleAssignmentPacket input = {};
    input.valid = true;
    input.image_format = EPAPER_BLE_FORMAT_G16Z;
    input.device_key = 0xA1B2C3D4;
    input.revision = 0x10203040;
    const uint8_t image_key[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    memcpy(input.image_key, image_key, sizeof(image_key));
    input.content_crc32 = 0x89ABCDEF;
    uint8_t encoded[EPAPER_BLE_PACKET_SIZE];
    assert(epaper_ble_encode_assignment(input, encoded));
    const uint8_t expected[EPAPER_BLE_PACKET_SIZE] = {
        0x11, 0x11, 0xd4, 0xc3, 0xb2, 0xa1, 0x40, 0x30, 0x20, 0x10,
        0, 1, 2, 3, 4, 5, 6, 7, 0xef, 0xcd, 0xab, 0x89,
    };
    assert(memcmp(encoded, expected, sizeof(expected)) == 0);

    EpaperBleAssignmentPacket decoded = {};
    assert(epaper_ble_decode_assignment(0x1234, encoded, sizeof(encoded), &decoded) ==
           EpaperBleCodecResult::NotOurs);
    const size_t invalid_lengths[] = {0, 1, 17, 21, 23, 30, 31, 255};
    uint8_t oversized[255] = {};
    for (size_t length : invalid_lengths) {
        const uint8_t *data = length <= sizeof(encoded) ? encoded : oversized;
        assert(epaper_ble_decode_assignment(EPAPER_BLE_COMPANY_ID, data, length,
                                            &decoded) ==
               EpaperBleCodecResult::InvalidLength);
    }
    assert(epaper_ble_decode_assignment(EPAPER_BLE_COMPANY_ID, encoded,
                                        sizeof(encoded), &decoded) ==
           EpaperBleCodecResult::Ok);
    assert(decoded.valid && decoded.device_key == input.device_key &&
           decoded.revision == input.revision &&
           decoded.content_crc32 == input.content_crc32);

    for (uint8_t format = 0; format <= 0x0F; ++format) {
        input.image_format = format;
        assert(epaper_ble_encode_assignment(input, encoded));
        assert(epaper_ble_decode_assignment(EPAPER_BLE_COMPANY_ID, encoded,
                                            sizeof(encoded), &decoded) ==
               EpaperBleCodecResult::Ok);
        assert(decoded.image_format == format);
    }
    input.valid = false;
    input.revision = 0;
    input.reserved_flags = 0x0E;
    assert(epaper_ble_encode_assignment(input, encoded));
    assert(epaper_ble_decode_assignment(EPAPER_BLE_COMPANY_ID, encoded,
                                        sizeof(encoded), &decoded) ==
           EpaperBleCodecResult::Ok);
    assert(!decoded.valid && decoded.revision == 0 && decoded.reserved_flags == 0x0E);

    encoded[0] = 0x21;
    assert(epaper_ble_decode_assignment(EPAPER_BLE_COMPANY_ID, encoded,
                                        sizeof(encoded), &decoded) ==
           EpaperBleCodecResult::UnsupportedVersion);
    encoded[0] = 0x01;
    assert(epaper_ble_decode_assignment(EPAPER_BLE_COMPANY_ID, encoded,
                                        sizeof(encoded), &decoded) ==
           EpaperBleCodecResult::UnsupportedVersion);
    encoded[0] = 0x12;
    assert(epaper_ble_decode_assignment(EPAPER_BLE_COMPANY_ID, encoded,
                                        sizeof(encoded), &decoded) ==
           EpaperBleCodecResult::WrongPacketType);
    encoded[0] = 0x13;
    assert(epaper_ble_decode_assignment(EPAPER_BLE_COMPANY_ID, encoded,
                                        sizeof(encoded), &decoded) ==
           EpaperBleCodecResult::WrongPacketType);
}

void test_ack(JsonDocument &vectors) {
    uint8_t prefix[EPAPER_BLE_ACK_AUTH_INPUT_SIZE];
    uint8_t expected_tag[EPAPER_BLE_AUTH_TAG_SIZE];
    parse_hex(vectors["ack_prefix_hex"], prefix, sizeof(prefix));
    parse_hex(vectors["ack_tag_hex"], expected_tag, sizeof(expected_tag));
    EpaperBleAckPacket packet = {};
    packet.result = 1;
    packet.device_key = 2843833127U;
    packet.revision = 0x12345678;
    memcpy(packet.image_key, prefix + 10, sizeof(packet.image_key));
    uint8_t encoded[EPAPER_BLE_PACKET_SIZE];
    assert(epaper_ble_encode_ack(packet, vectors["api_key"], encoded));
    assert(memcmp(encoded, prefix, sizeof(prefix)) == 0);
    assert(memcmp(encoded + 18, expected_tag, sizeof(expected_tag)) == 0);
    EpaperBleAckPacket decoded = {};
    assert(epaper_ble_verify_ack(EPAPER_BLE_COMPANY_ID, encoded, sizeof(encoded),
                                 vectors["api_key"], packet.device_key,
                                 packet.revision, packet.image_key, &decoded) ==
           EpaperBleCodecResult::Ok);
        const size_t invalid_lengths[] = {0, 1, 17, 21, 23, 30, 31, 255};
        uint8_t oversized[255] = {};
        for (size_t length : invalid_lengths) {
         const uint8_t *data = length <= sizeof(encoded) ? encoded : oversized;
         assert(epaper_ble_decode_ack(EPAPER_BLE_COMPANY_ID, data, length,
                         &decoded) ==
             EpaperBleCodecResult::InvalidLength);
        }
        assert(epaper_ble_decode_ack(0x1234, encoded, sizeof(encoded), &decoded) ==
            EpaperBleCodecResult::NotOurs);
        encoded[0] = 0x11;
        assert(epaper_ble_decode_ack(EPAPER_BLE_COMPANY_ID, encoded, sizeof(encoded),
                        &decoded) == EpaperBleCodecResult::WrongPacketType);
        encoded[0] = 0x12;
        assert(epaper_ble_verify_ack(EPAPER_BLE_COMPANY_ID, encoded, sizeof(encoded),
                         "test-device-api-key-00", packet.device_key,
                         packet.revision, packet.image_key, nullptr) ==
            EpaperBleCodecResult::AuthenticationFailed);
    encoded[18] ^= 1;
    assert(epaper_ble_verify_ack(EPAPER_BLE_COMPANY_ID, encoded, sizeof(encoded),
                                 vectors["api_key"], packet.device_key,
                                 packet.revision, packet.image_key, nullptr) ==
           EpaperBleCodecResult::AuthenticationFailed);
    encoded[18] ^= 1;
    encoded[10] ^= 1;
    assert(epaper_ble_verify_ack(EPAPER_BLE_COMPANY_ID, encoded, sizeof(encoded),
                                 vectors["api_key"], packet.device_key,
                                 packet.revision, packet.image_key, nullptr) ==
           EpaperBleCodecResult::AssignmentMismatch);
}

void test_revisions() {
    const uint32_t revisions[] = {0, 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF};
    for (uint32_t revision : revisions) {
        EpaperBleAssignmentPacket input = {};
        input.revision = revision;
        uint8_t encoded[EPAPER_BLE_PACKET_SIZE];
        EpaperBleAssignmentPacket decoded = {};
        assert(epaper_ble_encode_assignment(input, encoded));
        assert(epaper_ble_decode_assignment(EPAPER_BLE_COMPANY_ID, encoded,
                                            sizeof(encoded), &decoded) ==
               EpaperBleCodecResult::Ok);
        assert(decoded.revision == revision);
    }
    assert(epaper_ble_revision_compare(0x00000005, 0xFFFFFFF0) > 0);
    assert(epaper_ble_revision_compare(0xFFFFFFF0, 0x00000005) < 0);
    assert(epaper_ble_revision_compare(7, 7) == 0);
}

}  // namespace

int main() {
    JsonDocument vectors = load_vectors();
    test_vectors(vectors);
    test_assignment();
    test_ack(vectors);
    test_revisions();
    return 0;
}