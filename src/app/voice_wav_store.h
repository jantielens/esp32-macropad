#pragma once

#include "fs_indexed_store.h"

FsIndexedStoreReadResult voice_wav_store_read_metadata(const char* path, JsonObject metadata);
