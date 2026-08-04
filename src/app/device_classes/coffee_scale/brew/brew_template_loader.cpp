#include "brew_template_loader.h"

#if HAS_SCALE

#include "brew_template_dsl.h"
#include "brew_templates.h"
#include "brew_manager.h"
#include "log_manager.h"
#include "storage.h"

#define TAG "BrewLoad"

void brew_template_loader_load() {
    File dir = Storage.open(BREW_TEMPLATE_DIR);
    if (!dir || !dir.isDirectory()) {
        LOGD(TAG, "No template directory " BREW_TEMPLATE_DIR);
        return;
    }

    uint8_t loaded = 0;
    File entry = dir.openNextFile();
    while (entry) {
        const char* name = entry.name();
        if (!name || !strstr(name, ".json")) {
            entry.close();
            entry = dir.openNextFile();
            continue;
        }

        size_t size = entry.size();
        if (size == 0 || size > 8192) {
            LOGW(TAG, "Skipping '%s' (size=%u)", name, (unsigned)size);
            entry.close();
            entry = dir.openNextFile();
            continue;
        }

        // Read file into temporary buffer
        char* buf = new (std::nothrow) char[size + 1];
        if (!buf) {
            LOGE(TAG, "Alloc failed for '%s'", name);
            entry.close();
            entry = dir.openNextFile();
            continue;
        }
        size_t bytes_read = entry.readBytes(buf, size);
        buf[bytes_read] = '\0';
        entry.close();

        // Parse
        BrewTemplate* tmpl = nullptr;
        BrewStage* stages = nullptr;
        char err[80] = {};
        int rc = brew_dsl_parse(buf, bytes_read, &tmpl, &stages, err, sizeof(err));
        delete[] buf;

        if (rc != BREW_DSL_OK) {
            LOGW(TAG, "Parse error in '%s': %s (rc=%d)", name, err, rc);
        } else {
            brew_template_register(tmpl);
            loaded++;
        }

        entry = dir.openNextFile();
    }
    dir.close();

    if (loaded > 0) {
        LOGI(TAG, "Loaded %u dynamic template(s) from " BREW_TEMPLATE_DIR, (unsigned)loaded);
    }
}

void brew_template_loader_reload() {
    // Drop any active brew and the manager's cached template pointers BEFORE
    // freeing dynamic templates, otherwise s_template / s_last_template would
    // dangle into the storage clear_dynamic() is about to delete.
    if (brew_get_phase() != BREW_IDLE) brew_reset();
    brew_forget_templates();
    brew_templates_clear_dynamic();
    brew_template_loader_load();
}

#endif // HAS_SCALE
