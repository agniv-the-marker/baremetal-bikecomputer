// hw-uart hand rolled minimal pl011 rx init since the staff version is failing
//
// Wire: GPS TXD -> GPIO15 (PL011 RXD), VCC->5V. ST7735 SPI0. Run standalone.

#include "rpi.h"
#include "gpio.h"
#include "st7735.h"

enum {
    PL011_BASE = 0x20201000,
    U_DR = PL011_BASE + 0x00, U_FR = PL011_BASE + 0x18,
    U_IBRD = PL011_BASE + 0x24, U_FBRD = PL011_BASE + 0x28,
    U_LCRH = PL011_BASE + 0x2c, U_CR = PL011_BASE + 0x30,
    U_IMSC = PL011_BASE + 0x38, U_ICR = PL011_BASE + 0x44,
    FR_RXFE = 1 << 4,                   // RX FIFO empty
    GPS_BAUD = 9600, UARTCLK = 48000000,
};

static st7735_t d;
static void txt(int x, int y, const char *s, uint16_t c) {
    for (; *s; s++) { st7735_char(&d, x, y, *s, c, 2, 2); x += 12; }
}
static void banner(const char *s, uint16_t c) {
    st7735_fb_fill(&d, ST_C_BLACK); txt(4, 50, s, c); st7735_flush(&d);
}

static void pl011_hand_init(void) {
    dev_barrier();
    gpio_set_function(14, GPIO_FUNC_ALT0);   // TXD (unused but set for completeness)
    gpio_set_function(15, GPIO_FUNC_ALT0);   // RXD <- GPS TX
    dev_barrier();
    PUT32(U_CR, 0);                          // disable UART while configuring
    PUT32(U_ICR, 0x7ff);                     // clear all pending interrupts
    unsigned divx64 = (UARTCLK * 4u) / GPS_BAUD;   // = 64 * UARTCLK/(16*baud)
    PUT32(U_IBRD, divx64 / 64);              // 312
    PUT32(U_FBRD, divx64 % 64);              // 32
    PUT32(U_LCRH, (3 << 5) | (1 << 4));      // 8 data bits, FIFO enable
    PUT32(U_IMSC, 0);                        // mask all UART interrupts (we poll)
    dev_barrier();
    PUT32(U_CR, (1 << 0) | (1 << 9));        // UARTEN | RXE  (RX only)
    dev_barrier();
}
static int pl011_has(void) { return (GET32(U_FR) & FR_RXFE) == 0; }
static int pl011_get(void) { return GET32(U_DR) & 0xff; }

void notmain(void) {
    st7735_init(&d, ST7735_CE0, 25, 24, 16);
    d.flip180 = 1;
    banner("A display", ST_C_GREEN);   delay_ms(1500);

    pl011_hand_init(); // the call that crashed with the staff init
    banner("B pl011 ok", ST_C_CYAN);   delay_ms(1500);

    // RX loop: show heartbeat + byte count + last NMEA line.
    char last[22]; int li = 0; last[0] = 0;
    unsigned long bytes = 0; unsigned beat = 0, lines = 0;
    uint32_t t = timer_get_usec();
    while (1) {
        while (pl011_has()) {
            int ch = pl011_get(); bytes++;
            if (ch == '\n' || li >= 21) { last[li] = 0; li = 0; lines++; }
            else if (ch >= 32 && ch < 127) last[li++] = (char)ch;
        }
        if (timer_get_usec() - t > 250000) {
            t = timer_get_usec(); beat++;
            char l[24];
            st7735_fb_fill(&d, ST_C_BLACK);
            txt(2, 2, "RX hand", ST_C_YELLOW);
            snprintk(l, sizeof l, "beat=%d", beat);          txt(2, 28, l, ST_C_WHITE);
            snprintk(l, sizeof l, "bytes=%d", (int)bytes);   txt(2, 52, l, ST_C_WHITE);
            snprintk(l, sizeof l, "ln=%d", lines);           txt(2, 76, l, ST_C_CYAN);
            for (int i = 0; last[i]; i++) st7735_char(&d, 2 + i*6, 104, last[i], ST_C_GREEN, 1, 1);
            st7735_flush(&d);
        }
    }
}
