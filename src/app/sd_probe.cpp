#include "sd_probe.h"

#if SD_PROBE_ON_BOOT

#include "log_manager.h"

#include <Arduino.h>
#include <SD_MMC.h>

#define TAG "SDProbe"

// ----------------------------------------------------------------------------
// Pin mapping for jc4880p433 (same as sd_storage.cpp). Kept duplicated here
// because the probe is intended as a standalone diagnostic that can run
// before USE_SD_STORAGE is wired up.
// ----------------------------------------------------------------------------
static constexpr int SD_PIN_CLK   = 43;
static constexpr int SD_PIN_CMD   = 44;
static constexpr int SD_PIN_D0    = 39;
static constexpr int SD_PIN_D1    = 40;
static constexpr int SD_PIN_D2    = 41;
static constexpr int SD_PIN_D3    = 42;
static constexpr int SD_PIN_POWER = 45;
static constexpr int SD_LDO_CHANNEL = 4;

static void log_card_info() {
    const uint8_t type = SD_MMC.cardType();
    const char* type_name =
        (type == CARD_NONE) ? "NONE" :
        (type == CARD_MMC)  ? "MMC"  :
        (type == CARD_SD)   ? "SDSC" :
        (type == CARD_SDHC) ? "SDHC" : "UNKNOWN";
    LOGI(TAG, "  cardType   = %s", type_name);
    LOGI(TAG, "  cardSize   = %llu MB", SD_MMC.cardSize() / (1024ULL * 1024ULL));
    LOGI(TAG, "  totalBytes = %llu", (uint64_t)SD_MMC.totalBytes());
    LOGI(TAG, "  usedBytes  = %llu", (uint64_t)SD_MMC.usedBytes());
}

static void list_root() {
    File root = SD_MMC.open("/");
    if (!root || !root.isDirectory()) {
        LOGW(TAG, "  (no root directory)");
        if (root) root.close();
        return;
    }
    LOGI(TAG, "  Root directory:");
    int count = 0;
    File entry = root.openNextFile();
    while (entry) {
        if (entry.isDirectory()) {
            LOGI(TAG, "    [DIR]  %s", entry.name());
        } else {
            LOGI(TAG, "    [FILE] %s  (%u bytes)", entry.name(), (unsigned)entry.size());
        }
        entry.close();
        if (++count >= 32) {
            LOGI(TAG, "    ... (more entries; truncated)");
            break;
        }
        entry = root.openNextFile();
    }
    root.close();
}

static bool write_read_roundtrip() {
    const char* path = "/sd_probe_test.txt";
    const char* payload = "esp32-macropad sd_probe ok\n";
    {
        File f = SD_MMC.open(path, FILE_WRITE);
        if (!f) {
            LOGE(TAG, "  write open failed");
            return false;
        }
        size_t n = f.print(payload);
        f.close();
        if (n != strlen(payload)) {
            LOGE(TAG, "  write short: %u/%u", (unsigned)n, (unsigned)strlen(payload));
            return false;
        }
    }
    {
        File f = SD_MMC.open(path, FILE_READ);
        if (!f) {
            LOGE(TAG, "  read open failed");
            return false;
        }
        char buf[64] = {};
        size_t n = f.readBytes(buf, sizeof(buf) - 1);
        f.close();
        if (n != strlen(payload) || memcmp(buf, payload, n) != 0) {
            LOGE(TAG, "  read mismatch (n=%u)", (unsigned)n);
            return false;
        }
    }
    SD_MMC.remove(path);
    LOGI(TAG, "  write/read round-trip OK");
    return true;
}

static bool try_mount(bool one_bit, bool use_power_pin, const char* label) {
    LOGI(TAG, "Attempt: %s", label);
    if (use_power_pin) {
        pinMode(SD_PIN_POWER, OUTPUT);
        digitalWrite(SD_PIN_POWER, LOW);
        delay(10);
    }
    if (!SD_MMC.setPins(SD_PIN_CLK, SD_PIN_CMD,
                        SD_PIN_D0,
                        one_bit ? -1 : SD_PIN_D1,
                        one_bit ? -1 : SD_PIN_D2,
                        one_bit ? -1 : SD_PIN_D3)) {
        LOGE(TAG, "  setPins() failed");
        return false;
    }
    SD_MMC.setPowerChannel(SD_LDO_CHANNEL);
    if (!SD_MMC.begin("/sdcard", one_bit, false)) {
        LOGE(TAG, "  begin() failed");
#if !USE_SD_STORAGE
        if (use_power_pin) digitalWrite(SD_PIN_POWER, HIGH);
#endif
        return false;
    }
    LOGI(TAG, "  mount OK");
    log_card_info();
    list_root();
    write_read_roundtrip();
    SD_MMC.end();
#if !USE_SD_STORAGE
    // When USE_SD_STORAGE is enabled the subsequent sd_storage_mount() call
    // will reuse the powered card; only power down when no mount follows.
    if (use_power_pin) {
        digitalWrite(SD_PIN_POWER, HIGH);  // Power back off
    }
#endif
    return true;
}

void sd_probe_run() {
    LOGI(TAG, "==== SD card boot probe ====");
    if (try_mount(false, true,  "4-bit + power pin")) return;
    if (try_mount(true,  true,  "1-bit + power pin")) return;
    if (try_mount(false, false, "4-bit + no power pin")) return;
    LOGE(TAG, "All mount attempts failed");
}

#else  // !SD_PROBE_ON_BOOT

void sd_probe_run() {}

#endif
