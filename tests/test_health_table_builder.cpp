// ============================================================================
// Unit tests for health_table_builder
// ============================================================================

#include <cstdio>
#include <cstring>

#include <ArduinoJson.h>

#include "health_table_builder.h"

struct KvPair {
    const char* key;
    const char* value;
};

static int g_pass = 0;
static int g_fail = 0;
static const KvPair* g_lookup_data = nullptr;

static void check_true(bool cond, const char* label) {
    if (!cond) {
        std::printf("  FAIL [%s]\n", label);
        g_fail++;
        return;
    }
    g_pass++;
}

static void check_eq_str(const char* got, const char* expected, const char* label) {
    if (!got || std::strcmp(got, expected) != 0) {
        std::printf("  FAIL [%s]\n    got:      \"%s\"\n    expected: \"%s\"\n",
                    label, got ? got : "<null>", expected);
        g_fail++;
        return;
    }
    g_pass++;
}

static bool lookup_from_table(const char* key, char* out, size_t out_len) {
    if (!key || !out || out_len == 0) return false;
    out[0] = '\0';
    if (!g_lookup_data) return false;

    for (size_t i = 0; g_lookup_data[i].key; i++) {
        if (std::strcmp(g_lookup_data[i].key, key) == 0) {
            strlcpy(out, g_lookup_data[i].value ? g_lookup_data[i].value : "", out_len);
            return true;
        }
    }
    return false;
}

static JsonObjectConst row_at(JsonArrayConst rows, size_t index) {
    if (index >= rows.size() || !rows[index].is<JsonObjectConst>()) {
        return JsonObjectConst();
    }
    return rows[index].as<JsonObjectConst>();
}

static JsonObjectConst row_by_metric(JsonArrayConst rows, const char* metric) {
    for (JsonVariantConst row_var : rows) {
        if (!row_var.is<JsonObjectConst>()) continue;
        JsonObjectConst row = row_var.as<JsonObjectConst>();
        if (std::strcmp(row["metric"] | "", metric) == 0) {
            return row;
        }
    }
    return JsonObjectConst();
}

static const char* cell_text(JsonObjectConst row, const char* key) {
    JsonVariantConst cell = row[key];
    if (cell.is<const char*>()) {
        return cell.as<const char*>();
    }
    if (cell.is<JsonObjectConst>()) {
        return cell.as<JsonObjectConst>()["text"] | "";
    }
    return "";
}

static void test_standard_contract_and_shapes() {
    std::printf("--- standard payload contract ---\n");

    static const KvPair kData[] = {
        {"cpu", "52"},
        {"heap_internal", "153600"},
        {"psram_free", "2097152"},
        {"psram_total", "4194304"},
        {"wifi_connected", "ON"},
        {"rssi", "-65"},
        {"uptime", "3661"},
        {"ip", "192.168.1.22"},
        {"chip", "ESP32-P4"},
        {"firmware", "1.13.0"},
        {nullptr, nullptr},
    };
    g_lookup_data = kData;

    char out[2048];
    bool ok = health_table_build(false, lookup_from_table, out, sizeof(out));
    check_true(ok, "build standard payload");

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, out);
    check_true(!err, "parse standard payload JSON");

    JsonArrayConst cols = doc["columns"].as<JsonArrayConst>();
    JsonArrayConst rows = doc["rows"].as<JsonArrayConst>();
    check_true(cols.size() == 2, "has 2 columns");
    check_true(rows.size() == 6, "has 6 standard rows");

    check_eq_str(doc["header_text_color"] | "", "#404070", "header color");
    check_eq_str(doc["row_text_color"] | "", "#b0b0d0", "row text color");
    check_eq_str(doc["default_bg"] | "", "#12122a", "default bg");

    JsonObjectConst cpu_row = row_at(rows, 0);
    check_eq_str(cpu_row["metric"] | "", "CPU", "first row metric CPU");

    JsonObjectConst wifi_row = row_at(rows, 3);
    check_eq_str(wifi_row["metric"] | "", "WiFi", "fourth row metric WiFi");
}

static void test_threshold_and_fallback_behavior() {
    std::printf("--- thresholds and fallback behavior ---\n");

    static const KvPair kData[] = {
        {"cpu", "90"},
        {"heap_internal", "81920"},
        {"psram_free", "0"},
        {"psram_total", "0"},
        {"wifi_connected", "OFF"},
        {"rssi", "-90"},
        {"uptime", "5"},
        {"ip", "0.0.0.0"},
        {nullptr, nullptr},
    };
    g_lookup_data = kData;

    char out[2048];
    bool ok = health_table_build(false, lookup_from_table, out, sizeof(out));
    check_true(ok, "build threshold payload");

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, out);
    check_true(!err, "parse threshold payload JSON");

    JsonArrayConst rows = doc["rows"].as<JsonArrayConst>();
    check_true(rows.size() == 6, "still 6 standard rows");

    JsonObjectConst cpu_row = row_at(rows, 0);
    check_eq_str(cpu_row["_bg"] | "", "#200808", "cpu critical background");
    JsonObjectConst cpu_value = cpu_row["value"].as<JsonObjectConst>();
    check_eq_str(cpu_value["color"] | "", "#ff6b6b", "cpu critical text color");

    JsonObjectConst psram_row = row_by_metric(rows, "PSRAM");
    check_eq_str(cell_text(psram_row, "value"), "N/A", "psram no-total fallback text");
    check_eq_str(psram_row["_bg"] | "", "#12122a", "psram neutral background");

    JsonObjectConst wifi_row = row_by_metric(rows, "WiFi");
    check_eq_str(cell_text(wifi_row, "value"), "Disconnected", "wifi disconnected text");
    check_eq_str(wifi_row["_bg"] | "", "#200808", "wifi disconnected background");

    JsonObjectConst ip_row = row_at(rows, 5);
    check_eq_str(ip_row["_bg"] | "", "#200808", "ip error background when offline");
}

static void test_extended_adds_chip_and_fw_rows() {
    std::printf("--- extended payload rows ---\n");

    static const KvPair kData[] = {
        {"cpu", "10"},
        {"heap_internal", "307200"},
        {"psram_free", "1048576"},
        {"psram_total", "2097152"},
        {"wifi_connected", "ON"},
        {"rssi", "-55"},
        {"uptime", "60"},
        {"ip", "192.168.1.99"},
        {"chip", "ESP32-P4"},
        {"firmware", "1.13.0-dev"},
        {nullptr, nullptr},
    };
    g_lookup_data = kData;

    char out[2048];
    bool ok = health_table_build(true, lookup_from_table, out, sizeof(out));
    check_true(ok, "build extended payload");

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, out);
    check_true(!err, "parse extended payload JSON");

    JsonArrayConst rows = doc["rows"].as<JsonArrayConst>();
    check_true(rows.size() == 8, "extended has 8 rows");

    JsonObjectConst chip_row = row_at(rows, 6);
    JsonObjectConst fw_row = row_at(rows, 7);
    check_eq_str(chip_row["metric"] | "", "Chip", "extended chip row present");
    check_eq_str(fw_row["metric"] | "", "FW", "extended firmware row present");
}

static void test_brightness_and_volume_rows() {
    std::printf("--- brightness and volume rows ---\n");

    static const KvPair kData[] = {
        {"cpu", "30"},
        {"heap_internal", "200000"},
        {"psram_free", "1048576"},
        {"psram_total", "2097152"},
        {"wifi_connected", "ON"},
        {"rssi", "-60"},
        {"uptime", "120"},
        {"ip", "192.168.1.10"},
        {"brightness", "75"},
        {"volume", "50"},
        {nullptr, nullptr},
    };
    g_lookup_data = kData;

    char out[2048];
    bool ok = health_table_build(false, lookup_from_table, out, sizeof(out));
    check_true(ok, "build payload with brightness+volume");

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, out);
    check_true(!err, "parse brightness+volume JSON");

    JsonArrayConst rows = doc["rows"].as<JsonArrayConst>();
    check_true(rows.size() == 8, "standard + brightness + volume = 8 rows");

    JsonObjectConst bright_row = row_by_metric(rows, "Backlight");
    check_eq_str(cell_text(bright_row, "value"), "75%", "brightness value formatted");

    JsonObjectConst vol_row = row_by_metric(rows, "Volume");
    check_eq_str(cell_text(vol_row, "value"), "50%", "volume value formatted");
}

static void test_brightness_only_no_volume() {
    std::printf("--- brightness only (no volume key) ---\n");

    static const KvPair kData[] = {
        {"cpu", "20"},
        {"heap_internal", "250000"},
        {"psram_free", "1048576"},
        {"psram_total", "2097152"},
        {"wifi_connected", "ON"},
        {"rssi", "-50"},
        {"uptime", "60"},
        {"ip", "192.168.1.5"},
        {"brightness", "100"},
        {nullptr, nullptr},
    };
    g_lookup_data = kData;

    char out[2048];
    bool ok = health_table_build(false, lookup_from_table, out, sizeof(out));
    check_true(ok, "build payload with brightness only");

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, out);
    check_true(!err, "parse brightness-only JSON");

    JsonArrayConst rows = doc["rows"].as<JsonArrayConst>();
    check_true(rows.size() == 7, "standard + brightness = 7 rows (no volume)");

    JsonObjectConst bright_row = row_by_metric(rows, "Backlight");
    check_eq_str(cell_text(bright_row, "value"), "100%", "brightness 100% formatted");

    JsonObjectConst vol_row = row_by_metric(rows, "Volume");
    check_true(!vol_row, "no volume row when key absent");
}

int main() {
    std::printf("=== health_table_builder tests ===\n\n");

    test_standard_contract_and_shapes();
    test_threshold_and_fallback_behavior();
    test_extended_adds_chip_and_fw_rows();
    test_brightness_and_volume_rows();
    test_brightness_only_no_volume();

    std::printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}