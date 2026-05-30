// ============================================================================
// Coffee-scale command-length contract guard
// ============================================================================
// Protects the SCALE_CMD_MAX_LEN / BREW_CMD_MAX_LEN contract introduced in
// commit 864ad13 (post-audit polish). The original hardware bug: the scale/brew
// command buffers reused CONFIG_TIMER_CMD_MAX_LEN (12), which silently truncated
// "cal_weight_set" (15 incl. NUL) and "set_template" (13 incl. NUL) via strlcpy,
// producing "unknown cmd" dispatch failures with no compile-time warning.
//
// This test mirrors the literal command strings accepted by the parse/dispatch
// paths of scale_actions.cpp and brew_actions.cpp and asserts — at compile time
// — that each one fits its owning constant (including the NUL terminator). It
// pulls the constants from the real coffee_scale_payload.h header, so the test
// always checks against the production sizing, not a hand-copied value.
//
// WHEN THIS TEST FAILS: a command string was added to a dispatch chain (or
// lengthened) without bumping its *_CMD_MAX_LEN constant. The static_assert
// message names the offending command. Fix by raising the constant in
// coffee_scale_payload.h (the per-payload static_asserts there confirm the
// struct still fits ACTION_PAYLOAD_DEVICE_CLASS_BYTES).
//
// SYNTHETIC REGRESSION EXAMPLE: adding a brew command longer than 12 chars,
// e.g. registering "start_preheat" (14 incl. NUL > BREW_CMD_MAX_LEN=13), would
// trip:
//     BREW_CMD_FITS("start_preheat");
//   error: static assertion failed: brew_command "start_preheat" exceeds
//          BREW_CMD_MAX_LEN; bump it in coffee_scale_payload.h
// The fix is to raise BREW_CMD_MAX_LEN in coffee_scale_payload.h.
// ============================================================================

#include "device_classes/coffee_scale/coffee_scale_payload.h"

#include <cstdio>
#include <cstring>

// sizeof(string literal) includes the NUL terminator, so it equals the storage
// a strlcpy into a char[N] needs. The command fits iff sizeof(lit) <= N.
#define SCALE_CMD_FITS(lit)                                                     \
    static_assert(sizeof(lit) <= SCALE_CMD_MAX_LEN,                            \
                  "scale_command \"" lit "\" exceeds SCALE_CMD_MAX_LEN; "      \
                  "bump it in coffee_scale_payload.h")
#define BREW_CMD_FITS(lit)                                                      \
    static_assert(sizeof(lit) <= BREW_CMD_MAX_LEN,                            \
                  "brew_command \"" lit "\" exceeds BREW_CMD_MAX_LEN; "        \
                  "bump it in coffee_scale_payload.h")

// --- scale_actions.cpp accepted commands (scale_parse -> scale_dispatch) -----
SCALE_CMD_FITS("tare");
SCALE_CMD_FITS("calibrate");
SCALE_CMD_FITS("cal_weight");
SCALE_CMD_FITS("cal_weight_set");

// --- brew_actions.cpp accepted commands (brew_parse -> brew_dispatch) --------
BREW_CMD_FITS("set_template");
BREW_CMD_FITS("advance");
BREW_CMD_FITS("start");
BREW_CMD_FITS("next");
BREW_CMD_FITS("stop");
BREW_CMD_FITS("reset");
BREW_CMD_FITS("tare");

// Runtime mirror of the compile-time guard. The static_asserts above already
// make a too-long literal a hard compile error, so reaching main() means the
// contract holds; the runtime pass is for harness visibility and to print which
// command sits closest to its limit.
namespace {

struct CmdCase {
    const char* cmd;
    size_t      max_len;     // owning *_CMD_MAX_LEN
    const char* type_name;   // "scale" / "brew"
};

const CmdCase kCases[] = {
    {"tare",           SCALE_CMD_MAX_LEN, "scale"},
    {"calibrate",      SCALE_CMD_MAX_LEN, "scale"},
    {"cal_weight",     SCALE_CMD_MAX_LEN, "scale"},
    {"cal_weight_set", SCALE_CMD_MAX_LEN, "scale"},
    {"set_template",   BREW_CMD_MAX_LEN,  "brew"},
    {"advance",        BREW_CMD_MAX_LEN,  "brew"},
    {"start",          BREW_CMD_MAX_LEN,  "brew"},
    {"next",           BREW_CMD_MAX_LEN,  "brew"},
    {"stop",           BREW_CMD_MAX_LEN,  "brew"},
    {"reset",          BREW_CMD_MAX_LEN,  "brew"},
    {"tare",           BREW_CMD_MAX_LEN,  "brew"},
};

} // namespace

int main() {
    printf("=== Coffee-scale command-length contract ===\n");
    printf("SCALE_CMD_MAX_LEN = %d, BREW_CMD_MAX_LEN = %d\n",
           (int)SCALE_CMD_MAX_LEN, (int)BREW_CMD_MAX_LEN);

    int failures = 0;
    for (const CmdCase& c : kCases) {
        size_t need = strlen(c.cmd) + 1;  // include NUL
        bool fits = need <= c.max_len;
        printf("  %-5s %-16s len+NUL=%2zu / %2zu  %s\n",
               c.type_name, c.cmd, need, c.max_len, fits ? "OK" : "FAIL");
        if (!fits) {
            ++failures;
            printf("    FAIL: %s_command '%s' needs %zu bytes but limit is %zu\n",
                   c.type_name, c.cmd, need, c.max_len);
        }
    }

    if (failures) {
        printf("FAIL: %d command(s) exceed their *_CMD_MAX_LEN constant\n", failures);
        return 1;
    }
    printf("OK: all command literals fit their *_CMD_MAX_LEN constants\n");
    return 0;
}
