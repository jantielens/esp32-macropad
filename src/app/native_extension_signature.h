#pragma once

#include "board_config.h"

#if HAS_NATIVE_EXTENSIONS

#include <stddef.h>
#include <stdint.h>

constexpr size_t NATIVE_EXTENSION_SIGNATURE_SIZE = 64;

// Verifies an IEEE P1363 ECDSA P-256 signature over the exact ELF bytes.
// The implementation currently tries the built-in first-party key only.
bool native_extension_verify_signature(const uint8_t* elf, size_t elf_size,
                                       const uint8_t signature[NATIVE_EXTENSION_SIGNATURE_SIZE]);

#endif