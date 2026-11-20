#include "board_config.h"

#if HAS_VOICE_NOTES

#include "voice_notes_metadata.h"
#include "storage.h"
#include "voice_wav.h"

FsIndexedStoreReadResult voice_notes_metadata_read_metadata(const char* path, JsonObject metadata) {
    File file = Storage.open(path, "r");
    if (!file) return FsIndexedStoreReadResult::IoError;

    const uint64_t file_size = file.size();
    if (file_size < VOICE_WAV_HEADER_BYTES) {
        file.close();
        return FsIndexedStoreReadResult::Invalid;
    }

    uint8_t header[VOICE_WAV_HEADER_BYTES];
    const size_t read = file.read(header, sizeof(header));
    file.close();
    if (read != sizeof(header)) return FsIndexedStoreReadResult::IoError;

    VoiceWavInfo info = {};
    if (!voice_wav_read_header(header, sizeof(header), file_size, &info)) {
        return FsIndexedStoreReadResult::Invalid;
    }

    metadata["size_bytes"] = file_size;
    metadata["sample_rate"] = info.sample_rate;
    metadata["channels"] = info.channels;
    metadata["duration_ms"] = ((uint64_t)info.data_bytes / info.block_align) * 1000 / info.sample_rate;
    return FsIndexedStoreReadResult::Valid;
}

#endif // HAS_VOICE_NOTES
