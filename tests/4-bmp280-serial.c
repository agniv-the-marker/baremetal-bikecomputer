// bmp280 standalone test over serial
// wire: VCC->3V3, GND->GND, SDA->GPIO2, SCL->GPIO3, CSB->3V3, SDD/SDO->GND (addr 0x76).
// pass: serial prints a sane temp/pressure/altitude once per second.

#include "rpi.h"
#include "i2c.h"
#include "bmp280.h"

// SDD/SDO->GND => 0x76, SDD/SDO->3V3 => 0x77.
#define BMP_ADDR 0x76

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

    int ok = bmp280_init(BMP_ADDR);
    printk("bmp280: init %s\n", ok ? "OK" : "wrong chip id");

    while (1) {
        float t = 0, p = 0, alt = 0;
        if (ok && bmp280_read(&t, &p, &alt)) {
            char ts[12], as[12];
            fmt1(ts, sizeof ts, t);
            fmt1(as, sizeof as, alt);
            printk("bmp280: T=%s C  P=%d hPa  ALT=%s m\n",
                   ts, (int)(p / 100.0f + 0.5f), as);
        } else {
            printk("bmp280: read failed\n");
        }
        delay_ms(1000);
    }
}
