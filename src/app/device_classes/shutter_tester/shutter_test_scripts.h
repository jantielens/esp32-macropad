#pragma once

#include "board_config.h"

#if IS_SHUTTER_TESTER

#include <stdint.h>
#include <stddef.h>

// Maximum limits for test script definitions.
#define SHUTTER_TEST_MAX_SCRIPTS   16
#define SHUTTER_TEST_MAX_SPEEDS    32
#define SHUTTER_TEST_ID_MAX_LEN    17  // 16 chars + null
#define SHUTTER_TEST_NAME_MAX_LEN  64

// File path on persistent storage for test script storage.
#define SHUTTER_TEST_FILE_PATH     "/storage/shutter_tests.txt"

// A single speed entry in a test script.
struct ShutterTestSpeed {
    char speed[16];          // As written in script, e.g. "1/125"
    char speed_suffixed[16]; // With 's' suffix, e.g. "1/125s"
};

// A parsed test script definition.
struct ShutterTestScript {
    char id[SHUTTER_TEST_ID_MAX_LEN];
    char name[SHUTTER_TEST_NAME_MAX_LEN];
    uint8_t shots_per_speed;         // Default 1
    ShutterTestSpeed speeds[SHUTTER_TEST_MAX_SPEEDS];
    uint16_t speed_count;
};

// Result of parsing the test file.
struct ShutterTestParseResult {
    ShutterTestScript scripts[SHUTTER_TEST_MAX_SCRIPTS];
    uint8_t count;
};

// Parse the test script file from persistent storage.
// Returns the number of valid test scripts found (0 if file missing/empty).
// Fills `result` with parsed data. Caller owns the result (stack-allocated).
uint8_t shutter_test_scripts_parse(ShutterTestParseResult* result);

// Find a test script by id within a parse result.
// Returns pointer to the script within `result`, or nullptr if not found.
const ShutterTestScript* shutter_test_scripts_find(const ShutterTestParseResult* result,
                                                    const char* id);

#endif // IS_SHUTTER_TESTER
