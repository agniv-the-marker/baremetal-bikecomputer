// verify the DMA-driven eMMC data path
// read only and safe, if the cpu matches the dma path, then we're done!

#include "rpi.h"
#include "emmc.h"

static void dump16(const char *tag, uint8_t *p) {
    printk("%s:", tag);
    for (int i = 0; i < 16; i++) printk(" %x", p[i]);
    printk("\n");
}

void notmain(void) {
    printk("sd-dma: emmc_init...\n");
    if (!emmc_init()) { printk("sd-dma: emmc_init FAILED\n"); return; }
    printk("sd-dma: emmc_init OK\n");

    static uint8_t pio[512], dma[512];
    for (int i = 0; i < 512; i++) { pio[i] = 0x11; dma[i] = 0x22; }

    // 1) PIO read (default path).
    emmc_dma_enable(0);
    int r1 = emmc_read(0, pio, 512);
    printk("sd-dma: PIO read sector0 -> %d, sig=%x %x\n", r1, pio[510], pio[511]);

    // 2) DMA read of the same sector.
    emmc_dma_enable(1);
    int r2 = emmc_read(0, dma, 512);
    emmc_dma_enable(0);
    printk("sd-dma: DMA read sector0 -> %d, sig=%x %x\n", r2, dma[510], dma[511]);

    // 3) compare.
    int mism = 0, first = -1;
    for (int i = 0; i < 512; i++)
        if (pio[i] != dma[i]) { mism++; if (first < 0) first = i; }

    if (r1 == 512 && r2 == 512 && mism == 0) {
        printk("sd-dma: PASS - DMA read matches PIO byte-for-byte. "
               "DMA data path works.\n");
    } else {
        printk("sd-dma: FAIL - %d mismatched bytes (first @%d).\n", mism, first);
        dump16("  pio[0..15]", pio);
        dump16("  dma[0..15]", dma);
        printk("sd-dma: keep emmc_dma_enable(0) in the logger; DMA path needs work.\n");
    }
    while (1) {}
}
