// ============================================================================
// Unit tests for label_style_parse() — DSL tokenizer + binding-color capture
// ============================================================================
// Host-native test (no ESP32 needed). Exercises:
//   - Standard style keys (font_size / align / mode) still parse
//   - Static color:#RRGGBB sets the marker bit; capture buffer stays empty
//   - Binding-valued color: is captured into color_bind_out (marker bit clear)
//   - The ';' split is bracket- AND quote-aware (nested [] + quoted ':'/';')
//   - Empty/static input clears the capture buffer

#include <cstdio>
#include <cstring>

#include "pad_config.h"

static int g_failures = 0;

#define CHECK(cond) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", #cond, __LINE__); \
        g_failures++; \
    } \
} while (0)

int main() {
    printf("=== label_style_parse tests ===\n");

    // 1. Standard keys still parse; no color → marker clear, capture empty.
    {
        LabelStyle s;
        char bind[CONFIG_LABEL_STYLE_MAX_LEN];
        label_style_parse("font_size:14;align:right;mode:dot", &s, bind, sizeof(bind));
        CHECK(s.font_size == 14);
        CHECK(s.align == LABEL_ALIGN_RIGHT);
        CHECK(s.long_mode == LABEL_MODE_DOT);
        CHECK((s.color & 0x01000000) == 0);
        CHECK(bind[0] == '\0');
    }

    // 2. Static hex color sets the marker bit; capture stays empty.
    {
        LabelStyle s;
        char bind[CONFIG_LABEL_STYLE_MAX_LEN];
        label_style_parse("font_size:14;color:#FF6600", &s, bind, sizeof(bind));
        CHECK((s.color & 0x01000000) != 0);
        CHECK((s.color & 0xFFFFFF) == 0xFF6600);
        CHECK(bind[0] == '\0');
    }

    // 3. Binding-valued color captured; marker bit stays clear.
    {
        LabelStyle s;
        char bind[CONFIG_LABEL_STYLE_MAX_LEN];
        label_style_parse("font_size:14;color:[expr:[net:any]?\"#22c55e\":\"#94a3b8\"]",
                          &s, bind, sizeof(bind));
        CHECK(s.font_size == 14);
        CHECK((s.color & 0x01000000) == 0);
        CHECK(strcmp(bind, "[expr:[net:any]?\"#22c55e\":\"#94a3b8\"]") == 0);
    }

    // 4. ';' split is bracket + quote aware: a binding color containing ';' and
    //    quoted ':' survives, and a trailing key after the token still parses.
    {
        LabelStyle s;
        char bind[CONFIG_LABEL_STYLE_MAX_LEN];
        label_style_parse("color:[expr:threshold([mqtt:t;v];\"a;b\";10;\"#111\")];font_size:24",
                          &s, bind, sizeof(bind));
        CHECK(s.font_size == 24);
        CHECK((s.color & 0x01000000) == 0);
        CHECK(strcmp(bind, "[expr:threshold([mqtt:t;v];\"a;b\";10;\"#111\")]") == 0);
    }

    // 5. Capture buffer is cleared when the color is static / absent.
    {
        LabelStyle s;
        char bind[CONFIG_LABEL_STYLE_MAX_LEN];
        strcpy(bind, "stale");
        label_style_parse("align:left", &s, bind, sizeof(bind));
        CHECK(bind[0] == '\0');
    }

    // 6. Null/empty DSL is safe and clears capture.
    {
        LabelStyle s;
        char bind[CONFIG_LABEL_STYLE_MAX_LEN];
        strcpy(bind, "stale");
        label_style_parse("", &s, bind, sizeof(bind));
        CHECK(bind[0] == '\0');
        CHECK(s.font_size == 0);
    }

    if (g_failures == 0) {
        printf("All label_style_parse tests passed.\n");
        return 0;
    }
    printf("%d label_style_parse test(s) FAILED.\n", g_failures);
    return 1;
}
