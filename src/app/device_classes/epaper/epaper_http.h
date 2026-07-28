#pragma once

#include "board_config.h"

#if HAS_EPAPER

#include <stddef.h>
#include <stdint.h>

class HTTPClient;

// Read the current successful HTTP response body into PSRAM. The caller owns
// the HTTPClient lifecycle and can choose not to call this after inspecting
// headers (the Service cache-hit abort). body_bytes_read counts only bytes
// returned by stream->read().
bool epaper_http_read_body(HTTPClient& http, uint8_t** out_buf, size_t* out_len,
		size_t* body_bytes_read, bool honor_content_length = true);

// Download the full body of an HTTP(S) URL into a freshly-allocated PSRAM
// buffer, following up to 3 cross-host 3xx redirects (the image endpoint
// 302-redirects to a storage blob on a different host with a fresh TLS
// session; HTTPClient's built-in redirect following is unreliable on ESP32).
//
// Returns true and sets *out_buf / *out_len on a 200 response carrying a body;
// the caller owns the buffer and must release it with heap_caps_free().
// Redirected URLs are never logged because the Location can carry a storage
// SAS token.
//
// HTTPS uses WiFiClientSecure::setInsecure() — image integrity is the
// publisher's concern. This deliberately avoids InkplateLibrary's
// downloadFileHTTPS, whose host-string allocation overflows and which enters
// mbedTLS with an uninitialised CA-cert pointer (crashes on https:// hosts).
bool epaper_http_download(const char* url, uint8_t** out_buf, size_t* out_len);

#endif // HAS_EPAPER
