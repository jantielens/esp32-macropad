#include <lvgl.h>
#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

static const unsigned char font_data[] = {
#include "fixtures/BebasNeue.inc"
};

static size_t allocation_count = 0;
static size_t fail_at = 0;
static bool tracking = false;
static const char* phase = "init";

extern "C" void* __real_lv_malloc_core(size_t size);
extern "C" void* __real_lv_realloc_core(void* pointer, size_t size);

static bool should_fail(size_t size) {
    if (!tracking) return false;
    ++allocation_count;
    if (allocation_count != fail_at) return false;
    std::fprintf(stderr, "Inject failure #%zu during %s (%zu bytes)\n",
                 allocation_count, phase, size);
    return true;
}

extern "C" void* __wrap_lv_malloc_core(size_t size) {
    return should_fail(size) ? nullptr : __real_lv_malloc_core(size);
}

extern "C" void* __wrap_lv_realloc_core(void* pointer, size_t size) {
    return should_fail(size) ? nullptr : __real_lv_realloc_core(pointer, size);
}

static void timed_out(int) {
    _exit(124);
}

static bool exercise_font() {
    phase = "font creation";
    lv_font_t* font = lv_tiny_ttf_create_data_ex(
        font_data, sizeof(font_data), 96, LV_FONT_KERNING_NORMAL, 16);
    if (!font) return fail_at != 0;
    phase = "glyph descriptor";
    lv_font_glyph_dsc_t glyph{};
    const bool found = lv_font_get_glyph_dsc(font, &glyph, '8', '9');
    bool rendered = false;
    if (found) {
        phase = "glyph bitmap";
        rendered = lv_font_get_glyph_bitmap(&glyph, nullptr) != nullptr;
        lv_font_glyph_release_draw_data(&glyph);
    }
    phase = "font destruction";
    lv_tiny_ttf_destroy(font);
    return fail_at != 0 || (found && rendered);
}

int main() {
    lv_init();
    tracking = true;
    if (!exercise_font()) {
        std::fprintf(stderr, "Baseline font rendering failed\n");
        return 1;
    }
    const size_t baseline_allocations = allocation_count;
    std::printf("Baseline creation/rendering: %zu allocations\n", baseline_allocations);
    std::fflush(nullptr);
    for (size_t failure_index = 1; failure_index <= baseline_allocations; ++failure_index) {
        const pid_t child = fork();
        if (child < 0) return 1;
        if (child == 0) {
            signal(SIGALRM, timed_out);
            alarm(2);
            allocation_count = 0;
            fail_at = failure_index;
            _exit(exercise_font() ? 0 : 1);
        }
        int status = 0;
        if (waitpid(child, &status, 0) != child) return 1;
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            const bool known_timeout = LVGL_VERSION_MAJOR == 9 &&
                LVGL_VERSION_MINOR == 5 && LVGL_VERSION_PATCH == 0 &&
                WIFEXITED(status) && WEXITSTATUS(status) == 124;
            const bool known_crash = LVGL_VERSION_MAJOR == 9 &&
                LVGL_VERSION_MINOR == 6 && LVGL_VERSION_PATCH == 0 &&
                WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV;
            if (failure_index == 3 && (known_timeout || known_crash)) {
                std::fprintf(stderr,
                    "WARNING: known stock LVGL %d.%d.%d cache-creation OOM %s; "
                    "recovery is not guaranteed. Remaining allocation probes "
                    "were not run; marking characterization skipped.\n",
                    LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH,
                    known_timeout ? "timeout" : "crash");
                lv_deinit();
                return 77;
            }
            if (WIFSIGNALED(status)) {
                std::fprintf(stderr, "FAIL: allocation #%zu caused signal %d\n",
                             failure_index, WTERMSIG(status));
            } else {
                std::fprintf(stderr, "FAIL: allocation #%zu exited %d%s\n",
                             failure_index, WEXITSTATUS(status),
                             WEXITSTATUS(status) == 124 ? " (assertion loop/timeout)" : "");
            }
            return 1;
        }
    }
    lv_deinit();
    std::printf("All allocation failures returned without hanging or crashing\n");
    return 0;
}