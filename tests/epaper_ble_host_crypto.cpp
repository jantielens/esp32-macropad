#include <stddef.h>
#include <stdint.h>
#include <string.h>

namespace {

struct Sha256Context {
    uint32_t state[8];
    uint64_t bit_count;
    uint8_t buffer[64];
    size_t buffer_len;
};

constexpr uint32_t kRoundConstants[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
};

uint32_t rotate_right(uint32_t value, uint8_t shift) {
    return (value >> shift) | (value << (32 - shift));
}

void transform(Sha256Context *context, const uint8_t block[64]) {
    uint32_t words[64];
    for (size_t i = 0; i < 16; ++i) {
        words[i] = ((uint32_t)block[i * 4] << 24) |
                   ((uint32_t)block[i * 4 + 1] << 16) |
                   ((uint32_t)block[i * 4 + 2] << 8) |
                   block[i * 4 + 3];
    }
    for (size_t i = 16; i < 64; ++i) {
        const uint32_t s0 = rotate_right(words[i - 15], 7) ^
                            rotate_right(words[i - 15], 18) ^
                            (words[i - 15] >> 3);
        const uint32_t s1 = rotate_right(words[i - 2], 17) ^
                            rotate_right(words[i - 2], 19) ^
                            (words[i - 2] >> 10);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }

    uint32_t a = context->state[0];
    uint32_t b = context->state[1];
    uint32_t c = context->state[2];
    uint32_t d = context->state[3];
    uint32_t e = context->state[4];
    uint32_t f = context->state[5];
    uint32_t g = context->state[6];
    uint32_t h = context->state[7];
    for (size_t i = 0; i < 64; ++i) {
        const uint32_t sum1 = rotate_right(e, 6) ^ rotate_right(e, 11) ^
                              rotate_right(e, 25);
        const uint32_t choose = (e & f) ^ (~e & g);
        const uint32_t temp1 = h + sum1 + choose + kRoundConstants[i] + words[i];
        const uint32_t sum0 = rotate_right(a, 2) ^ rotate_right(a, 13) ^
                              rotate_right(a, 22);
        const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t temp2 = sum0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

void init(Sha256Context *context) {
    const uint32_t initial[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19,
    };
    memcpy(context->state, initial, sizeof(initial));
    context->bit_count = 0;
    context->buffer_len = 0;
}

void update(Sha256Context *context, const uint8_t *data, size_t length) {
    context->bit_count += (uint64_t)length * 8;
    while (length) {
        const size_t space = sizeof(context->buffer) - context->buffer_len;
        const size_t take = length < space ? length : space;
        memcpy(context->buffer + context->buffer_len, data, take);
        context->buffer_len += take;
        data += take;
        length -= take;
        if (context->buffer_len == sizeof(context->buffer)) {
            transform(context, context->buffer);
            context->buffer_len = 0;
        }
    }
}

void finish(Sha256Context *context, uint8_t out[32]) {
    const uint64_t original_bits = context->bit_count;
    const uint8_t marker = 0x80;
    update(context, &marker, 1);
    const uint8_t zero = 0;
    while (context->buffer_len != 56) update(context, &zero, 1);
    uint8_t length_bytes[8];
    for (size_t i = 0; i < 8; ++i) {
        length_bytes[7 - i] = (uint8_t)(original_bits >> (i * 8));
    }
    update(context, length_bytes, sizeof(length_bytes));
    for (size_t i = 0; i < 8; ++i) {
        out[i * 4] = (uint8_t)(context->state[i] >> 24);
        out[i * 4 + 1] = (uint8_t)(context->state[i] >> 16);
        out[i * 4 + 2] = (uint8_t)(context->state[i] >> 8);
        out[i * 4 + 3] = (uint8_t)context->state[i];
    }
}

}  // namespace

bool epaper_ble_host_sha256(const uint8_t *data, size_t data_len,
                            uint8_t out[32]) {
    if ((!data && data_len) || !out) return false;
    Sha256Context context;
    init(&context);
    update(&context, data, data_len);
    finish(&context, out);
    return true;
}

bool epaper_ble_host_hmac_sha256(const uint8_t *key, size_t key_len,
                                 const uint8_t *data, size_t data_len,
                                 uint8_t out[32]) {
    if ((!key && key_len) || (!data && data_len) || !out) return false;
    uint8_t normalized_key[64] = {};
    if (key_len > sizeof(normalized_key)) {
        epaper_ble_host_sha256(key, key_len, normalized_key);
    } else if (key_len) {
        memcpy(normalized_key, key, key_len);
    }
    uint8_t inner_pad[64];
    uint8_t outer_pad[64];
    for (size_t i = 0; i < 64; ++i) {
        inner_pad[i] = normalized_key[i] ^ 0x36;
        outer_pad[i] = normalized_key[i] ^ 0x5c;
    }
    Sha256Context context;
    uint8_t inner_hash[32];
    init(&context);
    update(&context, inner_pad, sizeof(inner_pad));
    update(&context, data, data_len);
    finish(&context, inner_hash);
    init(&context);
    update(&context, outer_pad, sizeof(outer_pad));
    update(&context, inner_hash, sizeof(inner_hash));
    finish(&context, out);
    memset(normalized_key, 0, sizeof(normalized_key));
    memset(inner_pad, 0, sizeof(inner_pad));
    memset(outer_pad, 0, sizeof(outer_pad));
    memset(inner_hash, 0, sizeof(inner_hash));
    return true;
}