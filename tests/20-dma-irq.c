// dma completion interrupt waking up the cpu

#include "rpi.h"
#include "rpi-interrupts.h"
#include "cycle-count.h"
#include "power.h" // power_wake_timer_init/ack, wfi

enum {
    DMA_BASE = 0x20007000, DMA_CH = 4,
    DMA_CH_BASE = DMA_BASE + DMA_CH * 0x100,
    DMA_ENABLE = DMA_BASE + 0xff0,
    TI_INTEN = 1 << 0, TI_DEST_INC = 1 << 4, TI_SRC_INC = 1 << 8,
    CS_ACTIVE = 1 << 0, CS_INT = 1 << 2, CS_RESET = 1u << 31,
    DMA_IRQ = 16 + DMA_CH, // BCM2835: DMA channel n -> IRQ 16+n
};

typedef struct { volatile uint32_t TI, SRC, DST, LEN, STRIDE, NEXT, p0, p1; }
    __attribute__((aligned(32))) cb_t;
typedef struct { volatile uint32_t CS, CB_ADDR, TI, SRC, DST, LEN, STR, NEXT, DBG; } ch_t;
#define CH ((volatile ch_t *)DMA_CH_BASE)

static cb_t cb __attribute__((aligned(32)));
static volatile int g_done = 0;
static volatile unsigned g_dma_irqs = 0, g_other_irqs = 0;
static inline uint32_t bus(volatile void *p){ return ((uint32_t)(uintptr_t)p)|0x40000000u; }

// single IRQ handler for all sources. Ack the ARM timer (our wfi safety wake), and
// if the DMA channel raised its completion interrupt, clear it + flag done.
void interrupt_vector(unsigned pc) {
    (void)pc; dev_barrier();
    if (CH->CS & CS_INT) { CH->CS = CS_INT; g_done = 1; g_dma_irqs++; } // W1C the INT
    else g_other_irqs++;
    power_wake_ack(); // clear ARM-timer pending
    dev_barrier();
}

enum { N = 64 * 1024 };
static uint8_t src[N], dst[N];

void notmain(void) {
    cycle_cnt_init();
    for (int i = 0; i < N; i++) { src[i] = (uint8_t)(i*7+1); dst[i] = 0; }

    interrupt_init(); // install vector table
    dev_barrier(); OR32(DMA_ENABLE, 1<<DMA_CH); dev_barrier(); // power the channel
    PUT32(IRQ_Enable_1, 1u << DMA_IRQ); // enable DMA ch IRQ at the controller
    power_wake_timer_init(2000); // ~2ms ARM-timer = safety wfi wake
    enable_interrupts(); // actually take IRQs now (vector installed)

    cb.TI = TI_SRC_INC | TI_DEST_INC | TI_INTEN;
    cb.SRC = bus(src); cb.DST = bus(dst); cb.LEN = N;
    cb.STRIDE = cb.NEXT = cb.p0 = cb.p1 = 0;

    dev_barrier();
    CH->CS = CS_RESET; dev_barrier();
    CH->CB_ADDR = bus(&cb); dev_barrier();
    g_done = 0;
    uint32_t t = cycle_cnt_read();
    CH->CS = CS_ACTIVE;
    dev_barrier();

    unsigned naps = 0;
    while (!g_done && naps < 100000) { wfi(); naps++; } // sleep until DMA-done IRQ
    uint32_t cyc = cycle_cnt_read() - t;

    int mism = 0;
    for (int i = 0; i < N; i++) if (src[i] != dst[i]) mism++;

    printk("dma-irq: done=%d  dma_irqs=%d  other_irqs=%d  naps=%d\n",
           g_done, g_dma_irqs, g_other_irqs, naps);
    printk("dma-irq: copy %s (%d mismatches, %dKB), wait=%d cyc\n",
           mism==0 ? "MATCH" : "FAIL", mism, N/1024, cyc);
    printk("dma-irq: %s\n", (g_done && mism==0) ? "PASS - DMA-done IRQ woke the CPU"
                                                : "needs work");
    while (1) {}
}
