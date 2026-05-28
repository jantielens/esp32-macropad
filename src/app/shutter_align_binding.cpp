#include "shutter_align_binding.h"

#if HAS_DISPLAY && IS_SHUTTER_TESTER

#include "shutter_capture.h"
#include <string.h>
#include <stdio.h>

bool shutter_align_binding_resolve(const char* key, char* out, size_t out_len) {
    // "active" is always available regardless of alignment state.
    if (strcmp(key, "active") == 0) {
        strlcpy(out, shutter_capture_is_alignment_active() ? "true" : "false", out_len);
        return true;
    }

    // All other keys require an active alignment reading.
    ShutterAlignmentReading reading;
    bool have_data = shutter_capture_get_alignment(&reading);

    if (!have_data) {
        strlcpy(out, "---", out_len);
        // Still return true for recognized keys (resolved, just no data).
        if (strcmp(key, "spread") == 0 ||
            strcmp(key, "status") == 0 ||
            strcmp(key, "hint") == 0 ||
            strcmp(key, "sensor_count") == 0) {
            return true;
        }
        // Per-sensor keys: s1_pct..s9_pct, s1_raw..s9_raw
        if (key[0] == 's' && key[1] >= '1' && key[1] <= '9') {
            const char* suffix = &key[2];
            if (strcmp(suffix, "_pct") == 0 || strcmp(suffix, "_raw") == 0) return true;
        }
        return false; // Unknown key
    }

    // Per-sensor keys: parse sensor index directly from "sN_pct" / "sN_raw" pattern.
    if (key[0] == 's' && key[1] >= '1' && key[1] <= '9') {
        int idx = key[1] - '1';  // 0-based sensor index
        const char* suffix = &key[2];
        if (strcmp(suffix, "_pct") == 0) {
            if (idx >= reading.sensor_count) {
                strlcpy(out, "---", out_len);
            } else {
                snprintf(out, out_len, "%u", (unsigned)reading.pct[idx]);
            }
            return true;
        }
        if (strcmp(suffix, "_raw") == 0) {
            if (idx >= reading.sensor_count) {
                strlcpy(out, "---", out_len);
            } else {
                snprintf(out, out_len, "%u", (unsigned)reading.raw[idx]);
            }
            return true;
        }
    }

    if (strcmp(key, "spread") == 0) {
        snprintf(out, out_len, "%u", (unsigned)reading.spread_pct);
        return true;
    }

    if (strcmp(key, "status") == 0) {
        strlcpy(out, reading.status ? reading.status : "---", out_len);
        return true;
    }

    if (strcmp(key, "hint") == 0) {
        strlcpy(out, reading.hint ? reading.hint : "", out_len);
        return true;
    }

    if (strcmp(key, "sensor_count") == 0) {
        snprintf(out, out_len, "%u", (unsigned)reading.sensor_count);
        return true;
    }

    return false; // Unknown key
}

#endif // HAS_DISPLAY && IS_SHUTTER_TESTER
