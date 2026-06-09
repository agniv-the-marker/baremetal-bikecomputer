// hw-uart, mini-uart, since pl011 is broken :< on the mailbox, and mini-uart doesn't need it.
//
// Baud reg = core_clk/(8*baud) - 1. Assume core = 250 MHz:
//   9600 -> 250e6/(8*9600) - 1 = 3254
//
// Wire: GPS TXD -> GPIO15 (mini-UART RXD1, ALT5), VCC->5V. ST7735 SPI0. Standalone.

#include "rpi.h"
#include "gpio.h"
#include "st7735.h"

enum {
    AUX = 0x20215000,
    AUX_ENABLES = AUX + 0x04, MU_IO = AUX + 0x40, MU_IER = AUX + 0x44,
    MU_IIR = AUX + 0x48, MU_LCR = AUX + 0x4c, MU_MCR = AUX + 0x50,
    MU_LSR = AUX + 0x54, MU_CNTL = AUX + 0x60, MU_BAUD = AUX + 0x68,
    LSR_RX_READY = 1 << 0,
    GPS_BAUD = 9600, CORE_HZ = 250000000,
};

static st7735_t d;
static void txt(int x, int y, const char *s, uint16_t c) {
    for (; *s; s++) { st7735_char(&d, x, y, *s, c, 2, 2); x += 12; }
}
static void banner(const char *s, uint16_t c) {
    st7735_fb_fill(&d, ST_C_BLACK); txt(4, 50, s, c); st7735_flush(&d);
}

static void miniuart_init(void) {
    dev_barrier();
    gpio_set_function(14, GPIO_FUNC_ALT5);   // TXD1
    gpio_set_function(15, GPIO_FUNC_ALT5);   // RXD1 <- GPS TX
    dev_barrier();
    PUT32(AUX_ENABLES, GET32(AUX_ENABLES) | 1);  // enable mini-UART (unlocks its regs)
    dev_barrier();
    PUT32(MU_CNTL, 0);                        // disable rx/tx while configuring
    PUT32(MU_IER, 0);                         // no interrupts (poll)
    PUT32(MU_LCR, 3);                         // 8-bit (datasheet erratum: 3, not 1)
    PUT32(MU_MCR, 0);
    PUT32(MU_BAUD, (CORE_HZ / (8 * GPS_BAUD)) - 1);   // 3254 @ 250MHz
    PUT32(MU_IIR, 0xc6);                      // clear RX+TX FIFOs
    dev_barrier();
    PUT32(MU_CNTL, 1);                        // enable RX only (bit0)
    dev_barrier();
}
static int mu_has(void) { return GET32(MU_LSR) & LSR_RX_READY; }
static int mu_get(void) { return GET32(MU_IO) & 0xff; }

void notmain(void) {
    st7735_init(&d, ST7735_CE0, 25, 24, 16);
    d.flip180 = 1;
    banner("A display", ST_C_GREEN);   delay_ms(1500);

    miniuart_init();
    banner("B mu ok", ST_C_CYAN);      delay_ms(1500);

    char last[22]; int li = 0; last[0] = 0;
    unsigned long bytes = 0; unsigned beat = 0, lines = 0;
    uint32_t t = timer_get_usec();
    while (1) {
        while (mu_has()) {
            int ch = mu_get(); bytes++;
            if (ch == '\n' || li >= 21) { last[li] = 0; li = 0; lines++; }
            else if (ch >= 32 && ch < 127) last[li++] = (char)ch;
        }
        if (timer_get_usec() - t > 250000) {
            t = timer_get_usec(); beat++;
            char l[24];
            st7735_fb_fill(&d, ST_C_BLACK);
            txt(2, 2, "RX mini", ST_C_YELLOW);
            snprintk(l, sizeof l, "beat=%d", beat);        txt(2, 28, l, ST_C_WHITE);
            snprintk(l, sizeof l, "bytes=%d", (int)bytes); txt(2, 52, l, ST_C_WHITE);
            snprintk(l, sizeof l, "ln=%d", lines);         txt(2, 76, l, ST_C_CYAN);
            for (int i = 0; last[i]; i++) st7735_char(&d, 2 + i*6, 104, last[i], ST_C_GREEN, 1, 1);
            st7735_flush(&d);
        }
    }
}
