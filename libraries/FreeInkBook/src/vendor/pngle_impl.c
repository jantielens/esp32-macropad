/* The miniz symbol renames (MinizConfig.h) must be active before pngle.c
 * makes its tinfl_* calls, or the linker binds them to the ESP32 ROM's
 * incompatible copy. */
#include "epub/MinizConfig.h"
#ifdef ARDUINO_ARCH_ESP32
#include <stdlib.h>
#include <esp_heap_caps.h>
#define calloc(count, size) heap_caps_calloc((count), (size), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)
#define free(ptr) heap_caps_free(ptr)
#endif
/* Compiles the vendored pngle against FreeInkBook's miniz (pngle.c's include
 * is patched to "../miniz/miniz.h" — ESP-IDF ships a ROM miniz.h whose
 * tinfl_decompressor layout differs, and pngle embeds that struct by value).
 * pngle allocates transient scanline buffers with calloc/free at decode time
 * only; that use is bounded by image width and never touches the page-turn
 * path. */
#include "../../third_party/pngle/pngle.c"
