// verify the interrupt-woken sw-UART GPS + WFI idle loop (power win), with a duty meter
// core wfi-sleeps and is woken by gpio16 falling edge (gps!) or slow timer fallback
// cpu can then idle for just under a second btwn the gps bursts.

// duty=99 pct  fixes=20 sat=5  sent=24 wakes=24  lat*1e5=3744704 lon*1e5=-12216209

#include "rpi.h"
#include "sw-uart.h"
#include "gps_nmea.h"
#include "power.h"
#include "cycle-count.h"

enum { GPS_TX = 20, GPS_RX = 16, GPS_BAUD = 9600 };

void notmain(void) {
    sw_uart_t u = sw_uart_init(GPS_TX, GPS_RX, GPS_BAUD);
    gps_t g; gps_init(&g);
    cycle_cnt_init();

    power_wake_gpio_falling_init(GPS_RX);   // wake on a GPS start bit
    power_wake_timer_init(250000);          // ~250ms fallback wake so we print even
                                            // with no GPS (and bound idle latency)

    printk("12-irq-uart: WFI idle loop; draining GPS on wake.\n");

    unsigned sentences = 0, fixes = 0, wakes = 0;
    uint32_t busy = 0;
    uint32_t win_start = cycle_cnt_read();
    uint32_t last_print = win_start;

    while (1) {
        uint32_t a = cycle_cnt_read(); // active region begins
        int c;
        while ((c = sw_uart_get8_timeout(&u, 2000)) >= 0) { // drain the burst
            if (gps_feed(&g, (char)c)) {
                sentences++;
                if (g.has_fix) fixes++;
            }
        }
        busy += cycle_cnt_read() - a; // active region ends

        uint32_t now = cycle_cnt_read();
        if (now - last_print > 700u * 1000u * 1000u * 2u) { // ~2s @700MHz
            uint32_t wall = now - win_start;
            unsigned duty = wall ? (unsigned)((uint64_t)busy * 100 / wall) : 0;
            char la[16], lo[16];
            int lai = (int)(g.lat * 100000), loi = (int)(g.lon * 100000);
            snprintk(la, sizeof la, "%d", lai);
            snprintk(lo, sizeof lo, "%d", loi);
            printk("duty=%d pct  fixes=%d sat=%d  sent=%d wakes=%d  lat*1e5=%s lon*1e5=%s\n",
                   duty, fixes, g.sats, sentences, wakes, la, lo);
            busy = 0; win_start = now; last_print = now;
        }

        wfi(); // core halts here
        power_wake_ack(); // clear timer pending
        power_wake_gpio_ack(GPS_RX); // clear GPIO event
        wakes++;
    }
}
