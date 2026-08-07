#include <cassert>
#include <cstring>
#include <cstdio>

#include "audio.h"
#include "binding_template.h"
#include "music_binding.h"

static AudioMusicInfo g_music_info = {};

void audio_get_music_info(AudioMusicInfo* out) {
    assert(out);
    *out = g_music_info;
}

static void check_resolves(const char* token, const char* expected) {
    char output[64] = {};
    binding_template_resolve(token, output, sizeof(output));
    assert(std::strcmp(output, expected) == 0);
}

int main() {
    music_binding_init();

    std::strcpy(g_music_info.file, "/media/albums/live/02-track.mp3");
    check_resolves("[music:file]", "/media/albums/live/02-track.mp3");
    check_resolves("[music:file_name]", "02-track.mp3");

    g_music_info = {};
    check_resolves("[music:file_name]", "---");
    assert(binding_template_validate_params("music", 5, "file_name") == nullptr);
    assert(binding_template_validate_params("music", 5, "filename") != nullptr);

    std::puts("music_binding: PASS");
    return 0;
}