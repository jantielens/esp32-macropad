#!/usr/bin/env python3
"""Generate and verify profile-specific photoframe Version 1 vectors."""

from __future__ import annotations

import argparse
import hashlib
import importlib
import io
import json
from pathlib import Path
import struct
import sys
import zlib


KIT_DIR = Path(__file__).resolve().parent
REPO_ROOT = Path(__file__).resolve().parents[5]
MANIFEST_NAME = "manifest.json"
G16_HEADER = struct.Struct("<4sBBHHII")
MEDIA_TYPES = {
    1: "image/jpeg",
    2: "application/vnd.photoframe.g16p",
    3: "application/vnd.photoframe.g16z",
}


def gzip_wrap(data: bytes) -> bytes:
    """Wrap data in a gzip container with byte-reproducible framing.

    ``gzip.compress`` is not stable across Python versions: releases up to 3.12
    emit the zlib OS byte (0x03 on Unix) while 3.13 and later normalise it to
    0xff (unknown). Committed goldens must regenerate identically everywhere, so
    the 10-byte header is written explicitly with a pinned OS byte.
    """
    header = b"\x1f\x8b\x08\x00" + b"\x00\x00\x00\x00" + b"\x02\xff"
    compressor = zlib.compressobj(9, zlib.DEFLATED, -zlib.MAX_WBITS)
    body = compressor.compress(data) + compressor.flush()
    trailer = struct.pack("<II", zlib.crc32(data) & 0xFFFFFFFF, len(data) & 0xFFFFFFFF)
    return header + body + trailer


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as source:
        return json.load(source)


def resolve_profile(value: str) -> Path:
    candidate = Path(value)
    if candidate.is_dir():
        return candidate.resolve()
    bundled = KIT_DIR / "profiles" / value
    if bundled.is_dir():
        return bundled
    raise ValueError(f"profile not found: {value}")


def validate_profile(profile: dict) -> list[str]:
    errors: list[str] = []
    if profile.get("schemaVersion") != 1:
        errors.append("profile schemaVersion must be 1")
    if not isinstance(profile.get("id"), str) or not profile["id"]:
        errors.append("profile id must be a non-empty string")
    geometry = profile.get("geometry", {})
    width = geometry.get("width")
    height = geometry.get("height")
    if not isinstance(width, int) or width <= 0:
        errors.append("profile width must be a positive integer")
    if not isinstance(height, int) or height <= 0:
        errors.append("profile height must be a positive integer")
    formats = profile.get("formats")
    if not isinstance(formats, list) or not formats:
        errors.append("profile formats must be a non-empty array")
    else:
        observed_codes: set[int] = set()
        for item in formats:
            code = item.get("code") if isinstance(item, dict) else None
            media_type = item.get("mediaType") if isinstance(item, dict) else None
            if code not in MEDIA_TYPES:
                errors.append(f"unsupported Version 1 format code: {code}")
            elif MEDIA_TYPES[code] != media_type:
                errors.append(f"format code {code} must use {MEDIA_TYPES[code]}")
            if code in observed_codes:
                errors.append(f"duplicate format code: {code}")
            observed_codes.add(code)
        if observed_codes.intersection({2, 3}):
            if isinstance(width, int) and width % 2:
                errors.append("profile width must be even when G16P or G16Z is supported")
            if isinstance(width, int) and width > 0xFFFF:
                errors.append("profile width exceeds the G16P header field")
            if isinstance(height, int) and height > 0xFFFF:
                errors.append("profile height exceeds the G16P header field")
    return errors


def validate_g16p(data: bytes, width: int, height: int) -> list[str]:
    errors: list[str] = []
    if len(data) < G16_HEADER.size:
        return ["g16p-header-length"]
    magic, version, flags, actual_width, actual_height, payload_length, payload_crc = (
        G16_HEADER.unpack_from(data)
    )
    payload = data[G16_HEADER.size :]
    if magic != b"G16P":
        errors.append("g16p-magic")
    if version != 1:
        errors.append("g16p-version")
    if flags != 0:
        errors.append("g16p-flags")
    if actual_width % 2:
        errors.append("g16p-even-width")
    if (actual_width, actual_height) != (width, height):
        errors.append("g16p-geometry")
    expected_payload_length = actual_width * actual_height // 2
    if payload_length != expected_payload_length:
        errors.append("g16p-payload-length-field")
    if len(payload) != payload_length:
        errors.append("g16p-total-length")
    if zlib.crc32(payload) & 0xFFFFFFFF != payload_crc:
        errors.append("g16p-payload-crc32")
    return errors


def validate_g16z(data: bytes, width: int, height: int) -> list[str]:
    if not data.startswith(b"G16Z"):
        return ["g16z-magic"]
    expected_size = G16_HEADER.size + width * height // 2
    inflater = zlib.decompressobj(-15)
    try:
        decompressed = inflater.decompress(data[4:], expected_size + 1)
        decompressed += inflater.flush()
    except zlib.error:
        return ["g16z-raw-deflate"]
    errors: list[str] = []
    if len(decompressed) > expected_size or inflater.unconsumed_tail:
        errors.append("g16z-decompressed-length")
    if not inflater.eof:
        errors.append("g16z-truncated-deflate")
    if inflater.unused_data:
        errors.append("g16z-trailing-bytes")
    errors.extend(validate_g16p(decompressed, width, height))
    return errors


def jpeg_frame(data: bytes) -> tuple[int, int, int] | None:
    if not data.startswith(b"\xff\xd8"):
        return None
    offset = 2
    while offset + 4 <= len(data):
        if data[offset] != 0xFF:
            return None
        while offset < len(data) and data[offset] == 0xFF:
            offset += 1
        if offset >= len(data):
            return None
        marker = data[offset]
        offset += 1
        if marker in (0x01, *range(0xD0, 0xDA)):
            continue
        if marker == 0xD9:
            return None
        if offset + 2 > len(data):
            return None
        segment_length = int.from_bytes(data[offset : offset + 2], "big")
        if segment_length < 2 or offset + segment_length > len(data):
            return None
        if marker in range(0xC0, 0xD0) and marker not in (0xC4, 0xC8, 0xCC):
            if segment_length < 7:
                return None
            height = int.from_bytes(data[offset + 3 : offset + 5], "big")
            width = int.from_bytes(data[offset + 5 : offset + 7], "big")
            return marker, width, height
        offset += segment_length
    return None


def validate_jpeg(data: bytes, width: int, height: int) -> list[str]:
    frame = jpeg_frame(data)
    if frame is None:
        return ["jpeg-malformed"]
    marker, actual_width, actual_height = frame
    errors: list[str] = []
    if marker != 0xC0:
        errors.append("jpeg-progressive")
    if (actual_width, actual_height) != (width, height):
        errors.append("jpeg-geometry")
    return errors


def validate_vector(data: bytes, kind: str, width: int, height: int) -> list[str]:
    if kind == "crc32-only":
        return []
    if kind == "jpeg":
        return validate_jpeg(data, width, height)
    if kind == "g16p":
        return validate_g16p(data, width, height)
    if kind == "g16z":
        return validate_g16z(data, width, height)
    return ["unknown-vector-kind"]


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def mutate_u16_le(data: bytes, offset: int, value: int) -> bytes:
    changed = bytearray(data)
    changed[offset : offset + 2] = value.to_bytes(2, "little")
    return bytes(changed)


def mutate_u32_le(data: bytes, offset: int, value: int) -> bytes:
    changed = bytearray(data)
    changed[offset : offset + 4] = value.to_bytes(4, "little")
    return bytes(changed)


def mutate_jpeg_width(data: bytes, value: int) -> bytes:
    marker = data.find(b"\xff\xc0")
    if marker < 0:
        raise ValueError("baseline JPEG has no SOF0 marker")
    changed = bytearray(data)
    changed[marker + 7 : marker + 9] = value.to_bytes(2, "big")
    return bytes(changed)


def production_gray16():
    module_dir = REPO_ROOT / "tools" / "photoframe-site"
    sys.path.insert(0, str(module_dir))
    try:
        return importlib.import_module("gray16")
    finally:
        sys.path.pop(0)


def generate_vectors(profile_dir: Path, profile: dict) -> None:
    try:
        from PIL import Image
    except ImportError as exc:
        raise RuntimeError("generation requires Pillow") from exc

    gray16 = production_gray16()
    width = profile["geometry"]["width"]
    height = profile["geometry"]["height"]
    supported_codes = {item["code"] for item in profile["formats"]}
    row = bytes((((index % 16) << 4) | (15 - (index % 16))) for index in range(width // 2))
    payload = row * height
    g16p = gray16.pack_g16p(payload, width, height)
    g16z = gray16.wrap_g16z(g16p)
    if not g16z.startswith(b"G16Z"):
        raise RuntimeError("production encoder did not compress the deterministic G16P fixture")

    source = Image.new("RGB", (width, height), (232, 232, 232))
    pixels = source.load()
    for y_start, color in ((0, (20, 40, 70)), (height // 3, (180, 90, 30)), (2 * height // 3, (240, 220, 80))):
        y_end = min(y_start + height // 3, height)
        for y in range(y_start, y_end):
            for x in range(width):
                if (x // 96 + y // 96) % 2 == 0:
                    pixels[x, y] = color
    jpeg, _ = gray16.encode_jpeg(source, width=width, height=height, grayscale=False)

    progressive_buffer = io.BytesIO()
    source.save(progressive_buffer, format="JPEG", quality=90, progressive=True)
    progressive_jpeg = progressive_buffer.getvalue()

    g16p_bad_magic = b"X" + g16p[1:]
    g16p_odd_width = mutate_u16_le(g16p, 6, width - 1)
    g16p_wrong_geometry = mutate_u16_le(g16p, 6, width + 2)
    g16p_wrong_length = mutate_u32_le(g16p, 10, len(payload) + 1)
    payload_crc = int.from_bytes(g16p[14:18], "little")
    g16p_bad_crc = mutate_u32_le(g16p, 14, payload_crc ^ 1)
    compressor = zlib.compressobj(9, zlib.DEFLATED, -15)
    raw_deflate = compressor.compress(g16p) + compressor.flush()

    artifacts = [
        ("valid/transport-crc32-zero.bin", b"", "crc32-only", True, None, None),
    ]
    if 1 in supported_codes:
        artifacts.extend(
            [
                ("valid/baseline.jpg", jpeg, "jpeg", True, None, 1),
                ("invalid/progressive.jpg", progressive_jpeg, "jpeg", False, "jpeg-progressive", 1),
                ("invalid/wrong-dimensions.jpg", mutate_jpeg_width(jpeg, width - 2), "jpeg", False, "jpeg-geometry", 1),
            ]
        )
    if 2 in supported_codes:
        artifacts.extend(
            [
                ("valid/frame.g16p", g16p, "g16p", True, None, 2),
                ("invalid/bad-magic.g16p", g16p_bad_magic, "g16p", False, "g16p-magic", 2),
                ("invalid/odd-width.g16p", g16p_odd_width, "g16p", False, "g16p-even-width", 2),
                ("invalid/wrong-dimensions.g16p", g16p_wrong_geometry, "g16p", False, "g16p-geometry", 2),
                ("invalid/wrong-payload-length.g16p", g16p_wrong_length, "g16p", False, "g16p-payload-length-field", 2),
                ("invalid/bad-payload-crc.g16p", g16p_bad_crc, "g16p", False, "g16p-payload-crc32", 2),
            ]
        )
    if 3 in supported_codes:
        artifacts.extend(
            [
                ("valid/frame.g16z", g16z, "g16z", True, None, 3),
                ("invalid/zlib-wrapper.g16z", b"G16Z" + zlib.compress(g16p), "g16z", False, "g16z-raw-deflate", 3),
                ("invalid/gzip-wrapper.g16z", b"G16Z" + gzip_wrap(g16p), "g16z", False, "g16z-raw-deflate", 3),
                ("invalid/trailing-bytes.g16z", b"G16Z" + raw_deflate + b"\x00", "g16z", False, "g16z-trailing-bytes", 3),
            ]
        )

    vectors_dir = profile_dir / "vectors"
    manifest_entries = []
    for relative_name, data, kind, valid, expected_error, format_code in artifacts:
        destination = vectors_dir / relative_name
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
        entry = {
            "path": relative_name,
            "kind": kind,
            "valid": valid,
            "size": len(data),
            "sha256": sha256(data),
            "contentCrc32": f"{zlib.crc32(data) & 0xFFFFFFFF:08x}",
        }
        if format_code is not None:
            entry["formatCode"] = format_code
            entry["mediaType"] = MEDIA_TYPES[format_code]
        if expected_error is not None:
            entry["expectedError"] = expected_error
        manifest_entries.append(entry)

    manifest = {
        "schemaVersion": 1,
        "profile": profile["id"],
        "generator": "tools/photoframe-site/gray16.py",
        "vectors": manifest_entries,
    }
    manifest_path = vectors_dir / MANIFEST_NAME
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


def verify_assertions() -> list[str]:
    errors: list[str] = []
    assertions = load_json(KIT_DIR / "assertions.json")
    if assertions.get("schemaVersion") != 1 or assertions.get("protocolMajor") != 1:
        errors.append("assertions must use schemaVersion 1 and protocolMajor 1")
    scenarios = assertions.get("scenarios")
    if not isinstance(scenarios, list) or not scenarios:
        return errors + ["assertions scenarios must be a non-empty array"]
    observed: set[str] = set()
    for scenario in scenarios:
        scenario_id = scenario.get("id") if isinstance(scenario, dict) else None
        if not isinstance(scenario_id, str) or not scenario_id:
            errors.append("every assertion requires a non-empty id")
            continue
        if scenario_id in observed:
            errors.append(f"duplicate assertion id: {scenario_id}")
        observed.add(scenario_id)
        for field in ("role", "setup", "action", "assert"):
            if not scenario.get(field):
                errors.append(f"assertion {scenario_id} requires {field}")
    return errors


def verify_vectors(profile_dir: Path, profile: dict) -> list[str]:
    errors: list[str] = []
    width = profile["geometry"]["width"]
    height = profile["geometry"]["height"]
    vectors_dir = profile_dir / "vectors"
    manifest_path = vectors_dir / MANIFEST_NAME
    if not manifest_path.is_file():
        return [f"missing vector manifest: {manifest_path}"]
    manifest = load_json(manifest_path)
    if manifest.get("schemaVersion") != 1:
        errors.append("vector manifest schemaVersion must be 1")
    if manifest.get("profile") != profile["id"]:
        errors.append("vector manifest profile does not match profile id")
    entries = manifest.get("vectors")
    if not isinstance(entries, list) or not entries:
        return errors + ["vector manifest must contain vectors"]
    supported = {item["code"]: item["mediaType"] for item in profile["formats"]}
    observed_paths: set[str] = set()
    valid_count = 0
    invalid_count = 0
    for entry in entries:
        relative_path = entry.get("path")
        if not isinstance(relative_path, str) or not relative_path:
            errors.append("vector entry has no path")
            continue
        if relative_path in observed_paths:
            errors.append(f"duplicate vector path: {relative_path}")
            continue
        observed_paths.add(relative_path)
        vector_path = vectors_dir / relative_path
        if not vector_path.is_file():
            errors.append(f"missing vector: {relative_path}")
            continue
        data = vector_path.read_bytes()
        if entry.get("size") != len(data):
            errors.append(f"size mismatch: {relative_path}")
        if entry.get("sha256") != sha256(data):
            errors.append(f"SHA-256 mismatch: {relative_path}")
        actual_crc = f"{zlib.crc32(data) & 0xFFFFFFFF:08x}"
        if entry.get("contentCrc32") != actual_crc:
            errors.append(f"content CRC32 mismatch: {relative_path}")
        format_code = entry.get("formatCode")
        if format_code is not None and supported.get(format_code) != entry.get("mediaType"):
            errors.append(f"vector format is not supported by profile: {relative_path}")
        vector_errors = validate_vector(data, entry.get("kind"), width, height)
        if entry.get("valid") is True:
            valid_count += 1
            if vector_errors:
                errors.append(f"valid vector rejected ({relative_path}): {', '.join(vector_errors)}")
        elif entry.get("valid") is False:
            invalid_count += 1
            expected_error = entry.get("expectedError")
            if not vector_errors:
                errors.append(f"invalid vector accepted: {relative_path}")
            elif expected_error not in vector_errors:
                errors.append(
                    f"invalid vector {relative_path} did not produce {expected_error}: "
                    f"{', '.join(vector_errors)}"
                )
        else:
            errors.append(f"vector validity must be boolean: {relative_path}")
    if valid_count == 0 or invalid_count == 0:
        errors.append("manifest must contain valid and invalid vectors")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("profile", help="bundled profile name or profile directory")
    parser.add_argument(
        "--generate",
        action="store_true",
        help="regenerate deterministic vectors with the production encoders",
    )
    args = parser.parse_args()
    try:
        profile_dir = resolve_profile(args.profile)
        profile = load_json(profile_dir / "profile.json")
        errors = validate_profile(profile)
        if errors:
            raise ValueError("; ".join(errors))
        if args.generate:
            generate_vectors(profile_dir, profile)
        errors = verify_assertions() + verify_vectors(profile_dir, profile)
    except (OSError, ValueError, RuntimeError, json.JSONDecodeError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    if errors:
        for error in errors:
            print(f"FAIL: {error}", file=sys.stderr)
        return 1
    manifest = load_json(profile_dir / "vectors" / MANIFEST_NAME)
    valid_count = sum(entry["valid"] is True for entry in manifest["vectors"])
    invalid_count = sum(entry["valid"] is False for entry in manifest["vectors"])
    print(
        f"PASS: {profile['id']} ({valid_count} valid vectors, "
        f"{invalid_count} expected-invalid vectors)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())