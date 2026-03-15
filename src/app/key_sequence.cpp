// ============================================================================
// Key Sequence DSL Parser — Pure C, no Arduino/ESP32 dependencies
// ============================================================================

#include "key_sequence.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ============================================================================
// Key name → HID usage lookup table
// ============================================================================

typedef struct {
    const char*  name;
    uint16_t     usage;
    KsUsageType  usage_type;
    uint8_t      modifier;     // non-zero if this is a modifier key
} KsKeyEntry;

// USB HID Keyboard usage codes (from HID Usage Tables 1.12)
#define HID_KEY_A          0x04
#define HID_KEY_Z          0x1D
#define HID_KEY_1          0x1E
#define HID_KEY_0          0x27
#define HID_KEY_ENTER      0x28
#define HID_KEY_ESC        0x29
#define HID_KEY_BACKSPACE  0x2A
#define HID_KEY_TAB        0x2B
#define HID_KEY_SPACE      0x2C
#define HID_KEY_MINUS      0x2D
#define HID_KEY_EQUAL      0x2E
#define HID_KEY_LBRACKET   0x2F
#define HID_KEY_RBRACKET   0x30
#define HID_KEY_BACKSLASH  0x31
#define HID_KEY_SEMICOLON  0x33
#define HID_KEY_QUOTE      0x34
#define HID_KEY_GRAVE      0x35
#define HID_KEY_COMMA      0x36
#define HID_KEY_PERIOD     0x37
#define HID_KEY_SLASH      0x38
#define HID_KEY_CAPSLOCK   0x39
#define HID_KEY_F1         0x3A
#define HID_KEY_F12        0x45
#define HID_KEY_F13        0x68
#define HID_KEY_F24        0x73
#define HID_KEY_PRINTSCREEN 0x46
#define HID_KEY_SCROLLLOCK 0x47
#define HID_KEY_PAUSE      0x48
#define HID_KEY_INSERT     0x49
#define HID_KEY_HOME       0x4A
#define HID_KEY_PAGEUP     0x4B
#define HID_KEY_DELETE     0x4C
#define HID_KEY_END        0x4D
#define HID_KEY_PAGEDOWN   0x4E
#define HID_KEY_RIGHT      0x4F
#define HID_KEY_LEFT       0x50
#define HID_KEY_DOWN       0x51
#define HID_KEY_UP         0x52
#define HID_KEY_NUMLOCK    0x53

// Consumer Control usage codes (from HID Usage Tables, Consumer Page 0x0C)
#define HID_CONSUMER_PLAY_PAUSE  0x00CD
#define HID_CONSUMER_STOP        0x00B7
#define HID_CONSUMER_NEXT_TRACK  0x00B5
#define HID_CONSUMER_PREV_TRACK  0x00B6
#define HID_CONSUMER_VOL_UP      0x00E9
#define HID_CONSUMER_VOL_DOWN    0x00EA
#define HID_CONSUMER_MUTE        0x00E2

// Key lookup table — searched linearly (small enough, only at parse time)
static const KsKeyEntry KEY_TABLE[] = {
    // Letters (single char matching handled separately in code)
    // Numbers (single char matching handled separately in code)

    // F-keys
    {"f1",  HID_KEY_F1,      KS_USAGE_KEYBOARD, 0},
    {"f2",  HID_KEY_F1 + 1,  KS_USAGE_KEYBOARD, 0},
    {"f3",  HID_KEY_F1 + 2,  KS_USAGE_KEYBOARD, 0},
    {"f4",  HID_KEY_F1 + 3,  KS_USAGE_KEYBOARD, 0},
    {"f5",  HID_KEY_F1 + 4,  KS_USAGE_KEYBOARD, 0},
    {"f6",  HID_KEY_F1 + 5,  KS_USAGE_KEYBOARD, 0},
    {"f7",  HID_KEY_F1 + 6,  KS_USAGE_KEYBOARD, 0},
    {"f8",  HID_KEY_F1 + 7,  KS_USAGE_KEYBOARD, 0},
    {"f9",  HID_KEY_F1 + 8,  KS_USAGE_KEYBOARD, 0},
    {"f10", HID_KEY_F1 + 9,  KS_USAGE_KEYBOARD, 0},
    {"f11", HID_KEY_F1 + 10, KS_USAGE_KEYBOARD, 0},
    {"f12", HID_KEY_F12,     KS_USAGE_KEYBOARD, 0},
    {"f13", HID_KEY_F13,     KS_USAGE_KEYBOARD, 0},
    {"f14", HID_KEY_F13 + 1, KS_USAGE_KEYBOARD, 0},
    {"f15", HID_KEY_F13 + 2, KS_USAGE_KEYBOARD, 0},
    {"f16", HID_KEY_F13 + 3, KS_USAGE_KEYBOARD, 0},
    {"f17", HID_KEY_F13 + 4, KS_USAGE_KEYBOARD, 0},
    {"f18", HID_KEY_F13 + 5, KS_USAGE_KEYBOARD, 0},
    {"f19", HID_KEY_F13 + 6, KS_USAGE_KEYBOARD, 0},
    {"f20", HID_KEY_F13 + 7, KS_USAGE_KEYBOARD, 0},
    {"f21", HID_KEY_F13 + 8, KS_USAGE_KEYBOARD, 0},
    {"f22", HID_KEY_F13 + 9, KS_USAGE_KEYBOARD, 0},
    {"f23", HID_KEY_F13 + 10, KS_USAGE_KEYBOARD, 0},
    {"f24", HID_KEY_F24,     KS_USAGE_KEYBOARD, 0},

    // Editing
    {"enter",     HID_KEY_ENTER,     KS_USAGE_KEYBOARD, 0},
    {"return",    HID_KEY_ENTER,     KS_USAGE_KEYBOARD, 0},
    {"esc",       HID_KEY_ESC,       KS_USAGE_KEYBOARD, 0},
    {"escape",    HID_KEY_ESC,       KS_USAGE_KEYBOARD, 0},
    {"tab",       HID_KEY_TAB,       KS_USAGE_KEYBOARD, 0},
    {"space",     HID_KEY_SPACE,     KS_USAGE_KEYBOARD, 0},
    {"backspace", HID_KEY_BACKSPACE, KS_USAGE_KEYBOARD, 0},
    {"bksp",      HID_KEY_BACKSPACE, KS_USAGE_KEYBOARD, 0},
    {"delete",    HID_KEY_DELETE,    KS_USAGE_KEYBOARD, 0},
    {"del",       HID_KEY_DELETE,    KS_USAGE_KEYBOARD, 0},
    {"insert",    HID_KEY_INSERT,    KS_USAGE_KEYBOARD, 0},
    {"ins",       HID_KEY_INSERT,    KS_USAGE_KEYBOARD, 0},

    // Navigation
    {"up",       HID_KEY_UP,       KS_USAGE_KEYBOARD, 0},
    {"down",     HID_KEY_DOWN,     KS_USAGE_KEYBOARD, 0},
    {"left",     HID_KEY_LEFT,     KS_USAGE_KEYBOARD, 0},
    {"right",    HID_KEY_RIGHT,    KS_USAGE_KEYBOARD, 0},
    {"home",     HID_KEY_HOME,     KS_USAGE_KEYBOARD, 0},
    {"end",      HID_KEY_END,      KS_USAGE_KEYBOARD, 0},
    {"pageup",   HID_KEY_PAGEUP,   KS_USAGE_KEYBOARD, 0},
    {"pgup",     HID_KEY_PAGEUP,   KS_USAGE_KEYBOARD, 0},
    {"pagedown", HID_KEY_PAGEDOWN, KS_USAGE_KEYBOARD, 0},
    {"pgdn",     HID_KEY_PAGEDOWN, KS_USAGE_KEYBOARD, 0},

    // Locks
    {"capslock",   HID_KEY_CAPSLOCK,   KS_USAGE_KEYBOARD, 0},
    {"numlock",    HID_KEY_NUMLOCK,    KS_USAGE_KEYBOARD, 0},
    {"scrolllock", HID_KEY_SCROLLLOCK, KS_USAGE_KEYBOARD, 0},

    // System
    {"printscreen", HID_KEY_PRINTSCREEN, KS_USAGE_KEYBOARD, 0},
    {"prtsc",       HID_KEY_PRINTSCREEN, KS_USAGE_KEYBOARD, 0},
    {"pause",       HID_KEY_PAUSE,       KS_USAGE_KEYBOARD, 0},

    // Punctuation (named keys for direct HID control)
    {"minus",     HID_KEY_MINUS,     KS_USAGE_KEYBOARD, 0},
    {"equal",     HID_KEY_EQUAL,     KS_USAGE_KEYBOARD, 0},
    {"lbracket",  HID_KEY_LBRACKET,  KS_USAGE_KEYBOARD, 0},
    {"rbracket",  HID_KEY_RBRACKET,  KS_USAGE_KEYBOARD, 0},
    {"backslash", HID_KEY_BACKSLASH, KS_USAGE_KEYBOARD, 0},
    {"semicolon", HID_KEY_SEMICOLON, KS_USAGE_KEYBOARD, 0},
    {"quote",     HID_KEY_QUOTE,     KS_USAGE_KEYBOARD, 0},
    {"grave",     HID_KEY_GRAVE,     KS_USAGE_KEYBOARD, 0},
    {"comma",     HID_KEY_COMMA,     KS_USAGE_KEYBOARD, 0},
    {"period",    HID_KEY_PERIOD,    KS_USAGE_KEYBOARD, 0},
    {"slash",     HID_KEY_SLASH,     KS_USAGE_KEYBOARD, 0},

    // Modifiers (as standalone keys + aliases)
    {"ctrl",    0, KS_USAGE_KEYBOARD, KS_MOD_LCTRL},
    {"lctrl",   0, KS_USAGE_KEYBOARD, KS_MOD_LCTRL},
    {"shift",   0, KS_USAGE_KEYBOARD, KS_MOD_LSHIFT},
    {"lshift",  0, KS_USAGE_KEYBOARD, KS_MOD_LSHIFT},
    {"alt",     0, KS_USAGE_KEYBOARD, KS_MOD_LALT},
    {"lalt",    0, KS_USAGE_KEYBOARD, KS_MOD_LALT},
    {"gui",     0, KS_USAGE_KEYBOARD, KS_MOD_LGUI},
    {"lgui",    0, KS_USAGE_KEYBOARD, KS_MOD_LGUI},
    {"win",     0, KS_USAGE_KEYBOARD, KS_MOD_LGUI},
    {"cmd",     0, KS_USAGE_KEYBOARD, KS_MOD_LGUI},
    {"super",   0, KS_USAGE_KEYBOARD, KS_MOD_LGUI},
    {"rctrl",   0, KS_USAGE_KEYBOARD, KS_MOD_RCTRL},
    {"rshift",  0, KS_USAGE_KEYBOARD, KS_MOD_RSHIFT},
    {"ralt",    0, KS_USAGE_KEYBOARD, KS_MOD_RALT},
    {"altgr",   0, KS_USAGE_KEYBOARD, KS_MOD_RALT},
    {"rgui",    0, KS_USAGE_KEYBOARD, KS_MOD_RGUI},
    {"rwin",    0, KS_USAGE_KEYBOARD, KS_MOD_RGUI},

    // Media / Consumer Control
    {"play_pause", HID_CONSUMER_PLAY_PAUSE, KS_USAGE_CONSUMER, 0},
    {"stop",       HID_CONSUMER_STOP,       KS_USAGE_CONSUMER, 0},
    {"next_track", HID_CONSUMER_NEXT_TRACK, KS_USAGE_CONSUMER, 0},
    {"prev_track", HID_CONSUMER_PREV_TRACK, KS_USAGE_CONSUMER, 0},
    {"vol_up",     HID_CONSUMER_VOL_UP,     KS_USAGE_CONSUMER, 0},
    {"vol_down",   HID_CONSUMER_VOL_DOWN,   KS_USAGE_CONSUMER, 0},
    {"mute",       HID_CONSUMER_MUTE,       KS_USAGE_CONSUMER, 0},
};

static const size_t KEY_TABLE_SIZE = sizeof(KEY_TABLE) / sizeof(KEY_TABLE[0]);

// Case-insensitive string compare for known length
static bool str_eq_ci(const char* a, const char* b, size_t len) {
    for (size_t i = 0; i < len; i++) {
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i])) return false;
    }
    return true;
}

// Look up a key name (not null-terminated, given by ptr+len)
static const KsKeyEntry* lookup_key(const char* name, size_t len) {
    // Single letter a-z → direct HID
    if (len == 1 && isalpha((unsigned char)name[0])) {
        static KsKeyEntry single_letter;
        single_letter.name = "";
        single_letter.usage = HID_KEY_A + (tolower((unsigned char)name[0]) - 'a');
        single_letter.usage_type = KS_USAGE_KEYBOARD;
        single_letter.modifier = 0;
        return &single_letter;
    }
    // Single digit 0-9 → direct HID
    if (len == 1 && name[0] >= '0' && name[0] <= '9') {
        static KsKeyEntry single_digit;
        single_digit.name = "";
        single_digit.usage_type = KS_USAGE_KEYBOARD;
        single_digit.modifier = 0;
        if (name[0] == '0') {
            single_digit.usage = HID_KEY_0;
        } else {
            single_digit.usage = HID_KEY_1 + (name[0] - '1');
        }
        return &single_digit;
    }
    // Table lookup
    for (size_t i = 0; i < KEY_TABLE_SIZE; i++) {
        size_t entry_len = strlen(KEY_TABLE[i].name);
        if (entry_len == len && str_eq_ci(name, KEY_TABLE[i].name, len)) {
            return &KEY_TABLE[i];
        }
    }
    return NULL;
}

// ============================================================================
// ASCII → HID mapping (US keyboard layout)
// ============================================================================

typedef struct {
    uint8_t  usage;
    uint8_t  shift;  // 1 if shift required
} AsciiHidEntry;

// Maps ASCII 0x20 (space) through 0x7E (~) to HID usage + shift
static const AsciiHidEntry ASCII_TO_HID[] = {
    // 0x20 space
    {HID_KEY_SPACE,     0},
    // 0x21 !
    {HID_KEY_1,         1},
    // 0x22 "
    {HID_KEY_QUOTE,     1},
    // 0x23 #
    {HID_KEY_1 + 2,     1},  // 3
    // 0x24 $
    {HID_KEY_1 + 3,     1},  // 4
    // 0x25 %
    {HID_KEY_1 + 4,     1},  // 5
    // 0x26 &
    {HID_KEY_1 + 6,     1},  // 7
    // 0x27 '
    {HID_KEY_QUOTE,     0},
    // 0x28 (
    {HID_KEY_1 + 8,     1},  // 9
    // 0x29 )
    {HID_KEY_0,         1},
    // 0x2A *
    {HID_KEY_1 + 7,     1},  // 8
    // 0x2B +
    {HID_KEY_EQUAL,     1},
    // 0x2C ,
    {HID_KEY_COMMA,     0},
    // 0x2D -
    {HID_KEY_MINUS,     0},
    // 0x2E .
    {HID_KEY_PERIOD,    0},
    // 0x2F /
    {HID_KEY_SLASH,     0},
    // 0x30-0x39: 0-9
    {HID_KEY_0,         0},  // 0
    {HID_KEY_1,         0},  // 1
    {HID_KEY_1 + 1,     0},  // 2
    {HID_KEY_1 + 2,     0},  // 3
    {HID_KEY_1 + 3,     0},  // 4
    {HID_KEY_1 + 4,     0},  // 5
    {HID_KEY_1 + 5,     0},  // 6
    {HID_KEY_1 + 6,     0},  // 7
    {HID_KEY_1 + 7,     0},  // 8
    {HID_KEY_1 + 8,     0},  // 9
    // 0x3A :
    {HID_KEY_SEMICOLON, 1},
    // 0x3B ;
    {HID_KEY_SEMICOLON, 0},
    // 0x3C <
    {HID_KEY_COMMA,     1},
    // 0x3D =
    {HID_KEY_EQUAL,     0},
    // 0x3E >
    {HID_KEY_PERIOD,    1},
    // 0x3F ?
    {HID_KEY_SLASH,     1},
    // 0x40 @
    {HID_KEY_1 + 1,     1},  // 2
    // 0x41-0x5A: A-Z (uppercase = shift + letter)
    {HID_KEY_A,      1}, {HID_KEY_A+1,  1}, {HID_KEY_A+2,  1}, {HID_KEY_A+3,  1},
    {HID_KEY_A+4,    1}, {HID_KEY_A+5,  1}, {HID_KEY_A+6,  1}, {HID_KEY_A+7,  1},
    {HID_KEY_A+8,    1}, {HID_KEY_A+9,  1}, {HID_KEY_A+10, 1}, {HID_KEY_A+11, 1},
    {HID_KEY_A+12,   1}, {HID_KEY_A+13, 1}, {HID_KEY_A+14, 1}, {HID_KEY_A+15, 1},
    {HID_KEY_A+16,   1}, {HID_KEY_A+17, 1}, {HID_KEY_A+18, 1}, {HID_KEY_A+19, 1},
    {HID_KEY_A+20,   1}, {HID_KEY_A+21, 1}, {HID_KEY_A+22, 1}, {HID_KEY_A+23, 1},
    {HID_KEY_A+24,   1}, {HID_KEY_A+25, 1},
    // 0x5B [
    {HID_KEY_LBRACKET,  0},
    // 0x5C backslash
    {HID_KEY_BACKSLASH, 0},
    // 0x5D ]
    {HID_KEY_RBRACKET,  0},
    // 0x5E ^
    {HID_KEY_1 + 5,     1},  // 6
    // 0x5F _
    {HID_KEY_MINUS,     1},
    // 0x60 `
    {HID_KEY_GRAVE,     0},
    // 0x61-0x7A: a-z (lowercase, no shift)
    {HID_KEY_A,      0}, {HID_KEY_A+1,  0}, {HID_KEY_A+2,  0}, {HID_KEY_A+3,  0},
    {HID_KEY_A+4,    0}, {HID_KEY_A+5,  0}, {HID_KEY_A+6,  0}, {HID_KEY_A+7,  0},
    {HID_KEY_A+8,    0}, {HID_KEY_A+9,  0}, {HID_KEY_A+10, 0}, {HID_KEY_A+11, 0},
    {HID_KEY_A+12,   0}, {HID_KEY_A+13, 0}, {HID_KEY_A+14, 0}, {HID_KEY_A+15, 0},
    {HID_KEY_A+16,   0}, {HID_KEY_A+17, 0}, {HID_KEY_A+18, 0}, {HID_KEY_A+19, 0},
    {HID_KEY_A+20,   0}, {HID_KEY_A+21, 0}, {HID_KEY_A+22, 0}, {HID_KEY_A+23, 0},
    {HID_KEY_A+24,   0}, {HID_KEY_A+25, 0},
    // 0x7B {
    {HID_KEY_LBRACKET,  1},
    // 0x7C |
    {HID_KEY_BACKSLASH, 1},
    // 0x7D }
    {HID_KEY_RBRACKET,  1},
    // 0x7E ~
    {HID_KEY_GRAVE,     1},
};

bool ks_ascii_to_hid(char c, uint16_t* usage, uint8_t* modifiers) {
    if (c < 0x20 || c > 0x7E) return false;
    const AsciiHidEntry* e = &ASCII_TO_HID[c - 0x20];
    *usage = e->usage;
    *modifiers = e->shift ? KS_MOD_LSHIFT : 0;
    return true;
}

// ============================================================================
// Parser
// ============================================================================

static void set_error(KsSequence* seq, const char* msg) {
    strncpy(seq->error, msg, sizeof(seq->error) - 1);
    seq->error[sizeof(seq->error) - 1] = '\0';
}

// Skip whitespace, return pointer to next non-space (or end)
static const char* skip_ws(const char* p) {
    while (*p == ' ' || *p == '\t') p++;
    return p;
}

// Try to parse a delay step: digits followed by "ms"
static bool try_parse_delay(const char* token, size_t len, KsStep* step) {
    // Must end with "ms" and have at least one digit before
    if (len < 3) return false;
    if (tolower((unsigned char)token[len-2]) != 'm' ||
        tolower((unsigned char)token[len-1]) != 's') return false;

    // Everything before "ms" must be digits
    for (size_t i = 0; i < len - 2; i++) {
        if (!isdigit((unsigned char)token[i])) return false;
    }

    // Parse the number
    unsigned long val = 0;
    for (size_t i = 0; i < len - 2; i++) {
        val = val * 10 + (token[i] - '0');
        if (val > 65535) return false; // uint16_t max
    }

    step->type = KS_STEP_DELAY;
    step->delay.ms = (uint16_t)val;
    return true;
}

// Parse a key combo token like "ctrl+shift+t" or "enter"
static bool parse_key_combo(const char* token, size_t len, KsStep* step, char* error, size_t error_len) {
    step->type = KS_STEP_KEY;
    step->key.modifiers = 0;
    step->key.usage = 0;
    step->key.usage_type = KS_USAGE_KEYBOARD;

    const char* p = token;
    const char* end = token + len;

    // Split by '+' — all but the last segment are modifiers, last is the key
    // First, count '+' separators to know how many segments
    int plus_count = 0;
    for (size_t i = 0; i < len; i++) {
        if (token[i] == '+') plus_count++;
    }

    // Parse segments
    int seg_idx = 0;
    while (p < end) {
        // Find next '+' or end
        const char* seg_end = p;
        while (seg_end < end && *seg_end != '+') seg_end++;
        size_t seg_len = seg_end - p;

        if (seg_len == 0) {
            snprintf(error, error_len, "empty segment in combo");
            return false;
        }

        const KsKeyEntry* entry = lookup_key(p, seg_len);
        if (!entry) {
            snprintf(error, error_len, "unknown key: %.*s", (int)seg_len, p);
            return false;
        }

        bool is_last = (seg_idx == plus_count);

        if (entry->modifier && !is_last) {
            // Modifier segment (not the last one)
            step->key.modifiers |= entry->modifier;
        } else if (entry->modifier && is_last && plus_count > 0) {
            // Modifier as the final key in a combo (e.g., ctrl+shift)
            // Treat it as a modifier-only press
            step->key.modifiers |= entry->modifier;
        } else if (!is_last) {
            // Non-modifier in non-last position: must be a modifier
            snprintf(error, error_len, "non-modifier in combo: %.*s", (int)seg_len, p);
            return false;
        } else {
            // Last segment: the actual key
            step->key.usage = entry->usage;
            step->key.usage_type = entry->usage_type;
        }

        seg_idx++;
        p = seg_end + 1; // skip '+'
    }

    return true;
}

bool ks_parse(const char* dsl, KsSequence* seq) {
    memset(seq, 0, sizeof(KsSequence));

    if (!dsl || !dsl[0]) {
        set_error(seq, "empty sequence");
        return false;
    }

    const char* p = skip_ws(dsl);

    if (!*p) {
        set_error(seq, "empty sequence");
        return false;
    }

    while (*p) {
        if (seq->count >= KS_MAX_STEPS) {
            set_error(seq, "too many steps");
            return false;
        }

        KsStep* step = &seq->steps[seq->count];

        if (*p == '"') {
            // Text literal: scan to closing quote (handle \" escape)
            p++; // skip opening quote
            const char* text_start = p;
            while (*p && !(*p == '"' && *(p-1) != '\\')) {
                p++;
            }
            if (!*p) {
                set_error(seq, "unterminated string");
                return false;
            }
            step->type = KS_STEP_TEXT;
            step->text.start = text_start;
            step->text.length = (uint16_t)(p - text_start);
            p++; // skip closing quote
            seq->count++;
        } else {
            // Token: scan to next space or end
            const char* token_start = p;
            while (*p && *p != ' ' && *p != '\t') p++;
            size_t token_len = p - token_start;

            // Try delay first (e.g., "200ms")
            if (try_parse_delay(token_start, token_len, step)) {
                seq->count++;
            } else {
                // Must be a key combo
                char err[64] = {0};
                if (!parse_key_combo(token_start, token_len, step, err, sizeof(err))) {
                    set_error(seq, err);
                    return false;
                }
                seq->count++;
            }
        }

        p = skip_ws(p);
    }

    return true;
}
