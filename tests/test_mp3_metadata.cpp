#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "mp3_metadata.h"

static void check(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        std::exit(1);
    }
}

static void put_be32(uint8_t* p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static void put_frame(uint8_t* p) {
    p[0] = 0xFF; p[1] = 0xFB; p[2] = 0x90; p[3] = 0x00;
}

int main() {
    {
        uint8_t data[128] = {};
        put_frame(data);
        memcpy(data + 36, "Xing", 4);
        put_be32(data + 40, 1);
        put_be32(data + 44, 1000);
        Mp3Metadata metadata = {};
        check(mp3_metadata_parse(data, sizeof(data), 123456, &metadata), "Xing frame must parse");
        check(metadata.duration_s == 26 && metadata.duration_source == MP3_DURATION_XING,
              "Xing duration must use frame count");
    }
    {
        uint8_t data[96] = {};
        put_frame(data);
        memcpy(data + 36, "VBRI", 4);
        put_be32(data + 50, 2000);
        Mp3Metadata metadata = {};
        check(mp3_metadata_parse(data, sizeof(data), 123456, &metadata), "VBRI frame must parse");
        check(metadata.duration_s == 52 && metadata.duration_source == MP3_DURATION_VBRI,
              "VBRI duration must use frame count");
    }
    {
        uint8_t data[64] = {};
        put_frame(data);
        Mp3Metadata metadata = {};
        check(mp3_metadata_parse(data, sizeof(data), 128000, &metadata), "CBR frame must parse");
        check(metadata.duration_s == 8 && metadata.duration_source == MP3_DURATION_CBR_ESTIMATE,
              "CBR duration must use file size and bitrate");
    }
    {
        uint8_t data[128] = {};
        memcpy(data, "ID3", 3);
        data[3] = 3;
        data[9] = 16;
        memcpy(data + 10, "TIT2", 4);
        put_be32(data + 14, 6);
        data[20] = 3;
        memcpy(data + 21, "Hello", 5);
        put_frame(data + 26);
        Mp3Metadata metadata = {};
        check(mp3_metadata_parse(data, sizeof(data), 128000, &metadata), "ID3 and frame must parse");
        check(std::strcmp(metadata.title, "Hello") == 0, "ID3 title must parse");
    }
    std::puts("mp3 metadata checks passed");
    return 0;
}