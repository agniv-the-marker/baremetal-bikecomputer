// sample test on ssd1306 oled display from lab 15
// Wire: VCC->3V3, GND->GND, SDA->GPIO2, SCL->GPIO3.
// pass: the OLED shows a "BIKE COMPUTER" banner + a few stat rows + a box.

#include "rpi.h"
#include "i2c.h"
#include "ssd1306-display-driver.h"

// draw a null-terminated string at (x,y), 5x7 font scaled by sz.
static void oled_str(int x, int y, const char *s, int sz) {
    for (; *s; s++) {
        ssd1306_display_draw_character_size(x, y, *s, COLOR_WHITE, sz, sz);
        x += 6 * sz; // 5px glyph + 1px gap
    }
}

void notmain(void) {
    delay_ms(100);
    i2c_init_clk_div(1500); // settle time for the display
    delay_ms(100);

    ssd1306_display_init();
    delay_ms(100);

    int frame = 0;
    while (1) {
        ssd1306_display_clear();

        oled_str(0, 0, "BIKE COMPUTER", 1);
        ssd1306_display_draw_horizontal_line(0, 127, 10, COLOR_WHITE);

        oled_str(0, 16, "SPEED 12.4", 1);
        oled_str(0, 28, "DIST  3.2km", 1);
        oled_str(0, 40, "TIME 00:14", 1);

        // little blinking box so we can see it refreshing
        ssd1306_display_draw_fill_rect(110, 54, 10, 8,
                                       (frame & 1) ? COLOR_WHITE : COLOR_BLACK);

        ssd1306_display_show();
        printk("oled: frame %d shown\n", frame++);
        delay_ms(500);
    }
}
