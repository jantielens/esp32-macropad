#include "time_binding.h"
#include "board_config.h"

#if HAS_DISPLAY

#include "binding_template.h"
#include "log_manager.h"
#include "time_service.h"

#include <Arduino.h>
#include <time.h>
#include <sys/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TAG "TimeBind"

// ============================================================================
// Scheme resolver — called by binding_template_resolve()
// ============================================================================

static BindingResolverStatus time_binding_resolve(const char* params, char* out, size_t out_len) {
    if (!params || !params[0]) {
        strlcpy(out, "ERR:no fmt", out_len);
        return BINDING_RESOLVER_UNAVAILABLE;
    }

    // Parse: "format" or "format;timezone"
    char fmt[64];
    char tz[64];
    fmt[0] = '\0';
    tz[0] = '\0';

    const char* sep = strchr(params, ';');
    if (!sep) {
        strlcpy(fmt, params, sizeof(fmt));
    } else {
        size_t flen = (size_t)(sep - params);
        if (flen >= sizeof(fmt)) flen = sizeof(fmt) - 1;
        memcpy(fmt, params, flen);
        fmt[flen] = '\0';
        strlcpy(tz, sep + 1, sizeof(tz));
    }

    // Handle %ums (uptime milliseconds) — standalone, no NTP needed
    if (strcmp(fmt, "%ums") == 0) {
        snprintf(out, out_len, "%lu", (unsigned long)millis());
        return BINDING_RESOLVER_RESOLVED;
    }

    // Check if NTP has synced (time > 2024-01-01)
    time_t now = time(nullptr);
    if (!time_service_ready()) {
        strlcpy(out, "--:--", out_len);
        return BINDING_RESOLVER_RESOLVED;
    }

    // Get sub-second precision via gettimeofday
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    int millis_in_sec = (int)(tv.tv_usec / 1000);  // 0-999

    // Expand custom sub-second format codes before strftime:
    //   %ms  → milliseconds within second (000-999)
    //   %ds  → deciseconds (0-9, 100ms granularity)
    //   %cs  → centiseconds (00-99, 10ms granularity)
    //   %ums → skipped here (handled as standalone above); if embedded
    //          in a compound format, consume it to avoid strftime seeing %u
    char expanded[128];
    char* dst = expanded;
    char* dst_end = expanded + sizeof(expanded) - 1;
    const char* src = fmt;
    while (*src && dst < dst_end) {
        if (src[0] == '%') {
            if (src[1] == 'u' && src[2] == 'm' && src[3] == 's') {
                // %ums in a compound format: expand to millis() as best effort
                int n = snprintf(dst, (size_t)(dst_end - dst), "%lu", (unsigned long)millis());
                dst += n;
                src += 4;
                continue;
            } else if (src[1] == 'm' && src[2] == 's') {
                int n = snprintf(dst, (size_t)(dst_end - dst), "%03d", millis_in_sec);
                dst += n;
                src += 3;
                continue;
            } else if (src[1] == 'd' && src[2] == 's') {
                int n = snprintf(dst, (size_t)(dst_end - dst), "%d", millis_in_sec / 100);
                dst += n;
                src += 3;
                continue;
            } else if (src[1] == 'c' && src[2] == 's') {
                int n = snprintf(dst, (size_t)(dst_end - dst), "%02d", millis_in_sec / 10);
                dst += n;
                src += 3;
                continue;
            }
        }
        *dst++ = *src++;
    }
    *dst = '\0';

    if (!time_service_format(now, expanded, tz, out, out_len)) {
        strlcpy(out, "ERR:fmt", out_len);
        return BINDING_RESOLVER_UNAVAILABLE;
    }
    return BINDING_RESOLVER_RESOLVED;
}

// ============================================================================
// No-op topic collector — time data is local, no subscriptions needed
// ============================================================================

static void time_binding_collect(const char* params, void* user_data) {
    (void)params;
    (void)user_data;
}

// ============================================================================
// Public API
// ============================================================================

void time_binding_init() {
    if (!binding_template_register("time", time_binding_resolve, time_binding_collect,
                                   {1, 2, 1, -1, BINDING_VALIDATION_STANDARD, true, nullptr, nullptr})) {
        LOGE(TAG, "Failed to register time binding scheme");
    }
}

void time_binding_start_ntp() {
    time_service_start_ntp();
    LOGI(TAG, "NTP sync started (pool.ntp.org)");
}

bool time_binding_is_synced() {
    return time_service_ready();
}

#else // !HAS_DISPLAY

void time_binding_init() {}
void time_binding_start_ntp() {}
bool time_binding_is_synced() { return false; }

#endif
