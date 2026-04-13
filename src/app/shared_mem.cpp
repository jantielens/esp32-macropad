#include "shared_mem.h"
#include "board_config.h"

#if IS_DARKROOM_TIMER

#include "binding_template.h"
#include "log_manager.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define TAG "SharedMem"

// ============================================================================
// Storage
// ============================================================================

static constexpr size_t MAX_ENTRIES = 8;
static constexpr size_t KEY_MAX_LEN = 16;

struct MemEntry {
    char  key[KEY_MAX_LEN];
    float value;
    bool  is_set;
};

static MemEntry g_entries[MAX_ENTRIES];
static portMUX_TYPE g_lock = portMUX_INITIALIZER_UNLOCKED;

static MemEntry* find_entry(const char* key) {
    for (size_t i = 0; i < MAX_ENTRIES; i++) {
        if (g_entries[i].is_set && strcmp(g_entries[i].key, key) == 0) {
            return &g_entries[i];
        }
    }
    return nullptr;
}

static MemEntry* alloc_entry(const char* key) {
    for (size_t i = 0; i < MAX_ENTRIES; i++) {
        if (!g_entries[i].is_set) {
            strlcpy(g_entries[i].key, key, KEY_MAX_LEN);
            g_entries[i].is_set = true;
            return &g_entries[i];
        }
    }
    return nullptr;
}

// ============================================================================
// Public API
// ============================================================================

void shared_mem_set(const char* key, float value) {
    if (!key || !key[0]) return;

    portENTER_CRITICAL(&g_lock);
    MemEntry* e = find_entry(key);
    if (!e) e = alloc_entry(key);
    if (e) e->value = value;
    portEXIT_CRITICAL(&g_lock);

    if (e) {
        LOGI(TAG, "Set %s = %.3f", key, value);
    } else {
        LOGW(TAG, "Store full, cannot set %s", key);
    }
}

float shared_mem_get(const char* key, bool* is_set) {
    if (!key || !key[0]) {
        if (is_set) *is_set = false;
        return 0.0f;
    }

    portENTER_CRITICAL(&g_lock);
    MemEntry* e = find_entry(key);
    float val = e ? e->value : 0.0f;
    bool found = (e != nullptr);
    portEXIT_CRITICAL(&g_lock);

    if (is_set) *is_set = found;
    return val;
}

void shared_mem_dispatch(const char* cmd) {
    if (!cmd) return;

    // Parse "set_<key>:<value>"
    if (strncmp(cmd, "set_", 4) != 0) {
        LOGW(TAG, "Unknown command: %s", cmd);
        return;
    }

    const char* key_start = cmd + 4;
    const char* colon = strchr(key_start, ':');
    if (!colon || colon == key_start) {
        LOGW(TAG, "Bad set command: %s", cmd);
        return;
    }

    size_t key_len = colon - key_start;
    if (key_len >= KEY_MAX_LEN) key_len = KEY_MAX_LEN - 1;

    char key[KEY_MAX_LEN];
    memcpy(key, key_start, key_len);
    key[key_len] = '\0';

    float value = strtof(colon + 1, nullptr);
    shared_mem_set(key, value);
}

// ============================================================================
// Binding resolver
// ============================================================================

// Params format: "<key>" or "<key>;<format>"
static bool mem_resolve(const char* params, char* out, size_t out_len) {
    if (!params || !params[0]) {
        snprintf(out, out_len, "ERR:no_key");
        return false;
    }

    // Split key and optional format at semicolon
    char key[KEY_MAX_LEN];
    const char* fmt = nullptr;
    const char* semi = strchr(params, ';');
    if (semi) {
        size_t klen = semi - params;
        if (klen >= KEY_MAX_LEN) klen = KEY_MAX_LEN - 1;
        memcpy(key, params, klen);
        key[klen] = '\0';
        fmt = semi + 1;
    } else {
        strlcpy(key, params, KEY_MAX_LEN);
    }

    bool is_set = false;
    float val = shared_mem_get(key, &is_set);
    if (!is_set) {
        snprintf(out, out_len, "---");
        return false;
    }

    if (fmt && fmt[0]) {
        // Validate format: must contain exactly one %-conversion for a float.
        // Reject user-supplied strings that could read stack memory.
        const char* p = fmt;
        int conv_count = 0;
        bool safe = true;
        while (*p) {
            if (*p == '%') {
                p++;
                if (*p == '%') { p++; continue; }  // literal %%
                // Skip flags, width, precision
                while (*p == '-' || *p == '+' || *p == ' ' || *p == '#' || *p == '0') p++;
                while (*p >= '0' && *p <= '9') p++;
                if (*p == '.') { p++; while (*p >= '0' && *p <= '9') p++; }
                // Only allow float conversions
                if (*p == 'f' || *p == 'e' || *p == 'g' || *p == 'F' || *p == 'E' || *p == 'G') {
                    conv_count++;
                    p++;
                } else {
                    safe = false; break;
                }
            } else {
                p++;
            }
        }
        if (safe && conv_count == 1) {
            snprintf(out, out_len, fmt, val);
        } else {
            snprintf(out, out_len, "%.1f", val);
        }
    } else {
        // Default: up to 1 decimal, strip trailing zeros
        snprintf(out, out_len, "%.1f", val);
        // Remove trailing ".0" for clean integers
        char* dot = strchr(out, '.');
        if (dot && dot[1] == '0' && dot[2] == '\0') {
            *dot = '\0';
        }
    }
    return true;
}

static void mem_collect(const char* params, void* user_data) {
    // No MQTT topics to collect — pure local store
    (void)params;
    (void)user_data;
}

// ============================================================================
// Init
// ============================================================================

void shared_mem_init() {
    memset(g_entries, 0, sizeof(g_entries));

    if (!binding_template_register("mem", mem_resolve, mem_collect)) {
        LOGE(TAG, "Failed to register mem binding scheme");
    } else {
        LOGI(TAG, "Shared memory binding scheme registered");
    }
}

#else // !IS_DARKROOM_TIMER

void  shared_mem_init() {}
void  shared_mem_dispatch(const char*) {}
float shared_mem_get(const char*, bool* is_set) { if (is_set) *is_set = false; return 0.0f; }
void  shared_mem_set(const char*, float) {}

#endif // IS_DARKROOM_TIMER
