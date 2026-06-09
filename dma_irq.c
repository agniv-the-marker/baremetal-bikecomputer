// shared dma-completion irq: dma ch n raises irq line 16+n on basic pending reg 1.
// logger runs with safety_timer=0 — a periodic timer would jitter the sw-uart gps.

#include "rpi.h"
#include "rpi-interrupts.h"
#include "power.h" // wfi(), power_wake_timer_init/ack
#include "dma_irq.h"

enum { DMA_BASE = 0x20007000, CS_INT = 1 << 2, DMA_IRQ_BASE = 16 };
#define CH_CS(ch) ((volatile uint32_t *)(DMA_BASE + (unsigned)(ch) * 0x100))

// bit <ch> set when channel <ch> raised its completion interrupt.
static volatile unsigned g_done = 0;

void dma_irq_isr(void) {
    dev_barrier();
    for (unsigned ch = 4; ch <= 5; ch++) { // the channels we use (SD + SPI)
        volatile uint32_t *cs = CH_CS(ch);
        if (*cs & CS_INT) { *cs = CS_INT; g_done |= (1u << ch); }   // write-1-clear
    }
    power_wake_ack(); // clear the ARM-timer safety wake
    dev_barrier();
}

void dma_irq_init(int safety_timer) {
    interrupt_init();  // install the vector table
    dev_barrier();
    PUT32(IRQ_Enable_1, (1u << (DMA_IRQ_BASE + 4)) | (1u << (DMA_IRQ_BASE + 5)));
    dev_barrier();
    if (safety_timer) power_wake_timer_init(5000);   // backup wake (tests only)
    enable_interrupts();
}

void dma_irq_clear(unsigned ch) { g_done &= ~(1u << ch); }

int dma_irq_wait(unsigned ch, int max_naps) {
    int n = 0;
    while (!((g_done >> ch) & 1) && n < max_naps) { wfi(); n++; }
    return (g_done >> ch) & 1;
}
