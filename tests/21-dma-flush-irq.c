// sd flush that wfi()s through the write via a DMA-completion IRQ.
// compare mode 2 vs mode 3 (whole transfer w/ busy versus interrupt)
// pmu cycle counter halts during wfi, so mode 3 should show fewer cycles.
// note is safe because of minimal fat32

// mode2 busy-wait: PASS  cpu_cyc=284542311
// mode3 irq+wfi : PASS  cpu_cyc=278588772
// so like less but not by much D:

#include "rpi.h"
#include "rpi-interrupts.h"
#include "emmc.h"
#include "fat32_min.h"
#include "cycle-count.h"
#include "dma_irq.h"

// forward the single IRQ vector to the shared DMA-IRQ handler.
void interrupt_vector(unsigned pc) { (void)pc; dma_irq_isr(); }

enum { FAT_PART_LBA = 2048, N = 64 * 1024 };
static fat32_t fs;
static uint8_t out[N], back[N];

// write N bytes via DMA <mode>, read back via PIO, verify. returns CPU cycles spent
// in the write (0 on failure).
static uint32_t trial(const char *tag, int mode) {
    fat32_file_t f;
    if (!fat32_find(&fs, "RIDE.GPX", &f)) { printk("%s: no RIDE.GPX\n", tag); return 0; }
    for (int i = 0; i < N; i++) { out[i] = (uint8_t)(i*3 + mode*61 + 1); back[i] = 0; }

    emmc_dma_set_mode(mode);
    uint32_t t = cycle_cnt_read();
    int w = fat32_write_into(&fs, &f, out, N);
    uint32_t cyc = cycle_cnt_read() - t;
    emmc_dma_set_mode(0);
    if (!w) { printk("%s: write FAIL\n", tag); return 0; }

    if (!fat32_find(&fs, "RIDE.GPX", &f)) return 0;
    uint32_t r = fat32_read(&fs, &f, back, N);
    int mism = 0; for (int i = 0; i < N; i++) if (out[i] != back[i]) mism++;
    if (r >= N && mism == 0) { printk("%s: PASS  cpu_cyc=%d\n", tag, cyc); return cyc ? cyc : 1; }
    printk("%s: FAIL mism=%d r=%d\n", tag, mism, r);
    return 0;
}

void notmain(void) {
    if (!emmc_init() || !fat32_mount(&fs, FAT_PART_LBA)) { printk("init FAIL\n"); return; }
    cycle_cnt_init();
    dma_irq_init(1); // install vector + enable DMA/timer IRQs

    uint32_t c2 = trial("mode2 busy-wait", 2);
    uint32_t c3 = trial("mode3 irq+wfi ", 3);

    if (c2 && c3)
        printk("flush-irq: BOTH PASS. busy=%d cpu_cyc  wfi=%d cpu_cyc  (%d pct)\n",
               c2, c3, (int)((uint64_t)c3 * 100 / c2));
    else
        printk("flush-irq: mode3 not working -> keep mode 1/2.\n");
    while (1) {}
}
