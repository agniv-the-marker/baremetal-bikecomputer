// hw-uart gps probe, doesn't work :(
// this is a test to see if the hardware UART is working properly
// use the st7735 display to show the status
// stage a : display only (known good)
// stage b : after pl011_rx_only_init()
// stage c : RX loop (heartbeat + byte count)
//
// wire: GPS TXD -> GPIO15, GPS VCC->5V. ST7735 SPI0 (SCK11/SDA10/CS8/DC25/RST24).
// run standalone: copy to kernel.img, battery boot, USB-serial unplugged.

#include "rpi.h"
#include "rpi-interrupts.h"
#include "st7735.h"
#include "pl011-uart.h"

enum { GPS_BAUD = 9600 };
static st7735_t d;

static void str(int x, int y, const char *s, uint16_t c, int sz) {
    for (; *s; s++) { st7735_char(&d, x, y, *s, c, sz, sz); x += 6 * sz; }
}
static void banner(const char *s, uint16_t c) {
    st7735_fb_fill(&d, ST_C_BLACK);
    str(4, 50, s, c, 2);
    st7735_flush(&d);
}

void notmain(void) {
    disable_interrupts();

    // STAGE A: display only, proves the panel/loop before touching the UART.
    st7735_init(&d, ST7735_CE0, 25, 24, 16);
    d.flip180 = 1;
    banner("A display", ST_C_GREEN);
    delay_ms(2000);

    // STAGE B: bring up the PL011 receiver, then re-mask interrupts.
    pl011_rx_only_init(GPS_BAUD);
    disable_interrupts(); // in case pl011 init re-enabled them
    banner("B pl011 ok", ST_C_CYAN);
    delay_ms(2000);

    // STAGE C: receive loop, heartbeat proves we're alive, bytes prove RX works.
    unsigned long bytes = 0;
    unsigned beat = 0;
    char line[24];
    uint32_t last = timer_get_usec();
    while (1) {
        while (pl011_has_data()) { (void)pl011_get8(); bytes++; }
        if (timer_get_usec() - last > 250000) {
            last = timer_get_usec();
            beat++;
            st7735_fb_fill(&d, ST_C_BLACK);
            str(4, 4, "C rx loop", ST_C_YELLOW, 2);
            snprintk(line, sizeof line, "beat=%d", (int)beat);
            str(4, 40, line, ST_C_WHITE, 2);
            snprintk(line, sizeof line, "bytes=%d", (int)bytes);
            str(4, 70, line, ST_C_WHITE, 2);
            st7735_flush(&d);
        }
    }
}

// didn't work, couldn't read in any data...