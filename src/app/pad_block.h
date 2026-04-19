#pragma once

#include "board_config.h"

#if HAS_DISPLAY

#include <stdint.h>

#define PAD_BLOCK_MAX_BUTTONS    24
#define PAD_BLOCK_MAX_BINDINGS    8
#define PAD_BLOCK_NAME_MAX_LEN   32
#define PAD_BLOCK_DESC_MAX_LEN   96
#define PAD_BLOCK_ICON_MAX_LEN    8  // UTF-8 emoji

// A single button within a building block (relative positioning)
struct PadBlockButton {
    uint8_t col_offset;    // relative to anchor (0-based)
    uint8_t row_offset;    // relative to anchor (0-based)
    uint8_t col_span;      // 1+ (default 1)
    uint8_t row_span;      // 1+ (default 1)
    const char* json;      // JSON object string for this button's fields
};

// A named binding to inject at pad level
struct PadBlockBinding {
    const char* name;
    const char* value;
};

// A building block definition
struct PadBlock {
    const char* id;
    const char* name;
    const char* desc;
    const char* icon;
    uint8_t min_cols;
    uint8_t min_rows;
    uint8_t min_free_cells;
    uint8_t button_count;
    const PadBlockButton* buttons;
    uint8_t binding_count;
    const PadBlockBinding* bindings;
};

// ---------------------------------------------------------------------------
// Registration API
//
// Blocks are added to a shared catalog via pad_block_register().
// Core blocks are registered in pad_block_init().
//
// Feature branches add their own blocks by:
//   1. Creating a separate file (e.g., pad_block_coffee.cpp)
//   2. Defining their block data as static const
//   3. Providing an init function (e.g., pad_block_coffee_init()) that
//      calls pad_block_register(&my_block) for each block
//   4. Calling that init function from their feature's startup code,
//      gated by the feature's HAS_* flag
//
// This avoids merge conflicts — each branch only touches its own file.
// The pattern mirrors binding_template_register() for scheme registration.
// ---------------------------------------------------------------------------

// Maximum number of blocks that can be registered across all modules.
#define PAD_BLOCK_MAX_CATALOG 16

// Register a building block. Called at startup before the web portal serves
// requests. Returns false if the catalog is full.
bool pad_block_register(const PadBlock* block);

// Register all core (main-branch) building blocks. Called once at startup.
void pad_block_init();

// Return the registered catalog and its size.
const PadBlock* const* pad_block_catalog();
uint8_t pad_block_catalog_count();

#endif // HAS_DISPLAY
