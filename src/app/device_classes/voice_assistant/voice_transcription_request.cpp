#include "voice_transcription_request.h"

#include <stdio.h>

int voice_transcription_build_multipart_prefix(char* out, size_t out_size,
                                               const char* boundary, const char* language) {
    if (!out || !out_size || !boundary || !boundary[0]) return -1;
    if (language && language[0]) {
        return snprintf(out, out_size,
            "--%s\r\nContent-Disposition: form-data; name=\"language\"\r\n\r\n%s\r\n"
            "--%s\r\nContent-Disposition: form-data; name=\"file\"; filename=\"recording.wav\"\r\n"
            "Content-Type: audio/wav\r\n\r\n",
            boundary, language, boundary);
    }
    return snprintf(out, out_size,
        "--%s\r\nContent-Disposition: form-data; name=\"file\"; filename=\"recording.wav\"\r\n"
        "Content-Type: audio/wav\r\n\r\n", boundary);
}