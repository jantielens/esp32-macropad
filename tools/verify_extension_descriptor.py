#!/usr/bin/env python3
"""Validate an ABI descriptor embedded in a 32-bit little-endian RISC-V ELF."""

import re
import struct
import sys
from pathlib import Path


def fail(message: str) -> None:
    print(f"Extension descriptor validation failed: {message}", file=sys.stderr)
    raise SystemExit(1)


def c_string(raw: bytes) -> str:
    return raw.split(b"\0", 1)[0].decode("ascii", errors="strict")


def constant(header: str, name: str) -> str:
    match = re.search(rf"^#define {name} (.+)$", header, re.MULTILINE)
    if not match:
        fail(f"missing {name} in ABI header")
    return match.group(1).strip()


def main() -> None:
    if len(sys.argv) != 3:
        fail("usage: verify_extension_descriptor.py <abi-header> <package.elf>")

    header_path = Path(sys.argv[1])
    elf_path = Path(sys.argv[2])
    header = header_path.read_text(encoding="ascii")
    abi_version = int(re.search(r"\d+", constant(header, "NATIVE_EXTENSION_ABI_VERSION")).group())
    target_abi = constant(header, "NATIVE_EXTENSION_TARGET_ABI").strip('"')
    descriptor_magic = int(re.search(r"0x[0-9A-Fa-f]+", constant(header, "NATIVE_EXTENSION_DESCRIPTOR_MAGIC")).group(), 16)

    match = re.fullmatch(r"([a-z0-9-]+)@([0-9]+\.[0-9]+\.[0-9]+)\.elf", elf_path.name)
    if not match:
        fail("filename must be <extension-id>@<package-semver>.elf")
    filename_id, filename_version = match.groups()

    data = elf_path.read_bytes()
    if len(data) < 52 or data[:4] != b"\x7fELF" or data[4] != 1 or data[5] != 1:
        fail("not a 32-bit little-endian ELF")
    (_, _, _, _, _, phoff, shoff, _, _, phentsize, phnum, shentsize, shnum, _) = struct.unpack_from("<16sHHIIIIIHHHHHH", data)
    if phentsize != 32 or shentsize != 40:
        fail("unexpected ELF header layout")

    programs = [struct.unpack_from("<IIIIIIII", data, phoff + index * phentsize) for index in range(phnum)]
    sections = [struct.unpack_from("<IIIIIIIIII", data, shoff + index * shentsize) for index in range(shnum)]

    descriptor_vaddr = None
    for (_, section_type, _, _, section_offset, section_size, link, _, _, entry_size) in sections:
        if section_type != 11 or entry_size != 16 or link >= len(sections):
            continue
        strings = sections[link]
        string_offset, string_size = strings[4], strings[5]
        for index in range(section_size // entry_size):
            name, value, _, _, _, section_index = struct.unpack_from("<IIIBBH", data, section_offset + index * entry_size)
            if section_index == 0 or name >= string_size:
                continue
            raw_name = data[string_offset + name:string_offset + string_size]
            if c_string(raw_name) == "native_extension_descriptor":
                descriptor_vaddr = value
                break
        if descriptor_vaddr is not None:
            break
    if descriptor_vaddr is None:
        fail("missing native_extension_descriptor export")

    descriptor_offset = None
    descriptor_size = struct.calcsize("<II24s32s16s40s")
    for program_type, offset, vaddr, _, file_size, _, _, _ in programs:
        if program_type == 1 and vaddr <= descriptor_vaddr and descriptor_vaddr + descriptor_size <= vaddr + file_size:
            descriptor_offset = offset + descriptor_vaddr - vaddr
            break
    if descriptor_offset is None or descriptor_offset + descriptor_size > len(data):
        fail("descriptor is not in a loadable ELF segment")

    magic, descriptor_abi, raw_target, raw_id, raw_version, raw_title = struct.unpack_from("<II24s32s16s40s", data, descriptor_offset)
    if magic != descriptor_magic:
        fail("descriptor magic does not match ABI header")
    if descriptor_abi != abi_version:
        fail(f"descriptor ABI {descriptor_abi} does not match firmware ABI {abi_version}")
    if c_string(raw_target) != target_abi:
        fail(f"descriptor target ABI {c_string(raw_target)!r} does not match {target_abi!r}")
    if c_string(raw_id) != filename_id or c_string(raw_version) != filename_version:
        fail("descriptor ID/version does not match package filename")
    if not c_string(raw_title):
        fail("descriptor title is empty")

    print(f"Descriptor: {filename_id}@{filename_version} (ABI {abi_version}, target {target_abi})")


if __name__ == "__main__":
    main()
