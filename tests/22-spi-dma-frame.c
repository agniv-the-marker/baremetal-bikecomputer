// push st7735 framebuffer over SPI0 via DMA while cpu is wfi()ing

// frame 0: cpu_blit=32855255 cyc   dma_blit=3832214 cyc   (11 pct)
// 32855255/3832214 = 8.57

#include "rpi.h"
#include "rpi-interrupts.h"
#include "st7735.h"
#include "cycle-count.h"
#include "dma_irq.h"

void interrupt_vector(unsigned pc) { (void)pc; dma_irq_isr(); }

static st7735_t d;
static void pattern(uint16_t corner) {
    st7735_fill_rect(&d, 0,  0,  64, 64, ST_C_RED);
    st7735_fill_rect(&d, 64, 0,  64, 64, ST_C_GREEN);
    st7735_fill_rect(&d, 0,  64, 64, 64, ST_C_BLUE);
    st7735_fill_rect(&d, 64, 64, 64, 64, ST_C_YELLOW);
    st7735_fill_rect(&d, 0, 0, 128, 1, ST_C_WHITE);
    st7735_fill_rect(&d, 0, 127, 128, 1, ST_C_WHITE);
    st7735_fill_rect(&d, 2, 2, 14, 14, corner);   // marker, shows which path drew
}

void notmain(void) {
    st7735_init(&d, ST7735_CE0, 25, 24, 16);
    d.flip180 = 1;
    cycle_cnt_init();
    dma_irq_init(1);

    unsigned k = 0;
    while (1) {
        // CPU PIO blit (magenta corner)
        pattern(ST_C_MAGENTA);
        uint32_t t = cycle_cnt_read();
        st7735_flush(&d);
        uint32_t c_cpu = cycle_cnt_read() - t;
        delay_ms(1200);

        // DMA blit (cyan corner), creen should show the SAME pattern, cyan corner
        pattern(ST_C_CYAN);
        t = cycle_cnt_read();
        st7735_flush_dma(&d);
        uint32_t c_dma = cycle_cnt_read() - t;
        delay_ms(1200);

        printk("frame %d: cpu_blit=%d cyc   dma_blit=%d cyc   (%d pct)\n",
               k++, c_cpu, c_dma, c_cpu ? (int)((uint64_t)c_dma*100/c_cpu) : 0);
    }
}
