#include "shutter_test_scripts.h"

#if IS_SHUTTER_TESTER

#include "shutter_measure.h"
#include "log_manager.h"

#include "storage.h"
#include <string.h>

#define TAG "TestScripts"

// ============================================================================
// Speed validation
// ============================================================================

// Validate a speed string against the standard speed table.
// Input is the script format (no 's' suffix), e.g. "1/125", "1", "2".
// Returns true if the speed matches a known standard speed.
static bool validate_speed(const char* speed) {
    return shutter_measure_is_valid_speed(speed);
}

// ============================================================================
// Line parsing helpers
// ============================================================================

// Trim leading and trailing whitespace in-place. Returns pointer to trimmed start.
static char* trim(char* s) {
    while (*s == ' ' || *s == '\t') s++;
    size_t len = strlen(s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t' || s[len-1] == '\r' || s[len-1] == '\n'))
        s[--len] = '\0';
    return s;
}

// Check if a line starts with a given prefix (case-sensitive).
static bool starts_with(const char* line, const char* prefix) {
    return strncmp(line, prefix, strlen(prefix)) == 0;
}

// ============================================================================
// Finalize current script
// ============================================================================

static bool finalize_script(ShutterTestParseResult* result, ShutterTestScript* current,
                            int line_num) {
    if (!current->id[0]) return false;  // no script started

    if (current->speed_count == 0) {
        LOGW(TAG, "Line %d: test '%s' has no valid speeds, skipping", line_num, current->id);
        return false;
    }

    // Check for duplicate id — last definition wins
    for (int i = 0; i < result->count; i++) {
        if (strcmp(result->scripts[i].id, current->id) == 0) {
            result->scripts[i] = *current;
            return true;
        }
    }

    if (result->count >= SHUTTER_TEST_MAX_SCRIPTS) {
        LOGW(TAG, "Line %d: max %d test scripts reached, skipping '%s'",
             line_num, SHUTTER_TEST_MAX_SCRIPTS, current->id);
        return false;
    }

    result->scripts[result->count] = *current;
    result->count++;
    return true;
}

// ============================================================================
// Public API
// ============================================================================

uint8_t shutter_test_scripts_parse(ShutterTestParseResult* result) {
    memset(result, 0, sizeof(ShutterTestParseResult));

    File f = Storage.open(SHUTTER_TEST_FILE_PATH, "r");
    if (!f) {
        LOGI(TAG, "No test script file at %s", SHUTTER_TEST_FILE_PATH);
        return 0;
    }

    char line_buf[256];
    int line_num = 0;
    ShutterTestScript current;
    memset(&current, 0, sizeof(current));
    current.shots_per_speed = 1;
    bool have_current = false;

    while (f.available()) {
        // Read one line
        size_t len = f.readBytesUntil('\n', line_buf, sizeof(line_buf) - 1);
        line_buf[len] = '\0';
        line_num++;

        char* trimmed = trim(line_buf);

        // Skip empty lines and comments
        if (!trimmed[0] || trimmed[0] == '#') continue;

        // Check for key: value lines
        if (starts_with(trimmed, "name:")) {
            // Finalize previous script if any
            if (have_current) {
                finalize_script(result, &current, line_num);
            }

            // Start new script
            memset(&current, 0, sizeof(current));
            current.shots_per_speed = 1;
            have_current = true;

            char* value = trimmed + 5;
            value = trim(value);

            // Split on '|' for id | display name
            char* pipe = strchr(value, '|');
            if (pipe) {
                *pipe = '\0';
                char* id_str = trim(value);
                char* name_str = trim(pipe + 1);
                strlcpy(current.id, id_str, sizeof(current.id));
                strlcpy(current.name, name_str, sizeof(current.name));
                if (strlen(id_str) > SHUTTER_TEST_ID_MAX_LEN - 1) {
                    LOGW(TAG, "Line %d: test id '%s' truncated to %d chars",
                         line_num, id_str, SHUTTER_TEST_ID_MAX_LEN - 1);
                }
            } else {
                strlcpy(current.id, value, sizeof(current.id));
                strlcpy(current.name, value, sizeof(current.name));
            }

            if (!current.id[0]) {
                LOGW(TAG, "Line %d: empty test id in name: line", line_num);
                have_current = false;
            }
            continue;
        }

        if (starts_with(trimmed, "shots_per_speed:")) {
            if (!have_current) continue;
            char* value = trimmed + 16;
            value = trim(value);
            int n = atoi(value);
            if (n < 1) n = 1;
            if (n > 255) n = 255;
            current.shots_per_speed = (uint8_t)n;
            continue;
        }

        // Check for other key: lines (forward compat — skip unknown keys)
        {
            const char* colon = strchr(trimmed, ':');
            if (colon) {
                // If there's a colon and no '/' before it, it's a key: line
                const char* slash = strchr(trimmed, '/');
                if (!slash || slash > colon) {
                    // Unknown key: line — skip for forward compatibility
                    continue;
                }
            }
        }

        // Speed entry
        if (!have_current) continue;

        if (current.speed_count >= SHUTTER_TEST_MAX_SPEEDS) {
            LOGW(TAG, "Line %d: max %d speeds reached for test '%s', skipping",
                 line_num, SHUTTER_TEST_MAX_SPEEDS, current.id);
            continue;
        }

        // Validate speed against standard speed table
        if (!validate_speed(trimmed)) {
            LOGW(TAG, "Line %d: unknown speed '%s', skipping", line_num, trimmed);
            continue;
        }

        ShutterTestSpeed& entry = current.speeds[current.speed_count];
        // Canonical format is bare (e.g. "1/60") — strip trailing 's' if present
        size_t tlen = strlen(trimmed);
        if (tlen > 0 && trimmed[tlen - 1] == 's') trimmed[tlen - 1] = '\0';
        strlcpy(entry.speed, trimmed, sizeof(entry.speed));
        snprintf(entry.speed_suffixed, sizeof(entry.speed_suffixed), "%ss", trimmed);
        current.speed_count++;
    }

    f.close();

    // Finalize last script
    if (have_current) {
        finalize_script(result, &current, line_num);
    }

    LOGI(TAG, "Parsed %d test script(s) from %s", result->count, SHUTTER_TEST_FILE_PATH);
    return result->count;
}

const ShutterTestScript* shutter_test_scripts_find(const ShutterTestParseResult* result,
                                                    const char* id) {
    if (!result || !id || !id[0]) return nullptr;
    for (int i = 0; i < result->count; i++) {
        if (strcmp(result->scripts[i].id, id) == 0) {
            return &result->scripts[i];
        }
    }
    return nullptr;
}

#endif // IS_SHUTTER_TESTER
