#include <assert.h>
#include <string.h>

class String {};

#include "device_telemetry.h"

static void assert_string(JsonDocument& doc, const char* key, const char* expected) {
    const char* actual = doc[key];
    assert(actual != nullptr);
    assert(strcmp(actual, expected) == 0);
}

int main() {
    FSHealthStats littlefs = {
        .backend = FS_BACKEND_LITTLEFS,
        .card_type = FS_CARD_TYPE_NOT_APPLICABLE,
        .storage_mounted = true,
        .storage_used_bytes = 12,
        .storage_total_bytes = 34,
    };
    JsonDocument api_doc;
    device_telemetry_append_fs_health(api_doc, littlefs);
    assert_string(api_doc, "fs_backend", "littlefs");
    assert(api_doc["fs_mounted"] == true);
    assert(api_doc["fs_used_bytes"] == 12);
    assert(api_doc["fs_total_bytes"] == 34);
    assert(api_doc["fs_card_type"].isNull());

    FSHealthStats sdmmc = {
        .backend = FS_BACKEND_SDMMC,
        .card_type = FS_CARD_TYPE_SDHC,
        .storage_mounted = true,
        .storage_used_bytes = 56,
        .storage_total_bytes = 78,
    };
    JsonDocument mqtt_doc;
    device_telemetry_append_fs_health(mqtt_doc, sdmmc);
    assert_string(mqtt_doc, "fs_backend", "sdmmc");
    assert(mqtt_doc["fs_mounted"] == true);
    assert(mqtt_doc["fs_used_bytes"] == 56);
    assert(mqtt_doc["fs_total_bytes"] == 78);
    assert_string(mqtt_doc, "fs_card_type", "sdhc");

    FSHealthStats unmounted_sdmmc = {
        .backend = FS_BACKEND_SDMMC,
        .card_type = FS_CARD_TYPE_NONE,
        .storage_mounted = false,
        .storage_used_bytes = 0,
        .storage_total_bytes = 0,
    };
    JsonDocument unmounted_doc;
    device_telemetry_append_fs_health(unmounted_doc, unmounted_sdmmc);
    assert_string(unmounted_doc, "fs_backend", "sdmmc");
    assert(unmounted_doc["fs_mounted"] == false);
    assert(unmounted_doc["fs_used_bytes"].isNull());
    assert(unmounted_doc["fs_total_bytes"].isNull());
    assert_string(unmounted_doc, "fs_card_type", "none");

    FSHealthStats unknown_sdmmc = unmounted_sdmmc;
    unknown_sdmmc.card_type = FS_CARD_TYPE_UNKNOWN;
    JsonDocument unknown_doc;
    device_telemetry_append_fs_health(unknown_doc, unknown_sdmmc);
    assert_string(unknown_doc, "fs_card_type", "unknown");
    return 0;
}