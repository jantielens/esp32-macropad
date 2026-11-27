#pragma once

#include <stddef.h>

#define MBEDTLS_PLATFORM_MEMORY

int mbedtls_platform_set_calloc_free(void* (*calloc_func)(size_t, size_t),
                                     void (*free_func)(void*));
