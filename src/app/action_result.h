#pragma once

#include <stdint.h>

// Result of starting an action within an ordered action array. Existing actions
// normally return ACTION_COMPLETE; ACTION_PENDING pauses the current array until
// the initiating action later completes its continuation token.
enum ActionResult : uint8_t {
    ACTION_COMPLETE = 0,
    ACTION_FAILED,
    ACTION_PENDING,
};