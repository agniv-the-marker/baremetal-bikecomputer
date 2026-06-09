// IRQ test for the PL011 UART.
//
// Wire: GPS TXD->GPIO15, VCC->5V; ST7735 SPI0. Run standalone (battery, no serial).

#include "rpi.h"
#include "rpi-interrupts.h"
#include "st7735.h"
#include "pl011-uart.h"

enum { GPS_BAUD = 9600 };
static st7735_t d;
static volatile unsigned g_irqs = 0;
static volatile unsigned long g_bytes = 0;

static void banner(const char *s, uint16_t c) {
    st7735_fb_fill(&d, ST_C_BLACK);
    for (int i = 0, x = 4; s[i]; i++, x += 12) st7735_char(&d, x, 50, s[i], c, 2, 2);
    st7735_flush(&d);
}

// C IRQ handler invoked by the staff interrupt_asm trampoline. Drain the PL011 FIFO
// (clears the RX interrupt source) and count. dev_barrier around device access.
void interrupt_vector(unsigned pc) {
    (void)pc;
    dev_barrier();
    while (pl011_has_data()) { (void)pl011_get8(); g_bytes++; }
    g_irqs++;
    dev_barrier();
}

void notmain(void) {
    st7735_init(&d, ST7735_CE0, 25, 24, 16);
    d.flip180 = 1;
    banner("A display", ST_C_GREEN);    delay_ms(1500);

    interrupt_init();                   // install the vector table (staff _interrupt_table)
    banner("B vec ok", ST_C_CYAN);      delay_ms(1500);

    pl011_rx_only_init(GPS_BAUD);       // the call that resets the bare probe
    banner("C pl011 ok", ST_C_YELLOW);  delay_ms(1500);

    enable_interrupts();                // unmask CPSR (in case pl011 didn't)
    banner("D ints on", ST_C_WHITE);    delay_ms(1500);

    char line[24];
    unsigned beat = 0;
    uint32_t last = timer_get_usec();
    while (1) {
        // also poll-drain in case the RX IRQ isn't the model (belt + suspenders)
        while (pl011_has_data()) { (void)pl011_get8(); g_bytes++; }
        if (timer_get_usec() - last > 250000) {
            last = timer_get_usec(); beat++;
            st7735_fb_fill(&d, ST_C_BLACK);
            for (int i=0,x=4; "RX loop"[i]; i++,x+=12) st7735_char(&d,x,2,"RX loop"[i],ST_C_YELLOW,2,2);
            snprintk(line, sizeof line, "beat=%d", beat);            for (int i=0,x=4;line[i];i++,x+=12) st7735_char(&d,x,34,line[i],ST_C_WHITE,2,2);
            snprintk(line, sizeof line, "irq=%d", g_irqs);           for (int i=0,x=4;line[i];i++,x+=12) st7735_char(&d,x,62,line[i],ST_C_CYAN,2,2);
            snprintk(line, sizeof line, "bytes=%d", (int)g_bytes);   for (int i=0,x=4;line[i];i++,x+=12) st7735_char(&d,x,90,line[i],ST_C_WHITE,2,2);
            st7735_flush(&d);
        }
    }
}
