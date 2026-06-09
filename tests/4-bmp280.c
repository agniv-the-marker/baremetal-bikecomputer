// bmp280 sensor test
// wire: VCC->3V3, GND->GND, SDA->GPIO2, SCL->GPIO3
// pass: temperature ~ room temp, pressure ~ 1000-1025 hPa, a plausible altitude.

#include "rpi.h"
#include "i2c.h"
#include "ssd1306-display-driver.h"
#include "bmp280.h"

static void oled_str(int x, int y, const char *s, int sz) {
    for (; *s; s++) {
        ssd1306_display_draw_character_size(x, y, *s, COLOR_WHITE, sz, sz);
        x += 6 * sz;
    }
}

// format <v> as "<whole>.<tenths>"
static void fmt1(char *buf, unsigned n, float v) {
    int neg = v < 0;
    if (neg) v = -v;
    int whole = (int)v;
    int tenths = (int)((v - whole) * 10.0f + 0.5f);
    if (tenths == 10) { whole++; tenths = 0; }
    snprintk(buf, n, "%s%d.%d", neg ? "-" : "", whole, tenths);
}

void notmain(void) {
    delay_ms(100);
    i2c_init_clk_div(1500);
    delay_ms(100);
    ssd1306_display_init();
    delay_ms(100);

    int ok = bmp280_init(0x76);
    printk("bmp280: init %s\n", ok ? "OK" : "NOT FOUND");

    while (1) {
        float t = 0, p = 0, alt = 0;
        int got = ok && bmp280_read(&t, &p, &alt);

        char l1[24], l2[24], l3[24];
        if (got) {
            char ts[12], as[12];
            fmt1(ts, sizeof ts, t);
            fmt1(as, sizeof as, alt);
            snprintk(l1, sizeof l1, "T: %s C", ts);
            snprintk(l2, sizeof l2, "P: %d hPa", (int)(p / 100.0f + 0.5f));
            snprintk(l3, sizeof l3, "ALT: %s m", as);
            printk("bmp280: %s  %s  %s\n", l1, l2, l3);
        } else {
            snprintk(l1, sizeof l1, "BMP280");
            snprintk(l2, sizeof l2, ok ? "read failed" : "not found");
            l3[0] = 0;
        }

        ssd1306_display_clear();
        oled_str(0, 0, "BMP280", 1);
        ssd1306_display_draw_horizontal_line(0, 127, 10, COLOR_WHITE);
        oled_str(0, 16, l1, 1);
        oled_str(0, 30, l2, 1);
        oled_str(0, 44, l3, 1);
        ssd1306_display_show();

        delay_ms(1000);
    }
}
