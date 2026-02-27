#!/usr/bin/env python3

import argparse
import os
import struct
import sys
from dataclasses import dataclass
from typing import Iterable, Optional, Tuple


PARTITION_ENTRY_SIZE = 32
PARTITION_MAGIC = 0x50AA  # little-endian 0xAA50
PARTITION_END_MARKER = b"\xFF" * PARTITION_ENTRY_SIZE
PARTITION_MD5_MAGIC = 0xEBEB


@dataclass(frozen=True)
class PartitionEntry:
    type: int
    subtype: int
    offset: int
    size: int
    label: str
    flags: int


def _iter_entries(blob: bytes) -> Iterable[PartitionEntry]:
    if len(blob) < PARTITION_ENTRY_SIZE:
        return

    for i in range(0, len(blob) - PARTITION_ENTRY_SIZE + 1, PARTITION_ENTRY_SIZE):
        entry = blob[i : i + PARTITION_ENTRY_SIZE]
        if entry == PARTITION_END_MARKER:
            return

        magic, ptype, subtype = struct.unpack_from("<HBB", entry, 0)
        # ESP-IDF may append an MD5 checksum record (0xEBEB...) and/or trailing 0xFFFF entries.
        if magic in (0xFFFF, PARTITION_MD5_MAGIC):
            return

        if magic != PARTITION_MAGIC:
            raise ValueError(
                f"Invalid partition entry magic at index {i // PARTITION_ENTRY_SIZE}: 0x{magic:04x}"
            )

        offset, size = struct.unpack_from("<II", entry, 4)
        raw_label = entry[12:28]
        label = raw_label.split(b"\x00", 1)[0].decode("ascii", errors="replace")
        flags = struct.unpack_from("<I", entry, 28)[0]

        yield PartitionEntry(
            type=ptype,
            subtype=subtype,
            offset=offset,
            size=size,
            label=label,
            flags=flags,
        )


def _read_file(path: str) -> bytes:
    with open(path, "rb") as f:
        return f.read()


def _find_app_partition(entries: Iterable[PartitionEntry]) -> PartitionEntry:
    app_entries = [e for e in entries if e.type == 0x00]
    if not app_entries:
        raise ValueError("No app partitions found")

    # Prefer explicit app0 label.
    for e in app_entries:
        if e.label == "app0":
            return e

    # Prefer OTA slot 0 (ota_0) if present.
    for e in app_entries:
        if e.subtype == 0x10:
            return e

    # Otherwise accept factory.
    for e in app_entries:
        if e.subtype == 0x00:
            return e

    return app_entries[0]


def _find_nvs_partition(entries: Iterable[PartitionEntry]) -> PartitionEntry:
    data_entries = [e for e in entries if e.type == 0x01]
    for e in data_entries:
        if e.label == "nvs" or e.subtype == 0x02:
            return e
    raise ValueError("No NVS partition found")


def _find_otadata_partition(entries: Iterable[PartitionEntry]) -> PartitionEntry:
    data_entries = [e for e in entries if e.type == 0x01]
    for e in data_entries:
        if e.label == "otadata" or e.subtype == 0x00:
            return e
    raise ValueError("No otadata partition found")


def _format_offset(value: int, fmt: str) -> str:
    if fmt == "hex":
        return f"0x{value:x}"
    if fmt == "dec":
        return str(value)
    raise ValueError(f"Unknown format: {fmt}")


def main(argv: Optional[Iterable[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description="Parse an ESP32 partitions.bin and extract offsets/sizes.",
    )
    parser.add_argument(
        "partitions_bin",
        help="Path to partitions binary (e.g. app.ino.partitions.bin)",
    )
    parser.add_argument(
        "--format",
        choices=("hex", "dec"),
        default="hex",
        help="Output format for offsets/sizes (default: hex)",
    )

    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument("--app-offset", action="store_true", help="Print app0/factory offset")
    group.add_argument("--nvs", action="store_true", help="Print NVS offset and size")
    group.add_argument("--otadata", action="store_true", help="Print otadata offset")

    args = parser.parse_args(list(argv) if argv is not None else None)

    path = args.partitions_bin
    if not os.path.isfile(path):
        print(f"ERROR: partitions bin not found: {path}", file=sys.stderr)
        return 2

    try:
        blob = _read_file(path)
        entries = list(_iter_entries(blob))
        if args.app_offset:
            app = _find_app_partition(entries)
            print(_format_offset(app.offset, args.format))
            return 0
        if args.nvs:
            nvs = _find_nvs_partition(entries)
            print(f"{_format_offset(nvs.offset, args.format)} {_format_offset(nvs.size, args.format)}")
            return 0
        if args.otadata:
            otadata = _find_otadata_partition(entries)
            print(_format_offset(otadata.offset, args.format))
            return 0
        return 2
    except Exception as e:
        print(f"ERROR: failed to parse partitions: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
