#!/usr/bin/env python3
import binascii
import hashlib
import hmac
import json
from pathlib import Path


vectors = json.loads(Path("tests/epaper_ble_vectors.json").read_text(encoding="utf-8"))

for vector in vectors["device_ids"]:
    digest = hashlib.sha256(vector["value"].encode("utf-8")).digest()
    assert digest[:4].hex() == vector["sha256_prefix_hex"]
    assert int.from_bytes(digest[:4], "little") == vector["device_key_le"]

image_digest = hashlib.sha256(vectors["image_id"].encode("utf-8")).digest()
assert image_digest[:8].hex() == vectors["image_key_hex"]
assert binascii.crc32(vectors["crc_input"].encode("utf-8")) == vectors["crc32"]
assert vectors["format_codes"] == {"g16z": 1, "jpeg": 2, "g16p": 3}
for format_code in range(16):
    flags = 0x01 | (format_code << 4)
    assert (flags >> 4) & 0x0F == format_code

context = b"esp32-macropad/epaper-ble-ack/v1"
derived_key = hmac.new(vectors["api_key"].encode("utf-8"), context, hashlib.sha256).digest()
packet_prefix = bytes.fromhex(vectors["ack_prefix_hex"])
tag = hmac.new(derived_key, packet_prefix, hashlib.sha256).digest()[:4]
assert tag.hex() == vectors["ack_tag_hex"]
tampered = bytearray(packet_prefix)
tampered[0] ^= 1
assert hmac.new(derived_key, tampered, hashlib.sha256).digest()[:4] != tag
wrong_key = bytearray(derived_key)
wrong_key[0] ^= 1
assert hmac.new(wrong_key, packet_prefix, hashlib.sha256).digest()[:4] != tag

print("e-paper BLE Python vectors OK")