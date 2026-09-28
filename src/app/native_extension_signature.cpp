#include "native_extension_signature.h"

#if HAS_NATIVE_EXTENSIONS

#include <mbedtls/ecdsa.h>
#include <mbedtls/sha256.h>
#include <mbedtls/version.h>

#if MBEDTLS_VERSION_NUMBER < 0x03000000
#define EXTENSION_ECP_FIELD(field) field
#else
#define EXTENSION_ECP_FIELD(field) MBEDTLS_PRIVATE(field)
#endif

namespace {

// Uncompressed SEC1 P-256 point for the project extension signing key.
// A future trust store can iterate configured publisher keys through the same
// verifier without changing the stage or loader trust boundaries.
constexpr uint8_t FIRST_PARTY_PUBLIC_KEY[] = {
    0x04, 0x78, 0xce, 0x58, 0xbb, 0x44, 0x57, 0x6f, 0x21, 0x95, 0x10,
    0x10, 0xb0, 0x10, 0xb8, 0x9b, 0x66, 0xc1, 0x3c, 0xfb, 0x65, 0x1d,
    0xca, 0xb8, 0x88, 0xa4, 0xe1, 0x30, 0x7e, 0x90, 0x60, 0x1e, 0xf2,
    0x82, 0xfb, 0xd2, 0x58, 0x5d, 0xe0, 0xd3, 0xbd, 0x6c, 0xf0, 0xf3,
    0xb1, 0xa1, 0x44, 0x40, 0x71, 0x74, 0xeb, 0xb6, 0xa3, 0x17, 0x55,
    0x9a, 0xd6, 0xf7, 0xf2, 0xcd, 0xe6, 0x01, 0x7f, 0xff, 0xbf,
};

bool verify_with_key(const uint8_t* elf, size_t elf_size,
                     const uint8_t signature[NATIVE_EXTENSION_SIGNATURE_SIZE],
                     const uint8_t* public_key, size_t public_key_size) {
    uint8_t hash[32] = {};
    mbedtls_ecdsa_context context;
    mbedtls_ecdsa_init(&context);
#if MBEDTLS_VERSION_NUMBER < 0x03000000
    mbedtls_sha256(elf, elf_size, hash, 0);
    const int hash_result = 0;
#else
    const int hash_result = mbedtls_sha256(elf, elf_size, hash, 0);
#endif
    const int group_result = hash_result == 0
                                 ? mbedtls_ecp_group_load(&context.EXTENSION_ECP_FIELD(grp), MBEDTLS_ECP_DP_SECP256R1)
                                 : -1;
    const int point_result = group_result == 0
                                 ? mbedtls_ecp_point_read_binary(&context.EXTENSION_ECP_FIELD(grp),
                                                                 &context.EXTENSION_ECP_FIELD(Q),
                                                                 public_key, public_key_size)
                                 : -1;
    mbedtls_mpi r;
    mbedtls_mpi s;
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    const int r_result = point_result == 0
                             ? mbedtls_mpi_read_binary(&r, signature, NATIVE_EXTENSION_SIGNATURE_SIZE / 2)
                             : -1;
    const int s_result = r_result == 0
                             ? mbedtls_mpi_read_binary(&s, signature + NATIVE_EXTENSION_SIGNATURE_SIZE / 2,
                                                       NATIVE_EXTENSION_SIGNATURE_SIZE / 2)
                             : -1;
    const int verify_result = s_result == 0
                                  ? mbedtls_ecdsa_verify(&context.EXTENSION_ECP_FIELD(grp), hash, sizeof(hash),
                                                         &context.EXTENSION_ECP_FIELD(Q), &r, &s)
                                  : -1;
    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_ecdsa_free(&context);
    return verify_result == 0;
}

} // namespace

bool native_extension_verify_signature(const uint8_t* elf, size_t elf_size,
                                       const uint8_t signature[NATIVE_EXTENSION_SIGNATURE_SIZE]) {
    return elf && elf_size > 0 && signature &&
           verify_with_key(elf, elf_size, signature, FIRST_PARTY_PUBLIC_KEY,
                           sizeof(FIRST_PARTY_PUBLIC_KEY));
}

#endif