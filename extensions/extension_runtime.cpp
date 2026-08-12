#include <stddef.h>

extern "C" void* memcpy(void* destination, const void* source, size_t length) {
    unsigned char* out = static_cast<unsigned char*>(destination);
    const unsigned char* in = static_cast<const unsigned char*>(source);
    for (size_t index = 0; index < length; ++index) out[index] = in[index];
    return destination;
}

extern "C" void* memset(void* destination, int value, size_t length) {
    unsigned char* out = static_cast<unsigned char*>(destination);
    for (size_t index = 0; index < length; ++index) out[index] = static_cast<unsigned char>(value);
    return destination;
}