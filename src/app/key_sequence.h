#pragma once

// ============================================================================
// Key Sequence DSL Parser — Pure C, no Arduino/ESP32 dependencies
// ============================================================================
// Parses a DSL string into a sequence of steps (key combos, text, delays).
// Designed to be unit-testable on any host.
//
// DSL syntax (space-separated steps):
//   Key/combo:    enter, ctrl+c, ctrl+shift+t, gui+l
//   Text literal: "Hello World" — types each ASCII char (US layout)
//   Delay:        200ms — pause N milliseconds
//
// Key names are case-insensitive.  Modifier aliases: win/cmd/super → gui
//
// Example: gui+r 200ms "notepad" enter

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// HID Constants
// ============================================================================

// Modifier bitmask (matches USB HID boot keyboard modifier byte)
#define KS_MOD_LCTRL   0x01
#define KS_MOD_LSHIFT  0x02
#define KS_MOD_LALT    0x04
#define KS_MOD_LGUI    0x08
#define KS_MOD_RCTRL   0x10
#define KS_MOD_RSHIFT  0x20
#define KS_MOD_RALT    0x40
#define KS_MOD_RGUI    0x80

// Step types in a parsed sequence
typedef enum {
    KS_STEP_KEY,    // key press: modifier mask + HID usage code
    KS_STEP_TEXT,   // type literal text: pointer + length into source string
    KS_STEP_DELAY,  // pause: milliseconds
} KsStepType;

// Whether a key is a keyboard or consumer (media) usage
typedef enum {
    KS_USAGE_KEYBOARD,
    KS_USAGE_CONSUMER,
} KsUsageType;

// A single parsed step
typedef struct {
    KsStepType type;
    union {
        struct {
            uint16_t    usage;      // HID usage code
            uint8_t     modifiers;  // KS_MOD_* bitmask
            KsUsageType usage_type; // keyboard or consumer
        } key;
        struct {
            const char* start;      // pointer into the source DSL string
            uint16_t    length;
        } text;
        struct {
            uint16_t    ms;
        } delay;
    };
} KsStep;

// Maximum steps in a single sequence
#define KS_MAX_STEPS 64

// Parsed sequence
typedef struct {
    KsStep   steps[KS_MAX_STEPS];
    uint8_t  count;
    char     error[64];   // empty on success, description on error
} KsSequence;

// ============================================================================
// API
// ============================================================================

// Parse a DSL string into a KsSequence.
// Returns true on success. On error, seq->error contains description.
bool ks_parse(const char* dsl, KsSequence* seq);

// Map an ASCII character (0x20..0x7E) to a HID keyboard usage code + modifier.
// Returns true if the char is mappable. Sets *usage and *modifiers.
bool ks_ascii_to_hid(char c, uint16_t* usage, uint8_t* modifiers);

// Default inter-step delay in milliseconds
#define KS_DEFAULT_DELAY_MS 20

#ifdef __cplusplus
}
#endif
