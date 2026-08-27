// ============================================================================
// Unit tests for print_log — ID generation, binding resolver, deferred I/O
// ============================================================================
// Host-native: compiled with stubs for LittleFS, Preferences, portMUX, and
// binding_template. Includes print_log.cpp directly (project test pattern).

#include <cassert>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdint>
#include <cstdarg>
#include <ctime>
#include <map>
#include <string>
#include <vector>

// Pre-define config_manager.h guard to block the real header
#define CONFIG_MANAGER_H

// Block the host board_config.h stub (tests/board_config.h) — it hardcodes
// HAS_DISPLAY=true and IS_SHUTTER_TESTER, which collide with this test's
// HAS_DISPLAY=0 / IS_DARKROOM_TIMER setup. print_log.{h,cpp} only need
// HAS_DISPLAY (set below) and IS_DARKROOM_TIMER (passed via -D); their other
// constants (DARKROOM_PRINT_LOG_MAX, PRINT_LOG_DIR, PRINT_LOG_MAX_SEGMENTS)
// are self-defined in print_log.h.
#define BOARD_CONFIG_H

// ---------------------------------------------------------------------------
// portMUX stubs — no-op on host (single-threaded tests)
// ---------------------------------------------------------------------------
struct portMUX_TYPE { int dummy; };
#define portMUX_INITIALIZER_UNLOCKED {0}
#define portENTER_CRITICAL(m) (void)(m)
#define portEXIT_CRITICAL(m) (void)(m)

// ---------------------------------------------------------------------------
// strlcpy shim — BSD/ESP libc function absent from host glibc. Normally
// provided via tests/board_config.h + tests/stubs.cpp, but this test blocks
// board_config.h and compiles standalone, so define it locally.
// ---------------------------------------------------------------------------
extern "C" size_t strlcpy(char* dst, const char* src, size_t siz) {
    size_t srclen = strlen(src);
    if (siz != 0) {
        size_t copylen = (srclen >= siz) ? (siz - 1) : srclen;
        memcpy(dst, src, copylen);
        dst[copylen] = '\0';
    }
    return srclen;
}

// ---------------------------------------------------------------------------
// Controllable time mock
// ---------------------------------------------------------------------------
static time_t s_mock_time = 0;  // 0 = no NTP

// Override time() for the CUT. Must match glibc's exception specifier
// (declared __THROW / noexcept via <ctime>) or the definitions conflict.
extern "C" time_t time(time_t* t) noexcept {
    if (t) *t = s_mock_time;
    return s_mock_time;
}

// Override localtime_r for the CUT
extern "C" struct tm* localtime_r(const time_t* timep, struct tm* result) noexcept {
    *result = *localtime(timep);
    return result;
}

// ---------------------------------------------------------------------------
// LittleFS stub — in-memory filesystem
// ---------------------------------------------------------------------------
static std::map<std::string, std::string> s_fs;  // path -> content
static std::string s_last_write_path;
static std::string s_last_write_content;

class FakeFile {
public:
    std::string path;
    std::string content;
    size_t pos = 0;
    bool is_write = false;
    bool valid = false;
    bool is_dir = false;

    // For directory iteration
    std::vector<std::string> dir_entries;
    int dir_idx = -1;
    FakeFile* child_file = nullptr;

    operator bool() const { return valid; }
    bool isDirectory() const { return is_dir; }

    size_t size() const { return content.size(); }

    void print(char c) {
        content += c;
    }
    void print(const char* s) {
        content += s;
    }
    size_t printf(const char* fmt, ...) {
        char buf[512];
        va_list args;
        va_start(args, fmt);
        int n = vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        if (n > 0) content.append(buf, n);
        return n > 0 ? n : 0;
    }

    size_t readBytes(char* buf, size_t len) {
        size_t avail = content.size() - pos;
        size_t rd = len < avail ? len : avail;
        memcpy(buf, content.data() + pos, rd);
        pos += rd;
        return rd;
    }

    size_t write(uint8_t c) {
        content += (char)c;
        return 1;
    }
    size_t write(const uint8_t* buf, size_t len) {
        content.append((const char*)buf, len);
        return len;
    }

    int available() { return (int)(content.size() - pos); }

    const char* name() const {
        // Return just the filename part
        auto slash = path.rfind('/');
        if (slash != std::string::npos) return path.c_str() + slash + 1;
        return path.c_str();
    }

    FakeFile openNextFile() {
        FakeFile f;
        dir_idx++;
        if (dir_idx >= (int)dir_entries.size()) return f;
        f.valid = true;
        f.path = dir_entries[dir_idx];
        f.content = s_fs.count(f.path) ? s_fs[f.path] : "";
        return f;
    }

    void close() {
        if (is_write && valid) {
            s_fs[path] = content;
            s_last_write_path = path;
            s_last_write_content = content;
        }
        valid = false;
    }
};

// Use FakeFile as the File type
#define File FakeFile

class FakeLittleFS {
public:
    bool exists(const char* path) {
        return s_fs.count(path) > 0 || std::string(path) == "/prints";
    }

    void mkdir(const char* path) {
        (void)path;
    }

    FakeFile open(const char* path, const char* mode = "r") {
        FakeFile f;
        f.path = path;
        std::string smode = mode ? mode : "r";

        // Check if it's a directory open (no mode or "r" on a dir-like path)
        // Heuristic: if path has no extension and entries exist under it
        std::string prefix = std::string(path) + "/";
        bool has_children = false;
        for (auto& kv : s_fs) {
            if (kv.first.substr(0, prefix.size()) == prefix) {
                has_children = true;
                break;
            }
        }

        if (has_children && smode == "r") {
            f.valid = true;
            f.is_dir = true;
            f.dir_idx = -1;
            for (auto& kv : s_fs) {
                if (kv.first.substr(0, prefix.size()) == prefix) {
                    // Only direct children
                    std::string rest = kv.first.substr(prefix.size());
                    if (rest.find('/') == std::string::npos) {
                        f.dir_entries.push_back(kv.first);
                    }
                }
            }
            return f;
        }

        if (smode == "w") {
            f.valid = true;
            f.is_write = true;
            f.content = "";
            return f;
        }

        if (s_fs.count(path)) {
            f.valid = true;
            f.content = s_fs[path];
            return f;
        }

        return f;  // invalid
    }

    bool remove(const char* path) {
        return s_fs.erase(path) > 0;
    }
};

static FakeLittleFS LittleFS;

// ---------------------------------------------------------------------------
// Preferences stub — in-memory NVS
// ---------------------------------------------------------------------------
static std::map<std::string, uint32_t> s_nvs_uint;
static std::map<std::string, uint16_t> s_nvs_ushort;

class Preferences {
public:
    std::string ns;
    bool begin(const char* name, bool readOnly) {
        (void)readOnly;
        ns = name;
        return true;
    }
    uint32_t getUInt(const char* key, uint32_t def = 0) {
        std::string k = ns + "." + key;
        return s_nvs_uint.count(k) ? s_nvs_uint[k] : def;
    }
    void putUInt(const char* key, uint32_t val) {
        s_nvs_uint[ns + "." + key] = val;
    }
    uint16_t getUShort(const char* key, uint16_t def = 0) {
        std::string k = ns + "." + key;
        return s_nvs_ushort.count(k) ? s_nvs_ushort[k] : def;
    }
    void putUShort(const char* key, uint16_t val) {
        s_nvs_ushort[ns + "." + key] = val;
    }
};

// ---------------------------------------------------------------------------
// Binding template stub — captures resolver
// ---------------------------------------------------------------------------
#include "binding_template.h"
static binding_resolver_fn s_print_resolver = nullptr;

bool binding_template_register(const char* scheme, binding_resolver_fn resolver,
                               binding_topic_collector_fn collector,
                               const BindingSchemeSpec& spec) {
    (void)scheme;
    (void)collector;
    (void)spec;
    s_print_resolver = resolver;
    return true;
}

// ---------------------------------------------------------------------------
// Block real LittleFS.h, Preferences.h, and display_manager.h
// ---------------------------------------------------------------------------
#define _LITTLEFS_H_
#define _PREFERENCES_H_
#undef HAS_DISPLAY
#define HAS_DISPLAY 0

// ---------------------------------------------------------------------------
// Unit under test
// ---------------------------------------------------------------------------
#include "print_log.h"
#include "print_log.cpp"

// ---------------------------------------------------------------------------
// Test harness
// ---------------------------------------------------------------------------
static int g_tests = 0;
static int g_passed = 0;

#define TEST(name) static void name()
#define RUN(name) do {                                       \
    g_tests++;                                               \
    printf("  %-55s ", #name);                               \
    name();                                                  \
    g_passed++;                                              \
    printf("PASS\n");                                        \
} while (0)

#define ASSERT_STR_EQ(actual, expected) do {                 \
    if (strcmp((actual), (expected)) != 0) {                  \
        printf("FAIL\n    %s:%d: \"%s\" != \"%s\"\n",       \
               __FILE__, __LINE__, (actual), (expected));    \
        assert(false);                                       \
    }                                                        \
} while (0)

#define ASSERT_EQ(actual, expected) do {                     \
    auto _a = (actual); auto _e = (expected);                \
    if (_a != _e) {                                          \
        printf("FAIL\n    %s:%d: %d != %d\n",               \
               __FILE__, __LINE__, (int)_a, (int)_e);       \
        assert(false);                                       \
    }                                                        \
} while (0)

#define ASSERT_TRUE(cond) do {                               \
    if (!(cond)) {                                           \
        printf("FAIL\n    %s:%d: assertion failed\n",        \
               __FILE__, __LINE__);                          \
        assert(false);                                       \
    }                                                        \
} while (0)

// ---------------------------------------------------------------------------
// Helper — reset all state between tests
// ---------------------------------------------------------------------------
static void reset() {
    s_fs.clear();
    s_nvs_uint.clear();
    s_nvs_ushort.clear();
    s_mock_time = 0;
    s_last_write_path.clear();
    s_last_write_content.clear();
    s_print_resolver = nullptr;

    // Re-init
    print_log_init();
}

static char s_resolve_buf[128];
static const char* resolve(const char* params) {
    assert(s_print_resolver);
    s_resolve_buf[0] = '\0';
    s_print_resolver(params, s_resolve_buf, sizeof(s_resolve_buf));
    return s_resolve_buf;
}

// ============================================================================
// Tests
// ============================================================================

TEST(init_registers_binding_and_sets_defaults) {
    reset();
    assert(s_print_resolver != nullptr);
    ASSERT_EQ(print_log_get_count(), 0);
}

TEST(binding_count_returns_zero_initially) {
    reset();
    ASSERT_STR_EQ(resolve("count"), "0");
}

TEST(binding_last_id_returns_placeholder_initially) {
    reset();
    ASSERT_STR_EQ(resolve("last_id"), "---");
}

TEST(binding_id_returns_placeholder_before_first_save) {
    reset();
    // Before any save, id shows placeholder
    ASSERT_STR_EQ(resolve("id"), "---");
}

TEST(binding_unknown_key_returns_error) {
    reset();
    ASSERT_STR_EQ(resolve("bogus"), "ERR:bad_key");
}

TEST(binding_empty_key_returns_error) {
    reset();
    ASSERT_STR_EQ(resolve(""), "ERR:no_key");
}

TEST(pend_exposure_and_loop_saves_file) {
    reset();

    PrintLogExposureData d = {};
    d.set_time_s = 10.0f;
    d.effective_time_s = 9.5f;
    d.dry_down_pct = 5.0f;
    d.lref = 0.0f;
    d.zone5_time = 0.0f;
    d.l_bright = -1.0f;
    d.l_dark = -1.0f;
    d.sbr = 0.0f;
    d.grade = 0.0f;
    d.grade_label = nullptr;
    d.mag_factor = 0.0f;

    print_log_pend_exposure(d);
    print_log_loop();

    // File should have been written
    ASSERT_TRUE(!s_last_write_path.empty());
    ASSERT_TRUE(s_last_write_content.find("\"exposure\"") != std::string::npos);
    ASSERT_TRUE(s_last_write_content.find("10.0") != std::string::npos);
}

TEST(exposure_increments_count_and_updates_bindings) {
    reset();

    PrintLogExposureData d = {};
    d.set_time_s = 5.0f;
    d.effective_time_s = 5.0f;
    d.l_bright = -1.0f;
    d.l_dark = -1.0f;

    print_log_pend_exposure(d);
    print_log_loop();

    ASSERT_STR_EQ(resolve("count"), "1");
    ASSERT_STR_EQ(resolve("last_id"), "000000-001");
    // After save, id shows the actual assigned ID
    ASSERT_STR_EQ(resolve("id"), "000000-001");
}

TEST(multiple_exposures_increment_count) {
    reset();

    PrintLogExposureData d = {};
    d.set_time_s = 3.0f;
    d.effective_time_s = 3.0f;
    d.l_bright = -1.0f;
    d.l_dark = -1.0f;

    for (int i = 0; i < 3; i++) {
        print_log_pend_exposure(d);
        print_log_loop();
    }

    ASSERT_STR_EQ(resolve("count"), "3");
    ASSERT_STR_EQ(resolve("last_id"), "000000-003");
    ASSERT_STR_EQ(resolve("id"), "000000-003");
}

TEST(pend_strip_and_loop_saves_file) {
    reset();

    PrintLogStripData d = {};
    d.base_time_s = 8.0f;
    d.step_label = "1/3 stop";
    d.step_stops = 0.333f;
    d.segment_count = 3;
    d.segments[0] = {0.0f, 8.0f, 8.0f};
    d.segments[1] = {0.333f, 10.1f, 2.1f};
    d.segments[2] = {0.667f, 12.7f, 2.6f};

    print_log_pend_strip(d);
    print_log_loop();

    ASSERT_TRUE(!s_last_write_path.empty());
    ASSERT_TRUE(s_last_write_content.find("\"test_strip\"") != std::string::npos);
    ASSERT_TRUE(s_last_write_content.find("\"segments\"") != std::string::npos);
}

TEST(ntp_time_produces_dated_id) {
    reset();

    // Set time to 2025-01-15 12:00:00 UTC
    s_mock_time = 1736942400;

    // Re-init with time set, then save an exposure to get a dated ID
    print_log_init();
    ASSERT_STR_EQ(resolve("id"), "---");  // placeholder before first save

    PrintLogExposureData d = {};
    d.set_time_s = 5.0f;
    d.effective_time_s = 5.0f;
    d.l_bright = -1.0f;
    d.l_dark = -1.0f;
    print_log_pend_exposure(d);
    print_log_loop();

    // After save, id shows actual YYMMDD-NNN
    const char* id = resolve("id");
    ASSERT_TRUE(strlen(id) == 10);  // YYMMDD-NNN
    ASSERT_TRUE(id[6] == '-');
    ASSERT_STR_EQ(id + 7, "001");
}

TEST(exposure_with_metering_context) {
    reset();

    PrintLogExposureData d = {};
    d.set_time_s = 15.0f;
    d.effective_time_s = 14.3f;
    d.dry_down_pct = 5.0f;
    d.lref = 100.0f;
    d.zone5_time = 12.0f;
    d.l_bright = 400.0f;
    d.l_dark = 25.0f;
    d.sbr = 4.0f;
    d.grade = 2.0f;
    d.grade_label = "Normal";
    d.mag_factor = 1.5f;

    print_log_pend_exposure(d);
    print_log_loop();

    ASSERT_TRUE(s_last_write_content.find("\"lref\"") != std::string::npos);
    ASSERT_TRUE(s_last_write_content.find("\"zone5_time\"") != std::string::npos);
    ASSERT_TRUE(s_last_write_content.find("\"sbr\"") != std::string::npos);
    ASSERT_TRUE(s_last_write_content.find("\"grade\"") != std::string::npos);
    ASSERT_TRUE(s_last_write_content.find("\"Normal\"") != std::string::npos);
    ASSERT_TRUE(s_last_write_content.find("\"mag_factor\"") != std::string::npos);
}

TEST(no_metering_omits_optional_fields) {
    reset();

    PrintLogExposureData d = {};
    d.set_time_s = 10.0f;
    d.effective_time_s = 10.0f;
    d.lref = 0.0f;
    d.zone5_time = 0.0f;
    d.l_bright = -1.0f;
    d.l_dark = -1.0f;
    d.mag_factor = 0.0f;

    print_log_pend_exposure(d);
    print_log_loop();

    ASSERT_TRUE(s_last_write_content.find("\"lref\"") == std::string::npos);
    ASSERT_TRUE(s_last_write_content.find("\"zone5_time\"") == std::string::npos);
    ASSERT_TRUE(s_last_write_content.find("\"sbr\"") == std::string::npos);
    ASSERT_TRUE(s_last_write_content.find("\"grade\"") == std::string::npos);
    ASSERT_TRUE(s_last_write_content.find("\"mag_factor\"") == std::string::npos);
}

TEST(clear_id_resets_to_placeholder) {
    reset();

    // Save an exposure so id has a real value
    PrintLogExposureData d = {};
    d.set_time_s = 5.0f;
    d.effective_time_s = 5.0f;
    d.l_bright = -1.0f;
    d.l_dark = -1.0f;
    print_log_pend_exposure(d);
    print_log_loop();

    ASSERT_STR_EQ(resolve("id"), "000000-001");

    // Clear resets to placeholder
    print_log_clear_id();
    ASSERT_STR_EQ(resolve("id"), "---");

    // Save again — id should show the new actual ID
    print_log_pend_exposure(d);
    print_log_loop();
    ASSERT_STR_EQ(resolve("id"), "000000-002");
}

TEST(strip_save_sets_actual_id) {
    reset();

    ASSERT_STR_EQ(resolve("id"), "---");

    PrintLogStripData d = {};
    d.base_time_s = 8.0f;
    d.step_label = "1/3 stop";
    d.step_stops = 0.333f;
    d.segment_count = 3;
    d.segments[0] = {0.0f, 8.0f, 8.0f};
    d.segments[1] = {0.333f, 10.1f, 2.1f};
    d.segments[2] = {0.667f, 12.7f, 2.6f};

    print_log_pend_strip(d);
    print_log_loop();

    // After strip save, id shows the actual assigned ID
    ASSERT_STR_EQ(resolve("id"), "000000-001");
    ASSERT_STR_EQ(resolve("last_id"), "000000-001");
    ASSERT_STR_EQ(resolve("count"), "1");
}

// ============================================================================
// Starring tests
// ============================================================================

static PrintLogExposureData make_exposure() {
    PrintLogExposureData d = {};
    d.set_time_s = 5.0f;
    d.effective_time_s = 5.0f;
    d.l_bright = -1.0f;
    d.l_dark = -1.0f;
    return d;
}

TEST(starred_defaults_to_zero) {
    reset();
    ASSERT_STR_EQ(resolve("starred"), "0");
    ASSERT_STR_EQ(resolve("star_label"), "");
}

TEST(toggle_star_noop_before_first_save) {
    reset();
    print_log_dispatch("toggle_star", "");
    ASSERT_STR_EQ(resolve("starred"), "0");
}

TEST(set_star_noop_before_first_save) {
    reset();
    print_log_dispatch("set_star", "1");
    ASSERT_STR_EQ(resolve("starred"), "0");
}

TEST(toggle_star_toggles_starred_state) {
    reset();
    auto d = make_exposure();
    print_log_pend_exposure(d);
    print_log_loop();

    ASSERT_STR_EQ(resolve("starred"), "0");
    print_log_dispatch("toggle_star", "");
    ASSERT_STR_EQ(resolve("starred"), "1");
    ASSERT_STR_EQ(resolve("star_label"), "*");

    print_log_dispatch("toggle_star", "");
    ASSERT_STR_EQ(resolve("starred"), "0");
    ASSERT_STR_EQ(resolve("star_label"), "");
}

TEST(set_star_sets_starred_explicitly) {
    reset();
    auto d = make_exposure();
    print_log_pend_exposure(d);
    print_log_loop();

    print_log_dispatch("set_star", "1");
    ASSERT_STR_EQ(resolve("starred"), "1");

    print_log_dispatch("set_star", "0");
    ASSERT_STR_EQ(resolve("starred"), "0");
}

TEST(new_save_resets_starred) {
    reset();
    auto d = make_exposure();
    print_log_pend_exposure(d);
    print_log_loop();

    print_log_dispatch("set_star", "1");
    ASSERT_STR_EQ(resolve("starred"), "1");

    // Save another exposure — starred should reset
    print_log_pend_exposure(d);
    print_log_loop();
    ASSERT_STR_EQ(resolve("starred"), "0");
}

TEST(starred_persists_in_json_file) {
    reset();
    auto d = make_exposure();
    print_log_pend_exposure(d);
    print_log_loop();

    print_log_dispatch("toggle_star", "");
    print_log_loop();  // flush deferred star write
    // Check the file was rewritten with "starred":true
    ASSERT_TRUE(s_last_write_content.find("\"starred\":true") != std::string::npos);

    // Toggle back — "starred" should be removed
    print_log_dispatch("toggle_star", "");
    print_log_loop();  // flush deferred star write
    ASSERT_TRUE(s_last_write_content.find("\"starred\"") == std::string::npos);
}

// ============================================================================
// main
// ============================================================================

int main() {
    printf("=== print_log tests ===\n");
    RUN(init_registers_binding_and_sets_defaults);
    RUN(binding_count_returns_zero_initially);
    RUN(binding_last_id_returns_placeholder_initially);
    RUN(binding_id_returns_placeholder_before_first_save);
    RUN(binding_unknown_key_returns_error);
    RUN(binding_empty_key_returns_error);
    RUN(pend_exposure_and_loop_saves_file);
    RUN(exposure_increments_count_and_updates_bindings);
    RUN(multiple_exposures_increment_count);
    RUN(pend_strip_and_loop_saves_file);
    RUN(ntp_time_produces_dated_id);
    RUN(exposure_with_metering_context);
    RUN(no_metering_omits_optional_fields);
    RUN(clear_id_resets_to_placeholder);
    RUN(strip_save_sets_actual_id);
    RUN(starred_defaults_to_zero);
    RUN(toggle_star_noop_before_first_save);
    RUN(set_star_noop_before_first_save);
    RUN(toggle_star_toggles_starred_state);
    RUN(set_star_sets_starred_explicitly);
    RUN(new_save_resets_starred);
    RUN(starred_persists_in_json_file);
    printf("\n  %d/%d tests passed\n", g_passed, g_tests);
    return g_passed == g_tests ? 0 : 1;
}
