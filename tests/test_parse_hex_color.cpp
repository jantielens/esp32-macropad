#include "pad_config.h"

#include <assert.h>
#include <stdio.h>

static uint32_t parsed(const char* s) {
    uint32_t v = 0xDEADBEEF;
    assert(parse_hex_color(s, &v));
    return v;
}

static void rejects(const char* s) {
    uint32_t v = 0xDEADBEEF;
    assert(!parse_hex_color(s, &v));
    assert(v == 0xDEADBEEF);
}

int main() {
    assert(parsed("#4CAF50") == 0x4CAF50);
    assert(parsed("4caf50") == 0x4CAF50);
    assert(parsed("0xFF0000") == 0xFF0000);
    assert(parsed("#FFF") == 0xFFFFFF);
    assert(parsed("#1a2") == 0x11AA22);
    assert(parsed("#00FF00\r\n") == 0x00FF00);

    rejects(nullptr);
    rejects("");
    rejects("#");
    rejects("---");
    rejects("ERR:bad");
    rejects("red");
    rejects("#12");
    rejects("#1234");
    rejects("#1234567");
    rejects("#FF000080");
    rejects("#GGGGGG");
    rejects("#FF0000x");
    rejects("#[mqtt:a]");

    puts("parse_hex_color: PASS");
    return 0;
}
