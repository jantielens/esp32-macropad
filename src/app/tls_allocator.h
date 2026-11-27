#pragma once

#include "board_config.h"

#if MBEDTLS_PSRAM_THRESHOLD_BYTES > 0
// Startup only: install before any TLS or other mbedTLS workers can run.
void tls_allocator_init();
#else
inline void tls_allocator_init() {}
#endif
