// st7735 (THANK YOU SO MUCH COLE) over SPI, testing
// 
// Wiring:
//   VCC -> 3V3        GND -> GND        LED -> 3V3 (backlight on)
//   SCK -> GPIO11 (pin 23)    SDA -> GPIO10 (pin 19)   CS -> GPIO8/CE0 (pin 24)
//   A0  -> GPIO25 (pin 22)    RESET -> GPIO24 (pin 18)
//
// pass: sold red/green/blue/white fills, then test pattern with quadrants and small sq

#include "rpi.h"
#include "st7735.h"

static st7735_t d;

static void show(uint16_t c, const char *name) {
    printk("st7735: fill %s\n", name);
    st7735_fb_fill(&d, c);
    st7735_flush(&d);
    delay_ms(700);
}

void notmain(void) {
    printk("st7735: init (SPI0 CE0, DC=25, RST=24)...\n");
    st7735_init(&d, ST7735_CE0, 25, 24, 16);
    printk("st7735: init done.\n");

    while (1) {
        show(ST_C_RED,   "RED");
        show(ST_C_GREEN, "GREEN");
        show(ST_C_BLUE,  "BLUE");
        show(ST_C_WHITE, "WHITE");

        // test pattern: quadrant grid + full-edge border + corner marker.
        printk("st7735: test pattern\n");
        st7735_fb_fill(&d, ST_C_BLACK);
        st7735_fill_rect(&d, 0,  0,  64, 64, ST_C_RED);
        st7735_fill_rect(&d, 64, 0,  64, 64, ST_C_GREEN);
        st7735_fill_rect(&d, 0,  64, 64, 64, ST_C_BLUE);
        st7735_fill_rect(&d, 64, 64, 64, 64, ST_C_YELLOW);
        // 1px white border on all four edges (checks offset/clipping).
        st7735_fill_rect(&d, 0, 0, 128, 1, ST_C_WHITE);
        st7735_fill_rect(&d, 0, 127, 128, 1, ST_C_WHITE);
        st7735_fill_rect(&d, 0, 0, 1, 128, ST_C_WHITE);
        st7735_fill_rect(&d, 127, 0, 1, 128, ST_C_WHITE);
        // magenta marker in the TOP-LEFT corner (checks orientation).
        st7735_fill_rect(&d, 2, 2, 12, 12, ST_C_MAGENTA);
        st7735_flush(&d);
        delay_ms(2000);
    }
}
