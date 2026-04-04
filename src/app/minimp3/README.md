# minimp3

Vendored single-header MP3 decoder from [lieff/minimp3](https://github.com/lieff/minimp3).

**License**: CC0 (public domain)

**Usage**: Include via `sound_player.cpp` which defines `MINIMP3_NO_SIMD` and
`MINIMP3_IMPLEMENTATION` before including `minimp3.h`. Only one translation unit
may define `MINIMP3_IMPLEMENTATION`.
