#include <cassert>
#include <cstdio>

#include "button_shadow_color.h"

int main() {
    assert(button_shadow_darken_color(0x336699, 0) == 0x336699);
    assert(button_shadow_darken_color(0x336699, 50) == 0x19334C);
    assert(button_shadow_darken_color(0x336699, 100) == 0x000000);
    std::puts("button_shadow_color: PASS");
    return 0;
}