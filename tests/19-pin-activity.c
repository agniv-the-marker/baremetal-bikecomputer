// literally just check if 15 or 16 has the signal
// 15 is fucking broken
// or something
// fuck my stupid chud life

#include "rpi.h"
#include "gpio.h"
#include "st7735.h"

static st7735_t d;
static void txt(int x, int y, const char *s, uint16_t c) {
    for (; *s; s++) { st7735_char(&d, x, y, *s, c, 2, 2); x += 12; }
}

void notmain(void) {
    st7735_init(&d, ST7735_CE0, 25, 24, 16);
    d.flip180 = 1;
    gpio_set_input(15); gpio_set_pullup(15);
    gpio_set_input(16); gpio_set_pullup(16);

    char l[24];
    while (1) {
        // sample both pins for ~300 ms, count high->low / low->high edges + lows seen
        int p15 = gpio_read(15), p16 = gpio_read(16);
        unsigned e15 = 0, e16 = 0, low15 = 0, low16 = 0;
        uint32_t t = timer_get_usec();
        while (timer_get_usec() - t < 300000) {
            int c15 = gpio_read(15), c16 = gpio_read(16);
            if (c15 != p15) { e15++; p15 = c15; }
            if (c16 != p16) { e16++; p16 = c16; }
            if (!c15) low15 = 1;
            if (!c16) low16 = 1;
        }
        st7735_fb_fill(&d, ST_C_BLACK);
        txt(2, 2, "pin activity", ST_C_YELLOW);
        snprintk(l, sizeof l, "G15 e=%d", (int)e15);
        txt(2, 34, l, e15 ? ST_C_GREEN : ST_C_GRAY);
        txt(2, 56, low15 ? "G15 saw LOW" : "G15 idle hi", e15 ? ST_C_GREEN : ST_C_GRAY);
        snprintk(l, sizeof l, "G16 e=%d", (int)e16);
        txt(2, 86, l, e16 ? ST_C_GREEN : ST_C_GRAY);
        txt(2, 108, low16 ? "G16 saw LOW" : "G16 idle hi", e16 ? ST_C_GREEN : ST_C_GRAY);
        st7735_flush(&d);
    }
}
