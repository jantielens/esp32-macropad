// ============================================================================
// MCP result-string copy-vs-link guard
// ============================================================================
// Regression test for the class of bug where an MCP tool stores a string into
// its result JsonObject by LINKING a `const char*` that points at transient
// memory (a stack buffer, or a heap struct the tool frees) instead of COPYING
// it. ArduinoJson stores a `const char*` by reference and a `char*` / `String`
// by value; the result document is serialized by the dispatcher AFTER the tool
// handler returns, so any linked pointer into the handler's locals dangles and
// serializes as garbage.
//
// The original hardware symptom (control tools): `scale_control` returned
//   {"status":"dispatchd\xCC\x8D...."}
// because finish_control did `result["status"] = (msg && msg[0]) ? msg : "ok";`
// — the ternary promoted the caller's stack `char msg[]` to `const char*`, which
// ArduinoJson linked. The same trap bit several read tools (e.g. the shutter
// `detected_travel` field copied from a freed PSRAM struct, and the core
// list_pads button labels read from a reused-then-freed PadConfig buffer).
//
// This test pins the ArduinoJson semantics our fixes rely on, using the real
// library: the SAFE patterns (String(...) and mutable char[]) must survive the
// source memory being clobbered/freed, while a raw const char* is shown to link
// (documenting exactly why the tools must copy).
//
// WHEN THIS TEST FAILS: either ArduinoJson changed its storage policy, or a tool
// pattern was "simplified" back to linking transient memory. Re-audit every
// `result[...] = <const char*>` in the mcp_tools_*.cpp files and wrap transient
// sources in String() (or assign a mutable char[]).
// ============================================================================

#include <ArduinoJson.h>

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>

// The device code wraps transient strings in Arduino's `String` to force a copy;
// that type is unavailable host-side. ArduinoJson copies std::string with the
// identical by-value policy, so the test uses std::string to exercise the same
// copy semantics.
using CopyString = std::string;

static int g_failures = 0;

#define CHECK(cond, label)                                                     \
    do {                                                                       \
        if (!(cond)) { std::printf("FAIL: %s\n", (label)); g_failures++; }     \
        else         { std::printf("ok:   %s\n", (label)); }                   \
    } while (0)

// Serialize `doc` into a fixed buffer for strcmp comparisons.
static const char* dump(const JsonDocument& doc, char* out, size_t out_len) {
    serializeJson(doc, out, out_len);
    return out;
}

// --- Case A: control-result status from a stack buffer (the reported bug) ----
// Mirrors finish_control(): the status text lives in the caller's `char msg[]`,
// which is reused/destroyed before serialization. Wrapping in String() must copy.
static void test_control_status_copies_stack_buffer() {
    JsonDocument doc;
    {
        char msg[96];
        std::strcpy(msg, "dispatched tare");
        doc["status"] = CopyString(msg);          // SAFE: copy
        std::memset(msg, 0xAB, sizeof(msg));       // clobber after assignment
    }
    char out[128];
    CHECK(std::strcmp(dump(doc, out, sizeof(out)), "{\"status\":\"dispatched tare\"}") == 0,
          "control status copied from clobbered stack buffer");
}

// --- Case B: read field from a heap struct the tool frees --------------------
// Mirrors sh_emit_measurement_summary() copying m.detected_travel out of a
// heap_caps-allocated struct that the handler frees before returning.
static void test_read_field_copies_freed_heap() {
    JsonDocument doc;
    char* heap = (char*)std::malloc(8);
    std::strcpy(heap, "V");
    doc["detected_travel"] = CopyString(heap);     // SAFE: copy
    std::memset(heap, 0, 8);
    std::free(heap);                               // freed before serialize
    char out[128];
    CHECK(std::strcmp(dump(doc, out, sizeof(out)), "{\"detected_travel\":\"V\"}") == 0,
          "read field copied from freed heap struct");
}

// --- Case C: mutable char[] is copied ----------------------------------------
// Assigning a non-const char[] (decays to char*) makes ArduinoJson copy. This is
// why get_brew_status / list_pads' `sid`/`nm` buffers are safe without String().
static void test_mutable_char_array_copies() {
    JsonDocument doc;
    char buf[16];
    std::strcpy(buf, "orig");
    doc["v"] = buf;                                // SAFE: char* -> copy
    std::strcpy(buf, "MUTATED");
    char out[64];
    CHECK(std::strcmp(dump(doc, out, sizeof(out)), "{\"v\":\"orig\"}") == 0,
          "mutable char[] assignment copies");
}

// --- Case D: raw const char* LINKS (documents the trap) ----------------------
// Proves the dangerous behavior the fixes avoid: assigning a const char* stores
// the pointer, so a later mutation of the source is reflected. A tool that links
// a pointer into a freed/reused buffer would serialize garbage the same way.
static void test_const_char_ptr_links() {
    JsonDocument doc;
    char buf[16];
    std::strcpy(buf, "orig");
    const char* p = buf;
    doc["v"] = p;                                  // UNSAFE: const char* -> link
    std::strcpy(buf, "MUTATED");                   // mutate after assignment
    char out[64];
    CHECK(std::strcmp(dump(doc, out, sizeof(out)), "{\"v\":\"MUTATED\"}") == 0,
          "raw const char* links (mutation reflected) — why tools must copy");
}

// --- Case E: ternary with a literal promotes to const char* (the exact bug) ---
// `cond ? char_buf : "literal"` has type const char* even though one branch is a
// writable buffer — so ArduinoJson links it. This is the precise shape that
// produced the garbled control-status output; the fixed code copies instead.
static void test_ternary_with_literal_links() {
    JsonDocument doc;
    char buf[16];
    std::strcpy(buf, "orig");
    doc["v"] = (buf[0] ? buf : "");                // promotes to const char* -> link
    std::strcpy(buf, "MUTATED");
    char out[64];
    CHECK(std::strcmp(dump(doc, out, sizeof(out)), "{\"v\":\"MUTATED\"}") == 0,
          "ternary 'cond ? buf : literal' links (the reported failure shape)");
}

int main() {
    test_control_status_copies_stack_buffer();
    test_read_field_copies_freed_heap();
    test_mutable_char_array_copies();
    test_const_char_ptr_links();
    test_ternary_with_literal_links();

    if (g_failures) {
        std::printf("\n%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("\nAll MCP result-string checks passed\n");
    return 0;
}
