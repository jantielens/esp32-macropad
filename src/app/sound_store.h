#pragma once

#include "board_config.h"

#if HAS_SOUND_PLAYER

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

// Maximum sound filename length (without path or extension)
#define SOUND_NAME_MAX_LEN 32

// Maximum number of sound files returned by sound_store_list()
#define SOUND_LIST_MAX 32

// Maximum file size accepted for upload (512 KB)
#define SOUND_MAX_FILE_SIZE (512 * 1024)

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the sound store. Creates /sounds/ directory if needed.
void sound_store_init();

// Check if a sound file exists by name (without extension).
bool sound_store_exists(const char* name);

// Save a sound file. Data is the raw MP3 bytes.
// name must be a valid sound name (alphanumeric, underscore, hyphen).
// Returns true on success.
bool sound_store_save(const char* name, const uint8_t* data, size_t len);

// Delete a sound file by name.
// Returns true if the file was deleted.
bool sound_store_delete(const char* name);

// List all sound file names (without extension).
// Fills names[] with up to max_count entries.
// Returns the number of entries written.
int sound_store_list(char names[][SOUND_NAME_MAX_LEN], int max_count);

// Build the full filesystem path for a sound file.
// out must be at least 48 bytes. Returns out for convenience.
char* sound_store_path(const char* name, char* out, size_t out_len);

// Validate a sound name: alphanumeric, underscore, hyphen, 1..SOUND_NAME_MAX_LEN-1
bool sound_store_validate_name(const char* name);

#ifdef __cplusplus
}
#endif

#endif // HAS_SOUND_PLAYER
