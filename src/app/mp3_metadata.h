#pragma once

#include <stddef.h>
#include <stdint.h>

#define MP3_METADATA_TITLE_LEN 64
#define MP3_METADATA_ARTIST_LEN 48
#define MP3_METADATA_ALBUM_LEN 48
#define MP3_METADATA_TRACK_LEN 12

enum Mp3DurationSource : uint8_t {
    MP3_DURATION_UNKNOWN,
    MP3_DURATION_XING,
    MP3_DURATION_VBRI,
    MP3_DURATION_CBR_ESTIMATE,
};

struct Mp3Metadata {
    char title[MP3_METADATA_TITLE_LEN];
    char artist[MP3_METADATA_ARTIST_LEN];
    char album[MP3_METADATA_ALBUM_LEN];
    char track[MP3_METADATA_TRACK_LEN];
    uint32_t duration_s;
    Mp3DurationSource duration_source;
};

// Parses a bounded prefix of an MP3. It skips ID3v2, extracts common ID3 text
// frames, and estimates duration from Xing/Info, VBRI, or CBR frame metadata.
// It never decodes audio frames or allocates memory.
bool mp3_metadata_parse(const uint8_t* data, size_t length, size_t file_size,
                        Mp3Metadata* out);