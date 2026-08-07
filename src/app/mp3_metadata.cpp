#include "mp3_metadata.h"

#include <string.h>

namespace {

constexpr size_t kMaxId3Bytes = 64 * 1024;

uint32_t be32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | p[3];
}

uint32_t syncsafe32(const uint8_t* p) {
    return ((uint32_t)(p[0] & 0x7F) << 21) | ((uint32_t)(p[1] & 0x7F) << 14) |
           ((uint32_t)(p[2] & 0x7F) << 7) | (p[3] & 0x7F);
}

uint32_t duration_seconds_from_frames(uint32_t frames, uint16_t samples_per_frame,
                                      uint32_t sample_rate) {
    if (sample_rate == 0) return 0;
    return (uint32_t)(((uint64_t)frames * samples_per_frame + sample_rate / 2) /
                      sample_rate);
}

bool frame_header(const uint8_t* p, size_t length, uint32_t* sample_rate,
                  uint16_t* bitrate_kbps, uint16_t* samples_per_frame,
                  uint8_t* version, uint8_t* channels) {
    if (!p || length < 4 || p[0] != 0xFF || (p[1] & 0xE0) != 0xE0) return false;
    const uint8_t version_bits = (p[1] >> 3) & 0x03;
    const uint8_t layer_bits = (p[1] >> 1) & 0x03;
    const uint8_t bitrate_index = (p[2] >> 4) & 0x0F;
    const uint8_t rate_index = (p[2] >> 2) & 0x03;
    if (version_bits == 1 || layer_bits != 1 || bitrate_index == 0 || bitrate_index == 15 || rate_index == 3) {
        return false;
    }

    static const uint16_t kMpeg1Layer3Bitrates[] =
        {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320};
    static const uint16_t kMpeg2Layer3Bitrates[] =
        {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160};
    static const uint32_t kMpeg1Rates[] = {44100, 48000, 32000};

    uint32_t rate = kMpeg1Rates[rate_index];
    if (version_bits == 2) rate /= 2;
    else if (version_bits == 0) rate /= 4;
    const bool mpeg1 = version_bits == 3;
    *sample_rate = rate;
    *bitrate_kbps = mpeg1 ? kMpeg1Layer3Bitrates[bitrate_index] : kMpeg2Layer3Bitrates[bitrate_index];
    *samples_per_frame = mpeg1 ? 1152 : 576;
    *version = version_bits;
    *channels = ((p[3] >> 6) == 3) ? 1 : 2;
    return true;
}

void copy_text(const uint8_t* payload, size_t length, char* out, size_t out_len) {
    if (!out || out_len == 0) return;
    out[0] = '\0';
    if (!payload || length < 2) return;
    const uint8_t encoding = payload[0];
    size_t source = 1;
    if (encoding == 1 || encoding == 2) {
        // Bounded ASCII fallback for UTF-16: preserve printable low bytes.
        if (length >= 3 && ((payload[1] == 0xFF && payload[2] == 0xFE) ||
                            (payload[1] == 0xFE && payload[2] == 0xFF))) source = 3;
        size_t written = 0;
        for (; source + 1 < length && written + 1 < out_len; source += 2) {
            const uint8_t c = payload[source + 1] ? payload[source + 1] : payload[source];
            if (c == 0) break;
            if (c >= 0x20 && c < 0x7F) out[written++] = (char)c;
        }
        out[written] = '\0';
        return;
    }
    size_t written = 0;
    for (; source < length && written + 1 < out_len; ++source) {
        const uint8_t c = payload[source];
        if (c == 0) break;
        if (c >= 0x20) out[written++] = (char)c;
    }
    out[written] = '\0';
}

void parse_id3v2(const uint8_t* data, size_t length, size_t tag_end, Mp3Metadata* out) {
    if (length < 10 || tag_end > length) return;
    const uint8_t version = data[3];
    size_t offset = 10;
    while (offset + 10 <= tag_end) {
        const uint8_t* header = data + offset;
        if (header[0] == 0) break;
        const uint32_t size = version == 4 ? syncsafe32(header + 4) : be32(header + 4);
        offset += 10;
        if (size == 0 || size > tag_end - offset) break;
        const uint8_t* payload = data + offset;
        if (memcmp(header, "TIT2", 4) == 0) copy_text(payload, size, out->title, sizeof(out->title));
        else if (memcmp(header, "TPE1", 4) == 0) copy_text(payload, size, out->artist, sizeof(out->artist));
        else if (memcmp(header, "TALB", 4) == 0) copy_text(payload, size, out->album, sizeof(out->album));
        else if (memcmp(header, "TRCK", 4) == 0) copy_text(payload, size, out->track, sizeof(out->track));
        offset += size;
    }
}

} // namespace

bool mp3_metadata_parse(const uint8_t* data, size_t length, size_t file_size,
                        Mp3Metadata* out) {
    if (!data || !out || length < 4) return false;
    *out = {};
    size_t audio_offset = 0;
    if (length >= 10 && memcmp(data, "ID3", 3) == 0) {
        const size_t tag_size = syncsafe32(data + 6);
        audio_offset = 10 + tag_size;
        if (data[5] & 0x10) audio_offset += 10;
        if (audio_offset > kMaxId3Bytes || audio_offset > length) return false;
        parse_id3v2(data, length, audio_offset, out);
    }

    for (size_t offset = audio_offset; offset + 4 <= length; ++offset) {
        uint32_t sample_rate = 0;
        uint16_t bitrate_kbps = 0;
        uint16_t samples_per_frame = 0;
        uint8_t version = 0;
        uint8_t channels = 0;
        if (!frame_header(data + offset, length - offset, &sample_rate, &bitrate_kbps,
                          &samples_per_frame, &version, &channels)) continue;
        const bool mpeg1 = version == 3;
        const size_t side_info = mpeg1 ? (channels == 1 ? 17 : 32) : (channels == 1 ? 9 : 17);
        // protection bit 1 means no CRC; bit 0 inserts a two-byte CRC.
        const size_t xing_offset = offset + 4 + (data[offset + 1] & 1 ? 0 : 2) + side_info;
        if (xing_offset + 12 <= length &&
            (memcmp(data + xing_offset, "Xing", 4) == 0 || memcmp(data + xing_offset, "Info", 4) == 0)) {
            const uint32_t flags = be32(data + xing_offset + 4);
            if ((flags & 1U) && xing_offset + 12 <= length) {
                const uint32_t frames = be32(data + xing_offset + 8);
                out->duration_s = duration_seconds_from_frames(
                    frames, samples_per_frame, sample_rate);
                out->duration_source = MP3_DURATION_XING;
                return true;
            }
        }
        const size_t vbri_offset = offset + 4 + 32;
        if (vbri_offset + 18 <= length && memcmp(data + vbri_offset, "VBRI", 4) == 0) {
            const uint32_t frames = be32(data + vbri_offset + 14);
            out->duration_s = duration_seconds_from_frames(
                frames, samples_per_frame, sample_rate);
            out->duration_source = MP3_DURATION_VBRI;
            return true;
        }
        if (bitrate_kbps) {
            const size_t audio_bytes = file_size > offset ? file_size - offset : 0;
            out->duration_s = (uint32_t)((audio_bytes * 8ULL + bitrate_kbps * 500ULL) /
                                         (bitrate_kbps * 1000ULL));
            out->duration_source = MP3_DURATION_CBR_ESTIMATE;
        }
        return true;
    }
    return false;
}