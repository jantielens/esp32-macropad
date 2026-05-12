#include "health_table_builder.h"
#include "board_config.h"

#if HAS_DISPLAY

#include <ArduinoJson.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

namespace {

constexpr const char* kHeaderTextColor = "#404070";
constexpr const char* kRowTextColor = "#b0b0d0";
constexpr const char* kDefaultBg = "#12122a";

constexpr const char* kBgOk = "#0a2a0a";
constexpr const char* kBgWarn = "#1e1600";
constexpr const char* kBgErr = "#200808";
constexpr const char* kBgNeutral = "#12122a";

constexpr const char* kFgOk = "#6edc8c";
constexpr const char* kFgWarn = "#f0c55f";
constexpr const char* kFgErr = "#ff6b6b";
constexpr const char* kFgNeutral = "#b0b0d0";

long parse_long_or(const char* s, long fallback) {
    if (!s || !s[0]) return fallback;
    char* end = nullptr;
    long v = strtol(s, &end, 10);
    return (end == s) ? fallback : v;
}

void format_bytes_short(long bytes, char* out, size_t out_len) {
    if (bytes <= 0) {
        strlcpy(out, "0 KB", out_len);
        return;
    }
    if (bytes >= 1024L * 1024L) {
        snprintf(out, out_len, "%.1f MB", (double)bytes / (1024.0 * 1024.0));
    } else {
        snprintf(out, out_len, "%ld KB", bytes / 1024L);
    }
}

void format_uptime(long sec, char* out, size_t out_len) {
    if (sec < 0) sec = 0;
    long h = sec / 3600L;
    long m = (sec % 3600L) / 60L;
    long s = sec % 60L;
    snprintf(out, out_len, "%ld:%02ld:%02ld", h, m, s);
}

void add_health_table_columns(JsonDocument& doc) {
    JsonArray cols = doc["columns"].to<JsonArray>();
    JsonObject metric = cols.add<JsonObject>();
    metric["key"] = "metric";
    metric["header"] = "Metric";
    metric["width_pct"] = 42;

    JsonObject value = cols.add<JsonObject>();
    value["key"] = "value";
    value["header"] = "Value";
    value["width_pct"] = 58;
}

void add_health_table_row(JsonArray rows, const char* metric, const char* value,
                          const char* bg, const char* value_color = nullptr) {
    JsonObject row = rows.add<JsonObject>();
    row["_bg"] = bg;
    row["metric"] = metric ? metric : "";

    if (value_color && value_color[0]) {
        JsonObject vobj = row["value"].to<JsonObject>();
        vobj["text"] = value ? value : "";
        vobj["color"] = value_color;
    } else {
        row["value"] = value ? value : "";
    }
}

} // namespace

bool health_table_build(bool extended, HealthTableLookupFn lookup, char* out, size_t out_len) {
    if (!lookup || !out || out_len == 0) {
        if (out && out_len > 0) out[0] = '\0';
        return false;
    }

    JsonDocument doc;
    doc["header_text_color"] = kHeaderTextColor;
    doc["row_text_color"] = kRowTextColor;
    doc["default_bg"] = kDefaultBg;
    add_health_table_columns(doc);

    JsonArray rows = doc["rows"].to<JsonArray>();

    char raw[64];
    char cpu_val[32];
    char heap_val[32];
    char psram_val[32];
    char wifi_val[32];
    char uptime_val[32];
    char ip_val[64];
    char chip_val[64];
    char fw_val[64];

    lookup("cpu", raw, sizeof(raw));
    long cpu = parse_long_or(raw, -1);
    if (cpu >= 0) snprintf(cpu_val, sizeof(cpu_val), "%ld%%", cpu);
    else strlcpy(cpu_val, "?", sizeof(cpu_val));
    add_health_table_row(rows, "CPU", cpu_val,
                         (cpu < 0) ? kBgWarn : (cpu >= 85 ? kBgErr : (cpu >= 60 ? kBgWarn : kBgOk)),
                         (cpu < 0) ? kFgWarn : (cpu >= 85 ? kFgErr : (cpu >= 60 ? kFgWarn : kFgOk)));

    lookup("heap_internal", raw, sizeof(raw));
    long heap_b = parse_long_or(raw, -1);
    if (heap_b >= 0) format_bytes_short(heap_b, heap_val, sizeof(heap_val));
    else strlcpy(heap_val, "?", sizeof(heap_val));
    add_health_table_row(rows, "Heap", heap_val,
                         (heap_b < 0) ? kBgWarn : (heap_b > 200L * 1024L ? kBgOk : (heap_b > 100L * 1024L ? kBgWarn : kBgErr)),
                         (heap_b < 0) ? kFgWarn : (heap_b > 200L * 1024L ? kFgOk : (heap_b > 100L * 1024L ? kFgWarn : kFgErr)));

    char psram_free_raw[32];
    char psram_total_raw[32];
    lookup("psram_free", psram_free_raw, sizeof(psram_free_raw));
    lookup("psram_total", psram_total_raw, sizeof(psram_total_raw));
    long psram_free_b = parse_long_or(psram_free_raw, 0);
    long psram_total_b = parse_long_or(psram_total_raw, 0);
    if (psram_total_b <= 0) {
        strlcpy(psram_val, "N/A", sizeof(psram_val));
        add_health_table_row(rows, "PSRAM", psram_val, kBgNeutral, kFgNeutral);
    } else {
        format_bytes_short(psram_free_b, psram_val, sizeof(psram_val));
        long free_pct = (psram_free_b * 100L) / psram_total_b;
        add_health_table_row(rows, "PSRAM", psram_val,
                             (free_pct >= 35) ? kBgOk : (free_pct >= 15 ? kBgWarn : kBgErr),
                             (free_pct >= 35) ? kFgOk : (free_pct >= 15 ? kFgWarn : kFgErr));
    }

    char wifi_state_raw[12];
    char rssi_raw[24];
    lookup("wifi_connected", wifi_state_raw, sizeof(wifi_state_raw));
    lookup("rssi", rssi_raw, sizeof(rssi_raw));
    bool wifi_on = (strcmp(wifi_state_raw, "ON") == 0);
    long rssi = parse_long_or(rssi_raw, -1000);
    if (!wifi_on) {
        strlcpy(wifi_val, "Disconnected", sizeof(wifi_val));
        add_health_table_row(rows, "WiFi", wifi_val, kBgErr, kFgErr);
    } else {
        snprintf(wifi_val, sizeof(wifi_val), "%ld dBm", rssi);
        add_health_table_row(rows, "WiFi", wifi_val,
                             (rssi >= -67) ? kBgOk : (rssi >= -75 ? kBgWarn : kBgErr),
                             (rssi >= -67) ? kFgOk : (rssi >= -75 ? kFgWarn : kFgErr));
    }

    lookup("uptime", raw, sizeof(raw));
    format_uptime(parse_long_or(raw, 0), uptime_val, sizeof(uptime_val));
    add_health_table_row(rows, "Uptime", uptime_val, kBgNeutral, kFgNeutral);

    lookup("ip", raw, sizeof(raw));
    strlcpy(ip_val, raw[0] ? raw : "0.0.0.0", sizeof(ip_val));
    add_health_table_row(rows, "IP", ip_val,
                         (!wifi_on || strcmp(ip_val, "0.0.0.0") == 0) ? kBgErr : kBgOk,
                         (!wifi_on || strcmp(ip_val, "0.0.0.0") == 0) ? kFgErr : kFgOk);

    char bright_val[16];
    if (lookup("brightness", raw, sizeof(raw))) {
        int bright = (int)parse_long_or(raw, -1);
        snprintf(bright_val, sizeof(bright_val), "%d%%", bright >= 0 ? bright : 0);
        add_health_table_row(rows, "Backlight", bright_val, kBgNeutral, kFgNeutral);
    }

    char vol_val[16];
    if (lookup("volume", raw, sizeof(raw))) {
        int vol = (int)parse_long_or(raw, -1);
        snprintf(vol_val, sizeof(vol_val), "%d%%", vol >= 0 ? vol : 0);
        add_health_table_row(rows, "Volume", vol_val, kBgNeutral, kFgNeutral);
    }

    if (extended) {
        lookup("chip", raw, sizeof(raw));
        strlcpy(chip_val, raw, sizeof(chip_val));
        add_health_table_row(rows, "Chip", chip_val, kBgNeutral, kFgNeutral);

        lookup("firmware", raw, sizeof(raw));
        strlcpy(fw_val, raw, sizeof(fw_val));
        add_health_table_row(rows, "FW", fw_val, kBgNeutral, kFgNeutral);
    }

    serializeJson(doc, out, out_len);
    return true;
}

#else

bool health_table_build(bool extended, HealthTableLookupFn lookup, char* out, size_t out_len) {
    (void)extended;
    (void)lookup;
    if (out && out_len > 0) out[0] = '\0';
    return false;
}

#endif
