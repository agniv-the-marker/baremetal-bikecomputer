// gps sensor test, read neo-6m over sw-uart and print decoded fixes.
// Wire (GPS module): VCC->3V3 (or 5V works, but it sparked), GND->GND,
//   GPS "TX" -> Pi GPIO16 (pin 36)
// we ignore the other ones (rx and pps) because we dont need to configure it
//
// pass: NMEA decodes, sats climb, once fix=1 the lat/lon match your location.

#include "rpi.h"
#include "sw-uart.h"
#include "gps_nmea.h"

enum {
    GPS_RX = 16, // Pi input, GPS TX
    GPS_TX = 20, // Pi output (unused, but sw_uart needs a tx pin)
    GPS_BAUD = 9600,
};

// format a float with 5 decimals.
static void fmt5(char *b, unsigned n, float v) {
    int neg = v < 0; if (neg) v = -v;
    int whole = (int)v;
    int frac = (int)((v - whole) * 100000.0f + 0.5f);
    if (frac >= 100000) { whole++; frac -= 100000; }
    snprintk(b, n, "%s%d.%d%d%d%d%d", neg ? "-" : "", whole,
             (frac/10000)%10, (frac/1000)%10, (frac/100)%10, (frac/10)%10, frac%10);
}

// format a float with 1 decimal.
static void fmt1(char *b, unsigned n, float v) {
    int neg = v < 0; if (neg) v = -v;
    int whole = (int)v;
    int t = (int)((v - whole) * 10.0f + 0.5f);
    if (t == 10) { whole++; t = 0; }
    snprintk(b, n, "%s%d.%d", neg ? "-" : "", whole, t);
}

void notmain(void) {
    sw_uart_t u = sw_uart_init(GPS_TX, GPS_RX, GPS_BAUD);
    gps_t g;
    gps_init(&g);

    printk("gps: reading NMEA @ %d on GPIO%d ...\n", GPS_BAUD, GPS_RX);

    unsigned count = 0;
    while (1) {
        int c = sw_uart_get8_timeout(&u, 2 * 1000 * 1000);   // 2s
        if (c < 0) {
            printk("gps: no data (check GPS TX -> GPIO%d, power, baud)\n", GPS_RX);
            continue;
        }
        if (!gps_feed(&g, (char)c))
            continue;

        // a sentence completed! print ~1/sec (NEO-6M emits ~5 sentences/sec)
        if (++count % 5)
            continue;

        char la[16], lo[16], sp[12], al[12];
        fmt5(la, sizeof la, g.lat);
        fmt5(lo, sizeof lo, g.lon);
        fmt1(sp, sizeof sp, g.speed_kmh);
        fmt1(al, sizeof al, g.alt_m);
        printk("gps: fix=%d sats=%d  lat=%s lon=%s  spd=%s km/h alt=%s m  %d:%d:%d UTC\n",
               g.has_fix, g.sats, la, lo, sp, al, g.hh, g.mm, g.ss);
    }
}
