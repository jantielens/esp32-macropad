#include "sound_store.h"

#if HAS_SOUND_PLAYER

#include "storage.h"
#include <string.h>
#include "log_manager.h"

#define TAG "SoundStore"

static const char* SOUND_DIR = "/sounds";

void sound_store_init() {
    if (!Storage.exists(SOUND_DIR)) {
        Storage.mkdir(SOUND_DIR);
        LOGI(TAG, "Created %s directory", SOUND_DIR);
    }
}

bool sound_store_validate_name(const char* name) {
    if (!name || !name[0]) return false;
    size_t len = strlen(name);
    if (len >= SOUND_NAME_MAX_LEN) return false;
    for (size_t i = 0; i < len; i++) {
        char c = name[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-')) {
            return false;
        }
    }
    return true;
}

char* sound_store_path(const char* name, char* out, size_t out_len) {
    snprintf(out, out_len, "%s/%s.mp3", SOUND_DIR, name);
    return out;
}

bool sound_store_exists(const char* name) {
    char path[48];
    sound_store_path(name, path, sizeof(path));
    return Storage.exists(path);
}

bool sound_store_save(const char* name, const uint8_t* data, size_t len) {
    if (!sound_store_validate_name(name)) {
        LOGW(TAG, "Invalid sound name: '%s'", name ? name : "(null)");
        return false;
    }
    if (!data || len == 0 || len > SOUND_MAX_FILE_SIZE) {
        LOGW(TAG, "Invalid data size: %u", (unsigned)len);
        return false;
    }

    char path[48];
    sound_store_path(name, path, sizeof(path));

    File f = Storage.open(path, "w");
    if (!f) {
        LOGE(TAG, "Failed to open %s for writing", path);
        return false;
    }

    size_t written = f.write(data, len);
    f.close();

    if (written != len) {
        LOGE(TAG, "Write incomplete: %u/%u bytes", (unsigned)written, (unsigned)len);
        Storage.remove(path);
        return false;
    }

    LOGI(TAG, "Saved %s (%u bytes)", path, (unsigned)len);
    return true;
}

bool sound_store_delete(const char* name) {
    char path[48];
    sound_store_path(name, path, sizeof(path));

    if (!Storage.exists(path)) {
        LOGW(TAG, "File not found: %s", path);
        return false;
    }

    bool ok = Storage.remove(path);
    LOGI(TAG, "Delete %s: %s", path, ok ? "OK" : "FAIL");
    return ok;
}

int sound_store_list(char names[][SOUND_NAME_MAX_LEN], int max_count) {
    File dir = Storage.open(SOUND_DIR);
    if (!dir || !dir.isDirectory()) {
        return 0;
    }

    int count = 0;
    File entry = dir.openNextFile();
    while (entry && count < max_count) {
        const char* fname = entry.name();
        // Only list .mp3 files
        size_t flen = strlen(fname);
        if (flen > 4 && strcmp(fname + flen - 4, ".mp3") == 0) {
            // Copy name without extension
            size_t name_len = flen - 4;
            if (name_len >= SOUND_NAME_MAX_LEN) name_len = SOUND_NAME_MAX_LEN - 1;
            memcpy(names[count], fname, name_len);
            names[count][name_len] = '\0';
            count++;
        }
        entry = dir.openNextFile();
    }

    return count;
}

#endif // HAS_SOUND_PLAYER
