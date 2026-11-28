/* tests/modes_test.c: the display-mode list the game is given (src/runtime/display_modes.h), without the game,
 * SDL or guest memory. Every test: bash tests/run.sh.
 */
#include "display_modes.h"
#include <stdio.h>
#include <string.h>

static int fails;

#define CHECK(cond, ...)                 \
    do {                                 \
        if (!(cond)) {                   \
            printf("FAIL: ");            \
            printf(__VA_ARGS__);         \
            printf("\n");                \
            fails++;                     \
        }                                \
    } while (0)

static int four_three(Sh3DisplayMode m) { return m.w * 3 == m.h * 4; }

/* A list of sixteen, the current mode first, all distinct, and the ones after it 4:3 at 32 bits and 60 Hz. */
static void check_list(uint32_t cur_w, uint32_t cur_h) {
    Sh3DisplayMode seen[SH3_MODE_COUNT], m;
    for (int i = 0; i < SH3_MODE_COUNT; i++) {
        m = sh3_display_mode(i, cur_w, cur_h);
        CHECK(m.w && m.h, "mode %d is empty for a %ux%u screen", i, cur_w, cur_h);
        CHECK(m.bpp == 32, "mode %d is %u-bit", i, m.bpp);
        CHECK(m.hz == 60, "mode %d runs at %u Hz", i, m.hz);
        if (i == 0)
            CHECK(m.w == cur_w && m.h == cur_h, "mode 0 is %ux%u, not the port's %ux%u", m.w, m.h, cur_w, cur_h);
        else
            CHECK(four_three(m), "mode %d is %ux%u, which is not 4:3", i, m.w, m.h);
        for (int k = 0; k < i; k++)
            CHECK(seen[k].w != m.w || seen[k].h != m.h, "mode %d repeats %ux%u", i, m.w, m.h);
        seen[i] = m;
    }
    m = sh3_display_mode(SH3_MODE_COUNT, cur_w, cur_h);
    CHECK(!m.w && !m.h, "the list does not end at %d modes", SH3_MODE_COUNT);
    m = sh3_display_mode(-1, cur_w, cur_h);
    CHECK(!m.w && !m.h, "a negative index returns a mode");
}

int main(void) {
    check_list(1280, 720);  /* the default: 16:9, not in the 4:3 table */
    check_list(1920, 1080);
    check_list(1024, 768);  /* a 4:3 mode the table also has: it must not appear twice */
    check_list(640, 480);

    /* DEVMODE packing: dmSize, dmFields and the four values the game reads back. */
    uint8_t dm[SH3_DEVMODE_SIZE + 8];
    Sh3DisplayMode m;
    memset(dm, 0xAA, sizeof dm);
    sh3_devmode_fill(dm, (Sh3DisplayMode){1600, 1200, 60, 32});
    CHECK(dm[36] == (SH3_DEVMODE_SIZE & 0xFF) && dm[37] == (SH3_DEVMODE_SIZE >> 8), "dmSize is %u", dm[36] | dm[37] << 8);
    CHECK((sh3_get32(dm + 40) & 0x1C0000) == 0x1C0000, "dmFields lacks a dimension it sets");
    CHECK(sh3_devmode_parse(dm, &m), "a filled DEVMODE does not parse");
    CHECK(m.w == 1600 && m.h == 1200 && m.bpp == 32 && m.hz == 60, "round trip gave %ux%u/%u/%u Hz", m.w, m.h, m.bpp, m.hz);
    CHECK(dm[SH3_DEVMODE_SIZE] == 0xAA, "the fill wrote past dmSize");

    memset(dm, 0, sizeof dm);
    CHECK(!sh3_devmode_parse(dm, &m), "an empty DEVMODE is accepted");
    sh3_devmode_fill(dm, (Sh3DisplayMode){8192, 8192, 60, 32});
    sh3_put32(dm + 108, 100000);  /* what a corrupt mode would look like */
    CHECK(!sh3_devmode_parse(dm, &m), "a 100000 px wide mode is accepted");

    /* What the game is allowed to switch to: the table's modes, and nothing else. */
    CHECK(sh3_mode_offered(1024, 768), "1024x768 is not offered");
    CHECK(sh3_mode_offered(3200, 2400), "3200x2400 is not offered");
    CHECK(!sh3_mode_offered(1280, 720), "1280x720 is offered although it is not in the table");
    CHECK(!sh3_mode_offered(0, 0), "an empty mode is offered");
    CHECK(!sh3_mode_offered(1024, 769), "a size next to a table mode is offered");

    printf(fails ? "%d check(s) failed\n" : "modes: all checks passed\n", fails);
    return fails != 0;
}
