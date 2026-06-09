// st7789 color test
// Wire: VCC->3V3, GND->GND, SCL->GPIO11, SDA->GPIO10,
//       DC->GPIO25, RES->GPIO24, BLK->3V3.


#include "rpi.h"
#include "st7789.h"

enum {
    DISP_DC   = 25,
    DISP_RST  = 24,
    SPI_DIV   = 256,  // 250MHz/256 ~= 1 MHz, conservative
};

void notmain(void) {
    static st7789_t disp;

    gpio_set_output(47);

    printk("st7789: HARDWARE SPI0 init (dc=%d rst=%d div=%d)\n",
           DISP_DC, DISP_RST, SPI_DIV);
    st7789_init(&disp, ST7789_CE0, DISP_DC, DISP_RST, SPI_DIV);
    printk("st7789: init done. starting color loop.\n");

    const uint16_t colors[] = { C_RED, C_GREEN, C_BLUE, C_WHITE };
    const char *names[]     = { "RED", "GREEN", "BLUE", "WHITE" };
    unsigned i = 0;
    while (1) {
        gpio_write(47, i & 1);
        printk("st7789: filling %s\n", names[i]);
        st7789_fb_fill(&disp, colors[i]);
        st7789_flush(&disp);
        i = (i + 1) % (sizeof colors / sizeof colors[0]);
        delay_ms(700);
    }
}
