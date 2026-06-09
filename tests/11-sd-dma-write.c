// verify the DMA-driven eMMC write path.
// 
// pass: writes into RIDE.GPX already allocated clusters

#include "rpi.h"
#include "emmc.h"
#include "fat32_min.h"

enum { FAT_PART_LBA = 2048, N = 4096 }; // 4 KB = 8 sectors, multi-block path

static fat32_t fs;

static int verify(const char *tag, int dma_on) {
    fat32_file_t f;
    if (!fat32_find(&fs, "RIDE.GPX", &f)) { printk("%s: RIDE.GPX not found\n", tag); return 0; }

    static uint8_t out[N], back[N];
    for (int i = 0; i < N; i++) { out[i] = (uint8_t)(i * 7 + dma_on * 91 + 1); back[i] = 0; }

    emmc_dma_enable(dma_on); // write under test (DMA or PIO)
    int w = fat32_write_into(&fs, &f, out, N);
    emmc_dma_enable(0); // read back always via PIO baseline

    if (!w) { printk("%s: write FAILED (chain too small?)\n", tag); return 0; }

    if (!fat32_find(&fs, "RIDE.GPX", &f)) { printk("%s: re-find FAILED\n", tag); return 0; }
    uint32_t r = fat32_read(&fs, &f, back, N);

    int mism = 0, first = -1;
    for (int i = 0; i < N; i++) if (out[i] != back[i]) { mism++; if (first < 0) first = i; }
    if (r >= N && mism == 0) { printk("%s: PASS (%d bytes round-trip)\n", tag, N); return 1; }
    printk("%s: FAIL r=%d mism=%d first@%d (out=%x back=%x)\n",
           tag, r, mism, first, first<0?0:out[first], first<0?0:back[first]);
    return 0;
}

void notmain(void) {
    printk("sd-dma-write: emmc_init...\n");
    if (!emmc_init())              { printk("emmc_init FAILED\n"); return; }
    if (!fat32_mount(&fs, FAT_PART_LBA)) { printk("fat32_mount FAILED\n"); return; }
    printk("sd-dma-write: mounted.\n");

    int a = verify("PIO-write", 0);          // baseline: known-good path
    int b = verify("DMA-write", 1);          // the path under test

    if (a && b)
        printk("sd-dma-write: ALL PASS - DMA write path works. Safe to "
               "emmc_dma_enable(1) in the logger.\n");
    else
        printk("sd-dma-write: keep emmc_dma_enable(0) in the logger.\n");
    while (1) {}
}
