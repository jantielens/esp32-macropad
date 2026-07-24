#!/usr/bin/env python3
"""Backfill transport CRC metadata for legacy photoframe image blobs."""

from __future__ import annotations

import argparse

import blobstore as bs
import config as cfg
import store


def backfill_device(sas: str, *, dry_run: bool = False) -> tuple[int, int]:
    """Return ``(stamped, skipped)`` for one container.

    Presence of the metadata key is the completion marker. A computed CRC of
    zero is therefore stamped once and skipped on every later sweep.
    """
    stamped = 0
    skipped = 0
    for blob_name, metadata in bs.list_blobs_with_metadata(sas, store.IMAGE_PREFIX).items():
        if store._split_image_name(blob_name) is None:
            continue
        if "content_crc32" in metadata:
            skipped += 1
            continue
        payload = bs.download_blob(sas, blob_name)
        if payload is None:
            continue
        updated = dict(metadata)
        updated["content_crc32"] = str(store.content_crc32(payload))
        if not dry_run:
            bs.set_blob_metadata(sas, blob_name, updated)
        stamped += 1
    return stamped, skipped


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Resumably stamp transport CRC metadata on legacy image blobs."
    )
    parser.add_argument(
        "--device-id", action="append", dest="device_ids", help="limit to a device"
    )
    parser.add_argument("--dry-run", action="store_true", help="read and report only")
    args = parser.parse_args()

    config = cfg.load_config()
    selected = set(args.device_ids or config.devices.keys())
    unknown = selected - config.devices.keys()
    if unknown:
        parser.error(f"unknown device(s): {', '.join(sorted(unknown))}")

    for device_id in sorted(selected):
        device = config.devices[device_id]
        stamped, skipped = backfill_device(
            device.container_sas_url, dry_run=args.dry_run
        )
        action = "would stamp" if args.dry_run else "stamped"
        print(f"{device_id}: {action} {stamped}, skipped {skipped}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())